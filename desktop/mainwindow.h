#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QSettings>
#include <QLabel>
#include <QTreeWidget>
#include <QIcon>
#include <QVariant>
#include <QFileDialog>
#include <QSet>
#include "storage.h"
#include "loginwindow.h"
#include "monacoeditor.h"
#include "theme.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QAction;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    QTabWidget *theWorkspace;
    MonacoEditor* curr;

private slots:
    void closeTab(int index);

    void on_actionNew_File_triggered();

    void on_actionNew_Text_File_triggered();

    void on_actionNew_Markdown_File_triggered();

    void on_actionThe_Vault_triggered();

    void on_actionOpen_triggered();

    void on_actionOpen_Folder_triggered();

    void on_actionUndo_triggered();

    void on_actionRedo_triggered();

    void on_actionCut_triggered();

    void on_actionCopy_triggered();

    void on_actionPaste_triggered();

    void on_actionSave_triggered();

    void on_actionClose_File_triggered();

    void localFilesItemClicked(QTreeWidgetItem* item);

    void cloudFilesItemClicked(QTreeWidgetItem* item);

    void on_actionRun_triggered();

    void on_actionLogin_triggered();

    void on_actionLogout_triggered();

    void on_actionUpload_triggered();

    // Manually retries the backend connection (loginToBackend(), which
    // on_actionSettings_triggered() already reuses after a URL change) --
    // lives right below Upload in the toolbar for when the backend was
    // unreachable and has since come back, or the URL was just fixed via
    // Settings, without needing to reopen Settings again just to trigger it.
    void on_actionRetry_triggered();

    // File menu equivalents of the toolbar Upload button and (new)
    // cloud-file deletion -- see mainwindow.cpp for what each does.
    void on_actionUpload_File_to_Cloud_triggered();
    void on_actionDelete_File_from_Cloud_triggered();

    void onSetCloudFiles(const QString &fileName, const QString &cloudFilePath);

    void onDownloadFile(const QByteArray &response, QObject *targetTab);

    void onEnableActionUpload(bool flag, const QString& idToken, const QString& uid);

    void on_actionClose_Folder_triggered();

    void onUploadSucceeded(const QString &localFilePath, const QString &cloudFilePath);
    void onUploadFailed(const QString &localFilePath, const QString &errorString);

    // Reports how many uploads succeeded once a wave (a single upload, or a
    // batch's worth queued back-to-back) has fully settled -- see
    // Storage::uploadBatchFinished().
    void onUploadBatchFinished(int succeededCount);

    // fileId lets this close any tab left open on the file that just got
    // deleted -- see Storage::deleteSucceeded().
    void onDeleteSucceeded(const QString &fileId, const QString &fileName);
    void onDeleteFailed(const QString &fileName, const QString &errorString);
    void onDeleteBatchFinished(int succeededCount);

    void onListFilesFailed(const QString &errorString);
    void onDownloadFailed(const QString &errorString, QObject *targetTab);

    void onTokenRefreshRequired();
    void onIdTokenRefreshed(const QString &idToken);
    void onSessionExpired();

    void onBackendLoginSucceeded();
    void onBackendLoginFailed(const QString &errorString);
    void on_actionSettings_triggered();

    // Profile > Delete Account -- confirms with the user, then starts the
    // deletion chain via storage->deleteAccount(). deleteAccountAction is
    // manually connected to this (not .ui-declared, same reasoning as the
    // rest of the Profile/View menus -- see mainwindow.cpp constructor), so
    // it isn't auto-wired by name the way on_actionX_triggered() slots are.
    void onDeleteAccountTriggered();

    // Storage::deleteAccount() has settled -- the backend row and every
    // cloud file it owned are gone either way this splits.
    // Succeeded: still need to delete the Firebase identity itself, via
    // LoginWindow::deleteAccount() -- see onFirebaseAccountDeleted().
    void onBackendAccountDeleted();
    // Failed: nothing was actually deleted -- safe to just report and let
    // the user retry.
    void onBackendAccountDeleteFailed(const QString &errorString);

    // LoginWindow::deleteAccount() has settled -- the local session is
    // already torn down by the time either of these fires (see its
    // comment), so both just need to reflect that in the UI.
    void onFirebaseAccountDeleted();
    void onFirebaseAccountDeleteFailed(const QString &errorString);

    void on_actionToggle_Comment_triggered();

    // Fired by QStyleHints whenever the effective color scheme changes --
    // either the OS's own appearance flipping while the theme menu is set
    // to "System", or setThemeMode() itself applying an explicit Light/Dark
    // override (see theme.h). Either way, this is the single place that
    // re-derives the effective scheme and pushes it out to the native UI
    // and every open Monaco tab.
    void onColorSchemeChanged();

private:
    // Marks `tab`'s title with a "● " prefix while its document has unsaved
    // changes, clearing it again once saved -- looked up by pointer rather
    // than a captured index since tabs are reorderable (setMovable(true)).
    void wireModifiedIndicator(MonacoEditor *tab);

    // Shared by the New File / New Text File / New Markdown File actions --
    // opens a blank tab titled "<titlePrefix> <n>[.defaultExtension]" and
    // tags it with the extension Save-As should fall back to if the user
    // saves without typing one of its own (see on_actionSave_triggered()).
    void newFileTab(const QString &titlePrefix, const QString &defaultExtension);

    // Figures out an actual local file path for on_actionRun_triggered() to
    // hand Terminal::runFile() -- Terminal itself has no notion of cloud
    // tabs. For a local tab this is just the normal Save flow (prompting
    // only if it's a brand-new, never-saved tab) followed by its filePath.
    //
    // A cloud tab has no real local path at all -- property("filePath")
    // holds the backend's numeric file id, not a disk location (see
    // on_actionSave_triggered()'s "one-off export" comment) -- so the first
    // Run on a given cloud tab prompts for where to put a local copy, same
    // as a brand-new local tab's first save, and remembers it afterward in
    // the "localRunPath" property so every later Run on that same tab just
    // rewrites that same file instead of prompting again. Deliberately
    // separate from Save's own cloud-export path, which always prompts --
    // Run shouldn't interrupt with a dialog on every single click.
    //
    // Returns an empty string if there's nothing runnable yet (a save/export
    // was cancelled, or writing the file failed) -- on_actionRun_triggered()
    // treats that as "nothing to do", not an error of its own.
    QString resolveRunnablePath();

    // Picks the file-tree icon for a given file name by its extension --
    // shared between the local "Open Folder" tree and the cloud files tree
    // so both classify extensions the same way. Falls back to a null QIcon
    // (no icon shown) for anything outside the IDE's recognized C++/docs
    // extensions. Reads currentThemeIsDark for the one icon (the plain-text
    // fallback) whose tint actually differs between themes -- see
    // applyTheme().
    QIcon iconForFileName(const QString &fileName);

    // True if the cloud files tree already has a top-level entry named
    // `fileName` -- used before uploading a *local* file for the first time
    // to warn if it would silently overwrite an unrelated cloud file that
    // just happens to share the same basename (cloud files are keyed on
    // filename alone server-side, not full path). Not used for re-uploading
    // a tab already known to be that exact cloud file (isCloudFile == true),
    // since overwriting there is the intended action.
    bool cloudFileExists(const QString &fileName) const;

    // The output_circle "this is an executable" icon shown in the local
    // files tree -- pulled out of on_actionOpen_Folder_triggered() so
    // refreshTreeIcons() can reuse the same theme-aware choice.
    QIcon iconForExecutable();

    // Loads an SVG file-tree icon such that it looks identical whether its
    // row is selected or not. Without this, Qt's item delegate asks QIcon
    // for a QIcon::Selected-mode pixmap on a selected row, and since a
    // plain QIcon(path) never registers one, Qt auto-generates a
    // desaturated/washed-out substitute -- explicitly registering the same
    // source for both modes here means there's nothing left for it to
    // generate. Used by iconForFileName()/iconForExecutable() and the
    // upload-success checkmark in onSetCloudFiles(), i.e. every icon shown
    // inside localFiles/cloudFiles.
    static QIcon loadFileTreeIcon(const QString &path);

    // Applies `mode` via Theme::setMode() and immediately re-derives and
    // pushes the effective scheme, rather than only relying on
    // onColorSchemeChanged() -- QStyleHints::colorSchemeChanged only fires
    // on an actual change, so picking "Light" while the OS (and thus the
    // prior System-mode effective scheme) is already light would otherwise
    // leave the UI unrefreshed on first selection.
    void setThemeMode(Theme::Mode mode);

    // Pushes `isDark` out to everything that isn't palette-driven: the
    // toolbar/menu icons (flat black/white glyphs, not tinted by QPalette),
    // the file-tree hover overlay and placeholder-label colors, the
    // already-populated file trees, and every open Monaco tab. QStyleHints'
    // color scheme itself (set by setThemeMode()/Theme::setMode()) already
    // handles ordinary native widget chrome -- this covers what it can't.
    void applyTheme(bool isDark);

    // Re-icons whatever's already in the local files tree after a theme
    // change -- see applyTheme(). Deliberately leaves the cloud files tree
    // alone: one of its icons can be a transient upload-success checkmark
    // (onSetCloudFiles()) that isn't recoverable from the item alone, and
    // the only icon that's actually theme-sensitive (the plain-text
    // fallback) is a subtle gray-on-gray difference either way -- it
    // corrects itself on the tree's next refresh regardless.
    void refreshTreeIcons();

    // Called right after a successful sign-in (onEnableActionUpload()) to
    // actually reach the backend. If a backend URL is already saved, this is
    // just storage->loginToBackend(). If not -- a fresh install, or a device
    // that's never had Settings configured -- there's nothing to try yet, so
    // this just says so and waits: the user fetches/enters a URL via
    // Settings (Fetch is enabled now that they're signed in) and either hits
    // Retry or OK, both of which retry on their own.
    void establishBackendSession();

    // What onBackendLoginSucceeded() shows -- set by whichever of
    // establishBackendSession()/on_actionRetry_triggered()/
    // on_actionSettings_triggered() is about to call loginToBackend(), since
    // "logged in" is only accurate for the first of those; the other two are
    // reconnecting an already-established session, not starting a new one.
    QString pendingBackendSuccessMessage;

    Ui::MainWindow *ui;
    QTreeWidget *localFiles;
    QTreeWidget *cloudFiles;
    QSplitter *splitter;
    QSplitter* theVault;
    QSettings settings;
    QString filePath;
    QString projectFolderPath;
    QTabBar tabBar;
    QFileInfo info;
    Storage *storage;
    LoginWindow *loginWindow;

    // Profile > Delete Account -- not ui-declared (see the View/Profile menu
    // comment in the constructor), so it's kept as a member rather than a
    // constructor-local the way loginAction/logoutAction are, since its
    // enabled state has to be kept in lockstep with ui->actionLogout's at
    // every one of the several places that toggles it (signed out at
    // startup, on logout, on sign-in, on session expiry) -- deleting the
    // account makes no sense whenever logging out wouldn't either.
    QAction *deleteAccountAction = nullptr;

    // Mirrors Theme::isDark() -- kept as a member (rather than re-querying
    // Theme::isDark() every time) so iconForFileName()/iconForExecutable()
    // can read it without needing every call site updated. Set by
    // applyTheme(), which is what actually changes it.
    bool currentThemeIsDark = true;

    // Guards against onSessionExpired() re-entering loginWindow->exec() --
    // a cascade of requests failing against the same dead session (e.g. a
    // retried listFiles() after abandonPendingRetries()) can each emit
    // sessionExpired() again while the first dialog is still showing.
    bool handlingSessionExpiry = false;

protected:
    void closeEvent(QCloseEvent *event) override;

    // Keeps the file trees' pointing-hand cursor scoped to actual rows.
    // A plain setCursor() on the tree widget itself covers its whole
    // viewport, hand included over the empty space below the last item --
    // this watches localFiles'/cloudFiles' viewports and swaps the cursor
    // based on whether indexAt() under the mouse is actually valid.
    bool eventFilter(QObject *watched, QEvent *event) override;
};
#endif // MAINWINDOW_H
