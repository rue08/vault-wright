#include "storage.h"
#include <QFileInfo>
#include <QUrl>
#include <QDebug>

namespace {
// Always serves the latest revision -- no commit SHA in the path. The
// developer edits this Gist's content by hand whenever the backend's real
// (ngrok) URL changes; this URL itself is permanent and safe to hardcode.
const QString kDiscoveryUrl = QStringLiteral(
    "https://gist.githubusercontent.com/rue08/9a38be7fc8c3415224b8fcc0a0cd792d/raw/vaultwright-backend-url.txt");
}

Storage::Storage(QObject *parent)
    : QObject{parent}
{
    networkAccessManager = new QNetworkAccessManager(this);
}

void Storage::setIdToken(const QString &idToken)
{
    this -> idToken = idToken;
}

QNetworkRequest Storage::buildRequest(const QUrl &url) const
{
    QNetworkRequest request{url};
    request.setRawHeader("ngrok-skip-browser-warning", "true");
    return request;
}


void Storage::setBackendUrl(const QString &backendUrl)
{
    // Trim any trailing slash so callers below can unconditionally do
    // backendUrl + "/path" without ever risking a doubled slash.
    QString trimmed = backendUrl;
    while (trimmed.endsWith('/'))
        trimmed.chop(1);

    this -> backendUrl = trimmed;
}


void Storage::loginToBackend()
{
    QJsonObject payload;
    payload["id_token"] = idToken;

    newRequest = buildRequest(QUrl(backendUrl + "/auth/firebase/login"));
    newRequest.setHeader(QNetworkRequest::ContentTypeHeader, QString("application/json"));

    QNetworkReply* reply = networkAccessManager -> post(newRequest, QJsonDocument(payload).toJson());

    connect(reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
        QByteArray response = reply -> readAll();

        // Deliberately not routed through deferIfAuthError: this call *is*
        // how the session gets established in the first place, so a 401
        // here means the idToken itself is bad -- retrying via the normal
        // refresh dance would just come back to this same call.
        QString error = extractError(reply, response);

        if (error.isEmpty())
            emit backendLoginSucceeded();
        else
            emit backendLoginFailed(error);

        reply->deleteLater();
    });
}


void Storage::fetchDiscoveryUrl()
{
    QNetworkRequest request{QUrl(kDiscoveryUrl)};
    QNetworkReply *reply = networkAccessManager -> get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QByteArray response = reply -> readAll();
        bool ok = reply->error() == QNetworkReply::NoError;
        QString transportError = reply->errorString();
        reply->deleteLater();

        if (!ok)
        {
            emit discoveryUrlFetchFailed(transportError);
            return;
        }

        QString url = QString::fromUtf8(response).trimmed();
        if (!url.startsWith("http://") && !url.startsWith("https://"))
        {
            emit discoveryUrlFetchFailed("The discovery file doesn't contain a valid URL.");
            return;
        }

        emit discoveryUrlFetched(url);
    });
}


QString Storage::extractError(QNetworkReply *reply, const QByteArray &response)
{
    // The backend's error responses are shaped like {"error": "message"} --
    // a flat string, unlike Firebase's nested {"error": {"message": ...}}.
    // Finding this shape means our own server code answered with a specific,
    // actionable problem (bad token, wrong file extension, etc.).
    QJsonDocument jsonDocument = QJsonDocument::fromJson(response);
    if (jsonDocument.isObject() && jsonDocument.object().contains("error"))
    {
        QString message = jsonDocument.object().value("error").toString();
        return message.isEmpty() ? QStringLiteral("The server returned an error.") : message;
    }

    // Anything else that failed means something other than our backend code
    // answered -- ngrok's own error page, a dropped connection, DNS failure,
    // a sleeping laptop, etc. We can't reliably tell *which* of those it was
    // (an expired tunnel and a briefly-offline backend can look identical
    // from here), so don't guess -- just say plainly that the shared backend
    // couldn't be reached, and point at the fix that covers both cases.
    if (reply->error() != QNetworkReply::NoError)
    {
        qWarning() << "Backend unreachable:" << reply->errorString();
        return QStringLiteral("Can't reach the cloud backend right now. It may be offline, "
                               "or its URL may have changed -- check Settings.");
    }

    return QString();
}


bool Storage::isAuthTokenError(QNetworkReply *reply, const QByteArray &response)
{
    Q_UNUSED(response);

    // The backend returns 401 specifically for a missing/invalid/expired
    // token (see middleware/auth.js) -- as opposed to 403, which means the
    // token is fine but there's no users row for it yet (loginToBackend()
    // was never called), a refresh wouldn't fix that.
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401;
}


bool Storage::deferIfAuthError(QNetworkReply *reply, const QByteArray &response,
                                std::function<void(bool)> retry)
{
    if (!isAuthTokenError(reply, response))
        return false;

    pendingRetries.append(std::move(retry));

    // Several requests can fail on the same stale token at once (a
    // multi-file upload batch, for instance) -- only ask for one refresh and
    // let everything else queue behind it.
    if (!refreshInProgress)
    {
        refreshInProgress = true;
        emit tokenRefreshRequired();
    }

    return true;
}


void Storage::resumeAfterTokenRefresh(const QString &idToken)
{
    this -> idToken = idToken;
    refreshInProgress = false;

    const auto retries = pendingRetries;
    pendingRetries.clear();

    for (const auto &retry : retries)
        retry(true);
}


void Storage::abandonPendingRetries()
{
    refreshInProgress = false;

    const auto retries = pendingRetries;
    pendingRetries.clear();

    for (const auto &retry : retries)
        retry(false);
}


void Storage::uploadFile(const QByteArray& fileData, const QString& localFilePath)
{
    // pendingUploads == 0 means the previous wave (if any) has already fully
    // settled and been reported -- this call starts a new one.
    if (pendingUploads == 0)
        succeededUploads = 0;

    pendingUploads++;
    startUpload(fileData, localFilePath);
}


void Storage::startUpload(const QByteArray& fileData, const QString& localFilePath)
{
    QString filename = QFileInfo(localFilePath).fileName();

    QJsonObject payload;
    payload["filename"] = filename;
    payload["content"] = QString::fromUtf8(fileData);

    newRequest = buildRequest(QUrl(backendUrl + "/files"));
    newRequest.setRawHeader("Authorization", ("Bearer " + idToken).toUtf8());
    newRequest.setHeader(QNetworkRequest::ContentTypeHeader, QString("application/json"));

    QNetworkReply* reply = networkAccessManager -> post(newRequest, QJsonDocument(payload).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, fileData, localFilePath]() {
        this -> onUploadFinished(fileData, localFilePath);
    });
}


void Storage::onUploadFinished(const QByteArray &fileData, const QString& localFilePath)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QByteArray response = reply -> readAll();

    if (deferIfAuthError(reply, response, [this, fileData, localFilePath](bool refreshed) {
            if (refreshed)
                this -> startUpload(fileData, localFilePath);
            else
            {
                emit uploadFailed(localFilePath, "Session expired. Please log in again.");
                finishPendingUpload();
            }
        }))
    {
        reply->deleteLater();
        return;
    }

    QString error = extractError(reply, response);
    if (!error.isEmpty())
    {
        emit uploadFailed(localFilePath, error);
        reply->deleteLater();
        finishPendingUpload();
        return;
    }

    // The backend hands back the row it just created/updated, keyed by its
    // numeric id -- that id is what setCloudFiles()/downloadFile() use from
    // here on to refer to this file.
    QJsonDocument jsonDocument = QJsonDocument::fromJson(response);
    QString fileId = QString::number(jsonDocument.object().value("id").toInt());

    succeededUploads++;
    emit uploadSucceeded(localFilePath, fileId);
    finishPendingUpload();

    reply->deleteLater();
}


void Storage::finishPendingUpload()
{
    // Refresh the list and report the wave's outcome once every upload
    // queued since it started has finished (successfully or not) -- never
    // before, and never left hanging. If the caller queues more uploads
    // later (e.g. resuming a batch loop after a confirmation dialog), that's
    // a new wave -- see the pendingUploads == 0 check in uploadFile().
    if (--pendingUploads <= 0)
    {
        listFiles();
        emit uploadBatchFinished(succeededUploads);
    }
}


void Storage::deleteFile(const QString &fileId, const QString &fileName)
{
    if (pendingDeletes == 0)
        succeededDeletes = 0;

    pendingDeletes++;
    startDelete(fileId, fileName);
}


void Storage::startDelete(const QString &fileId, const QString &fileName)
{
    newRequest = buildRequest(QUrl(backendUrl + "/files/" + fileId));
    newRequest.setRawHeader("Authorization", ("Bearer " + idToken).toUtf8());

    QNetworkReply* reply = networkAccessManager -> deleteResource(newRequest);

    connect(reply, &QNetworkReply::finished, this, [this, fileId, fileName]() {
        this -> onDeleteFinished(fileId, fileName);
    });
}


void Storage::onDeleteFinished(const QString &fileId, const QString &fileName)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QByteArray response = reply -> readAll();

    if (deferIfAuthError(reply, response, [this, fileId, fileName](bool refreshed) {
            if (refreshed)
                this -> startDelete(fileId, fileName);
            else
            {
                emit deleteFailed(fileName, "Session expired. Please log in again.");
                finishPendingDelete();
            }
        }))
    {
        reply->deleteLater();
        return;
    }

    QString error = extractError(reply, response);
    if (!error.isEmpty())
    {
        emit deleteFailed(fileName, error);
        reply->deleteLater();
        finishPendingDelete();
        return;
    }

    // DELETE /files/:id responds 204 No Content on success -- nothing to
    // parse out of `response`, fileId/fileName already carry everything the
    // caller needs.
    succeededDeletes++;
    emit deleteSucceeded(fileId, fileName);
    finishPendingDelete();

    reply->deleteLater();
}


void Storage::finishPendingDelete()
{
    // Same reasoning as finishPendingUpload() above.
    if (--pendingDeletes <= 0)
    {
        listFiles();
        emit deleteBatchFinished(succeededDeletes);
    }
}


void Storage::deleteAccount()
{
    newRequest = buildRequest(QUrl(backendUrl + "/auth/account"));
    newRequest.setRawHeader("Authorization", ("Bearer " + idToken).toUtf8());

    QNetworkReply* reply = networkAccessManager -> deleteResource(newRequest);
    connect(reply, &QNetworkReply::finished, this, &Storage::onDeleteAccountFinished);
}


void Storage::onDeleteAccountFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QByteArray response = reply -> readAll();

    if (deferIfAuthError(reply, response, [this](bool refreshed) {
            if (refreshed)
                this -> deleteAccount();
            else
                emit accountDeleteFailed("Session expired. Please log in again.");
        }))
    {
        reply->deleteLater();
        return;
    }

    QString error = extractError(reply, response);
    if (!error.isEmpty())
    {
        emit accountDeleteFailed(error);
        reply->deleteLater();
        return;
    }

    // DELETE /auth/account responds 204 No Content on success -- nothing to
    // parse out of `response`.
    emit accountDeleteSucceeded();
    reply->deleteLater();
}


void Storage::listFiles()
{
    emit cloudFilesCleared();

    QNetworkRequest request = buildRequest(QUrl(backendUrl + "/files"));
    request.setRawHeader("Authorization", ("Bearer " + idToken).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QString("application/json"));

    QNetworkReply *reply = networkAccessManager -> get(request);
    connect(reply, &QNetworkReply::finished, this, &Storage::onListFilesFinished);
}


void Storage::onListFilesFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QByteArray response = reply -> readAll();

    if (deferIfAuthError(reply, response, [this](bool refreshed) {
            if (refreshed)
                this -> listFiles();
            else
                emit listFilesFailed("Session expired. Please log in again.");
        }))
    {
        reply->deleteLater();
        return;
    }

    QString error = extractError(reply, response);
    if (!error.isEmpty())
        emit listFilesFailed(error);
    else
        parseResponse(response);

    reply->deleteLater();
}


void Storage::parseResponse(const QByteArray &response)
{
    QJsonDocument jsonDocument = QJsonDocument::fromJson(response);
    QJsonArray files = jsonDocument.array();

    for (int i = 0; i < files.size(); i++)
    {
        QJsonObject file = files[i].toObject();

        QString fileName = file.value("filename").toString();
        QString fileId = QString::number(file.value("id").toInt());

        emit setCloudFiles(fileName, fileId);
    }
}


void Storage::downloadFile(const QString &fileId, QObject *targetTab)
{
    newRequest = buildRequest(QUrl(backendUrl + "/files/" + fileId));
    newRequest.setRawHeader("Authorization", ("Bearer " + idToken).toUtf8());

    QNetworkReply* reply = networkAccessManager -> get(newRequest);

    // Track targetTab via QPointer, not a raw pointer: if the caller's widget
    // is destroyed before this download finishes (tab closed, app torn down
    // mid-download), the pointer safely resolves to null instead of dangling.
    QPointer<QObject> trackedTarget(targetTab);
    connect(reply, &QNetworkReply::finished, this, [this, fileId, trackedTarget]() {
        this -> onDownloadFinished(fileId, trackedTarget);
    });
}


void Storage::onDownloadFinished(const QString &fileId, QPointer<QObject> targetTab)
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    QByteArray response = reply -> readAll();

    if (deferIfAuthError(reply, response, [this, fileId, targetTab](bool refreshed) {
            // The tab may have been closed while we were waiting on the
            // refresh -- targetTab safely reads as null in that case, so
            // there's nothing left to retry or report into.
            if (targetTab.isNull())
                return;

            if (refreshed)
                this -> downloadFile(fileId, targetTab.data());
            else
                emit downloadFailed("Session expired. Please log in again.", targetTab.data());
        }))
    {
        reply->deleteLater();
        return;
    }

    QString error = extractError(reply, response);
    if (!error.isEmpty())
    {
        emit downloadFailed(error, targetTab.data());
        reply->deleteLater();
        return;
    }

    // The backend returns the file row as JSON ({id, filename, content,
    // updated_at}) -- unlike the old Firebase Storage GET, the response body
    // itself isn't the raw file content.
    QJsonDocument jsonDocument = QJsonDocument::fromJson(response);
    QByteArray content = jsonDocument.object().value("content").toString().toUtf8();

    emit setDownloadFile(content, targetTab.data());

    reply->deleteLater();
}
