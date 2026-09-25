#include "loginwindow.h"
#include "ui_loginwindow.h"
#include "mainwindow.h"
#include <QPropertyAnimation>
#include <QStatusBar>
#include <QAction>
#include <QList>

LoginWindow::LoginWindow(MainWindow *mainWindow, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LoginWindow)
    , mainWindow(mainWindow)
{
    ui->setupUi(this);

    auth = new Authenticator(this);

    connect(auth, &Authenticator::loginFailed, this, &LoginWindow::onLoginFailed);
    connect(auth, &Authenticator::loginSucceeded, this, &LoginWindow::onLoginSucceeded);
    connect(auth, &Authenticator::tokenRefreshed, this, &LoginWindow::onTokenRefreshed);
    connect(auth, &Authenticator::sessionExpired, this, &LoginWindow::onSessionExpired);
    connect(auth, &Authenticator::accountDeleted, this, &LoginWindow::onAccountDeleted);
    connect(auth, &Authenticator::accountDeletionFailed, this, &LoginWindow::onAccountDeletionFailed);
}


void LoginWindow::refreshSessionNow()
{
    auth -> refreshIdToken();
}


void LoginWindow::onTokenRefreshed(const QString &idToken)
{
    currentIdToken = idToken;

    if (restoringSession)
    {
        restoringSession = false;
        isLoggedIn = true;
        // uid is unused downstream (the backend derives identity from the
        // token itself -- see MainWindow::onEnableActionUpload()), and a
        // bare token refresh doesn't carry it anyway.
        emit enableActionUpload(true, idToken, QString());
        return;
    }

    emit idTokenRefreshed(idToken);
}


void LoginWindow::onSessionExpired()
{
    // The session actually ended -- a subsequent login is a real new
    // sign-in, not a no-op repeat. Also covers a failed restoreSession()
    // attempt (a saved session turning out to be invalid), which reports
    // through this exact same path.
    isLoggedIn = false;
    loggedInEmail.clear();
    currentIdToken.clear();
    restoringSession = false;

    emit sessionExpired();
}


void LoginWindow::logOut()
{
    isLoggedIn = false;
    loggedInEmail.clear();
    currentIdToken.clear();

    auth -> logOut();
}


void LoginWindow::deleteAccount()
{
    auth -> deleteAccount(currentIdToken);
}


void LoginWindow::onAccountDeleted()
{
    // Same local teardown as logOut() -- deleteAccount() is only ever
    // called once the backend has already destroyed this account's data, so
    // there's nothing left for a lingering local session to be useful for.
    isLoggedIn = false;
    loggedInEmail.clear();
    currentIdToken.clear();
    auth -> logOut();

    emit accountDeleted();
}


void LoginWindow::onAccountDeletionFailed(const QString &errorString)
{
    // The Firebase identity survived, but the backend data behind it is
    // already gone (see deleteAccount()'s comment) -- still tear down the
    // local session; MainWindow surfaces `errorString` as a warning.
    isLoggedIn = false;
    loggedInEmail.clear();
    currentIdToken.clear();
    auth -> logOut();

    emit accountDeletionFailed(errorString);
}


void LoginWindow::restoreSession()
{
    if (!auth -> hasPersistedSession())
        return;

    restoringSession = true;
    loggedInEmail = auth -> persistedEmail();

    auth -> refreshIdToken();
}

LoginWindow::~LoginWindow()
{
    delete ui;
}

void LoginWindow::on_googleSignInButton_clicked()
{
    mainWindow -> statusBar() -> showMessage("Waiting for Google sign-in in your browser...", 5000);
    auth -> signInWithGoogle();
}

void LoginWindow::onLoginSucceeded(const QString &idToken, const QString &uid, const QString &email)
{
    isLoggedIn = true;
    loggedInEmail = email;

    emit enableActionUpload(true, idToken, uid);
    close();
}

void LoginWindow::onLoginFailed()
{
    mainWindow -> statusBar() -> showMessage("Google sign-in failed. Please try again.", 3000);
}
