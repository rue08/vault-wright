#ifndef STORAGE_H
#define STORAGE_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>
#include <QPointer>
#include <QVector>
#include <functional>

class MainWindow;

class Storage : public QObject
{
    Q_OBJECT
public:
    explicit Storage(QObject *parent = nullptr);

    void setIdToken(const QString &idToken);

    // Base URL of the self-hosted backend (e.g. "http://localhost:3000" or
    // an ngrok tunnel), with no trailing slash. Unlike the Firebase REST
    // endpoints this replaces, it's not a compile-time constant -- it's
    // meant to be user-configurable, since it can change on every tunnel
    // restart. Every request below is built against this.
    void setBackendUrl(const QString &backendUrl);

    // Verifies the current idToken against the backend and upserts the
    // corresponding users row -- must succeed once per session (right after
    // login) before any other request below will be accepted; the backend
    // rejects requests from tokens with no matching users row. Reports
    // backendLoginSucceeded() or backendLoginFailed().
    void loginToBackend();

    // `targetTab` is opaque to Storage -- it's just echoed back on
    // setDownloadFile()/downloadFailed() so the caller can tell which of its
    // own widgets this particular download was for, even if several are in
    // flight at once or the UI has moved on by the time the reply arrives.
    // If targetTab is destroyed before the download finishes, it comes back
    // as nullptr instead of a dangling pointer.
    //
    // `fileId` is the backend's numeric file id (as a string), as handed
    // back via setCloudFiles().
    void downloadFile(const QString &fileId, QObject *targetTab);
    void listFiles();

    // Deletes one cloud file by its backend id. Safe to call repeatedly in a
    // loop for a multi-file batch -- mirrors uploadFile()'s wave tracking
    // (see its comment above): deleteBatchFinished() fires once per wave
    // with an accurate count of how many succeeded, and the cloud file list
    // refreshes once the wave settles. `fileName` is only carried along for
    // deleteSucceeded()/deleteFailed() to report -- the backend only needs
    // the id.
    void deleteFile(const QString &fileId, const QString &fileName);

    // Permanently deletes the caller's account on the backend -- the users
    // row plus, via ON DELETE CASCADE (migrations/001_init.sql), every cloud
    // file they own, in one request. The caller (MainWindow) only invokes
    // this after confirming with the user, and follows a success with
    // LoginWindow::deleteAccount() to also delete the Firebase identity --
    // Storage has no reach into that, it only ever talks to our own backend.
    // Reports accountDeleteSucceeded() or accountDeleteFailed().
    void deleteAccount();

    // Queues a single file for upload. Safe to call repeatedly in a loop for
    // a multi-file batch: Storage tracks how many uploads are outstanding
    // internally and, once every upload queued since the last time it was
    // idle has settled (success or failure), refreshes the cloud file list
    // and reports how many of them succeeded via uploadBatchFinished(). If
    // the caller pauses between uploadFile() calls (e.g. a confirmation
    // dialog for one file in a batch) and the ones already queued finish in
    // the meantime, that's reported as its own wave rather than held back --
    // resuming the loop afterwards starts a fresh wave rather than waiting.
    void uploadFile(const QByteArray &fileData, const QString &localFilePath);

    // Called by the owner once a token refresh Storage asked for (via
    // tokenRefreshRequired()) has resolved: on success, sets the new idToken
    // and transparently redoes every request that was waiting on it; on
    // failure, settles them all as failures instead of leaving them hanging.
    void resumeAfterTokenRefresh(const QString &idToken);
    void abandonPendingRetries();

    // Fetches the current backend URL from a fixed, permanent discovery
    // endpoint (a GitHub Gist kept up to date by hand whenever the backend's
    // real URL changes, e.g. an ngrok restart). Entirely unauthenticated and
    // unrelated to idToken/backendUrl above -- lets Settings offer a "Fetch
    // latest" shortcut instead of everyone needing to be told the new URL by
    // hand. Reports discoveryUrlFetched() or discoveryUrlFetchFailed().
    void fetchDiscoveryUrl();

private:
    QNetworkAccessManager *networkAccessManager;
    QNetworkRequest newRequest;
    QString idToken;
    QString backendUrl;

    QString cloudFileName = "";

    int pendingUploads = 0;

    // How many uploads have succeeded in the current wave (reset whenever a
    // new wave starts, i.e. uploadFile() is called while pendingUploads is
    // 0) -- reported via uploadBatchFinished() once the wave settles.
    int succeededUploads = 0;

    // Same pattern as pendingUploads/succeededUploads above, for deleteFile().
    int pendingDeletes = 0;
    int succeededDeletes = 0;

    // Requests deferred behind an in-flight token refresh. Each one knows how
    // to redo its own exact request (retry(true)) or how to settle itself as
    // a failure if the refresh didn't pan out (retry(false)).
    bool refreshInProgress = false;
    QVector<std::function<void(bool refreshed)>> pendingRetries;

    // Builds a QNetworkRequest for `url` with the header ngrok's free tier
    // requires to skip its "you are about to visit..." browser-warning
    // interstitial (ERR_NGROK_6024) -- without it, every GET through the
    // tunnel comes back as that warning page's HTML instead of a real
    // response. All requests below go through this instead of constructing
    // QNetworkRequest directly, so no future endpoint can forget it.
    QNetworkRequest buildRequest(const QUrl &url) const;

    void startUpload(const QByteArray &fileData, const QString &localFilePath);
    void onUploadFinished(const QByteArray &fileData, const QString &localFilePath);
    void onListFilesFinished();
    void onDownloadFinished(const QString &cloudFilePath, QPointer<QObject> targetTab);

    void startDelete(const QString &fileId, const QString &fileName);
    void onDeleteFinished(const QString &fileId, const QString &fileName);

    void onDeleteAccountFinished();

    void parseResponse(const QByteArray &response);

    // Called exactly once per uploadFile() call, on success or failure.
    void finishPendingUpload();

    // Called exactly once per deleteFile() call, on success or failure.
    void finishPendingDelete();

    // Returns a human-readable error if the reply failed (transport error, or
    // a backend-style {"error": "..."} JSON body), otherwise a null QString.
    static QString extractError(QNetworkReply *reply, const QByteArray &response);

    // True specifically when the failure means "the idToken was rejected" --
    // as opposed to a network problem, permissions issue, or anything else
    // that a token refresh wouldn't fix.
    static bool isAuthTokenError(QNetworkReply *reply, const QByteArray &response);

    // If this failure was an expired/invalid token, queues `retry` to run
    // once the session is healed or given up on, coalescing any other
    // concurrently-failing requests into the same single refresh, and
    // returns true (caller should stop, this failure has been handled).
    // Returns false if this wasn't a token problem, so the caller should
    // report it as an ordinary failure instead.
    bool deferIfAuthError(QNetworkReply *reply, const QByteArray &response,
                           std::function<void(bool refreshed)> retry);

signals:
    // Emitted right before a fresh file list is fetched, so the UI can clear
    // its view without every caller having to remember to do it themselves.
    void cloudFilesCleared();
    void setCloudFiles(const QString &fileName, const QString &cloudFilePath);
    void setDownloadFile(const QByteArray &response, QObject *targetTab);

    void uploadSucceeded(const QString &localFilePath, const QString &cloudFilePath);
    void uploadFailed(const QString &localFilePath, const QString &errorString);

    // Emitted once every upload queued since the current wave started has
    // settled -- `succeededCount` is how many of them actually succeeded
    // (may be 0 if the whole wave failed, in which case this still fires so
    // the cloud file list still gets refreshed, but callers should treat 0
    // as "nothing to announce" rather than showing "Uploaded 0 files").
    void uploadBatchFinished(int succeededCount);
    void listFilesFailed(const QString &errorString);
    void downloadFailed(const QString &errorString, QObject *targetTab);

    // `fileId` lets the caller close a tab open on the file that just got
    // deleted; `fileName` is carried along purely for display.
    void deleteSucceeded(const QString &fileId, const QString &fileName);
    void deleteFailed(const QString &fileName, const QString &errorString);

    // Same semantics as uploadBatchFinished() above, for deleteFile() waves.
    void deleteBatchFinished(int succeededCount);

    // Result of deleteAccount() above.
    void accountDeleteSucceeded();
    void accountDeleteFailed(const QString &errorString);

    void backendLoginSucceeded();
    void backendLoginFailed(const QString &errorString);

    void discoveryUrlFetched(const QString &url);
    void discoveryUrlFetchFailed(const QString &errorString);

    // Storage has no access to Authenticator -- this just asks whoever owns
    // both to perform a refresh and report back via resumeAfterTokenRefresh()
    // or abandonPendingRetries().
    void tokenRefreshRequired();
};

#endif // STORAGE_H
