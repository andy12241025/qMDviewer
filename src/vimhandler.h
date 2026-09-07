#ifndef VIMHANDLER_H
#define VIMHANDLER_H

#include <QHash>
#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QAbstractScrollArea;
class QFont;
class QKeyEvent;
class QPlainTextEdit;
class QTextCursor;
class QTextDocument;
class QTextEdit;
class QWidget;
QT_END_NAMESPACE

// Modal editing for a QPlainTextEdit, or vim-style reading for a read-only
// QTextEdit, installed as an event filter on the widget.
//
// This is a working subset of vim rather than an emulation: normal, insert and
// visual modes, counts, operators with motions and text objects, registers,
// search and the handful of ex commands a viewer-turned-editor needs. On a
// read-only target the motions, scrolling, visual selection and yanking all
// work; anything that would change the text is refused, and searching is
// delegated to the window's find bar so that both agree on the term.
class VimHandler : public QObject
{
    Q_OBJECT

public:
    enum class Mode { Normal, Insert, Visual, VisualLine, CommandLine };

    explicit VimHandler(QPlainTextEdit *editor, QObject *parent = nullptr);
    explicit VimHandler(QTextEdit *view, QObject *parent = nullptr);

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    bool isReadOnly() const { return m_readOnly; }

    Mode mode() const { return m_mode; }
    QString statusText() const;

    // Call after the target's font changed: the block cursor is a character
    // wide, so its width has to be recomputed.
    void updateCursorWidth();

signals:
    void statusChanged(const QString &text);
    void saveRequested();
    void quitRequested(bool force);
    void reloadRequested();
    void lineNumbersRequested(bool visible);
    void message(const QString &text);

    // Read-only targets leave searching to the find bar, so that the visible
    // term, F3 and n/N all stay in step.
    void findRequested();
    void findNextRequested(bool backwards);
    void searchWordRequested(const QString &word, bool backwards);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class CharClass { Blank, Word, Punctuation };

    // The two kinds of target differ only in these accessors.
    QWidget *targetWidget() const;
    QAbstractScrollArea *targetArea() const;
    QTextDocument *document() const;
    QTextCursor textCursor() const;
    void setTextCursor(const QTextCursor &cursor);
    QFont targetFont() const;
    int targetCursorWidth() const;
    void setTargetCursorWidth(int width);
    void undoTarget();
    void redoTarget();

    bool claimsKey(const QKeyEvent *event) const;
    bool handleScrollKey(QKeyEvent *event);
    bool handleNormal(QKeyEvent *event);
    bool handleVisual(QKeyEvent *event);
    bool handleInsert(QKeyEvent *event);
    bool handleCommandLine(QKeyEvent *event);
    bool refuseEdit(QChar key);

    void setMode(Mode mode);
    void enterInsert();
    void leaveInsert();
    void resetPending();
    void emitStatus();

    QChar charAt(int position) const;
    CharClass classify(QChar character, bool bigWord) const;
    int documentEnd() const;
    int lineStart(int position) const;
    int lineEnd(int position) const;         // before the newline
    int firstNonBlank(int position) const;

    int nextWordStart(int position, int count, bool bigWord) const;
    int previousWordStart(int position, int count, bool bigWord) const;
    int wordEnd(int position, int count, bool bigWord) const;
    int findInLine(int position, QChar target, bool forward, bool till, int count) const;
    int paragraphMove(int position, bool forward, int count) const;
    int headingMove(int position, bool forward, int count) const;
    QString wordUnderCursor() const;

    // Resolves a motion key into a target position; -1 when unhandled.
    int motionTarget(QChar key, const QString &sequence, int count, bool *linewise,
                     bool *inclusive) const;
    bool textObjectRange(const QString &object, int *from, int *to, bool *linewise) const;

    void applyOperator(QChar op, int from, int to, bool linewise);
    void yank(int from, int to, bool linewise);
    void deleteRange(int from, int to, bool linewise);
    void paste(bool after, int count);
    void indentLines(int from, int to, bool addIndent, int count);
    void toggleCaseAt(int count);
    void joinLines(int count);
    void searchWord(bool backwards);

    bool search(const QString &pattern, bool forward, bool moveOnly);
    void repeatSearch(bool sameDirection);
    void runExCommand(const QString &command);

    QPlainTextEdit *m_editor = nullptr;
    QTextEdit *m_view = nullptr;
    bool m_readOnly = false;

    Mode m_mode = Mode::Normal;
    bool m_enabled = false;

    QString m_pending;          // keys collected for a multi-key command
    int m_count = 0;
    QChar m_operator;
    QChar m_register;
    QHash<QChar, QString> m_registers;
    QHash<QChar, bool> m_registerIsLinewise;

    QString m_commandLine;      // includes the leading ':' or '/'
    QString m_lastSearch;
    bool m_lastSearchForward = true;
    QChar m_lastFindChar;
    bool m_lastFindForward = true;
    bool m_lastFindTill = false;
    int m_visualAnchor = 0;
    int m_cursorWidth = 1;
};

#endif // VIMHANDLER_H
