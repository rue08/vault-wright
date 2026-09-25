#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QDialog>
#include "authenticator.h"

namespace Ui {
class LoginWindow;
}

class MainWindow;

class LoginWindow : public QDialog
{
    Q_OBJECT

public:
    explicit LoginWindow(MainWindow *mainWindow = nullptr, QWidget *parent = nullptr);
    ~LoginWindow();

    // Asks Authenticator to exchange the stored refresh token for a new
    // idToken. Called when Storage reports a request just failed because the
    // token had already gone stale.
    void refreshSessionNow();

    // Clears the in-memory session (this session tracking, plus
    // Authenticator's stored refresh token) on an explicit logout. Unlike
    // onSessionExpired(), this doesn't emit sessionExpired() or reopen the
    // login dialog -- logging out is a deliberate action, not an error state.
    void logOut();

    // Deletes the Firebase identity behind the current session (via
    // Authenticator::deleteAccount()), then tears down the local session
    // exactly like logOut() -- called only as the last step of "Delete
    // Account", after MainWindow has already had the backend delete the
    // users row/cloud files. Reports accountDeleted() or
    // accountDeletionFailed(); either way the local session is gone by the
    // time one of those fires, since there's nothing left worth keeping it
    // alive for regardless of whether this last step itself succeeded.
    void deleteAccount();

    // Attempts to silently restore a session saved from a previous run --
    // call once, right after construction. No-op if Authenticator has
    // nothing persisted (first launch, or after an explicit logout). If
    // something *was* saved but turns out to be invalid, this surfaces
    // exactly like a live session dying mid-use (onSessionExpired()) --
    // status message plus the login dialog forced open.
    void restoreSession();

private slots:
    void on_googleSignInButton_clicked();
    void onLoginSucceeded(const QString &idToken, const QString &uid, const QString &email);
    void onLoginFailed();
    void onTokenRefreshed(const QString &idToken);
    void onSessionExpired();
    void onAccountDeleted();
    void onAccountDeletionFailed(const QString &errorString);

private:
    MainWindow *mainWindow;
    Ui::LoginWindow *ui;
    Authenticator *auth;

    // The idToken from the most recent successful sign-in or refresh --
    // mirrors what's handed to Storage via enableActionUpload()/
    // idTokenRefreshed(), kept here too since deleteAccount() needs to hand
    // it to Authenticator directly rather than through MainWindow/Storage,
    // which have no reason to know about Firebase's accounts:delete at all.
    QString currentIdToken;

    // Tracks the in-memory session -- reset in onSessionExpired(), set on a
    // successful sign-in. Not persisted: closing and reopening the app still
    // requires restoreSession() to succeed (see below); this flag just backs
    // the "already logged in" bookkeeping between those two.
    bool isLoggedIn = false;
    QString loggedInEmail;

    // True while a refreshIdToken() call in flight was triggered by
    // restoreSession() specifically, rather than the ordinary reactive
    // refresh Storage triggers mid-session (refreshSessionNow()). The two
    // need different handling once Authenticator::tokenRefreshed() comes
    // back: a mid-session refresh only needs to hand the new idToken to
    // Storage (the session's already established); restoreSession() needs
    // the full "establish a new session" treatment, same as a normal login
    // succeeding. Checked and cleared in onTokenRefreshed().
    bool restoringSession = false;

signals:
    void enableActionUpload(bool flag, const QString &idToken, const QString &uid);
    void idTokenRefreshed(const QString &idToken);
    void sessionExpired();

    // Result of deleteAccount() above -- the local session is already torn
    // down by the time either of these fires, see deleteAccount()'s comment.
    void accountDeleted();
    void accountDeletionFailed(const QString &errorString);
};

#endif // LOGINWINDOW_H
