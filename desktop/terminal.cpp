#include "terminal.h"
#include "mainwindow.h"
#include "monacoeditor.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QStatusBar>
#include <QMessageBox>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

Terminal::Terminal(MainWindow *mainWindow, QWidget *parent)
    : QWidget(parent),
    mainWindow(mainWindow)
{
    // Initialize properly with 'this' so it is deleted when Terminal is deleted
    myProcess = new QProcess(this);
}

QString Terminal::operatingSystem()
{
    return QSysInfo::productType();
}

bool Terminal::isSafeForShellInterpolation(const QString &value, QString *errorOut)
{
    // Both launch mechanisms below build a command by interpolating this
    // value into a quoted string (AppleScript on macOS, cmd.exe on
    // Windows), rather than passing it as a properly-escaped argument. Any
    // of these characters could let the interpolated value break out of
    // that quoting and inject additional commands -- e.g. a filename
    // synced down from the cloud that was crafted maliciously.
    static const QString dangerousChars = QStringLiteral("\"`$&|^%<>;");
    for (const QChar &c : value)
    {
        if (dangerousChars.contains(c))
        {
            if (errorOut)
                *errorOut = QString("Can't run this file: its path contains an unsupported character ('%1').").arg(c);
            return false;
        }
    }
    return true;
}

void Terminal::runFile(const QString &filePath)
{
    // 1. Get current file info -- filePath comes fully resolved from
    // MainWindow::resolveRunnablePath(), which is what actually knows
    // whether the current tab is a local file (real filePath property) or a
    // cloud one (no real local path -- resolved to a local export copy
    // instead). See its comment for why this can't just be read back off
    // the tab's own property the way it used to be.
    path = filePath;
    QFileInfo fi(path);
    name = fi.fileName();
    if (path.isEmpty() || name.isEmpty()) return;

    QString outputName = name;
    if (outputName.endsWith(".cpp"))
        outputName.chop(4);

    qsizetype len = name.size();
    path.chop(len);

    QString shellError;
    if (!isSafeForShellInterpolation(path, &shellError) || !isSafeForShellInterpolation(name, &shellError))
    {
        mainWindow -> statusBar() -> showMessage(shellError, 4000);
        return;
    }

    // Both the .bat (Windows) and trailer-script (macOS) temp files below
    // used to be named from applicationPid() alone, which is constant for
    // the app's whole lifetime -- every Run click overwrote the exact same
    // file. Harmless if the previous run had already finished, but if an
    // earlier console window was still actively executing that file (a slow
    // compile, or a long-running program) when Run was clicked again, this
    // run's write would land on a file the previous window's interpreter is
    // still reading -- cmd.exe in particular reads .bat files incrementally,
    // seeking through the file as it executes rather than loading it fully
    // upfront, so overwriting it mid-run can genuinely corrupt whatever that
    // still-running window does next. Appended here so every call gets its
    // own file regardless of what's still in flight from an earlier one.
    static int runCounter = 0;
    QString runId = QString("%1_%2").arg(QCoreApplication::applicationPid()).arg(++runCounter);

#if defined(Q_OS_WIN)
    // --- Windows ---
    // Prefer the MinGW-w64 toolchain bundled next to the app (copied in at
    // build/packaging time -- see windows/README.md) so Run works with no
    // install step on the user's machine. Falls back to PATH so local dev
    // builds still work before the toolchain has been dropped in.
    QString bundledGpp = QCoreApplication::applicationDirPath() + "/mingw64/bin/g++.exe";
    QString compiler = QFileInfo::exists(bundledGpp) ? bundledGpp : QStandardPaths::findExecutable("g++");

    if (compiler.isEmpty())
    {
        // A dialog, not just a status bar message -- this is the most likely
        // reason Run silently does "nothing" from a user's perspective (no
        // new window ever appears), and a fleeting statusbar line at the
        // bottom of the window is easy to miss entirely if what you're
        // actually watching for is a console window popping up.
        QMessageBox::warning(mainWindow, "No Compiler Found",
            "No C++ compiler found.\n\n"
            "Expected a bundled one at \"" + bundledGpp + "\", and none was found on PATH either. "
            "See BUILD.md in the VaultWright repository for bundling MinGW-w64, or install it "
            "yourself and make sure g++ is on PATH.");
        return;
    }

    // `path` (shared with the macOS branch above) always ends in a trailing
    // separator -- see the chop() near the top of this function. On macOS
    // that's a harmless trailing "/", but toNativeSeparators() would turn it
    // into "\" here, which is its own separate Windows quoting hazard.
    // Trimmed regardless, below, before it's ever quoted.
    QString trimmedPath = path;
    while (trimmedPath.endsWith('/') || trimmedPath.endsWith('\\'))
        trimmedPath.chop(1);

    QString nativePath = QDir::toNativeSeparators(trimmedPath);

    // Written to a .bat file rather than passed inline as a `cmd.exe /k
    // "<command>"` argument. cmd.exe's /K switch does its own ad hoc
    // parsing of whatever follows it (see `cmd /?`), entirely separate from
    // the normal CreateProcess argument-escaping convention QProcess uses to
    // build that argument in the first place -- the two disagree on what a
    // `\"` means. `cmd /?`'s own documented rule only cleanly preserves
    // quoting when the command tail contains *exactly two* quote characters;
    // compiling and then running needs four quoted paths (compiler, source,
    // output -- twice), so cmd.exe falls back to a much cruder "strip the
    // first quote, strip the last quote" heuristic that mangles everything
    // in between. A .bat file sidesteps this completely: its contents are
    // parsed by cmd's ordinary line parser -- the same rules as typing it
    // directly at a prompt -- with none of /K's special-cased quirks. Same
    // reasoning as the trailer script on the macOS branch below, just
    // covering the whole command here instead of one trailing step.
    //
    // -static is the actual fix for output silently going missing --
    // confirmed by hand: the bundled MinGW-w64 toolchain (windows/mingw64/,
    // see windows/README.md) links against runtime DLLs (libstdc++-6.dll,
    // libgcc_s_seh-1.dll, libwinpthread-1.dll) that live next to g++.exe
    // itself, not next to wherever the user's source file happens to be --
    // without -static, the compiled program can fail to actually run
    // correctly once launched from an arbitrary project folder, with
    // nothing printed and no visible error either. Statically linking bakes
    // those runtime pieces into the .exe itself, so it runs correctly
    // regardless of where it ends up.
    //
    // Compile and run back to chained on one line with `&&` -- a separate
    // `if errorlevel 1 goto :eof` line was tried in between at one point
    // (on a theory that same-line chaining was itself losing output), but
    // that turned out to be a false lead: the comparison that pointed at it
    // had accidentally also swapped which g++ was being used, not just
    // chained-vs-not on the same one. -static above is the real fix, so
    // there's no reason to keep the extra line (and its echoed-by-default
    // noise) around -- `&&` already gives the same "only run if the compile
    // succeeded" behavior on its own.
    QString batContents = QString("cd /d \"%1\"\n\"%2\" \"%3\" -o \"%4.exe\" -static && \"%4.exe\"\n")
                               .arg(nativePath, compiler, name, outputName);

    QString batPath = QDir::tempPath() + QString("/ide_run_%1.bat").arg(runId);
    QFile batFile(batPath);
    if (!batFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        mainWindow -> statusBar() -> showMessage("Couldn't write the run script to " + batPath, 4000);
        return;
    }
    batFile.write(batContents.toUtf8());
    batFile.close();

    // Launched directly via a configured QProcess rather than
    // QProcess::startDetached(fullCommand, arguments) (the shared call at
    // the bottom of this function, still used by the macOS branch below) --
    // Windows needs setCreateProcessArgumentsModifier() to fix two things
    // Qt's own defaults get wrong for a GUI app spawning a console
    // subprocess, neither of which the earlier `cmd /c start ""` wrapper
    // trick actually fixed (confirmed against Qt 6.10's qprocess_win.cpp):
    //
    // 1. QProcessPrivate::startProcess()/startDetached() both compute
    //    `dwCreationFlags = (GetConsoleWindow() ? 0 : CREATE_NO_WINDOW)` --
    //    since VaultWright.exe is a GUI app with no console of its own,
    //    that's *always* CREATE_NO_WINDOW here, meaning Qt explicitly asks
    //    Windows not to give the child any window at all. (The `start`
    //    wrapper got a window anyway because `start` is cmd.exe's own
    //    builtin, doing its own separate CreateProcess call for its target
    //    that ignores whatever flags launched the outer cmd.exe.)
    // 2. QProcessPrivate::createStartupInfo() *always* sets
    //    STARTF_USESTDHANDLES, falling back to this process's own
    //    GetStdHandle() values when nothing was explicitly redirected --
    //    and a GUI app's standard handles are invalid/NULL. Passed through
    //    to CreateProcess as-is, that poisons the whole descendant chain's
    //    stdout (cmd.exe -> g++.exe -> the compiled program itself) with a
    //    handle that doesn't point at any real console, *regardless* of
    //    whether the process otherwise got a new console window -- which is
    //    exactly what the `start` wrapper's window showed: compiling and
    //    launching genuinely succeeded, but nothing the compiled program
    //    itself printed ever reached it, because it wasn't writing to that
    //    window's buffer at all.
    //
    // Clearing STARTF_USESTDHANDLES here lets the new console's own default
    // I/O apply instead of the explicitly-passed broken handles, and
    // CREATE_NEW_CONSOLE (replacing CREATE_NO_WINDOW) is what actually
    // guarantees a fresh, visible window regardless of whatever console
    // context launched VaultWright itself (Explorer, a shortcut, or a
    // debugger/IDE that already attaches its own console for output
    // capture, like Qt Creator's Application Output pane) -- no more need
    // to route through `start` as an indirect way to get one.
    QProcess consoleProcess;
    consoleProcess.setProgram("cmd.exe");
    consoleProcess.setArguments({"/k", QDir::toNativeSeparators(batPath)});
    consoleProcess.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args -> flags &= ~static_cast<unsigned long>(CREATE_NO_WINDOW);
        args -> flags |= CREATE_NEW_CONSOLE;
        args -> startupInfo -> dwFlags &= ~STARTF_USESTDHANDLES;
        args -> inheritHandles = false;
    });

    if (!consoleProcess.startDetached())
    {
        mainWindow -> statusBar() -> showMessage(
            "Couldn't launch a console window to run this file.", 6000);
    }
    return;
#else
    // --- macOS ---
    QString fullCommand;
    QStringList arguments;

    if (QStandardPaths::findExecutable("g++").isEmpty())
    {
        mainWindow -> statusBar() -> showMessage(
            "g++ was not found on PATH. Install the Xcode Command Line Tools "
            "(run 'xcode-select --install' in Terminal) and try again.", 6000);
        return;
    }

    // We use AppleScript (osascript) to tell the Terminal app to run our command.
    // This forces a new native Terminal window to open.

    // 1. Create the command chain (Compile -> Run -> Read/Pause)
    // We add a pause at the end so the terminal doesn't close immediately if the program ends
    // fast, whether the compile/run actually succeeded or not. A bare 'read' blocks silently with
    // no indication of what's happening or that the user needs to do anything -- echoing a message
    // first makes it explicit that pressing Enter is what releases the terminal back to a normal
    // prompt (and is required before the next Run will work, since Run reuses this same window).
    //
    // The echo+read pair lives in a small trailer script rather than being typed inline, because
    // do script echoes back whatever text it's given as the literal "command line" before showing
    // its output -- typing "... ; echo '...' ; read" in directly would show that plumbing as part
    // of the command itself, right above its own output repeating it. Keeping the compile/run
    // chain itself inline (rather than wrapping the whole thing in a script) means the visible
    // command line still reads as the real thing you'd type by hand, not an opaque script
    // invocation -- only the trailing pause step is routed through the script.
    //
    // Not done via read's own -p flag: that flag isn't portable across shells -- bash treats -p as
    // "show this prompt", but zsh (the macOS default since Catalina) treats it as "read from a
    // coprocess" instead, which fails with "no coprocess".
    QString trailerContents = QStringLiteral(
        "echo \"Press Enter to exit this command execution...\"\n"
        "read\n"
    );

    QString trailerPath = QDir::tempPath() + QString("/ide_run_trailer_%1.sh").arg(runId);
    QFile trailerFile(trailerPath);
    if (!trailerFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        mainWindow -> statusBar() -> showMessage("Couldn't write the run script to " + trailerPath, 4000);
        return;
    }
    trailerFile.write(trailerContents.toUtf8());
    trailerFile.close();
    trailerFile.setPermissions(trailerFile.permissions() | QFile::ExeOwner);

    QString macCmd = QString("cd \\\"%1\\\" && g++ %2 -o %3 && \\\"%1\\\"%3 ; sh \\\"%4\\\"").arg(path, name, outputName, trailerPath);

    // 2. Wrap it in AppleScript
    fullCommand = "osascript";
    arguments << "-e" << "tell application \"Terminal\" to activate"
              << "-e" << QString("tell application \"Terminal\" to do script \"%1\" in window 1").arg(macCmd);

    // Note for macOS: You might need to grant your IDE "Automation" permission
    // for Terminal the first time you run this.

    // startDetached()'s return value was previously discarded -- if
    // launching fullCommand itself fails (not found, no permission, etc.),
    // that failed completely silently: no window, no message, nothing to
    // even suggest Run was clicked at all. Surfaced now so a launch failure
    // is at least visible somewhere, even without a detailed reason attached
    // (startDetached() doesn't provide one).
    //
    // Windows doesn't reach here at all -- its branch above launches its own
    // way (setCreateProcessArgumentsModifier() needs a live QProcess, not
    // the plain static startDetached() this call is) and returns early, so
    // this tail is macOS-only now, moved inside this #else specifically so
    // fullCommand/arguments (declared at the top of this branch) stay in
    // scope for it -- they used to be declared before the #if/#else split,
    // which compiled fine on macOS but left them undeclared on a Windows
    // build, since this code sat after the #endif and so was compiled
    // unconditionally on both platforms even though only macOS ever uses it.
    bool started = QProcess::startDetached(fullCommand, arguments);
    if (!started)
    {
        mainWindow -> statusBar() -> showMessage(
            "Couldn't launch \"" + fullCommand + "\" to run this file.", 6000);
    }
#endif
}

