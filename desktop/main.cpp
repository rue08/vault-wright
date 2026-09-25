#include "mainwindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    // Recommended by Qt WebEngine before QApplication is constructed --
    // MonacoEditor embeds a QWebEngineView, and Chromium's compositor needs
    // GL contexts shared across windows/widgets to render correctly.
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication a(argc, argv);

    // Covers the taskbar/Alt-Tab/title-bar icon on Windows and Linux, and a
    // running-but-unbundled dev build on macOS. The packaged macOS .app and
    // Windows .exe additionally get their icon baked in via CMakeLists.txt
    // (MACOSX_BUNDLE_ICON_FILE / windows/app.rc), which is what Finder/
    // Explorer/Dock show before the app is even launched.
    a.setWindowIcon(QIcon(":/icons/Icons/app-icon.svg"));

    MainWindow w;
    w.show();
    return a.exec();
}
