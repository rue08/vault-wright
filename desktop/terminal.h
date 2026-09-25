#ifndef TERMINAL_H
#define TERMINAL_H

#include <QProcess>
#include <QSysInfo>
#include <QFileInfo>
#include <QWidget>

class MainWindow; // Forward declaration

class Terminal : public QWidget
{
    Q_OBJECT

public:
    explicit Terminal(MainWindow *mainWindow, QWidget *parent = nullptr);

    // Compiles and runs `filePath` -- resolved by MainWindow beforehand
    // (see MainWindow::resolveRunnablePath()), since only it knows how to
    // turn a cloud tab (which has no real local path of its own) into an
    // actual file on disk. filePath must be non-empty and already written
    // with the tab's current content.
    void runFile(const QString &filePath);

private slots:
    QString operatingSystem();

private:
    MainWindow *mainWindow;
    QProcess *myProcess; // Initialization moved to .cpp
    QString name = "";
    QString path = "";

    // Rejects path/filename components that contain characters which could
    // break out of the quoted context of a shell command (AppleScript on
    // macOS, cmd.exe on Windows) if interpolated directly. Returns false --
    // and leaves an explanatory message in errorOut -- if `value` isn't
    // safe to interpolate.
    static bool isSafeForShellInterpolation(const QString &value, QString *errorOut);
};

#endif // TERMINAL_H
