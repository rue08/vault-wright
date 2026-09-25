#ifndef MONACOEDITOR_H
#define MONACOEDITOR_H

#include <QWidget>
#include <QString>

class QWebEngineView;
class QWebChannel;
class MonacoBridge;

// The QWebChannel-exposed object living on the JS side as `bridge` (see
// monaco-host/index.html). Kept private to this file/MonacoEditor -- nothing
// outside this pair ever needs to touch it directly.
//
// Direction of travel: JS calls the public slots below directly (that's
// what QWebChannel exposes to script); MonacoEditor connects to the
// signals to find out when something happened on the JS side. The
// loadContent/triggerAction signals below run the other way -- MonacoEditor
// emits them, and index.html's JS is what's connected to those.
class MonacoBridge : public QObject
{
    Q_OBJECT

public:
    explicit MonacoBridge(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void contentChanged(const QString &text) { emit userEdited(text); }
    void editorReady() { emit ready(); }
    void fontSizeChanged(int size) { emit fontSizeEdited(size); }

signals:
    void userEdited(const QString &text);
    void ready();
    void fontSizeEdited(int size);

    // Connected to from JS; not emitted from anywhere else in C++.
    void loadContent(const QString &text);
    void triggerAction(const QString &actionId);
    void themeChanged(bool isDark);
};


// Monaco-backed replacement for a workspace tab, embedding the real Monaco
// Editor (VS Code's editor core) via QWebEngineView instead of a native
// QPlainTextEdit. It replaced an earlier hand-rolled CodeEditor (a
// QPlainTextEdit subclass -- line numbers, bracket matching, auto-close,
// auto-indent, syntax highlighting via a QSyntaxHighlighter), all of which
// Monaco now provides itself, so those files were removed rather than kept
// around unused -- see git history if that native path is ever needed again.
//
// Text only ever leaves the JS side by the editor pushing it to us on every
// keystroke (onDidChangeModelContent in monaco-host/index.html calling
// bridge.contentChanged()), which we cache in `cachedText`. That's what
// makes toPlainText() a synchronous, ordinary getter despite the editor
// itself living across an async WebEngine/JS boundary -- everywhere in the
// app that used to call QPlainTextEdit::toPlainText() synchronously (save,
// upload, the close-tab "has this changed?" check) keeps working unchanged.
//
// `modified` is *not* a one-way "has this been touched" latch -- it's
// recomputed on every change as cachedText != savedText (savedText being
// whatever's last known to be on disk/uploaded, updated in setPlainText()
// on load and in setModified(false) on save). That's what makes undoing
// back to the exact saved text clear the tab's "unsaved changes" dot again,
// same as VS Code -- a plain "was it ever edited" flag can't do that, since
// it never has a reason to flip back to false on its own.
//
// The page load (QWebEngineView finishing navigation) and Monaco's own
// startup (its AMD loader pulling in editor.main, then monaco.editor.create)
// both happen asynchronously after this constructor returns. setPlainText()
// called before that's done just queues the text; it's pushed in once
// onEditorReady() fires.
//
// Font size (Cmd/Ctrl +/-/0 inside the editor, see monaco-host/index.html)
// is persisted to QSettings under "editorFontSize" so it survives across
// tabs and app restarts -- unlike content, it isn't queued through the
// bridge on startup, since it needs to be present for Monaco's very first
// paint. It's passed as a URL query parameter on the page load instead
// (see the constructor), which JS reads before calling monaco.editor.create().
class MonacoEditor : public QWidget
{
    Q_OBJECT

public:
    explicit MonacoEditor(QWidget *parent = nullptr);

    QString toPlainText() const { return cachedText; }
    void setPlainText(const QString &text);

    bool isModified() const { return modified; }
    void setModified(bool m);

    // Routed through Monaco's own command IDs via the bridge rather than
    // QWebEnginePage's WebActions, since undo/redo/comment-toggle need to
    // operate on Monaco's own edit-stack/language-aware logic, not the raw
    // DOM/contentEditable state underneath it.
    void undo();
    void redo();
    void toggleCommentSelection();

    // These three *do* go through QWebEnginePage's WebActions -- ordinary
    // OS-clipboard cut/copy/paste on whatever's focused inside the page
    // (Monaco's hidden textarea) works correctly there with no permission
    // prompt, unlike routing clipboard access through Monaco/JS would need.
    void cut();
    void copy();
    void paste();

    // Pushes a live theme change to this tab's Monaco instance -- the
    // View > Theme menu, or the OS appearance changing while it's set to
    // "System" (see theme.h). The editor's *initial* paint is instead
    // seeded via a "theme" URL query param at construction (mirrors the
    // fontSize pattern below) so there's no flash of the wrong theme while
    // the page/Monaco are still loading.
    void applyTheme(bool isDark);

signals:
    // Mirrors QTextDocument::modificationChanged, which is what callers
    // used to connect to on the native editor's document().
    void modifiedChanged(bool modified);

private slots:
    void onUserEdited(const QString &text);
    void onEditorReady();
    void onFontSizeEdited(int size);

private:
    QWebEngineView *view;
    QWebChannel *channel;
    MonacoBridge *bridge;

    QString cachedText;
    QString savedText;
    bool modified = false;

    bool editorReady = false;
    QString pendingText;
    bool hasPendingText = false;
};

#endif // MONACOEDITOR_H
