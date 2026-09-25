#include "authenticator.h"
#include "config.h"
#include <QUrl>
#include <QUrlQuery>
#include <QSettings>
#include <QDesktopServices>
#include <QRandomGenerator>
#include <QCryptographicHash>

namespace {
const char *REFRESH_TOKEN_SETTINGS_KEY = "session/refreshToken";
const char *EMAIL_SETTINGS_KEY = "session/email";

const char *GOOGLE_AUTH_ENDPOINT = "https://accounts.google.com/o/oauth2/v2/auth";
const char *GOOGLE_TOKEN_ENDPOINT = "https://oauth2.googleapis.com/token";
}

Authenticator::Authenticator(QObject *parent)
    : QObject(parent)
{
    networkAccessManager = new QNetworkAccessManager(this);

    // Whatever session (if any) survived from a previous run -- empty
    // QSettings values just leave these blank, same as a fresh install.
    // See hasPersistedSession()/persistedEmail(), used by
    // LoginWindow::restoreSession().
    QSettings settings;
    refreshToken = settings.value(REFRESH_TOKEN_SETTINGS_KEY).toString();
    sessionEmail = settings.value(EMAIL_SETTINGS_KEY).toString();
}


bool Authenticator::hasPersistedSession() const
{
    return !refreshToken.isEmpty();
}


QString Authenticator::persistedEmail() const
{
    return sessionEmail;
}


void Authenticator::persistSession()
{
    QSettings settings;
    settings.setValue(REFRESH_TOKEN_SETTINGS_KEY, refreshToken);
    settings.setValue(EMAIL_SETTINGS_KEY, sessionEmail);
}


void Authenticator::clearPersistedSession()
{
    QSettings settings;
    settings.remove(REFRESH_TOKEN_SETTINGS_KEY);
    settings.remove(EMAIL_SETTINGS_KEY);
}


QString Authenticator::randomUrlSafeToken(int numBytes)
{
    QByteArray bytes(numBytes, Qt::Uninitialized);
    for (int i = 0; i < numBytes; ++i)
        bytes[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));

    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}


QString Authenticator::pkceChallenge(const QString &verifier)
{
    QByteArray hash = QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}


void Authenticator::signInWithGoogle()
{
    // Only one sign-in flow at a time -- a second click while one's already
    // waiting on the browser just restarts it against a fresh server/state.
    teardownLoopbackServer();

    loopbackServer = new QTcpServer(this);
    if (!loopbackServer->listen(QHostAddress::LocalHost, 0))
    {
        teardownLoopbackServer();
        emit loginFailed();
        return;
    }
    connect(loopbackServer, &QTcpServer::newConnection, this, &Authenticator::onLoopbackNewConnection);

    pendingState = randomUrlSafeToken(16);
    pendingCodeVerifier = randomUrlSafeToken(32);

    QUrl authUrl(GOOGLE_AUTH_ENDPOINT);
    QUrlQuery query;
    query.addQueryItem("client_id", GOOGLE_OAUTH_CLIENT_ID);
    query.addQueryItem("redirect_uri", QString("http://127.0.0.1:%1").arg(loopbackServer->serverPort()));
    query.addQueryItem("response_type", "code");
    query.addQueryItem("scope", "openid email profile");
    query.addQueryItem("code_challenge", pkceChallenge(pendingCodeVerifier));
    query.addQueryItem("code_challenge_method", "S256");
    query.addQueryItem("state", pendingState);
    // Forces the account chooser every time rather than silently reusing
    // whatever Google session happens to already be active in the browser.
    query.addQueryItem("prompt", "select_account");
    authUrl.setQuery(query);

    QDesktopServices::openUrl(authUrl);
}


void Authenticator::teardownLoopbackServer()
{
    if (!loopbackServer)
        return;

    loopbackServer->close();
    loopbackServer->deleteLater();
    loopbackServer = nullptr;
    loopbackBuffer.clear();
}


void Authenticator::onLoopbackNewConnection()
{
    QTcpSocket *socket = loopbackServer->nextPendingConnection();
    if (!socket)
        return;

    connect(socket, &QTcpSocket::readyRead, this, &Authenticator::onLoopbackSocketReadyRead);
}


void Authenticator::onLoopbackSocketReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;

    loopbackBuffer.append(socket->readAll());

    // GET requests carry no body -- the request line alone (terminated by
    // the first \r\n) is enough to read the redirect's query string. Wait
    // for more data if it hasn't fully arrived yet.
    int lineEnd = loopbackBuffer.indexOf("\r\n");
    if (lineEnd < 0)
        return;

    QByteArray requestLine = loopbackBuffer.left(lineEnd);
    QList<QByteArray> parts = requestLine.split(' ');
    QString path = parts.size() >= 2 ? QString::fromUtf8(parts.at(1)) : QString();

    QUrlQuery query(QUrl("http://127.0.0.1" + path).query());

    static const QByteArray responseBody =
        "<html><body><p>Signed in -- you can close this tab.</p></body></html>";
    QByteArray httpResponse = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: "
        + QByteArray::number(responseBody.size()) + "\r\n\r\n" + responseBody;
    socket->write(httpResponse);
    socket->flush();
    socket->disconnectFromHost();
    socket->deleteLater();

    QString code = query.queryItemValue("code");
    QString state = query.queryItemValue("state");
    QString error = query.queryItemValue("error");
    QString redirectUri = QString("http://127.0.0.1:%1").arg(loopbackServer->serverPort());

    teardownLoopbackServer();

    // A mismatched/missing state means this request didn't actually come
    // from the authorization request we just sent -- treat it the same as
    // any other failure rather than trusting the code.
    if (!error.isEmpty() || code.isEmpty() || state != pendingState)
    {
        emit loginFailed();
        return;
    }

    exchangeGoogleAuthCode(code, redirectUri);
}


void Authenticator::exchangeGoogleAuthCode(const QString &code, const QString &redirectUri)
{
    QUrlQuery body;
    body.addQueryItem("client_id", GOOGLE_OAUTH_CLIENT_ID);
    body.addQueryItem("client_secret", GOOGLE_OAUTH_CLIENT_SECRET);
    body.addQueryItem("code", code);
    body.addQueryItem("code_verifier", pendingCodeVerifier);
    body.addQueryItem("grant_type", "authorization_code");
    body.addQueryItem("redirect_uri", redirectUri);

    QNetworkRequest request{QUrl(GOOGLE_TOKEN_ENDPOINT)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QString("application/x-www-form-urlencoded"));

    QNetworkReply *reply = networkAccessManager->post(request, body.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, &Authenticator::onGoogleTokenExchangeFinished);
}


void Authenticator::onGoogleTokenExchangeFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QByteArray response = reply->readAll();
    reply->deleteLater();

    QJsonDocument jsonDocument = QJsonDocument::fromJson(response);
    QJsonObject object = jsonDocument.object();

    if (!jsonDocument.isObject() || object.contains("error") || !object.contains("id_token"))
    {
        emit loginFailed();
        return;
    }

    signInToFirebaseWithGoogleIdToken(object.value("id_token").toString());
}


void Authenticator::signInToFirebaseWithGoogleIdToken(const QString &googleIdToken)
{
    QString endpoint = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithIdp?key=";
    endpoint += FIREBASE_API_KEY;

    QVariantMap payload;
    payload["postBody"] = "id_token=" + googleIdToken + "&providerId=google.com";
    payload["requestUri"] = "http://localhost";
    payload["returnSecureToken"] = true;

    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QString("application/json"));

    QNetworkReply *reply = networkAccessManager->post(request, QJsonDocument::fromVariant(payload).toJson());
    connect(reply, &QNetworkReply::finished, this, &Authenticator::onFirebaseGoogleSignInFinished);
}


void Authenticator::onFirebaseGoogleSignInFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QByteArray response = reply->readAll();
    reply->deleteLater();

    QJsonDocument jsonDocument = QJsonDocument::fromJson(response);
    QJsonObject object = jsonDocument.object();

    // A transport failure (offline, timeout, DNS) can leave `response` empty
    // or non-JSON -- require a real idToken before treating this as success.
    if (!jsonDocument.isObject() || object.contains("error") || !object.contains("idToken"))
    {
        emit loginFailed();
        return;
    }

    QString idToken = object.value("idToken").toString();
    QString uid = object.value("localId").toString();

    refreshToken = object.value("refreshToken").toString();
    sessionEmail = object.value("email").toString();
    persistSession();

    emit loginSucceeded(idToken, uid, sessionEmail);
}


void Authenticator::refreshIdToken()
{
    if (refreshInProgress)
        return;
    refreshInProgress = true;

    QString refreshEndpoint = "https://securetoken.googleapis.com/v1/token?key=" + FIREBASE_API_KEY;

    // Unlike every other Firebase Auth call here, this endpoint takes a
    // form-encoded body, not JSON.
    QUrlQuery body;
    body.addQueryItem("grant_type", "refresh_token");
    body.addQueryItem("refresh_token", refreshToken);

    QNetworkRequest request{QUrl(refreshEndpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QString("application/x-www-form-urlencoded"));

    QNetworkReply *reply = networkAccessManager->post(request, body.query(QUrl::FullyEncoded).toUtf8());

    connect(reply, &QNetworkReply::finished, this, &Authenticator::onRefreshFinished);
}


void Authenticator::logOut()
{
    refreshToken.clear();
    clearPersistedSession();
}


void Authenticator::deleteAccount(const QString &idToken)
{
    QString endpoint = "https://identitytoolkit.googleapis.com/v1/accounts:delete?key=";
    endpoint += FIREBASE_API_KEY;

    QVariantMap payload;
    payload["idToken"] = idToken;

    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QString("application/json"));

    QNetworkReply *reply = networkAccessManager->post(request, QJsonDocument::fromVariant(payload).toJson());
    connect(reply, &QNetworkReply::finished, this, &Authenticator::onDeleteAccountFinished);
}


void Authenticator::onDeleteAccountFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QByteArray response = reply->readAll();
    reply->deleteLater();

    QJsonDocument jsonDocument = QJsonDocument::fromJson(response);
    QJsonObject object = jsonDocument.object();

    // Unlike sign-in/refresh, a successful accounts:delete response carries
    // no token of its own to check for -- just a bare {"kind": "..."} object.
    // "no error" is the actual success signal here.
    if (jsonDocument.isObject() && object.contains("error"))
    {
        QString message = object.value("error").toObject().value("message").toString();
        emit accountDeletionFailed(message.isEmpty()
            ? QStringLiteral("Google sign-in couldn't be reached.")
            : message);
        return;
    }

    emit accountDeleted();
}


void Authenticator::onRefreshFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QByteArray response = reply->readAll();
    reply->deleteLater();

    refreshInProgress = false;

    QJsonDocument jsonDocument = QJsonDocument::fromJson(response);
    QJsonObject object = jsonDocument.object();

    // This endpoint's response uses snake_case field names, unlike the
    // sign-in endpoints' camelCase -- same values, different keys.
    if (!jsonDocument.isObject() || object.contains("error") || !object.contains("id_token"))
    {
        // The refresh token itself was rejected: account disabled/deleted,
        // revoked, or long unused. No way back from here except signing in
        // again -- and no point keeping a known-dead token around to retry
        // next launch either.
        clearPersistedSession();
        emit sessionExpired();
        return;
    }

    QString idToken = object.value("id_token").toString();
    QString refreshToken = object.value("refresh_token").toString();

    this->refreshToken = refreshToken;
    persistSession(); // Firebase rotates the refresh token on each use -- keep the saved copy current

    emit tokenRefreshed(idToken);
}
