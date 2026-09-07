#include "markdowneditor.h"

#include "vimhandler.h"

#include <QFontDatabase>
#include <QFontInfo>
#include <QPainter>
#include <QWheelEvent>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextBlock>

namespace {

QString monospaceFamily()
{
    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (QFontInfo(fixed).fixedPitch())
        return fixed.family();
    for (const char *name : { "Menlo", "Consolas", "DejaVu Sans Mono", "Liberation Mono",
                              "Noto Sans Mono", "Courier New" }) {
        const QString candidate = QString::fromLatin1(name);
        if (QFontInfo(QFont(candidate)).family().compare(candidate, Qt::CaseInsensitive) == 0)
            return candidate;
    }
    QFont hinted;
    hinted.setStyleHint(QFont::Monospace);
    hinted.setFamily(QStringLiteral("monospace"));
    return QFontInfo(hinted).family();
}

// Just enough highlighting to make the source readable while editing.
class SourceHighlighter : public QSyntaxHighlighter
{
public:
    explicit SourceHighlighter(QTextDocument *document)
        : QSyntaxHighlighter(document)
    {
    }

    void setDarkMode(bool dark)
    {
        m_dark = dark;
        rehighlight();
    }

protected:
    void highlightBlock(const QString &text) override
    {
        struct Rule
        {
            QRegularExpression pattern;
            QTextCharFormat format;
        };

        QTextCharFormat heading;
        heading.setForeground(m_dark ? QColor(0x6c, 0xb6, 0xff) : QColor(0x03, 0x66, 0xd6));
        heading.setFontWeight(QFont::Bold);

        QTextCharFormat emphasis;
        emphasis.setForeground(m_dark ? QColor(0xff, 0xa6, 0x57) : QColor(0x95, 0x38, 0x00));

        QTextCharFormat code;
        code.setForeground(m_dark ? QColor(0xa5, 0xd6, 0xff) : QColor(0x0a, 0x30, 0x69));

        QTextCharFormat link;
        link.setForeground(m_dark ? QColor(0x7e, 0xe7, 0x87) : QColor(0x11, 0x63, 0x29));

        QTextCharFormat marker;
        marker.setForeground(m_dark ? QColor(0x8b, 0x94, 0x9e) : QColor(0x6e, 0x77, 0x81));

        static const QVector<QPair<QString, int>> patterns = {
            { QStringLiteral("^#{1,6}\\s.*$"), 0 },                   // heading
            { QStringLiteral("(\\*\\*|__)(?=\\S)(.+?)(?<=\\S)\\1"), 1 }, // strong
            { QStringLiteral("`[^`]+`"), 2 },                          // inline code
            { QStringLiteral("\\[[^\\]]*\\]\\([^)]*\\)"), 3 },         // link
            { QStringLiteral("^\\s*([-*+]|\\d+\\.)\\s"), 4 },          // list marker
            { QStringLiteral("^\\s*>.*$"), 4 },                        // quote
            { QStringLiteral("^\\s*(```|~~~).*$"), 4 },                // fence
            { QStringLiteral("^\\s*\\|.*\\|\\s*$"), 4 },               // table row
        };

        const QTextCharFormat formats[] = { heading, emphasis, code, link, marker };
        for (const auto &entry : patterns) {
            const QRegularExpression pattern(entry.first);
            auto it = pattern.globalMatch(text);
            while (it.hasNext()) {
                const auto match = it.next();
                setFormat(int(match.capturedStart()), int(match.capturedLength()),
                          formats[entry.second]);
            }
        }
    }

private:
    bool m_dark = false;
};

} // namespace

// The gutter is a plain child widget; the editor does the painting.
class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(MarkdownEditor *editor)
        : QWidget(editor)
        , m_editor(editor)
    {
    }

    QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }

protected:
    void paintEvent(QPaintEvent *event) override { m_editor->paintLineNumbers(event); }

private:
    MarkdownEditor *m_editor = nullptr;
};

MarkdownEditor::MarkdownEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setFrameShape(QFrame::NoFrame);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabChangesFocus(false);
    setCenterOnScroll(true);

    setFont(QFont(monospaceFamily()));

    m_lineNumbers = new LineNumberArea(this);
    m_highlighter = new SourceHighlighter(document());
    m_vim = new VimHandler(this, this);
    applyFont();

    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateViewportMargins(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        if (dy != 0)
            m_lineNumbers->scroll(0, dy);
        else
            m_lineNumbers->update(0, rect.y(), m_lineNumbers->width(), rect.height());
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] { highlightCurrentLine(); });

    applyPalette();
    updateViewportMargins();
    highlightCurrentLine();
}

void MarkdownEditor::setVimEnabled(bool enabled)
{
    m_vim->setEnabled(enabled);
}

bool MarkdownEditor::isVimEnabled() const
{
    return m_vim->isEnabled();
}

void MarkdownEditor::setLineNumbersVisible(bool visible)
{
    m_lineNumbersVisible = visible;
    m_lineNumbers->setVisible(visible);
    updateViewportMargins();
}

void MarkdownEditor::setBasePointSize(qreal points)
{
    if (points <= 0 || qFuzzyCompare(m_basePointSize, points))
        return;
    m_basePointSize = points;
    applyFont();
}

void MarkdownEditor::setZoomPercent(int percent)
{
    const int clamped = qBound(50, percent, 400);
    if (clamped == m_zoom)
        return;
    m_zoom = clamped;
    applyFont();
    emit zoomChanged(m_zoom);
}

void MarkdownEditor::applyFont()
{
    QFont current = font();
    current.setPointSizeF(m_basePointSize * m_zoom / 100.0);
    setFont(current);
    setTabStopDistance(4 * QFontMetricsF(current).horizontalAdvance(QLatin1Char(' ')));
    // Only the cursor width depends on the font; re-enabling the handler here
    // would drop the user back into normal mode mid-edit.
    if (m_vim)
        m_vim->updateCursorWidth();
    updateViewportMargins();
    viewport()->update();
}

void MarkdownEditor::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const int delta = event->angleDelta().y();
        if (delta != 0)
            setZoomPercent(m_zoom + (delta > 0 ? 10 : -10));
        event->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(event);
}

void MarkdownEditor::setDarkMode(bool dark)
{
    m_dark = dark;
    applyPalette();
    static_cast<SourceHighlighter *>(m_highlighter)->setDarkMode(dark);
    highlightCurrentLine();
}

void MarkdownEditor::applyPalette()
{
    QPalette pal = palette();
    if (m_dark) {
        pal.setColor(QPalette::Base, QColor(0x17, 0x1b, 0x21));
        pal.setColor(QPalette::Text, QColor(0xd4, 0xda, 0xe0));
        pal.setColor(QPalette::Highlight, QColor(0x2f, 0x5d, 0x94));
        pal.setColor(QPalette::HighlightedText, Qt::white);
    } else {
        pal.setColor(QPalette::Base, QColor(0xfc, 0xfc, 0xfd));
        pal.setColor(QPalette::Text, QColor(0x24, 0x29, 0x2f));
        pal.setColor(QPalette::Highlight, QColor(0xac, 0xce, 0xf7));
        pal.setColor(QPalette::HighlightedText, QColor(0x11, 0x14, 0x18));
    }
    setPalette(pal);
}

void MarkdownEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> selections;
    QTextEdit::ExtraSelection current;
    current.format.setBackground(m_dark ? QColor(0x21, 0x26, 0x2d) : QColor(0xf2, 0xf5, 0xf9));
    current.format.setProperty(QTextFormat::FullWidthSelection, true);
    current.cursor = textCursor();
    current.cursor.clearSelection();
    selections.append(current);
    setExtraSelections(selections);
}

int MarkdownEditor::lineNumberAreaWidth() const
{
    if (!m_lineNumbersVisible)
        return 0;
    int digits = 1;
    for (int lines = qMax(1, blockCount()); lines >= 10; lines /= 10)
        ++digits;
    const int advance = qRound(QFontMetricsF(font()).horizontalAdvance(QLatin1Char('9')));
    return 10 + advance * qMax(3, digits);
}

void MarkdownEditor::updateViewportMargins()
{
    if (!m_lineNumbers)
        return;
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    m_lineNumbers->setGeometry(0, 0, lineNumberAreaWidth(), height());
}

void MarkdownEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    updateViewportMargins();
}

void MarkdownEditor::paintLineNumbers(QPaintEvent *event)
{
    QPainter painter(m_lineNumbers);
    painter.fillRect(event->rect(), m_dark ? QColor(0x14, 0x18, 0x1d) : QColor(0xf2, 0xf3, 0xf5));

    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    const int currentLine = textCursor().blockNumber();

    while (block.isValid() && top <= event->rect().bottom()) {
        const qreal bottom = top + blockBoundingRect(block).height();
        if (block.isVisible() && bottom >= event->rect().top()) {
            const bool isCurrent = number == currentLine;
            painter.setPen(isCurrent ? (m_dark ? QColor(0xd4, 0xda, 0xe0) : QColor(0x24, 0x29, 0x2f))
                                     : (m_dark ? QColor(0x5b, 0x64, 0x70)
                                               : QColor(0xa0, 0xa8, 0xb4)));
            painter.drawText(QRectF(0, top, m_lineNumbers->width() - 5,
                                    blockBoundingRect(block).height()),
                             Qt::AlignRight | Qt::AlignTop, QString::number(number + 1));
        }
        top = bottom;
        block = block.next();
        ++number;
    }
}
