#include "vimhandler.h"

#include <QFontMetricsF>
#include <QAbstractScrollArea>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace {

bool isMotionKey(QChar key)
{
    static const QString keys = QStringLiteral("hjkl0$^wWbBeE{}GgfFtT[]");
    return keys.contains(key);
}

} // namespace

VimHandler::VimHandler(QPlainTextEdit *editor, QObject *parent)
    : QObject(parent)
    , m_editor(editor)
{
}

VimHandler::VimHandler(QTextEdit *view, QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_readOnly(true)
{
}

QWidget *VimHandler::targetWidget() const
{
    return m_editor ? static_cast<QWidget *>(m_editor) : static_cast<QWidget *>(m_view);
}

QAbstractScrollArea *VimHandler::targetArea() const
{
    return m_editor ? static_cast<QAbstractScrollArea *>(m_editor)
                    : static_cast<QAbstractScrollArea *>(m_view);
}

QTextDocument *VimHandler::document() const
{
    return m_editor ? m_editor->document() : m_view->document();
}

QTextCursor VimHandler::textCursor() const
{
    return m_editor ? m_editor->textCursor() : m_view->textCursor();
}

void VimHandler::setTextCursor(const QTextCursor &cursor)
{
    if (m_editor) {
        m_editor->setTextCursor(cursor);
        m_editor->ensureCursorVisible();
    } else {
        m_view->setTextCursor(cursor);
        m_view->ensureCursorVisible();
    }
    // ensureCursorVisible() scrolls the least it can, which leaves the
    // document's top margin off screen on the first line - not what "gg"
    // should look like.
    if (cursor.position() == 0) {
        QScrollBar *bar = targetArea()->verticalScrollBar();
        bar->setValue(bar->minimum());
    }
}

QFont VimHandler::targetFont() const
{
    return targetWidget()->font();
}

int VimHandler::targetCursorWidth() const
{
    return m_editor ? m_editor->cursorWidth() : m_view->cursorWidth();
}

void VimHandler::setTargetCursorWidth(int width)
{
    if (m_editor)
        m_editor->setCursorWidth(width);
    else
        m_view->setCursorWidth(width);
}

void VimHandler::undoTarget()
{
    if (m_editor)
        m_editor->undo();
}

void VimHandler::redoTarget()
{
    if (m_editor)
        m_editor->redo();
}

// Nothing that changes the text applies to the preview.
bool VimHandler::refuseEdit(QChar key)
{
    if (!m_readOnly)
        return false;
    static const QString editing = QStringLiteral("iaIAoOxXDCsSpPrJ~udc<>");
    if (!editing.contains(key))
        return false;
    emit message(tr("The preview is read-only - press Ctrl+E to edit the source"));
    resetPending();
    emitStatus();
    return true;
}

void VimHandler::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    if (!targetWidget())
        return;

    if (enabled) {
        targetWidget()->installEventFilter(this);
        m_cursorWidth = targetCursorWidth();
        setMode(Mode::Normal);
    } else {
        targetWidget()->removeEventFilter(this);
        setTargetCursorWidth(m_cursorWidth);
        m_mode = Mode::Insert; // an ordinary editor is always "inserting"
        resetPending();
        emitStatus();
    }
}

QString VimHandler::statusText() const
{
    if (!m_enabled)
        return QString();
    switch (m_mode) {
    case Mode::Insert:
        return QStringLiteral("-- INSERT --");
    case Mode::Visual:
        return QStringLiteral("-- VISUAL --");
    case Mode::VisualLine:
        return QStringLiteral("-- VISUAL LINE --");
    case Mode::CommandLine:
        return m_commandLine;
    case Mode::Normal:
        break;
    }
    QString status = QStringLiteral("-- NORMAL --");
    if (m_count > 0 || !m_pending.isEmpty() || !m_operator.isNull()) {
        status += QLatin1String("  ");
        if (m_count > 0)
            status += QString::number(m_count);
        if (!m_operator.isNull())
            status += m_operator;
        status += m_pending;
    }
    return status;
}

void VimHandler::emitStatus()
{
    emit statusChanged(statusText());
}

void VimHandler::updateCursorWidth()
{
    if (!targetWidget() || !m_enabled)
        return;
    // A block cursor in normal mode, a bar while inserting.
    const QFontMetricsF metrics(targetFont());
    const int width = m_mode == Mode::Insert
            ? m_cursorWidth
            : qMax(2, qRound(metrics.horizontalAdvance(QLatin1Char('m'))));
    setTargetCursorWidth(width);
}

void VimHandler::setMode(Mode mode)
{
    m_mode = mode;
    updateCursorWidth();
    emitStatus();
}

void VimHandler::resetPending()
{
    m_pending.clear();
    m_count = 0;
    m_operator = QChar();
    m_register = QChar();
}

// ---------------------------------------------------------------------------
// text helpers
// ---------------------------------------------------------------------------

QChar VimHandler::charAt(int position) const
{
    const QTextBlock block = document()->findBlock(position);
    if (!block.isValid())
        return QChar();
    const int offset = position - block.position();
    const QString text = block.text();
    if (offset < 0 || offset > text.size())
        return QChar();
    if (offset == text.size())
        return QLatin1Char('\n');
    return text.at(offset);
}

VimHandler::CharClass VimHandler::classify(QChar character, bool bigWord) const
{
    if (character.isNull() || character.isSpace())
        return CharClass::Blank;
    if (bigWord)
        return CharClass::Word;
    if (character.isLetterOrNumber() || character == QLatin1Char('_'))
        return CharClass::Word;
    return CharClass::Punctuation;
}

int VimHandler::documentEnd() const
{
    return qMax(0, document()->characterCount() - 1);
}

int VimHandler::lineStart(int position) const
{
    const QTextBlock block = document()->findBlock(position);
    return block.isValid() ? block.position() : 0;
}

int VimHandler::lineEnd(int position) const
{
    const QTextBlock block = document()->findBlock(position);
    if (!block.isValid())
        return position;
    return block.position() + block.text().size();
}

int VimHandler::firstNonBlank(int position) const
{
    const QTextBlock block = document()->findBlock(position);
    if (!block.isValid())
        return position;
    const QString text = block.text();
    int offset = 0;
    while (offset < text.size() && text.at(offset).isSpace())
        ++offset;
    return block.position() + offset;
}

int VimHandler::nextWordStart(int position, int count, bool bigWord) const
{
    const int end = documentEnd();
    int at = position;
    for (int i = 0; i < count && at < end; ++i) {
        const CharClass start = classify(charAt(at), bigWord);
        if (start != CharClass::Blank) {
            while (at < end && classify(charAt(at), bigWord) == start)
                ++at;
        }
        while (at < end && classify(charAt(at), bigWord) == CharClass::Blank)
            ++at;
    }
    return qMin(at, end);
}

int VimHandler::previousWordStart(int position, int count, bool bigWord) const
{
    int at = position;
    for (int i = 0; i < count && at > 0; ++i) {
        --at;
        while (at > 0 && classify(charAt(at), bigWord) == CharClass::Blank)
            --at;
        const CharClass start = classify(charAt(at), bigWord);
        while (at > 0 && classify(charAt(at - 1), bigWord) == start)
            --at;
    }
    return qMax(0, at);
}

int VimHandler::wordEnd(int position, int count, bool bigWord) const
{
    const int end = documentEnd();
    int at = position;
    for (int i = 0; i < count && at < end; ++i) {
        ++at;
        while (at < end && classify(charAt(at), bigWord) == CharClass::Blank)
            ++at;
        const CharClass start = classify(charAt(at), bigWord);
        while (at + 1 < end && classify(charAt(at + 1), bigWord) == start)
            ++at;
    }
    return qMin(at, end);
}

int VimHandler::findInLine(int position, QChar target, bool forward, bool till, int count) const
{
    const int start = lineStart(position);
    const int stop = lineEnd(position);
    int at = position;
    for (int i = 0; i < count; ++i) {
        if (forward) {
            int probe = at + 1;
            while (probe < stop && charAt(probe) != target)
                ++probe;
            if (probe >= stop)
                return -1;
            at = probe;
        } else {
            int probe = at - 1;
            while (probe >= start && charAt(probe) != target)
                --probe;
            if (probe < start)
                return -1;
            at = probe;
        }
    }
    if (till)
        at += forward ? -1 : 1;
    return at;
}

int VimHandler::paragraphMove(int position, bool forward, int count) const
{
    QTextBlock block = document()->findBlock(position);
    for (int i = 0; i < count; ++i) {
        block = forward ? block.next() : block.previous();
        while (block.isValid() && block.text().trimmed().isEmpty())
            block = forward ? block.next() : block.previous();
        while (block.isValid() && !block.text().trimmed().isEmpty()) {
            const QTextBlock following = forward ? block.next() : block.previous();
            if (!following.isValid())
                break;
            if (following.text().trimmed().isEmpty()) {
                block = following;
                break;
            }
            block = following;
        }
        if (!block.isValid())
            return forward ? documentEnd() : 0;
    }
    return block.isValid() ? block.position() : (forward ? documentEnd() : 0);
}

// Sections, in vim's sense of [[ and ]]: Markdown headings. Rich documents
// carry a heading level; in the source editor the text still starts with '#'.
int VimHandler::headingMove(int position, bool forward, int count) const
{
    auto isHeading = [](const QTextBlock &block) {
        return block.blockFormat().headingLevel() > 0
                || block.text().trimmed().startsWith(QLatin1Char('#'));
    };

    QTextBlock block = document()->findBlock(position);
    int found = -1;
    for (int i = 0; i < count; ++i) {
        for (block = forward ? block.next() : block.previous(); block.isValid();
             block = forward ? block.next() : block.previous()) {
            if (isHeading(block)) {
                found = block.position();
                break;
            }
        }
        if (!block.isValid())
            break;
    }
    if (found >= 0)
        return found;
    return forward ? documentEnd() : 0;
}

QString VimHandler::wordUnderCursor() const
{
    auto looksLikeWord = [](const QString &text) {
        for (const QChar character : text) {
            if (character.isLetterOrNumber() || character == QLatin1Char('_'))
                return true;
        }
        return false; // punctuation runs are words to vim, but useless to search for
    };

    // An existing selection is the most explicit answer, and it means "*"
    // repeats the match you just landed on.
    const QTextCursor selected = textCursor();
    if (selected.hasSelection()) {
        const QString text = selected.selectedText().trimmed();
        if (looksLikeWord(text))
            return text;
    }

    // Otherwise the word under the cursor, or - as vim does - the next word on
    // the line when the cursor sits on a space.
    int position = selected.position();
    if (classify(charAt(position), false) == CharClass::Blank) {
        const int stop = lineEnd(position);
        while (position < stop && classify(charAt(position), false) == CharClass::Blank)
            ++position;
        if (position >= stop)
            return QString();
    }

    QTextCursor probe = textCursor();
    probe.setPosition(position);
    const CharClass klass = classify(charAt(position), false);
    int from = position;
    while (from > 0 && classify(charAt(from - 1), false) == klass)
        --from;
    int to = position;
    while (to < documentEnd() && classify(charAt(to), false) == klass)
        ++to;

    probe.setPosition(from);
    probe.setPosition(to, QTextCursor::KeepAnchor);
    const QString word = probe.selectedText().trimmed();
    return looksLikeWord(word) ? word : QString();
}

// "*" and "#" search for the word the cursor sits on.
void VimHandler::searchWord(bool backwards)
{
    const QString word = wordUnderCursor();
    if (word.isEmpty()) {
        emit message(tr("No word under the cursor"));
        return;
    }
    m_lastSearch = word;
    m_lastSearchForward = !backwards;
    if (m_readOnly) {
        // The find bar owns the term, so it shows what n and F3 will repeat.
        emit searchWordRequested(word, backwards);
        return;
    }
    search(word, !backwards, true);
}

// ---------------------------------------------------------------------------
// motions and text objects
// ---------------------------------------------------------------------------

int VimHandler::motionTarget(QChar key, const QString &sequence, int count, bool *linewise,
                             bool *inclusive) const
{
    *linewise = false;
    *inclusive = false;
    const QTextCursor cursor = textCursor();
    const int position = cursor.position();

    switch (key.unicode()) {
    case 'h':
        return qMax(lineStart(position), position - count);
    case 'l':
        return qMin(lineEnd(position), position + count);
    case '0':
        return lineStart(position);
    case '^':
        return firstNonBlank(position);
    case '$': {
        QTextCursor probe = cursor;
        for (int i = 1; i < count; ++i)
            probe.movePosition(QTextCursor::Down);
        // lineEnd() already sits after the last character, so the range is
        // complete without being marked inclusive; +1 would take the newline.
        return lineEnd(probe.position());
    }
    case 'j':
    case 'k': {
        QTextCursor probe = cursor;
        probe.movePosition(key == QLatin1Char('j') ? QTextCursor::Down : QTextCursor::Up,
                           QTextCursor::MoveAnchor, count);
        *linewise = true;
        return probe.position();
    }
    case 'w':
        return nextWordStart(position, count, false);
    case 'W':
        return nextWordStart(position, count, true);
    case 'b':
        return previousWordStart(position, count, false);
    case 'B':
        return previousWordStart(position, count, true);
    case 'e':
        *inclusive = true;
        return wordEnd(position, count, false);
    case 'E':
        *inclusive = true;
        return wordEnd(position, count, true);
    case '{':
        return paragraphMove(position, false, count);
    case '}':
        return paragraphMove(position, true, count);
    case 'G': {
        *linewise = true;
        if (count > 0 && m_count > 0) {
            const QTextBlock block = document()->findBlockByNumber(count - 1);
            return block.isValid() ? block.position() : documentEnd();
        }
        return documentEnd();
    }
    case 'g':
        if (sequence == QLatin1String("gg")) {
            *linewise = true;
            if (m_count > 0) {
                const QTextBlock block = document()->findBlockByNumber(count - 1);
                return block.isValid() ? block.position() : 0;
            }
            return 0;
        }
        return -1;
    case '[':
    case ']':
        if (sequence == QLatin1String("]]"))
            return headingMove(position, true, count);
        if (sequence == QLatin1String("[["))
            return headingMove(position, false, count);
        return -1;
    case 'f':
    case 'F':
    case 't':
    case 'T': {
        if (sequence.size() < 2)
            return -1;
        const QChar target = sequence.at(1);
        const bool forward = key == QLatin1Char('f') || key == QLatin1Char('t');
        const bool till = key == QLatin1Char('t') || key == QLatin1Char('T');
        *inclusive = forward;
        return findInLine(position, target, forward, till, count);
    }
    default:
        break;
    }
    return -1;
}

bool VimHandler::textObjectRange(const QString &object, int *from, int *to, bool *linewise) const
{
    if (object.size() < 2)
        return false;
    const bool inner = object.at(0) == QLatin1Char('i');
    const QChar kind = object.at(1);
    const int position = textCursor().position();
    *linewise = false;

    if (kind == QLatin1Char('w') || kind == QLatin1Char('W')) {
        const bool bigWord = kind == QLatin1Char('W');
        const CharClass klass = classify(charAt(position), bigWord);
        int start = position;
        while (start > 0 && classify(charAt(start - 1), bigWord) == klass)
            --start;
        int end = position;
        while (end < documentEnd() && classify(charAt(end), bigWord) == klass)
            ++end;
        if (!inner) {
            while (end < documentEnd() && classify(charAt(end), bigWord) == CharClass::Blank)
                ++end;
        }
        *from = start;
        *to = end;
        return true;
    }

    static const QHash<QChar, QPair<QChar, QChar>> pairs = {
        { QLatin1Char('('), { QLatin1Char('('), QLatin1Char(')') } },
        { QLatin1Char(')'), { QLatin1Char('('), QLatin1Char(')') } },
        { QLatin1Char('b'), { QLatin1Char('('), QLatin1Char(')') } },
        { QLatin1Char('['), { QLatin1Char('['), QLatin1Char(']') } },
        { QLatin1Char(']'), { QLatin1Char('['), QLatin1Char(']') } },
        { QLatin1Char('{'), { QLatin1Char('{'), QLatin1Char('}') } },
        { QLatin1Char('}'), { QLatin1Char('{'), QLatin1Char('}') } },
        { QLatin1Char('"'), { QLatin1Char('"'), QLatin1Char('"') } },
        { QLatin1Char('\''), { QLatin1Char('\''), QLatin1Char('\'') } },
        { QLatin1Char('`'), { QLatin1Char('`'), QLatin1Char('`') } },
    };
    const auto found = pairs.constFind(kind);
    if (found == pairs.constEnd())
        return false;

    const QChar open = found->first;
    const QChar close = found->second;
    const int start = lineStart(position);
    const int stop = lineEnd(position);

    int left = -1;
    int right = -1;
    if (open == close) {
        // Quotes: scan the line and take the pair around the cursor.
        int probe = start;
        while (probe < stop) {
            if (charAt(probe) == open) {
                int closing = probe + 1;
                while (closing < stop && charAt(closing) != close)
                    ++closing;
                if (closing < stop && position >= probe && position <= closing) {
                    left = probe;
                    right = closing;
                    break;
                }
                probe = closing + 1;
                continue;
            }
            ++probe;
        }
    } else {
        int depth = 0;
        for (int probe = position; probe >= start; --probe) {
            const QChar c = charAt(probe);
            if (c == close && probe != position)
                ++depth;
            else if (c == open) {
                if (depth == 0) {
                    left = probe;
                    break;
                }
                --depth;
            }
        }
        depth = 0;
        for (int probe = qMax(left, position); probe < stop; ++probe) {
            const QChar c = charAt(probe);
            if (c == open && probe != left)
                ++depth;
            else if (c == close) {
                if (depth == 0) {
                    right = probe;
                    break;
                }
                --depth;
            }
        }
    }
    if (left < 0 || right < 0)
        return false;

    *from = inner ? left + 1 : left;
    *to = inner ? right : right + 1;
    return true;
}

// ---------------------------------------------------------------------------
// operators
// ---------------------------------------------------------------------------

void VimHandler::yank(int from, int to, bool linewise)
{
    QTextCursor cursor = textCursor();
    cursor.setPosition(qMin(from, to));
    cursor.setPosition(qMax(from, to), QTextCursor::KeepAnchor);
    QString text = cursor.selectedText();
    text.replace(QChar(QChar::ParagraphSeparator), QLatin1Char('\n'));
    if (linewise && !text.endsWith(QLatin1Char('\n')))
        text += QLatin1Char('\n');

    const QChar name = m_register.isNull() ? QLatin1Char('"') : m_register;
    m_registers.insert(name, text);
    m_registerIsLinewise.insert(name, linewise);
    m_registers.insert(QLatin1Char('"'), text);
    m_registerIsLinewise.insert(QLatin1Char('"'), linewise);
    emit message(linewise ? tr("%n line(s) yanked", nullptr, text.count(QLatin1Char('\n')))
                          : tr("yanked"));
}

void VimHandler::deleteRange(int from, int to, bool linewise)
{
    yank(from, to, linewise);
    QTextCursor cursor = textCursor();
    // One edit block, or "u" would only undo the newline.
    cursor.beginEditBlock();
    cursor.setPosition(qMin(from, to));
    cursor.setPosition(qMax(from, to), QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    if (linewise) {
        // The trailing newline goes with the lines.
        if (cursor.position() < documentEnd() && charAt(cursor.position()) == QLatin1Char('\n'))
            cursor.deleteChar();
        cursor.setPosition(firstNonBlank(cursor.position()));
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void VimHandler::paste(bool after, int count)
{
    const QChar name = m_register.isNull() ? QLatin1Char('"') : m_register;
    const QString text = m_registers.value(name);
    if (text.isEmpty())
        return;
    const bool linewise = m_registerIsLinewise.value(name, false);

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    if (linewise) {
        const int target = after ? lineEnd(cursor.position()) : lineStart(cursor.position());
        cursor.setPosition(target);
        QString payload = text;
        if (after) {
            cursor.insertText(QLatin1String("\n"));
            if (payload.endsWith(QLatin1Char('\n')))
                payload.chop(1);
        }
        for (int i = 0; i < count; ++i) {
            if (i > 0)
                cursor.insertText(QLatin1String("\n"));
            cursor.insertText(payload);
        }
        if (!after) {
            if (!payload.endsWith(QLatin1Char('\n')))
                cursor.insertText(QLatin1String("\n"));
            cursor.setPosition(target);
        }
        cursor.setPosition(firstNonBlank(cursor.position()));
    } else {
        if (after && cursor.position() < lineEnd(cursor.position()))
            cursor.setPosition(cursor.position() + 1);
        for (int i = 0; i < count; ++i)
            cursor.insertText(text);
        cursor.setPosition(qMax(0, cursor.position() - 1));
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void VimHandler::indentLines(int from, int to, bool addIndent, int count)
{
    QTextDocument *target = document();
    QTextBlock block = target->findBlock(qMin(from, to));
    const QTextBlock last = target->findBlock(qMax(from, to));
    const QString indent = QString(4 * qMax(1, count), QLatin1Char(' '));

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    while (block.isValid()) {
        cursor.setPosition(block.position());
        if (addIndent) {
            cursor.insertText(indent);
        } else {
            const QString text = block.text();
            int strip = 0;
            while (strip < indent.size() && strip < text.size()
                   && text.at(strip) == QLatin1Char(' '))
                ++strip;
            if (strip > 0) {
                cursor.setPosition(block.position() + strip, QTextCursor::KeepAnchor);
                cursor.removeSelectedText();
            }
        }
        if (block == last)
            break;
        block = block.next();
    }
    cursor.endEditBlock();
}

void VimHandler::toggleCaseAt(int count)
{
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    for (int i = 0; i < count; ++i) {
        const int position = cursor.position();
        if (position >= lineEnd(position))
            break;
        const QChar character = charAt(position);
        const QChar flipped = character.isUpper() ? character.toLower() : character.toUpper();
        cursor.setPosition(position);
        cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
        cursor.insertText(flipped);
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void VimHandler::joinLines(int count)
{
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    for (int i = 0; i < qMax(1, count); ++i) {
        cursor.setPosition(lineEnd(cursor.position()));
        if (cursor.position() >= documentEnd())
            break;
        cursor.setPosition(cursor.position() + 1, QTextCursor::KeepAnchor);
        // Leading whitespace on the joined line collapses to a single space.
        int extra = cursor.position();
        while (extra < documentEnd() && charAt(extra).isSpace()
               && charAt(extra) != QLatin1Char('\n')) {
            ++extra;
        }
        cursor.setPosition(extra, QTextCursor::KeepAnchor);
        cursor.insertText(QLatin1String(" "));
        cursor.setPosition(cursor.position() - 1);
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void VimHandler::applyOperator(QChar op, int from, int to, bool linewise)
{
    int start = qMin(from, to);
    int end = qMax(from, to);
    if (linewise) {
        start = lineStart(start);
        end = lineEnd(end);
    }

    switch (op.unicode()) {
    case 'd':
        deleteRange(start, end, linewise);
        break;
    case 'y': {
        yank(start, end, linewise);
        QTextCursor cursor = textCursor();
        cursor.setPosition(start);
        setTextCursor(cursor);
        break;
    }
    case 'c':
        if (linewise) {
            // Keep the (now empty) line and start typing on it.
            yank(start, end, true);
            QTextCursor cursor = textCursor();
            cursor.setPosition(start);
            cursor.setPosition(end, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            setTextCursor(cursor);
        } else {
            deleteRange(start, end, false);
        }
        enterInsert();
        break;
    case '>':
        indentLines(start, end, true, 1);
        break;
    case '<':
        indentLines(start, end, false, 1);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// search and ex commands
// ---------------------------------------------------------------------------

bool VimHandler::search(const QString &pattern, bool forward, bool moveOnly)
{
    if (pattern.isEmpty())
        return false;
    if (!moveOnly) {
        m_lastSearch = pattern;
        m_lastSearchForward = forward;
    }

    QTextDocument::FindFlags flags;
    if (!forward)
        flags |= QTextDocument::FindBackward;

    QTextCursor from = textCursor();
    if (forward)
        from.setPosition(qMin(from.position() + 1, documentEnd()));
    QTextCursor found = document()->find(pattern, from, flags);
    if (found.isNull()) {
        QTextCursor wrapped = textCursor();
        wrapped.setPosition(forward ? 0 : documentEnd());
        found = document()->find(pattern, wrapped, flags);
        if (found.isNull()) {
            emit message(tr("Pattern not found: %1").arg(pattern));
            return false;
        }
        emit message(tr("Search wrapped around"));
    }
    found.setPosition(found.selectionStart());
    setTextCursor(found);
    return true;
}

void VimHandler::repeatSearch(bool sameDirection)
{
    if (m_lastSearch.isEmpty())
        return;
    search(m_lastSearch, sameDirection ? m_lastSearchForward : !m_lastSearchForward, true);
}

void VimHandler::runExCommand(const QString &command)
{
    const QString text = command.trimmed();
    if (text.isEmpty())
        return;

    // :s/from/to/flags and :%s/from/to/flags
    static const QRegularExpression substitute(
        QStringLiteral("^(%?)s/((?:[^/\\\\]|\\\\.)*)/((?:[^/\\\\]|\\\\.)*)/?([gi]*)$"));
    const auto match = substitute.match(text);
    if (match.hasMatch()) {
        if (m_readOnly) {
            emit message(tr("The preview is read-only - press Ctrl+E to edit the source"));
            return;
        }
        const bool wholeFile = match.captured(1) == QLatin1String("%");
        const QString flags = match.captured(4);
        QRegularExpression pattern(match.captured(2));
        if (flags.contains(QLatin1Char('i')))
            pattern.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        if (!pattern.isValid()) {
            emit message(tr("Invalid pattern: %1").arg(match.captured(2)));
            return;
        }
        const QString replacement = match.captured(3);
        const bool global = flags.contains(QLatin1Char('g'));

        QTextCursor cursor = textCursor();
        cursor.beginEditBlock();
        int replaced = 0;
        QTextBlock block = wholeFile ? document()->begin()
                                     : document()->findBlock(cursor.position());
        const QTextBlock last = wholeFile ? QTextBlock() : block;
        while (block.isValid()) {
            QString line = block.text();
            const QString before = line;
            if (global) {
                line.replace(pattern, replacement);
            } else {
                const auto first = pattern.match(line);
                if (first.hasMatch()) {
                    line.replace(first.capturedStart(), first.capturedLength(),
                                 QString(replacement));
                }
            }
            if (line != before) {
                ++replaced;
                QTextCursor lineCursor(block);
                lineCursor.setPosition(block.position());
                lineCursor.setPosition(block.position() + before.size(),
                                       QTextCursor::KeepAnchor);
                lineCursor.insertText(line);
            }
            if (!wholeFile && block == last)
                break;
            block = block.next();
        }
        cursor.endEditBlock();
        emit message(tr("%n line(s) changed", nullptr, replaced));
        return;
    }

    if (text == QLatin1String("w") || text == QLatin1String("write")) {
        emit saveRequested();
        return;
    }
    if (text == QLatin1String("wq") || text == QLatin1String("x") || text == QLatin1String("wq!")) {
        emit saveRequested();
        emit quitRequested(true);
        return;
    }
    if (text == QLatin1String("q") || text == QLatin1String("quit")) {
        emit quitRequested(false);
        return;
    }
    if (text == QLatin1String("q!") || text == QLatin1String("quit!")) {
        emit quitRequested(true);
        return;
    }
    if (text == QLatin1String("e!") || text == QLatin1String("edit!")) {
        emit reloadRequested();
        return;
    }
    if (text == QLatin1String("noh") || text == QLatin1String("nohlsearch")) {
        return;
    }
    if (text.startsWith(QLatin1String("set "))) {
        const QString option = text.mid(4).trimmed();
        if (option == QLatin1String("nu") || option == QLatin1String("number"))
            emit lineNumbersRequested(true);
        else if (option == QLatin1String("nonu") || option == QLatin1String("nonumber"))
            emit lineNumbersRequested(false);
        else
            emit message(tr("Unknown option: %1").arg(option));
        return;
    }

    bool isNumber = false;
    const int line = text.toInt(&isNumber);
    if (isNumber) {
        const QTextBlock block = document()->findBlockByNumber(qMax(0, line - 1));
        if (block.isValid()) {
            QTextCursor cursor = textCursor();
            cursor.setPosition(block.position());
            setTextCursor(cursor);
        }
        return;
    }
    if (text == QLatin1String("$")) {
        QTextCursor cursor = textCursor();
        cursor.setPosition(lineStart(documentEnd()));
        setTextCursor(cursor);
        return;
    }

    emit message(tr("Not an editor command: %1").arg(text));
}

// ---------------------------------------------------------------------------
// modes
// ---------------------------------------------------------------------------

void VimHandler::enterInsert()
{
    setMode(Mode::Insert);
    resetPending();
}

void VimHandler::leaveInsert()
{
    QTextCursor cursor = textCursor();
    if (cursor.position() > lineStart(cursor.position()))
        cursor.setPosition(cursor.position() - 1);
    setTextCursor(cursor);
    setMode(Mode::Normal);
    resetPending();
}

bool VimHandler::handleInsert(QKeyEvent *event)
{
    const bool escape = event->key() == Qt::Key_Escape
            || (event->key() == Qt::Key_BracketLeft
                && event->modifiers().testFlag(Qt::ControlModifier));
    if (escape) {
        leaveInsert();
        return true;
    }
    return false; // let the editor type
}

bool VimHandler::handleCommandLine(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        m_commandLine.clear();
        setMode(Mode::Normal);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const QString entered = m_commandLine;
        m_commandLine.clear();
        setMode(Mode::Normal);
        if (entered.startsWith(QLatin1Char(':')))
            runExCommand(entered.mid(1));
        else if (entered.startsWith(QLatin1Char('/')))
            search(entered.mid(1), true, false);
        else if (entered.startsWith(QLatin1Char('?')))
            search(entered.mid(1), false, false);
        return true;
    }
    case Qt::Key_Backspace:
        if (m_commandLine.size() <= 1) {
            m_commandLine.clear();
            setMode(Mode::Normal);
        } else {
            m_commandLine.chop(1);
            emitStatus();
        }
        return true;
    default:
        break;
    }
    if (!event->text().isEmpty() && event->text().at(0).isPrint()) {
        m_commandLine += event->text();
        emitStatus();
    }
    return true;
}

bool VimHandler::handleVisual(QKeyEvent *event)
{
    const QString text = event->text();
    const QChar key = text.isEmpty() ? QChar() : text.at(0);

    if (event->key() == Qt::Key_Escape) {
        QTextCursor cursor = textCursor();
        cursor.clearSelection();
        setTextCursor(cursor);
        setMode(Mode::Normal);
        resetPending();
        return true;
    }
    if (handleScrollKey(event))
        return true;

    if (key.isDigit() && !(key == QLatin1Char('0') && m_count == 0)) {
        m_count = m_count * 10 + key.digitValue();
        emitStatus();
        return true;
    }
    const int count = qMax(1, m_count);

    // Operators act on the selection and return to normal mode.
    if (key == QLatin1Char('d') || key == QLatin1Char('x') || key == QLatin1Char('y')
        || key == QLatin1Char('c') || key == QLatin1Char('>') || key == QLatin1Char('<')) {
        if (m_readOnly && key != QLatin1Char('y')) {
            emit message(tr("The preview is read-only - press Ctrl+E to edit the source"));
            return true;
        }
        QTextCursor cursor = textCursor();
        int from = qMin(cursor.position(), m_visualAnchor);
        int to = qMax(cursor.position(), m_visualAnchor);
        const bool linewise = m_mode == Mode::VisualLine;
        if (!linewise)
            to = qMin(to + 1, documentEnd()); // visual selections are inclusive
        setMode(Mode::Normal);
        const QChar op = key == QLatin1Char('x') ? QLatin1Char('d') : key;
        applyOperator(op, from, to, linewise);
        resetPending();
        return true;
    }

    if (key == QLatin1Char('o')) {
        // Swap which end of the selection the cursor sits on.
        QTextCursor cursor = textCursor();
        const int position = cursor.position();
        cursor.setPosition(position);
        cursor.setPosition(m_visualAnchor, QTextCursor::KeepAnchor);
        m_visualAnchor = position;
        setTextCursor(cursor);
        return true;
    }
    if (key == QLatin1Char('v')) {
        setMode(m_mode == Mode::Visual ? Mode::VisualLine : Mode::Visual);
        return true;
    }
    if (key == QLatin1Char('V')) {
        setMode(Mode::VisualLine);
        return true;
    }

    // Motions extend the selection.
    m_pending += key;
    bool linewise = false;
    bool inclusive = false;
    const int target = motionTarget(m_pending.at(0), m_pending, count, &linewise, &inclusive);
    if (target < 0) {
        if (m_pending.size() >= 2 || !isMotionKey(m_pending.at(0)))
            resetPending();
        emitStatus();
        return true;
    }

    QTextCursor cursor = textCursor();
    if (m_mode == Mode::VisualLine) {
        // Highlight whole lines, so the selection matches what an operator
        // would act on.
        const int from = qMin(m_visualAnchor, target);
        const int to = qMax(m_visualAnchor, target);
        const bool forward = target >= m_visualAnchor;
        cursor.setPosition(forward ? lineStart(from) : lineEnd(to));
        cursor.setPosition(forward ? lineEnd(to) : lineStart(from), QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(m_visualAnchor);
        cursor.setPosition(target, QTextCursor::KeepAnchor);
    }
    setTextCursor(cursor);
    resetPending();
    emitStatus();
    return true;
}

bool VimHandler::handleNormal(QKeyEvent *event)
{
    const QString text = event->text();
    const QChar key = text.isEmpty() ? QChar() : text.at(0);

    if (event->key() == Qt::Key_Escape) {
        resetPending();
        emitStatus();
        return true;
    }

    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        if (handleScrollKey(event))
            return true;
        if (event->key() == Qt::Key_R) {
            redoTarget();
            return true;
        }
        return false; // leave the application's own shortcuts alone
    }

    // A register prefix: "a before an operator or a paste.
    if (m_pending.isEmpty() && m_operator.isNull() && key == QLatin1Char('"')) {
        m_pending = QStringLiteral("\"");
        emitStatus();
        return true;
    }
    if (m_pending == QLatin1String("\"")) {
        m_register = key;
        m_pending.clear();
        emitStatus();
        return true;
    }

    if (key.isDigit() && !(key == QLatin1Char('0') && m_count == 0 && m_pending.isEmpty())) {
        m_count = m_count * 10 + key.digitValue();
        emitStatus();
        return true;
    }
    const int count = qMax(1, m_count);

    if (m_pending.isEmpty() && m_operator.isNull() && refuseEdit(key))
        return true;

    // Waiting for an operator's motion or text object.
    if (!m_operator.isNull()) {
        m_pending += key;
        if (m_pending.at(0) == QLatin1Char('i') || m_pending.at(0) == QLatin1Char('a')) {
            if (m_pending.size() < 2) {
                emitStatus();
                return true;
            }
            int from = 0;
            int to = 0;
            bool linewise = false;
            if (textObjectRange(m_pending, &from, &to, &linewise)) {
                const QChar op = m_operator;
                const QChar reg = m_register;
                resetPending();
                m_register = reg;
                applyOperator(op, from, to, linewise);
                m_register = QChar();
            } else {
                resetPending();
            }
            emitStatus();
            return true;
        }
        // Doubled operator: dd, yy, cc, >>, <<
        if (m_pending.at(0) == m_operator) {
            const QChar op = m_operator;
            const QChar reg = m_register;
            const int position = textCursor().position();
            QTextCursor probe = textCursor();
            probe.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, count - 1);
            resetPending();
            m_register = reg; // survives the reset for this one command
            applyOperator(op, position, probe.position(), true);
            m_register = QChar();
            emitStatus();
            return true;
        }
        bool linewise = false;
        bool inclusive = false;
        const int target = motionTarget(m_pending.at(0), m_pending, count, &linewise, &inclusive);
        if (target < 0) {
            if (m_pending.size() >= 2 || !isMotionKey(m_pending.at(0)))
                resetPending();
            emitStatus();
            return true;
        }
        const int position = textCursor().position();
        const QChar op = m_operator;
        const QChar reg = m_register;
        resetPending();
        m_register = reg;
        applyOperator(op, position, inclusive ? target + 1 : target, linewise);
        m_register = QChar();
        emitStatus();
        return true;
    }

    // Multi-key commands that are not operators.
    if (!m_pending.isEmpty()) {
        m_pending += key;
        bool linewise = false;
        bool inclusive = false;
        const int target = motionTarget(m_pending.at(0), m_pending, count, &linewise, &inclusive);
        if (target >= 0) {
            QTextCursor cursor = textCursor();
            cursor.setPosition(target);
            setTextCursor(cursor);
            resetPending();
        } else if (m_pending.size() >= 2) {
            resetPending();
        }
        emitStatus();
        return true;
    }

    switch (key.unicode()) {
    case 'i':
        enterInsert();
        return true;
    case 'a': {
        QTextCursor cursor = textCursor();
        if (cursor.position() < lineEnd(cursor.position()))
            cursor.setPosition(cursor.position() + 1);
        setTextCursor(cursor);
        enterInsert();
        return true;
    }
    case 'I': {
        QTextCursor cursor = textCursor();
        cursor.setPosition(firstNonBlank(cursor.position()));
        setTextCursor(cursor);
        enterInsert();
        return true;
    }
    case 'A': {
        QTextCursor cursor = textCursor();
        cursor.setPosition(lineEnd(cursor.position()));
        setTextCursor(cursor);
        enterInsert();
        return true;
    }
    case 'o':
    case 'O': {
        QTextCursor cursor = textCursor();
        cursor.beginEditBlock();
        if (key == QLatin1Char('o')) {
            cursor.setPosition(lineEnd(cursor.position()));
            cursor.insertText(QLatin1String("\n"));
        } else {
            cursor.setPosition(lineStart(cursor.position()));
            cursor.insertText(QLatin1String("\n"));
            cursor.setPosition(cursor.position() - 1);
        }
        cursor.endEditBlock();
        setTextCursor(cursor);
        enterInsert();
        return true;
    }
    case 'v':
        m_visualAnchor = textCursor().position();
        setMode(Mode::Visual);
        return true;
    case 'V': {
        QTextCursor cursor = textCursor();
        m_visualAnchor = cursor.position();
        cursor.setPosition(lineStart(cursor.position()));
        cursor.setPosition(lineEnd(cursor.position()), QTextCursor::KeepAnchor);
        setTextCursor(cursor);
        setMode(Mode::VisualLine);
        return true;
    }
    case 'x': {
        const int position = textCursor().position();
        const int to = qMin(position + count, lineEnd(position));
        if (to > position)
            deleteRange(position, to, false);
        resetPending();
        return true;
    }
    case 'X': {
        const int position = textCursor().position();
        const int from = qMax(position - count, lineStart(position));
        if (from < position)
            deleteRange(from, position, false);
        resetPending();
        return true;
    }
    case 'D': {
        const int position = textCursor().position();
        deleteRange(position, lineEnd(position), false);
        resetPending();
        return true;
    }
    case 'C': {
        const int position = textCursor().position();
        deleteRange(position, lineEnd(position), false);
        enterInsert();
        return true;
    }
    case 'Y': {
        const int position = textCursor().position();
        QTextCursor probe = textCursor();
        probe.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, count - 1);
        yank(lineStart(position), lineEnd(probe.position()), true);
        resetPending();
        return true;
    }
    case 's': {
        const int position = textCursor().position();
        const int to = qMin(position + count, lineEnd(position));
        if (to > position)
            deleteRange(position, to, false);
        enterInsert();
        return true;
    }
    case 'S': {
        const int position = textCursor().position();
        applyOperator(QLatin1Char('c'), position, position, true);
        return true;
    }
    case 'p':
        paste(true, count);
        resetPending();
        return true;
    case 'P':
        paste(false, count);
        resetPending();
        return true;
    case 'u':
        for (int i = 0; i < count; ++i)
            undoTarget();
        resetPending();
        return true;
    case 'J':
        joinLines(count);
        resetPending();
        return true;
    case '~':
        toggleCaseAt(count);
        resetPending();
        return true;
    case 'r':
        m_pending = QStringLiteral("r");
        emitStatus();
        return true;
    case 'n':
        if (m_readOnly)
            emit findNextRequested(false);
        else
            repeatSearch(true);
        resetPending();
        return true;
    case 'N':
        if (m_readOnly)
            emit findNextRequested(true);
        else
            repeatSearch(false);
        resetPending();
        return true;
    case '*':
        searchWord(false);
        resetPending();
        return true;
    case '#':
        searchWord(true);
        resetPending();
        return true;
    case ';':
        if (!m_lastFindChar.isNull()) {
            const int target = findInLine(textCursor().position(), m_lastFindChar,
                                         m_lastFindForward, m_lastFindTill, count);
            if (target >= 0) {
                QTextCursor cursor = textCursor();
                cursor.setPosition(target);
                setTextCursor(cursor);
            }
        }
        resetPending();
        return true;
    case '/':
    case '?':
        if (m_readOnly) {
            // The find bar is the search UI for the preview.
            emit findRequested();
            resetPending();
            return true;
        }
        m_commandLine = key;
        setMode(Mode::CommandLine);
        return true;
    case ':':
        m_commandLine = key;
        setMode(Mode::CommandLine);
        return true;
    case 'd':
    case 'c':
    case 'y':
    case '>':
    case '<':
        m_operator = key;
        emitStatus();
        return true;
    default:
        break;
    }

    // Plain motions.
    if (isMotionKey(key)) {
        bool linewise = false;
        bool inclusive = false;
        const int target = motionTarget(key, QString(key), count, &linewise, &inclusive);
        if (target >= 0) {
            QTextCursor cursor = textCursor();
            cursor.setPosition(target);
            setTextCursor(cursor);
            resetPending();
            emitStatus();
            return true;
        }
        // Needs another key, such as f<char> or gg.
        m_pending = QString(key);
        emitStatus();
        return true;
    }

    // Normal mode swallows anything else so stray letters cannot edit the text.
    return true;
}

// Keys that must reach the editor as key presses even though the window binds
// them as shortcuts - Esc closes the find bar and Ctrl+R reloads the file.
// Accepting the ShortcutOverride event tells Qt to deliver the key normally.
bool VimHandler::claimsKey(const QKeyEvent *event) const
{
    if (event->key() == Qt::Key_Escape)
        return true;
    if (!event->modifiers().testFlag(Qt::ControlModifier))
        return false;
    switch (event->key()) {
    case Qt::Key_BracketLeft: // Ctrl+[ is another way to leave insert mode
    case Qt::Key_R:           // redo
    case Qt::Key_D:           // half a page down
    case Qt::Key_U:           // half a page up
    case Qt::Key_F:           // a page down, not the find bar
    case Qt::Key_B:           // a page up
        return true;
    default:
        return false;
    }
}

// Ctrl+D/U/F/B move by a fraction of the visible page, as in vim.
bool VimHandler::handleScrollKey(QKeyEvent *event)
{
    if (!event->modifiers().testFlag(Qt::ControlModifier))
        return false;

    const int lineHeight = qMax(1, qRound(QFontMetricsF(targetFont()).lineSpacing()));
    const int visible = qMax(1, targetArea()->viewport()->height() / lineHeight);
    int lines = 0;
    switch (event->key()) {
    case Qt::Key_D:
        lines = visible / 2;
        break;
    case Qt::Key_U:
        lines = -(visible / 2);
        break;
    case Qt::Key_F:
        lines = qMax(1, visible - 2); // vim keeps two lines of context
        break;
    case Qt::Key_B:
        lines = -qMax(1, visible - 2);
        break;
    default:
        return false;
    }

    QTextCursor cursor = textCursor();
    const bool keepAnchor = m_mode == Mode::Visual || m_mode == Mode::VisualLine;
    cursor.movePosition(lines > 0 ? QTextCursor::Down : QTextCursor::Up,
                        keepAnchor ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor,
                        qAbs(lines));
    setTextCursor(cursor);
    return true;
}

bool VimHandler::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_enabled || watched != targetWidget())
        return QObject::eventFilter(watched, event);

    if (event->type() == QEvent::ShortcutOverride) {
        QKeyEvent *override = static_cast<QKeyEvent *>(event);
        if (!claimsKey(override))
            return QObject::eventFilter(watched, event);
        override->accept();
        return true;
    }
    if (event->type() != QEvent::KeyPress)
        return QObject::eventFilter(watched, event);

    QKeyEvent *key = static_cast<QKeyEvent *>(event);

    // "r<char>" replaces a single character.
    if (m_mode == Mode::Normal && m_pending == QLatin1String("r") && !key->text().isEmpty()
        && key->text().at(0).isPrint()) {
        QTextCursor cursor = textCursor();
        if (cursor.position() < lineEnd(cursor.position())) {
            cursor.setPosition(cursor.position() + 1, QTextCursor::KeepAnchor);
            cursor.insertText(key->text());
            cursor.setPosition(cursor.position() - 1);
            setTextCursor(cursor);
        }
        resetPending();
        emitStatus();
        return true;
    }
    // Remember the target of f/F/t/T for ";".
    if (m_mode == Mode::Normal && m_pending.size() == 1
        && QStringLiteral("fFtT").contains(m_pending.at(0)) && !key->text().isEmpty()) {
        m_lastFindChar = key->text().at(0);
        m_lastFindForward = m_pending.at(0) == QLatin1Char('f')
                || m_pending.at(0) == QLatin1Char('t');
        m_lastFindTill = m_pending.at(0).toLower() == QLatin1Char('t');
    }

    switch (m_mode) {
    case Mode::Insert:
        return handleInsert(key);
    case Mode::CommandLine:
        return handleCommandLine(key);
    case Mode::Visual:
    case Mode::VisualLine:
        return handleVisual(key);
    case Mode::Normal:
        return handleNormal(key);
    }
    return false;
}
