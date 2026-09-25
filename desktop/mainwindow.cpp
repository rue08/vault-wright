#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "monacoeditor.h"
#include "terminal.h"
#include <QCloseEvent>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QDir>
#include <QDirIterator>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QStyleHints>
#include <QActionGroup>
#include <QAbstractButton>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Reapplies whatever theme mode was last chosen from the View > Theme
    // menu -- QStyleHints's color-scheme override doesn't itself persist
    // across process restarts, only the QSettings value backing
    // Theme::mode() does. Also reacts live to further changes, whether from
    // the OS's own appearance (while in "System" mode) or setThemeMode()
    // applying an explicit override.
    Theme::setMode(Theme::mode());
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &MainWindow::onColorSchemeChanged);

    projectFolderPath = QStringLiteral(PROJECT_ROOT_DIR);

    storage = new Storage(this);
    loginWindow = new LoginWindow(this, this);

    // No hardcoded default -- backend URL changes on every ngrok tunnel
    // restart, so it's read fresh from settings each launch and only ever
    // updated through the Settings action, never compiled in.
    storage -> setBackendUrl(settings.value("backendUrl").toString());

    splitter = new QSplitter(Qt::Horizontal);
    setCentralWidget(splitter);

    theVault = new QSplitter(Qt::Vertical, splitter);

    // Each pane is a persistent "Local Files"/"Cloud Files" header pinned
    // above its tree -- unlike the old design, where the pane's only label
    // was a low-opacity empty-state placeholder that stood in for a heading
    // when there was nothing to show and vanished the moment real content
    // loaded. The tree itself (empty or not) is always visible now, so
    // there's no separate empty-state widget to switch to/from.
    QWidget *localFilesPane = new QWidget(theVault);
    QVBoxLayout *localFilesPaneLayout = new QVBoxLayout(localFilesPane);
    localFilesPaneLayout -> setContentsMargins(0, 0, 0, 0);
    localFilesPaneLayout -> setSpacing(0);

    QLabel *localFilesHeader = new QLabel("Local Files");
    localFilesHeader -> setStyleSheet("font-weight: bold; padding: 4px 6px;");
    localFilesPaneLayout -> addWidget(localFilesHeader);

    localFiles = new QTreeWidget;
    localFilesPaneLayout -> addWidget(localFiles);

    QWidget *cloudFilesPane = new QWidget(theVault);
    QVBoxLayout *cloudFilesPaneLayout = new QVBoxLayout(cloudFilesPane);
    cloudFilesPaneLayout -> setContentsMargins(0, 0, 0, 0);
    cloudFilesPaneLayout -> setSpacing(0);

    QLabel *cloudFilesHeader = new QLabel("Cloud Files");
    cloudFilesHeader -> setStyleSheet("font-weight: bold; padding: 4px 6px;");
    cloudFilesPaneLayout -> addWidget(cloudFilesHeader);

    cloudFiles = new QTreeWidget;
    cloudFilesPaneLayout -> addWidget(cloudFiles);

    theWorkspace = new QTabWidget(splitter);

    theVault -> setMinimumWidth(105);

    // Explicit rather than left to the active QStyle's PM_SplitterWidth --
    // that defaults to 7px on macOS's native style but would come out
    // different on Windows/Fusion, and the handle's color is forced by a
    // stylesheet in applyTheme() below regardless, which already means
    // neither splitter renders its platform's native grip/shading anymore.
    // Pinning the width here too keeps both consistent across OSes rather
    // than just the color.
    splitter -> setHandleWidth(4);
    theVault -> setHandleWidth(4);

    QWidget *spacer = new QWidget();
    spacer -> setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    ui -> toolBar -> addWidget(spacer);
    ui -> toolBar -> addAction(ui -> actionRun);
    ui -> toolBar -> addAction(ui -> actionLogin);
    ui -> toolBar -> addAction(ui -> actionLogout);
    ui -> toolBar -> addAction(ui -> actionSettings);
    ui -> actionUpload -> setEnabled(false);
    ui -> actionRetry -> setEnabled(false);
    ui -> actionLogout -> setEnabled(false);
    ui -> actionUpload_File_to_Cloud -> setEnabled(false);
    ui -> actionDelete_File_from_Cloud -> setEnabled(false);
    // deleteAccountAction doesn't exist yet at this point in the constructor
    // -- disabled once it's created just below instead.


    // View > Theme -- System/Light/Dark, mutually exclusive. Built
    // programmatically rather than in mainwindow.ui, same as the toolbar
    // actions above, since its checked state has to be derived from
    // Theme::mode() at startup rather than a fixed .ui default.
    QMenu *viewMenu = ui -> menubar -> addMenu("View");
    QMenu *themeMenu = viewMenu -> addMenu("Theme");

    QAction *systemThemeAction = themeMenu -> addAction("System");
    QAction *lightThemeAction = themeMenu -> addAction("Light");
    QAction *darkThemeAction = themeMenu -> addAction("Dark");

    QMenu *profileMenu = ui -> menubar -> addMenu("Profile");

    profileMenu -> addAction(ui -> actionLogin);
    profileMenu -> addAction(ui -> actionLogout);
    deleteAccountAction = profileMenu -> addAction("Delete Account");
    deleteAccountAction -> setIcon(QIcon(":/icons/Icons/delete_24dp_FF0000_FILL0_wght400_GRAD0_opsz24.svg"));

    // Only meaningful while signed in -- kept in lockstep with
    // ui->actionLogout's enabled state everywhere that's toggled (below, and
    // in on_actionLogout_triggered()/onEnableActionUpload()/
    // onSessionExpired()), since deleting the account makes no sense
    // whenever logging out wouldn't either.
    deleteAccountAction -> setEnabled(false);
    connect(deleteAccountAction, &QAction::triggered, this, &MainWindow::onDeleteAccountTriggered);

    QActionGroup *themeGroup = new QActionGroup(this);
    for (QAction *action : {systemThemeAction, lightThemeAction, darkThemeAction})
    {
        action -> setCheckable(true);
        themeGroup -> addAction(action);
    }

    switch (Theme::mode())
    {
    case Theme::Mode::Light: lightThemeAction -> setChecked(true); break;
    case Theme::Mode::Dark: darkThemeAction -> setChecked(true); break;
    case Theme::Mode::System: default: systemThemeAction -> setChecked(true); break;
    }

    connect(systemThemeAction, &QAction::triggered, this, [this]() { setThemeMode(Theme::Mode::System); });
    connect(lightThemeAction, &QAction::triggered, this, [this]() { setThemeMode(Theme::Mode::Light); });
    connect(darkThemeAction, &QAction::triggered, this, [this]() { setThemeMode(Theme::Mode::Dark); });

    theWorkspace -> setMovable(true);
    theWorkspace -> setTabsClosable(true);
    theWorkspace -> setTabShape(QTabWidget::Triangular);

    // Connectors
    connect(theWorkspace, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);

    localFiles -> setHeaderHidden(true);
    localFiles -> setColumnCount(1);
    localFiles -> setSelectionMode(QAbstractItemView::ExtendedSelection);

    cloudFiles -> setHeaderHidden(true);
    cloudFiles -> setColumnCount(1);
    cloudFiles -> setSelectionMode(QAbstractItemView::ExtendedSelection);

    // Files are double-click-to-open but previously gave zero indication of
    // that -- no cursor change, no hover feedback, same as any inert label.
    // Hand cursor matches the treatment already used on the Login button
    // (loginwindow.ui) for other clickable things; the hover highlight's
    // actual color (originally a hardcoded low-alpha white) is filled in by
    // applyTheme() below, since a white overlay meant to stay subtle against
    // a dark background does the opposite against a light one.
    //
    // Cursor is scoped to actual rows via eventFilter() rather than a plain
    // setCursor() here -- that would cover the tree's whole viewport, hand
    // included over the empty space below the last item, which is
    // misleading since there's nothing to click there.
    localFiles -> viewport() -> setMouseTracking(true);
    cloudFiles -> viewport() -> setMouseTracking(true);
    localFiles -> viewport() -> installEventFilter(this);
    cloudFiles -> viewport() -> installEventFilter(this);

    // Installed on the whole application, not one specific widget, so this
    // reaches every button anywhere -- toolbar actions (QToolButton, once
    // ui->toolBar renders them), Profile/View menu items, dialog buttons in
    // Settings/LoginWindow, even QMessageBox's own standard buttons -- with
    // no need to touch each dialog's own construction code individually, and
    // automatically covering anything added later too. See eventFilter()
    // for the actual QAbstractButton handling.
    qApp -> installEventFilter(this);


    connect(localFiles, &QTreeWidget::itemDoubleClicked, this, &MainWindow::localFilesItemClicked);
    connect(cloudFiles, &QTreeWidget::itemDoubleClicked, this, &MainWindow::cloudFilesItemClicked);
    connect(storage, &Storage::cloudFilesCleared, cloudFiles, &QTreeWidget::clear);
    connect(storage, &Storage::setCloudFiles, this, &MainWindow::onSetCloudFiles);
    connect(storage, &Storage::setDownloadFile, this, &MainWindow::onDownloadFile);
    connect(storage, &Storage::uploadSucceeded, this, &MainWindow::onUploadSucceeded);
    connect(storage, &Storage::uploadFailed, this, &MainWindow::onUploadFailed);
    connect(storage, &Storage::uploadBatchFinished, this, &MainWindow::onUploadBatchFinished);
    connect(storage, &Storage::deleteSucceeded, this, &MainWindow::onDeleteSucceeded);
    connect(storage, &Storage::deleteFailed, this, &MainWindow::onDeleteFailed);
    connect(storage, &Storage::deleteBatchFinished, this, &MainWindow::onDeleteBatchFinished);
    connect(storage, &Storage::listFilesFailed, this, &MainWindow::onListFilesFailed);
    connect(storage, &Storage::downloadFailed, this, &MainWindow::onDownloadFailed);
    connect(storage, &Storage::tokenRefreshRequired, this, &MainWindow::onTokenRefreshRequired);
    connect(storage, &Storage::backendLoginSucceeded, this, &MainWindow::onBackendLoginSucceeded);
    connect(storage, &Storage::backendLoginFailed, this, &MainWindow::onBackendLoginFailed);
    connect(storage, &Storage::accountDeleteSucceeded, this, &MainWindow::onBackendAccountDeleted);
    connect(storage, &Storage::accountDeleteFailed, this, &MainWindow::onBackendAccountDeleteFailed);
    connect(loginWindow, &LoginWindow::enableActionUpload, this, &MainWindow::onEnableActionUpload);
    connect(loginWindow, &LoginWindow::idTokenRefreshed, this, &MainWindow::onIdTokenRefreshed);
    connect(loginWindow, &LoginWindow::sessionExpired, this, &MainWindow::onSessionExpired);
    connect(loginWindow, &LoginWindow::accountDeleted, this, &MainWindow::onFirebaseAccountDeleted);
    connect(loginWindow, &LoginWindow::accountDeletionFailed, this, &MainWindow::onFirebaseAccountDeleteFailed);

    // Silently signs back in from a previous run, if a session was saved --
    // a no-op otherwise (nothing saved). Deliberately after every connect()
    // above: the actual response only ever arrives asynchronously once the
    // event loop is running (well after this constructor returns), but
    // keeping it below everything it depends on avoids any doubt about that.
    loginWindow -> restoreSession();

    if (!settings.contains("splitterDimensions"))
        splitter -> setSizes({100, 100});

    // Not letting the workspace completely collapse
    splitter -> setCollapsible(1, false);
    // Setting minimum width for the workspace
    theWorkspace -> setMinimumWidth(100);

    // Remembering the previous sizes of each layout
    splitter -> restoreState(settings.value("splitterDimensions").toByteArray());

    // restoreState() above doesn't just restore pane sizes -- it also
    // restores whatever handle width was in effect when that state was
    // saved (confirmed: saveState()/restoreState() round-trips
    // handleWidth() same as sizes). Any state saved before this file
    // started pinning it to 4px would silently put it back to the
    // platform's old default (7px on macOS) the instant this line runs, so
    // it's re-asserted here, after restoreState(), to actually win.
    // theVault has no restoreState() call of its own, so it isn't affected.
    splitter -> setHandleWidth(4);

    // Seeds icons/stylesheets for the initial paint -- deliberately last in
    // the constructor, since applyTheme() touches splitter/theVault and the
    // file trees (among other things constructed above), and calling it any
    // earlier than everything it touches exists is a use of uninitialized
    // member pointers. Can't rely solely on
    // onColorSchemeChanged() for this either, since Theme::setMode() near
    // the top only emits colorSchemeChanged when the effective scheme
    // actually changes.
    applyTheme(Theme::isDark());
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_actionThe_Vault_triggered()
{
    if (theVault -> isHidden())
        theVault -> show();
    else
        theVault -> hide();
}


void MainWindow::closeTab(int index)
{
    curr = qobject_cast<MonacoEditor*>(theWorkspace -> widget(index));

    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);
    msgBox.setEscapeButton(QMessageBox::Cancel);

    filePath = curr -> property("filePath").toString();

    if (curr -> property("isCloudFile").toBool())
    {
        // Nothing to reconcile if the tab's content matches what was last
        // downloaded/uploaded -- mirrors the unchanged-file fast path just
        // below for local files.
        if (!curr -> isModified())
        {
            theWorkspace -> removeTab(index);
            delete curr;
            return;
        }

        QMessageBox msgBox(this);
        msgBox.setIcon(QMessageBox::Information);

        QPushButton *uploadButton = msgBox.addButton("Upload", QMessageBox::AcceptRole);
        msgBox.setDefaultButton(uploadButton);

        QPushButton *saveButton = msgBox.addButton("Save", QMessageBox::ActionRole);

        QPushButton *closeButton = msgBox.addButton("Close Tab", QMessageBox::ActionRole);

        msgBox.addButton(QMessageBox::Cancel);
        msgBox.setEscapeButton(QMessageBox::Cancel);

        msgBox.setText("Choose whether to upload or save the document.");
        msgBox.exec();

        if (msgBox.clickedButton() -> text() == uploadButton->text())
            storage -> uploadFile(curr -> toPlainText().toUtf8(), curr -> property("cloudFileName").toString());
        else if (msgBox.clickedButton() -> text() == saveButton->text())
            on_actionSave_triggered();
        else if (msgBox.clickedButton()->text() == closeButton->text())
            theWorkspace -> removeTab(index);

        return;
    }

    if (filePath == "")
    {
        if ((curr -> toPlainText()).isEmpty())
        {
            theWorkspace -> removeTab(index);
            delete curr;
            return;
        }
        else
            msgBox.setText("The document has not been saved.");
    }
    else
    {
        QFile closeFile(filePath);

        if (!closeFile.open(QIODevice::ReadOnly | QIODevice::Text) || curr -> toPlainText() != closeFile.readAll())
            msgBox.setText("The document has been modified.");
        else
        {
            theWorkspace -> removeTab(index);
            delete curr;
            return;
        }
    }

    msgBox.setInformativeText("Do you want to save your changes?");

    int reply = msgBox.exec();

    if (reply == QMessageBox::Discard)
    {
        theWorkspace->removeTab(index);
        delete curr;
        return;
    }
    else if (reply == QMessageBox::Cancel)
        return;
    else if (reply == QMessageBox::Save)
    {
        on_actionSave_triggered();
        if (!filePath.isEmpty())
        {
            theWorkspace->removeTab(index);
            delete curr;
        }
    }
}


QIcon MainWindow::iconForFileName(const QString &fileName)
{
    // Extensions this C++ IDE actually recognizes -- C++ source/header
    // variants, plus the docs commonly kept alongside C++ code. Anything
    // else falls through to a null QIcon (no icon shown), same as before.
    //
    // Icons are pulled from VS Code's own default "Seti" icon theme
    // (microsoft/vscode, extensions/theme-seti -- MIT licensed, ultimately
    // sourced from jesseweed/seti-ui) so files look the way they would in
    // VS Code itself. Headers split into two icons, matching Seti's own
    // distinction: plain .h gets the "C" glyph ("_c_1"), while
    // .hpp/.hh/.hxx/.h++ get the "C++" glyph ("_cpp_1") -- both purple,
    // vs. blue for actual source files. .txt falls back to Seti's generic
    // gray "_default" file glyph.
    static const QSet<QString> cppSourceExtensions = {"cpp", "cc", "cxx", "c++"};
    static const QSet<QString> cppPlusPlusHeaderExtensions = {"hpp", "hh", "hxx", "h++"};

    QString suffix = QFileInfo(fileName).suffix().toLower();

    if (cppSourceExtensions.contains(suffix))
        return loadFileTreeIcon(":/icons/Icons/seti_cpp_24dp_519ABA.svg");
    if (suffix == "h")
        return loadFileTreeIcon(":/icons/Icons/seti_h_24dp_A074C4.svg");
    if (cppPlusPlusHeaderExtensions.contains(suffix))
        return loadFileTreeIcon(":/icons/Icons/seti_hpp_24dp_A074C4.svg");
    if (suffix == "md")
        return loadFileTreeIcon(":/icons/Icons/seti_markdown_24dp_519ABA.svg");
    if (suffix == "txt")
        // The one Seti icon actually swapped per-theme (see applyTheme()) --
        // everything else above keeps its VS Code Seti color regardless of
        // theme, same as VS Code itself does.
        return loadFileTreeIcon(currentThemeIsDark
            ? ":/icons/Icons/seti_default_24dp_D4D7D6.svg"
            : ":/icons/Icons/seti_default_24dp_6E7681.svg");

    return QIcon();
}


QIcon MainWindow::iconForExecutable()
{
    return loadFileTreeIcon(currentThemeIsDark
        ? ":/icons/Icons/output_circle_24dp_FFFFFF_FILL0_wght400_GRAD0_opsz24.svg"
        : ":/icons/Icons/output_circle_24dp_000000_FILL0_wght400_GRAD0_opsz24.svg");
}


QIcon MainWindow::loadFileTreeIcon(const QString &path)
{
    QIcon icon;
    icon.addFile(path, QSize(), QIcon::Normal);
    icon.addFile(path, QSize(), QIcon::Selected);
    return icon;
}


void MainWindow::wireModifiedIndicator(MonacoEditor *tab)
{
    connect(tab, &MonacoEditor::modifiedChanged, this, [this, tab](bool modified) {
        int index = theWorkspace -> indexOf(tab);
        if (index == -1)
            return; // tab's been closed since; nothing left to update

        QString title = theWorkspace -> tabText(index);
        if (title.startsWith("● "))
            title.remove(0, 2);
        if (modified)
            title.prepend("● ");

        theWorkspace -> setTabText(index, title);
    });
}


void MainWindow::newFileTab(const QString &titlePrefix, const QString &defaultExtension)
{
    QString title = defaultExtension.isEmpty()
        ? QString("%1 %2").arg(titlePrefix).arg(theWorkspace -> count() + 1)
        : QString("%1 %2.%3").arg(titlePrefix).arg(theWorkspace -> count() + 1).arg(defaultExtension);

    theWorkspace -> addTab(new MonacoEditor(), title);
    curr = qobject_cast<MonacoEditor*>(theWorkspace -> currentWidget());
    curr -> setProperty("filePath", QVariant(""));

    // Read back by on_actionSave_triggered()'s Save-As fallback so a blank
    // tab saved with no extension typed lands on the extension it was
    // actually created for, instead of always defaulting to .cpp.
    curr -> setProperty("defaultExtension", QVariant(defaultExtension.isEmpty() ? QStringLiteral("cpp") : defaultExtension));

    theWorkspace -> setCurrentIndex(theWorkspace -> count() - 1);

    wireModifiedIndicator(curr);
}


void MainWindow::on_actionNew_File_triggered()
{
    newFileTab("Tab", "");
}


void MainWindow::on_actionNew_Text_File_triggered()
{
    newFileTab("Untitled", "txt");
}


void MainWindow::on_actionNew_Markdown_File_triggered()
{
    newFileTab("Untitled", "md");
}


void MainWindow::on_actionOpen_triggered()
{
    // Scoped to the extensions this IDE actually recognizes (see
    // iconForFileName()) -- deliberately no "All Files" catch-all, this is
    // a C++ IDE, not a general-purpose file browser.
    filePath = QFileDialog::getOpenFileName(this, "", projectFolderPath,
        "C++ Source Files (*.cpp *.cc *.cxx *.c++);;"
        "Header Files (*.h *.hpp *.hh *.hxx *.h++);;"
        "Docs (*.md *.txt)");
    QFile openFile(filePath);

    if (filePath.isEmpty())
        return;

    if (!openFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        statusBar() -> showMessage("Couldn't open \"" + QFileInfo(filePath).fileName() + "\".", 4000);
        return;
    }

    info = QFileInfo(filePath);
    theWorkspace->addTab(new MonacoEditor(), info.fileName());
    theWorkspace -> setCurrentIndex(theWorkspace -> count() - 1);

    curr = qobject_cast<MonacoEditor*>(theWorkspace -> currentWidget());
    curr -> setPlainText(openFile.readAll()); // resets the modified flag internally -- loading existing content isn't an edit

    curr -> setProperty("filePath", QVariant(filePath));

    wireModifiedIndicator(curr);

    openFile.close();
}


void MainWindow::on_actionOpen_Folder_triggered()
{
    QDir dir(projectFolderPath);
    QString folderPath = QFileDialog::getExistingDirectory(this, "", dir.path());

    if (folderPath.isEmpty())
        return;

    dir = QDir(folderPath);

    QTreeWidgetItem* root = new QTreeWidgetItem(localFiles);
    root -> setText(0, dir.dirName());
    root -> setIcon(0, QIcon::fromTheme("folder-open"));

    QDirIterator it(dir.path(), QDir::Files | QDir::NoDotAndDotDot);

    while (it.hasNext())
    {
        it.next();
        QTreeWidgetItem* child = new QTreeWidgetItem(root);
        child -> setText(0, it.fileName());

        info = QFileInfo(it.filePath());
        if (info.isExecutable())
            child -> setIcon(0, iconForExecutable());
        else
            child -> setIcon(0, iconForFileName(it.fileName()));

        child -> setData(0, Qt::UserRole, it.filePath());
    }
}


void MainWindow::on_actionSave_triggered()
{
    if (theWorkspace -> currentIndex() == -1)
        return;

    curr = qobject_cast<MonacoEditor*>(theWorkspace -> currentWidget());

    if (curr -> toPlainText().isEmpty())
    {
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Information);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.setEscapeButton(QMessageBox::Ok);
        msgBox.setText("This file is empty.");
        msgBox.exec();

        return;
    }

    bool isCloudTab = curr -> property("isCloudFile").toBool();

    // Cloud tabs have no real local path -- property("filePath") holds the
    // backend's numeric file id, not a disk location. "Save" here means
    // exporting the cloud file's current content to a new local file (e.g.
    // to get a copy back after deleting the local original), never a sync
    // back to the cloud -- that's what Upload is for. Always prompts for a
    // location, pre-filled with the cloud file's name, and deliberately
    // leaves the tab's filePath/isCloudFile properties and modified state
    // untouched below: this is a one-off export, not a conversion, and the
    // "unsaved changes" dot should still only ever clear once the content
    // actually reaches the cloud (see onUploadSucceeded()).
    QString targetPath;

    if (isCloudTab)
    {
        targetPath = QFileDialog::getSaveFileName(this, "",
            QDir(projectFolderPath).filePath(curr -> property("cloudFileName").toString()),
            "C++ Source Files (*.cpp *.cc *.cxx *.c++);;"
            "Header Files (*.h *.hpp *.hh *.hxx *.h++);;"
            "Docs (*.md *.txt)");

        if (targetPath.isEmpty())
            return;

        // Cloud files are always .cpp (see the backend's upload validation),
        // so the pre-filled name already carries the right extension --
        // this only matters if the user typed over it with something
        // unrecognized.
        if (iconForFileName(QFileInfo(targetPath).fileName()).isNull())
            targetPath += ".cpp";
    }
    else
    {
        filePath = curr -> property("filePath").toString();

        // Also re-prompts if the remembered path no longer points at a real
        // file (deleted externally since the last save) -- QFile's
        // WriteOnly mode creates a missing file rather than failing, so
        // without this check the next save would just silently resurrect it
        // at the exact same location instead of letting the user pick
        // somewhere else.
        if (filePath == "" || !QFileInfo::exists(filePath))
        {
            filePath = QFileDialog::getSaveFileName(this, "", projectFolderPath,
                "C++ Source Files (*.cpp *.cc *.cxx *.c++);;"
                "Header Files (*.h *.hpp *.hh *.hxx *.h++);;"
                "Docs (*.md *.txt)");

            if (filePath.isEmpty())
                return;

            // Only fall back to an extension when nothing recognized was
            // typed/chosen at all -- respects an explicit .h/.md/.txt name
            // instead of overriding it. The fallback itself comes from
            // whichever "New ___ File" action created this tab (newFileTab()),
            // so a blank Markdown tab saved as bare "notes" lands on notes.md
            // rather than always defaulting to .cpp.
            if (iconForFileName(QFileInfo(filePath).fileName()).isNull())
            {
                QString defaultExtension = curr -> property("defaultExtension").toString();
                if (defaultExtension.isEmpty())
                    defaultExtension = "cpp";
                filePath += "." + defaultExtension;
            }

            curr -> setProperty("filePath", QVariant(filePath));

            info = QFileInfo(filePath);
            theWorkspace -> setTabText(theWorkspace -> currentIndex(), info.fileName());

            // Deliberately falls through to the write below instead of
            // returning here -- this used to return right after picking a
            // filename, which meant the very first save of a new file renamed
            // the tab but never actually wrote it to disk.
        }

        targetPath = filePath;
    }


    QFile saveFile(targetPath);

    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, "Save Failed",
            "Couldn't write to \"" + QFileInfo(targetPath).fileName() + "\":\n" + saveFile.errorString());
        return;
    }

    QTextStream out(&saveFile);
    out << curr -> toPlainText();

    saveFile.flush();
    saveFile.close();

    if (!isCloudTab)
        curr -> setModified(false);
}


void MainWindow::on_actionUndo_triggered()
{
    if (theWorkspace -> currentIndex() != -1)
        qobject_cast<MonacoEditor*>(theWorkspace -> currentWidget()) -> undo();
}


void MainWindow::on_actionRedo_triggered()
{
    if (theWorkspace -> currentIndex() != -1)
        qobject_cast<MonacoEditor*>(theWorkspace -> currentWidget()) -> redo();
}


void MainWindow::on_actionCut_triggered()
{
    if (theWorkspace -> currentIndex() != -1)
        qobject_cast<MonacoEditor*>(theWorkspace -> currentWidget()) -> cut();
}


void MainWindow::on_actionCopy_triggered()
{
    if (theWorkspace -> currentIndex() != -1)
        qobject_cast<MonacoEditor*>(theWorkspace -> currentWidget()) -> copy();
}


void MainWindow::on_actionPaste_triggered()
{
    if (theWorkspace -> currentIndex() != -1)
        qobject_cast<MonacoEditor*>(theWorkspace -> currentWidget()) -> paste();
}


void MainWindow::on_actionToggle_Comment_triggered()
{
    if (theWorkspace -> currentIndex() == -1)
        return;

    // Monaco's own comment-toggle command replaces the hand-rolled
    // line-comment logic this used to have -- it's language-aware and
    // already knows how to toggle a selection the same way VS Code does.
    // This action has no shortcut of its own anymore (see mainwindow.ui) --
    // Ctrl+/ is left to Monaco's own built-in binding for the same command
    // when the editor has focus, rather than having both fire for the same
    // keypress. This slot is now only reachable via the menu/toolbar entry.
    qobject_cast<MonacoEditor*>(theWorkspace -> currentWidget()) -> toggleCommentSelection();
}


void MainWindow::on_actionClose_File_triggered()
{
    if (theWorkspace->currentIndex() != -1)
        closeTab(theWorkspace->currentIndex());
    else
        this -> close();
}


void MainWindow::on_actionLogin_triggered()
{
    loginWindow -> setModal(true);
    loginWindow -> exec();
}


void MainWindow::on_actionLogout_triggered()
{
    loginWindow -> logOut();
    storage -> setIdToken(QString());

    ui -> actionUpload -> setEnabled(false);
    ui -> actionRetry -> setEnabled(false);
    ui -> actionLogout -> setEnabled(false);
    ui -> actionUpload_File_to_Cloud -> setEnabled(false);
    ui -> actionDelete_File_from_Cloud -> setEnabled(false);
    deleteAccountAction -> setEnabled(false);

    cloudFiles -> clear();

    // Deliberately not reopening the login dialog (unlike onSessionExpired())
    // -- this was a deliberate action, not an error state that needs fixing
    // before the app is usable again.
    ui -> statusbar -> showMessage("Logged out.", 3000);
}


void MainWindow::onDeleteAccountTriggered()
{
    QMessageBox::StandardButton reply = QMessageBox::warning(this, "Delete Account",
        "This permanently deletes your account and every file you have stored in the cloud. "
        "This cannot be undone.\n\nAre you sure you want to continue?",
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

    if (reply != QMessageBox::Yes)
        return;

    // Disabled for the duration of the request so a second click can't fire
    // a second deletion while the first is still in flight -- re-enabled by
    // onBackendAccountDeleteFailed() if it doesn't pan out, left disabled by
    // every other outcome since the account (or at least its data) is gone
    // by then regardless.
    deleteAccountAction -> setEnabled(false);
    ui -> statusbar -> showMessage("Deleting account...", 0);
    storage -> deleteAccount();
}


void MainWindow::onBackendAccountDeleted()
{
    // The users row and every cloud file it owned are gone. Last step is
    // deleting the Firebase identity itself -- see onFirebaseAccountDeleted()/
    // onFirebaseAccountDeleteFailed() for how the local session gets torn
    // down once that settles.
    loginWindow -> deleteAccount();
}


void MainWindow::onBackendAccountDeleteFailed(const QString &errorString)
{
    // Nothing was actually deleted -- safe to just report and let the user
    // retry (same reasoning as onBackendLoginFailed()/onDeleteFailed()).
    deleteAccountAction -> setEnabled(true);
    QMessageBox::warning(this, "Delete Account Failed", errorString);
}


void MainWindow::onFirebaseAccountDeleted()
{
    // Same UI teardown as on_actionLogout_triggered(), minus
    // loginWindow->logOut() -- LoginWindow already tore down its own session
    // as part of deleteAccount() succeeding (see its comment).
    storage -> setIdToken(QString());

    ui -> actionUpload -> setEnabled(false);
    ui -> actionRetry -> setEnabled(false);
    ui -> actionLogout -> setEnabled(false);
    ui -> actionUpload_File_to_Cloud -> setEnabled(false);
    ui -> actionDelete_File_from_Cloud -> setEnabled(false);
    deleteAccountAction -> setEnabled(false);

    cloudFiles -> clear();

    ui -> statusbar -> showMessage("Account deleted.", 3000);
}


void MainWindow::onFirebaseAccountDeleteFailed(const QString &errorString)
{
    // The account's data is already gone (this only runs after
    // onBackendAccountDeleted()) -- just the Firebase identity itself
    // survived. Still tear down the local session, same as the success path
    // above, since there's nothing left worth staying signed in for.
    storage -> setIdToken(QString());

    ui -> actionUpload -> setEnabled(false);
    ui -> actionRetry -> setEnabled(false);
    ui -> actionLogout -> setEnabled(false);
    ui -> actionUpload_File_to_Cloud -> setEnabled(false);
    ui -> actionDelete_File_from_Cloud -> setEnabled(false);
    deleteAccountAction -> setEnabled(false);

    cloudFiles -> clear();

    ui -> statusbar -> showMessage("Account data deleted.", 3000);
    QMessageBox::warning(this, "Delete Account",
        "Your account and cloud files were deleted, but your Google sign-in couldn't be revoked:\n\n"
        + errorString + "\n\nYou've been signed out either way.");
}

void MainWindow::onEnableActionUpload(bool flag, const QString& idToken, const QString& uid)
{
    Q_UNUSED(uid); // the backend derives identity from the token itself, server-side

    ui -> actionUpload -> setEnabled(flag);
    ui -> actionRetry -> setEnabled(flag);
    ui -> actionLogout -> setEnabled(flag);
    ui -> actionUpload_File_to_Cloud -> setEnabled(flag);
    ui -> actionDelete_File_from_Cloud -> setEnabled(flag);
    deleteAccountAction -> setEnabled(flag);
    storage -> setIdToken(idToken);

    if (flag)
        establishBackendSession();
}


void MainWindow::establishBackendSession()
{
    QString backendUrl = settings.value("backendUrl").toString();
    if (backendUrl.isEmpty())
    {
        // No backend URL saved yet -- a fresh install, or Settings was never
        // opened on this machine. Nothing to try loginToBackend() against,
        // so don't fail loudly against an empty URL -- wait for the user to
        // fetch/enter one via Settings (Fetch is enabled now that they're
        // signed in) and hit Retry, or OK, which retries automatically --
        // see on_actionSettings_triggered().
        statusBar() -> showMessage("Signed in. Set your backend URL in Settings to sync files.", 5000);
        return;
    }

    // Establishes/refreshes the users row for this session before anything
    // else is allowed to touch /files -- see onBackendLoginSucceeded().
    pendingBackendSuccessMessage = "Successfully logged in.";
    storage -> loginToBackend();
}


void MainWindow::on_actionRetry_triggered()
{
    // Manual retry, for when the backend was unreachable and has since come
    // back (or Settings was updated without going through its own OK-retries
    // path) -- reuses the exact same call establishBackendSession()/Settings
    // do, so success/failure feedback (onBackendLoginSucceeded/Failed) is
    // identical either way, aside from the success message below. No interim
    // "Retrying..." message -- loginToBackend() usually resolves fast enough
    // that it just gets instantly stomped on by the real result anyway.
    pendingBackendSuccessMessage = "Successfully refreshed.";
    storage -> loginToBackend();
}


void MainWindow::onBackendLoginSucceeded()
{
    statusBar() -> showMessage(pendingBackendSuccessMessage, 2000);
    storage -> listFiles();
}


void MainWindow::onBackendLoginFailed(const QString &errorString)
{
    // Deliberately doesn't touch actionUpload/actionLogout/etc. -- the
    // backend being unreachable says nothing about whether the sign-in
    // itself (Firebase/Google) is still valid, which is what those actually
    // track. Disabling them here used to also disable actionRetry and
    // actionLogout, trapping the user: unable to retry *or* log out until
    // the backend happened to come back on its own.
    statusBar() -> showMessage("Logged in successfully, but the cloud backend is unreachable.", 5000);
    QMessageBox::warning(this, "Backend Unavailable", errorString);
}


void MainWindow::on_actionSettings_triggered()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Settings");
    dialog.setMinimumWidth(440);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    layout -> addWidget(new QLabel("Backend URL (e.g. an ngrok tunnel):"));

    QLineEdit *urlEdit = new QLineEdit(settings.value("backendUrl").toString());
    layout -> addWidget(urlEdit);

    QPushButton *fetchButton = new QPushButton("Fetch Latest from GitHub");
    // Gating this on actionUpload's enabled state is only safe now that
    // onBackendLoginFailed() no longer disables it -- actionUpload being
    // enabled now genuinely tracks "signed in", nothing else, so this can't
    // get stuck disabled by a backend outage the way it used to.
    fetchButton -> setEnabled(ui -> actionUpload -> isEnabled());
    layout -> addWidget(fetchButton);

    QLabel *statusLabel = new QLabel();
    statusLabel -> setWordWrap(true);
    layout -> addWidget(statusLabel);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout -> addWidget(buttonBox);

    // Scoped to this dialog's lifetime via the &dialog context object --
    // Qt auto-disconnects these the moment dialog goes out of scope below,
    // so there's nothing left listening to storage's signals afterwards.
    connect(fetchButton, &QPushButton::clicked, &dialog, [this, &dialog, fetchButton, statusLabel]() {
        fetchButton -> setEnabled(false);
        statusLabel -> setText("Fetching...");
        dialog.adjustSize();
        storage -> fetchDiscoveryUrl();
    });
    connect(storage, &Storage::discoveryUrlFetched, &dialog, [&dialog, urlEdit, fetchButton, statusLabel](const QString &url) {
        urlEdit -> setText(url);
        statusLabel -> setText("Fetched the latest URL -- click OK to use it.");
        fetchButton -> setEnabled(true);
        // Changing statusLabel's wrapped text doesn't automatically grow the
        // dialog window on its own -- without this, the new text just
        // overlaps whatever's below it instead of pushing the window taller.
        dialog.adjustSize();
    });
    connect(storage, &Storage::discoveryUrlFetchFailed, &dialog, [&dialog, fetchButton, statusLabel](const QString &errorString) {
        statusLabel -> setText("Couldn't fetch the latest URL: " + errorString);
        fetchButton -> setEnabled(true);
        dialog.adjustSize();
    });

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    QString newUrl = urlEdit -> text().trimmed();
    while (newUrl.endsWith('/'))
        newUrl.chop(1);

    settings.setValue("backendUrl", newUrl);
    storage -> setBackendUrl(newUrl);

    // setBackendUrl() alone doesn't hit any endpoint -- without this, a
    // change made here (fetched or typed in) sits unused until the next full
    // logout/login, which is the "need a refresh button" gap: already being
    // signed in, this is what actually retries against the new URL.
    if (ui -> actionUpload -> isEnabled())
    {
        // No interim "Reconnecting..." message -- same reasoning as
        // on_actionRetry_triggered(): it just gets stomped on instantly by
        // the real result on anything but a slow/failing connection.
        pendingBackendSuccessMessage = "Successfully reconnected.";
        storage -> loginToBackend();
    }
}


void MainWindow::on_actionRun_triggered()
{
    if (theWorkspace -> currentIndex() == -1)
    {
        statusBar() -> showMessage("Select a file to execute.", 3000);
        return;
    }

    QString runPath = resolveRunnablePath();
    if (runPath.isEmpty())
        return;

    Terminal* myTerminal = new Terminal(this, this);
    myTerminal -> runFile(runPath);
}


QString MainWindow::resolveRunnablePath()
{
    curr = qobject_cast<MonacoEditor*>(theWorkspace -> currentWidget());

    if (!curr -> property("isCloudFile").toBool())
    {
        // Unchanged: local tabs go through the normal Save flow (which only
        // prompts if this is a brand-new, never-saved tab) and then run
        // whatever filePath that leaves behind -- empty if that save was
        // cancelled, same as before this refactor.
        on_actionSave_triggered();
        return curr -> property("filePath").toString();
    }

    QString runPath = curr -> property("localRunPath").toString();

    // Same reasoning as on_actionSave_triggered()'s equivalent check: also
    // re-prompt if the remembered copy was deleted externally, rather than
    // silently resurrecting it at the exact same spot.
    if (runPath.isEmpty() || !QFileInfo::exists(runPath))
    {
        runPath = QFileDialog::getSaveFileName(this, "",
            QDir(projectFolderPath).filePath(curr -> property("cloudFileName").toString()),
            "C++ Source Files (*.cpp *.cc *.cxx *.c++);;"
            "Header Files (*.h *.hpp *.hh *.hxx *.h++);;"
            "Docs (*.md *.txt)");

        if (runPath.isEmpty())
            return QString();

        if (iconForFileName(QFileInfo(runPath).fileName()).isNull())
            runPath += ".cpp";

        curr -> setProperty("localRunPath", runPath);
    }

    QFile runFile(runPath);
    if (!runFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, "Run Failed",
            "Couldn't write to \"" + QFileInfo(runPath).fileName() + "\":\n" + runFile.errorString());
        return QString();
    }

    QTextStream out(&runFile);
    out << curr -> toPlainText();
    runFile.flush();
    runFile.close();

    return runPath;
}


void MainWindow::localFilesItemClicked(QTreeWidgetItem* item)
{
    filePath = item -> data(0, Qt::UserRole).toString();
    // Reuses iconForFileName()'s recognized-extension set as the single
    // source of truth for "is this a file type the IDE knows how to open" --
    // previously this only allowed anything containing ".cpp" (which also
    // loosely matched unrelated names like "notes.cpp.bak"), and silently
    // refused to open .h/.md/.txt files even though they're now shown with
    // their own icons in the tree.
    if (iconForFileName(QFileInfo(filePath).fileName()).isNull())
        return;
    info = QFileInfo(filePath);
    if (info.isExecutable())
    {
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Information);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.setEscapeButton(QMessageBox::Ok);
        msgBox.setText("This file cannot be opened.");
        msgBox.exec();
        return;
    }

    QFile openFile(filePath);

    if (openFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        theWorkspace -> addTab(new MonacoEditor(), info.fileName());
        theWorkspace -> setCurrentIndex(theWorkspace -> count() - 1);

        curr = qobject_cast<MonacoEditor*>(theWorkspace -> currentWidget());
        curr -> setPlainText(openFile.readAll()); // resets the modified flag internally -- loading existing content isn't an edit

        curr -> setProperty("filePath", QVariant(filePath));

        wireModifiedIndicator(curr);

        openFile.close();
    }
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    settings.setValue("splitterDimensions", splitter -> saveState());

    QMainWindow::closeEvent(event);
}


bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    QTreeWidget *tree = nullptr;
    if (watched == localFiles -> viewport())
        tree = localFiles;
    else if (watched == cloudFiles -> viewport())
        tree = cloudFiles;

    if (tree)
    {
        if (event -> type() == QEvent::MouseMove)
        {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            bool overItem = tree -> indexAt(mouseEvent -> pos()).isValid();
            tree -> viewport() -> setCursor(overItem ? Qt::PointingHandCursor : Qt::ArrowCursor);
        }
        else if (event -> type() == QEvent::Leave)
            tree -> viewport() -> setCursor(Qt::ArrowCursor);
    }

    // Every button in the app (toolbar actions, dialog buttons, QMessageBox's
    // own Yes/Cancel/etc.) gets a hand cursor while enabled, and the plain
    // arrow while disabled -- installed on qApp (see the constructor) rather
    // than each button individually, so this reaches every QPushButton/
    // QToolButton anywhere, including ones inside dialogs that don't exist
    // yet at startup. QEvent::Show catches a button's cursor the first time
    // it becomes visible (its enabled state may already be settled by
    // then -- constructing a button disabled-from-the-start never fires
    // EnabledChange on its own); QEvent::EnabledChange catches every
    // setEnabled() call afterward, whichever of the app's many scattered
    // call sites it comes from.
    //
    // QEvent::Enter re-asserts the same cursor right as the pointer actually
    // enters the button -- on macOS this is a harmless no-op (Show/
    // EnabledChange already had it right, so Enter just sets the identical
    // value again), but it's there for Windows: native/Vista-styled
    // QToolButtons re-resolve their cursor on WM_SETCURSOR as the OS paints
    // hover, which can silently override a cursor set proactively earlier
    // instead of at the moment of hover. Re-setting it on Enter forces that
    // resolution to land on ours.
    if (event -> type() == QEvent::Show || event -> type() == QEvent::EnabledChange
        || event -> type() == QEvent::Enter)
    {
        if (auto *button = qobject_cast<QAbstractButton*>(watched))
            button -> setCursor(button -> isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

    return QMainWindow::eventFilter(watched, event);
}


bool MainWindow::cloudFileExists(const QString &fileName) const
{
    for (int i = 0; i < cloudFiles -> topLevelItemCount(); i++)
    {
        if (cloudFiles -> topLevelItem(i) -> text(0) == fileName)
            return true;
    }

    return false;
}


void MainWindow::on_actionUpload_triggered()
{
    curr = qobject_cast<MonacoEditor*>(theWorkspace -> currentWidget());
    QList<QTreeWidgetItem*> ls = localFiles -> selectedItems();
    if (!curr && ls.size() == 0)
    {
        ui -> statusbar -> showMessage("Select a file to upload.", 3000);
        return;
    }
    if (curr && ls.size() == 0)
    {
        if (curr -> property("isCloudFile").toBool())
        {
            storage -> uploadFile(curr -> toPlainText().toUtf8(), curr -> property("cloudFileName").toString());
            return;
        }
        on_actionSave_triggered();

        // This tab isn't tracked as a cloud file (isCloudFile branch above
        // handles that case), so its name might coincidentally match an
        // unrelated cloud file -- cloud files are keyed on filename alone,
        // server-side, so uploading here would silently overwrite it.
        QString fileName = QFileInfo(curr -> property("filePath").toString()).fileName();
        if (cloudFileExists(fileName))
        {
            QMessageBox::StandardButton choice = QMessageBox::question(this, "File Already Exists",
                QString("A cloud file named \"%1\" already exists. Overwrite it?").arg(fileName),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

            if (choice != QMessageBox::Yes)
                return;
        }

        storage -> uploadFile(curr -> toPlainText().toUtf8(), curr -> property("filePath").toString());
        return;
    }

    QStringList unreadableFiles;
    QStringList skippedFiles;

    for (int i = 0; i < ls.size(); i++)
    {
        filePath = ls[i] -> data(0, Qt::UserRole).toString();
        QFile openFile(filePath);

        if (!openFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            unreadableFiles << QFileInfo(filePath).fileName();
            continue;
        }

        QString fileName = QFileInfo(filePath).fileName();
        if (cloudFileExists(fileName))
        {
            QMessageBox::StandardButton choice = QMessageBox::question(this, "File Already Exists",
                QString("A cloud file named \"%1\" already exists. Overwrite it?").arg(fileName),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);

            // Cancel abandons the rest of the batch outright, including any
            // files not yet reached -- not just the colliding ones.
            if (choice == QMessageBox::Cancel)
                break;

            if (choice == QMessageBox::No)
            {
                skippedFiles << fileName;
                continue;
            }
        }

        storage -> uploadFile(openFile.readAll(), filePath);
    }

    if (!unreadableFiles.isEmpty())
        QMessageBox::warning(this, "Upload Failed",
            "The following files could not be read and were skipped:\n" + unreadableFiles.join("\n"));

    if (!skippedFiles.isEmpty())
        QMessageBox::information(this, "Upload Skipped",
            "The following files already exist in the cloud and were left unchanged:\n" + skippedFiles.join("\n"));
}


void MainWindow::on_actionUpload_File_to_Cloud_triggered()
{
    // The File menu's "Upload File to Cloud" is the toolbar Upload button in
    // every respect -- same target resolution, same collision warning.
    on_actionUpload_triggered();
}


void MainWindow::on_actionDelete_File_from_Cloud_triggered()
{
    // Cloud-side only, deliberately: local files aren't a valid target here
    // (see the point 7 discussion -- unlike upload, delete doesn't try to
    // resolve a matching cloud file by filename from the local tree).
    QList<QTreeWidgetItem*> ls = cloudFiles -> selectedItems();

    QStringList fileIds;
    QStringList fileNames;

    if (ls.size() > 0)
    {
        for (QTreeWidgetItem *item : ls)
        {
            fileIds << item -> data(0, Qt::UserRole).toString();
            fileNames << item -> text(0);
        }
    }
    else
    {
        curr = qobject_cast<MonacoEditor*>(theWorkspace -> currentWidget());
        if (curr && curr -> property("isCloudFile").toBool())
        {
            fileIds << curr -> property("filePath").toString();
            fileNames << curr -> property("cloudFileName").toString();
        }
    }

    if (fileIds.isEmpty())
    {
        ui -> statusbar -> showMessage("Select a cloud file to delete.", 3000);
        return;
    }

    QString message = fileIds.size() == 1
        ? QString("Delete the cloud copy of \"%1\"? This can't be undone.").arg(fileNames.first())
        : QString("Delete these %1 cloud files? This can't be undone.\n\n%2")
              .arg(fileIds.size()).arg(fileNames.join("\n"));

    QMessageBox::StandardButton choice = QMessageBox::question(this, "Delete File from Cloud", message,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (choice != QMessageBox::Yes)
        return;

    for (int i = 0; i < fileIds.size(); i++)
        storage -> deleteFile(fileIds[i], fileNames[i]);
}


void MainWindow::onDeleteSucceeded(const QString &fileId, const QString &fileName)
{
    Q_UNUSED(fileName);

    // Close every tab open on this file -- normally at most one, but
    // cloudFilesItemClicked() always opens a fresh tab rather than reusing
    // an existing one, so more than one is possible. Iterated backwards so
    // removing a tab doesn't invalidate the indices still to be checked.
    for (int i = theWorkspace -> count() - 1; i >= 0; i--)
    {
        MonacoEditor *tab = qobject_cast<MonacoEditor*>(theWorkspace -> widget(i));
        if (tab && tab -> property("isCloudFile").toBool() && tab -> property("filePath").toString() == fileId)
        {
            theWorkspace -> removeTab(i);
            delete tab;
        }
    }
}


void MainWindow::onDeleteFailed(const QString &fileName, const QString &errorString)
{
    QMessageBox::warning(this, "Delete Failed",
        QString("Could not delete \"%1\" from the cloud:\n%2").arg(fileName, errorString));
}


void MainWindow::onDeleteBatchFinished(int succeededCount)
{
    if (succeededCount <= 0)
        return; // nothing succeeded in this wave -- onDeleteFailed() already covers failures

    ui -> statusbar -> showMessage(
        succeededCount == 1 ? "Deleted 1 file from the cloud." : QString("Deleted %1 files from the cloud.").arg(succeededCount),
        4000);
}


void MainWindow::onSetCloudFiles(const QString &fileName, const QString &cloudFilePath)
{
    QTreeWidgetItem* child = new QTreeWidgetItem(cloudFiles);
    child -> setText(0, fileName);
    child -> setIcon(0, iconForFileName(fileName));
    child -> setData(0, Qt::UserRole, cloudFilePath);
}


void MainWindow::onUploadSucceeded(const QString &localFilePath, const QString &cloudFilePath)
{
    Q_UNUSED(cloudFilePath);
    // No per-file status message here -- see onUploadBatchFinished(), which
    // reports how many files succeeded once the whole wave settles.

    // Clears the "unsaved changes" dot on whichever open tab this upload was
    // for -- its content just reached the cloud, so it's no longer ahead of
    // what's saved there (same reasoning as on_actionSave_triggered()'s
    // setModified(false), just for the upload path instead of the disk-save
    // path). Matched by property rather than tab identity, since Storage
    // only reports the upload by path/name, not which tab (if any)
    // triggered it -- and which property to match on depends on how the
    // upload started (see on_actionUpload_triggered()): a cloud-tab
    // re-upload passes its cloudFileName (a display name, not a real path)
    // as localFilePath, while a local-file upload (tab or tree selection)
    // passes the real path.
    for (int i = 0; i < theWorkspace -> count(); i++)
    {
        MonacoEditor *tab = qobject_cast<MonacoEditor*>(theWorkspace -> widget(i));
        if (!tab)
            continue;

        bool isCloudTab = tab -> property("isCloudFile").toBool();
        bool matches = isCloudTab
            ? tab -> property("cloudFileName").toString() == localFilePath
            : tab -> property("filePath").toString() == localFilePath;

        if (matches)
            tab -> setModified(false);
    }
}


void MainWindow::onUploadBatchFinished(int succeededCount)
{
    if (succeededCount <= 0)
        return; // nothing succeeded in this wave -- onUploadFailed() already covers failures

    ui -> statusbar -> showMessage(
        succeededCount == 1 ? "Uploaded 1 file" : QString("Uploaded %1 files").arg(succeededCount),
        4000);
}


void MainWindow::onUploadFailed(const QString &localFilePath, const QString &errorString)
{
    QMessageBox::warning(this, "Upload Failed",
        QString("Could not upload \"%1\":\n%2").arg(QFileInfo(localFilePath).fileName(), errorString));
}


void MainWindow::onListFilesFailed(const QString &errorString)
{
    ui -> statusbar -> showMessage("Could not refresh cloud files: " + errorString, 5000);
}


void MainWindow::onDownloadFailed(const QString &errorString, QObject *targetTab)
{
    // The tab opened for this download is now permanently empty and useless
    // -- close it instead of leaving a dead placeholder tab behind. It may
    // already be gone (user closed it while the download was in flight), in
    // which case targetTab is null and there's nothing to clean up.
    MonacoEditor *tab = qobject_cast<MonacoEditor*>(targetTab);
    if (tab)
    {
        int index = theWorkspace -> indexOf(tab);
        if (index != -1)
            theWorkspace -> removeTab(index);
        delete tab;
    }

    QMessageBox::warning(this, "Download Failed", "Could not download the file:\n" + errorString);
}


void MainWindow::onTokenRefreshRequired()
{
    // Shown indefinitely (timeout 0) -- it gets naturally replaced once the
    // retried request resolves, via onUploadSucceeded/onUploadFailed/etc.
    ui -> statusbar -> showMessage("Session expired — refreshing, please wait...", 0);
    loginWindow -> refreshSessionNow();
}


void MainWindow::onIdTokenRefreshed(const QString &idToken)
{
    // Updates the token and transparently redoes everything that was queued
    // waiting on this refresh.
    storage -> resumeAfterTokenRefresh(idToken);
}


void MainWindow::onSessionExpired()
{
    if (handlingSessionExpiry)
        return;
    handlingSessionExpiry = true;

    storage -> abandonPendingRetries();

    ui -> actionUpload -> setEnabled(false);
    ui -> actionRetry -> setEnabled(false);
    ui -> actionLogout -> setEnabled(false);
    ui -> actionUpload_File_to_Cloud -> setEnabled(false);
    ui -> actionDelete_File_from_Cloud -> setEnabled(false);
    deleteAccountAction -> setEnabled(false);
    cloudFiles -> clear();

    ui -> statusbar -> showMessage("Your session has expired. Please log in again.", 5000);

    loginWindow -> setModal(true);
    loginWindow -> exec();

    handlingSessionExpiry = false;
}

void MainWindow::cloudFilesItemClicked(QTreeWidgetItem *item)
{
    theWorkspace -> addTab(new MonacoEditor(), item->text(0));
    theWorkspace -> setCurrentIndex(theWorkspace -> count() - 1);

    filePath = item -> data(0, Qt::UserRole).toString();

    curr = qobject_cast<MonacoEditor*>(theWorkspace -> currentWidget());
    curr -> setProperty("filePath", QVariant(filePath));

    // filePath here is the backend's numeric file id, not a real path --
    // isCloudFile/cloudFileName are how the upload/close-tab flows tell this
    // tab apart from a tab backed by a real local file.
    curr -> setProperty("isCloudFile", true);
    curr -> setProperty("cloudFileName", item -> text(0));

    wireModifiedIndicator(curr);

    // Pass this exact tab through, rather than relying on "whichever tab is
    // active" when the (asynchronous) download eventually completes -- the
    // user may have switched to a different tab by then.
    storage -> downloadFile(filePath, curr);
}

void MainWindow::onDownloadFile(const QByteArray &response, QObject *targetTab)
{
    MonacoEditor *tab = qobject_cast<MonacoEditor*>(targetTab);
    if (!tab)
        return; // the tab this download was for has since been closed

    tab -> setPlainText(response); // resets the modified flag internally -- loading downloaded content isn't an edit
}


void MainWindow::on_actionClose_Folder_triggered()
{
    QList<QTreeWidgetItem*> ls = localFiles->selectedItems();
    for (int i = 0; i < ls.size(); i++)
    {
        if (ls[i] -> data(0, Qt::UserRole).toString() == "")
            delete ls[i];
    }
}


void MainWindow::setThemeMode(Theme::Mode mode)
{
    Theme::setMode(mode);
    applyTheme(Theme::isDark());
}


void MainWindow::onColorSchemeChanged()
{
    applyTheme(Theme::isDark());
}


void MainWindow::applyTheme(bool isDark)
{
    currentThemeIsDark = isDark;

    // Toolbar/menu icons are flat black/white glyphs (VS Code's Seti theme
    // has no "system-tinted" notion for these) -- swapped explicitly here
    // rather than via QPalette, which only reaches palette-driven native
    // widget chrome, not icon pixmaps.
    ui -> actionSettings -> setIcon(QIcon(isDark
        ? ":/icons/Icons/settings_24dp_FFFFFF_FILL0_wght400_GRAD0_opsz24.svg"
        : ":/icons/Icons/settings_24dp_000000_FILL0_wght400_GRAD0_opsz24.svg"));
    ui -> actionLogin -> setIcon(QIcon(isDark
        ? ":/icons/Icons/login_24dp_FFFFFF_FILL0_wght400_GRAD0_opsz24.svg"
        : ":/icons/Icons/login_24dp_000000_FILL0_wght400_GRAD0_opsz24.svg"));
    ui -> actionLogout -> setIcon(QIcon(isDark
        ? ":/icons/Icons/logout_24dp_FFFFFF_FILL0_wght400_GRAD0_opsz24.svg"
        : ":/icons/Icons/logout_24dp_000000_FILL0_wght400_GRAD0_opsz24.svg"));
    ui -> actionThe_Vault -> setIcon(QIcon(isDark
        ? ":/icons/Icons/folder_code_24dp_FFFFFF_FILL0_wght400_GRAD0_opsz24.svg"
        : ":/icons/Icons/folder_code_24dp_000000_FILL0_wght400_GRAD0_opsz24.svg"));
    ui -> actionUpload -> setIcon(QIcon(isDark
        ? ":/icons/Icons/cloud_upload_24dp_FFFFFF_FILL0_wght400_GRAD0_opsz24.svg"
        : ":/icons/Icons/cloud_upload_24dp_000000_FILL0_wght400_GRAD0_opsz24.svg"));
    ui -> actionRetry -> setIcon(QIcon(isDark
        ? ":/icons/Icons/refresh_24dp_FFFFFF_FILL0_wght400_GRAD0_opsz24.svg"
        : ":/icons/Icons/refresh_24dp_000000_FILL0_wght400_GRAD0_opsz24.svg"));

    // File-tree hover overlay and the "no folder/no files open" placeholder
    // text -- both were hardcoded white-on-dark, unreadable once the
    // background actually goes light.
    //
    // The ":selected" rule below is what keeps a selected row's highlight
    // from being clobbered by the ":hover" rule above the instant the mouse
    // moves over it: putting any stylesheet rule on QTreeWidget::item
    // switches Qt to CSS box-model painting for every pseudo-state of that
    // item, selected+hovered included, and with only ":hover" defined that
    // combined state fell through to the hover overlay instead of staying
    // selected. ":hover" and ":selected" are equal-specificity selectors
    // (one pseudo-class each on the same subcontrol), so on a combined
    // selected+hovered row Qt's cascade breaks the tie in favor of whichever
    // was declared last -- ":selected" is deliberately second here so it
    // wins that tie, without a separate ":selected:hover" rule.
    //
    // Light mode uses fixed grays (VS Code's own light-theme file-explorer
    // hover/selected colors, not macOS's native blue) rather than
    // "palette(highlight)" -- picked deliberately, per the user, over
    // deferring to the native highlight color, since forcing the app's
    // color scheme (Theme::setMode(Light), see theme.h) doesn't fully
    // replicate native rendering anyway (confirmed: explicit Light-mode
    // selection looked visibly different from System-mode-while-the-OS-is-
    // light, even though both are "light" and should look the same). Fixed
    // colors sidestep that mismatch entirely, since System-with-a-light-OS
    // and explicit Light both collapse to isDark == false here regardless.
    // Dark mode keeps deferring to the native highlight color.
    //
    // "color: palette(text);" on both rules is deliberate -- only the
    // background should ever change on hover/selection, per the user; file
    // names should read identically to a plain unselected row. Without it,
    // Qt's item delegate paints selected/hovered text using the palette's
    // "inactive highlighted text" color group, which comes out as a washed-
    // out gray rather than the normal text color. See loadFileTreeIcon() in
    // mainwindow.h for the equivalent fix on the icon side -- Qt dims icon
    // pixmaps for a selected row the same way unless told not to.
    QString fileTreeHoverStyle = isDark
        ? "QTreeWidget::item:hover { background: rgba(255,255,255,25); color: palette(text); }"
          "QTreeWidget::item:selected { background: palette(highlight); color: palette(text); }"
        : "QTreeWidget::item:hover { background: #E6E6E9; color: palette(text); }"
          "QTreeWidget::item:selected { background: #D7D7D9; color: palette(text); }";
    localFiles -> setStyleSheet(fileTreeHoverStyle);
    cloudFiles -> setStyleSheet(fileTreeHoverStyle);

    // #000000 in light mode, #FFFFFF in dark -- same mirrored-hex pattern as
    // placeholderStyle just above. Any stylesheet on QSplitter::handle drops
    // the active QStyle's native grip/shading in favor of plain CSS box-model
    // painting, which is why this is a flat fill rather than palette(...):
    // there's no more native rendering underneath for a palette role to tint.
    QString splitterHandleStyle = QString("QSplitter::handle { background-color: %1; }")
        .arg(isDark ? "#FFFFFF" : "#000000");
    splitter -> setStyleSheet(splitterHandleStyle);
    theVault -> setStyleSheet(splitterHandleStyle);

    refreshTreeIcons();

    // Keeps every already-open Monaco tab in lockstep -- a half-and-half
    // state (light chrome, dark editor or vice versa) is a regression, not
    // a valid in-between.
    for (int i = 0; i < theWorkspace -> count(); ++i)
    {
        MonacoEditor *tab = qobject_cast<MonacoEditor*>(theWorkspace -> widget(i));
        if (tab)
            tab -> applyTheme(isDark);
    }
}


void MainWindow::refreshTreeIcons()
{
    // Only the local files tree is walked here -- see the reasoning on the
    // refreshTreeIcons() declaration in mainwindow.h.
    for (int i = 0; i < localFiles -> topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *root = localFiles -> topLevelItem(i);
        for (int j = 0; j < root -> childCount(); ++j)
        {
            QTreeWidgetItem *child = root -> child(j);
            QString path = child -> data(0, Qt::UserRole).toString();
            if (path.isEmpty())
                continue;

            if (QFileInfo(path).isExecutable())
                child -> setIcon(0, iconForExecutable());
            else
                child -> setIcon(0, iconForFileName(child -> text(0)));
        }
    }
}

