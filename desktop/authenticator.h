#ifndef AUTHENTICATOR_H
#define AUTHENTICATOR_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <QTcpServer>
#include <QTcpSocket>

class Authenticator : public QObject
{
    Q_OBJECT

public:
    explicit Authenticator(QObject *parent = nullptr);

    // Starts a Google sign-in: opens the system browser at Google's consent
    // screen and spins up a one-shot local HTTP server on 127.0.0.1 to catch
    // the redirect (the "loopback" flow from Google's "Using OAuth 2.0 for
    // Desktop Apps" guide). See onLoopbackSocketReadyRead() for the rest of
    // the chain: it hands the returned code to exchangeGoogleAuthCode(),
    // which hands Google's id_token to signInToFirebaseWithGoogleIdToken().
    void signInWithGoogle();

    // Exchanges the stored refresh token for a new idToken. Only ever called
    // reactively -- Storage reports a request failed because the token had
    // already gone stale, and this is how the session recovers. Works
    // regardless of which provider the session originally signed in with.
    void refreshIdToken();

    // Discards the stored refresh token on an explicit logout, so it can't
    // be used to silently resurrect the session (e.g. via refreshIdToken()
    // being triggered by some in-flight request that hadn't settled yet).
    // Also clears the persisted copy on disk -- see the constructor.
    void logOut();

    // True if a refresh token is available to use right now -- checked by
    // LoginWindow::restoreSession() immediately after construction, when
    // this can only mean one survived from a previous run (see the
    // constructor, which loads it from QSettings before anything else has
    // had a chance to sign in).
    bool hasPersistedSession() const;

    // Permanently deletes the Firebase account behind `idToken`, via Identity
    // Toolkit's accounts:delete. Only ever called as the last step of
    // "Delete Account", after the backend has already deleted the users row
    // (and, via ON DELETE CASCADE, every cloud file) -- so by the time this
    // runs, the account has nothing left to lose even if this step itself
    // fails. Reports accountDeleted() or accountDeletionFailed().
    void deleteAccount(const QString &idToken);

    // The email persisted alongside the refresh token at the last
    // successful sign-in -- restoreSession() uses this to restore
    // LoginWindow's isLoggedIn/loggedInEmail bookkeeping across a restart,
    // since a bare token refresh doesn't carry it.
    QString persistedEmail() const;

private:
    QNetworkAccessManager *networkAccessManager;

    QString refreshToken;

    // The email of the current/most recent successful sign-in -- read
    // straight out of Firebase's response (see onFirebaseGoogleSignInFinished()),
    // not something the caller provides up front the way the old
    // email/password flow did.
    QString sessionEmail;

    // The one-shot local server used to catch Google's OAuth redirect --
    // torn down as soon as it's served its single expected request (or a
    // new sign-in is started before that happens). Null whenever no sign-in
    // is in flight.
    QTcpServer *loopbackServer = nullptr;

    // Accumulates bytes for the in-flight loopback connection until a full
    // request line has arrived.
    QByteArray loopbackBuffer;

    // CSRF guard: generated fresh in signInWithGoogle(), checked against the
    // "state" query param on the loopback redirect in
    // onLoopbackSocketReadyRead() before the code is trusted.
    QString pendingState;

    // PKCE code_verifier for the in-flight sign-in -- generated alongside
    // pendingState, its hash sent as code_challenge in the authorization
    // request, then sent in the clear in exchangeGoogleAuthCode(). This is
    // what makes a stolen auth code (e.g. another local process racing our
    // loopback server) useless on its own.
    QString pendingCodeVerifier;

    // Writes refreshToken/sessionEmail to QSettings (called wherever
    // refreshToken is updated on success -- initial sign-in and every
    // subsequent refresh, since Firebase rotates it each time it's used) or
    // clears them (logOut(), or a refresh actually getting rejected).
    void persistSession();
    void clearPersistedSession();

    // Guards against refreshIdToken() being invoked again while one is
    // already in flight (e.g. several requests failing on the same stale
    // token in quick succession).
    bool refreshInProgress = false;

    void onRefreshFinished();

    // Closes and discards loopbackServer (if any) -- called both to clean up
    // after a completed/failed flow and defensively at the start of a new
    // signInWithGoogle() call.
    void teardownLoopbackServer();

    void onLoopbackNewConnection();
    void onLoopbackSocketReadyRead();

    void exchangeGoogleAuthCode(const QString &code, const QString &redirectUri);
    void onGoogleTokenExchangeFinished();

    void signInToFirebaseWithGoogleIdToken(const QString &googleIdToken);
    void onFirebaseGoogleSignInFinished();

    void onDeleteAccountFinished();

    // Cryptographically random, URL-safe token generator -- used for both
    // pendingState and pendingCodeVerifier (with different lengths).
    static QString randomUrlSafeToken(int numBytes);

    // PKCE code_challenge (base64url(sha256(verifier))) for a given
    // code_verifier, per RFC 7636.
    static QString pkceChallenge(const QString &verifier);

signals:
    void loginSucceeded(const QString &idToken, const QString &uid, const QString &email);
    void loginFailed();

    // A new idToken is ready -- an on-demand refresh, triggered by Storage
    // hitting a stale token, succeeded.
    void tokenRefreshed(const QString &idToken);

    // The refresh token itself was rejected (account disabled/deleted,
    // explicitly revoked, or long unused). There's no recovery from this
    // short of signing in again.
    void sessionExpired();

    // Result of deleteAccount() above.
    void accountDeleted();
    void accountDeletionFailed(const QString &errorString);
};

#endif // AUTHENTICATOR_H
