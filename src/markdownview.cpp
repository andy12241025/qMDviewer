#include "markdownview.h"

#include "codehighlighter.h"
#include "vimhandler.h"

#include <QAbstractTextDocumentLayout>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QImage>
#include <QPagedPaintDevice>
#include <QPair>
#include <QPixmap>
#include <QRegularExpression>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFrame>
#include <QTextTable>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
#  error "qMDviewer needs Qt 5.14 or newer for QTextDocument::setMarkdown()."
#endif

namespace {

struct Colors
{
    QColor text;
    QColor background;
    QColor heading;
    QColor muted;
    QColor link;
    QColor codeBackground;
    QColor border;
    QColor tableHeader;
    QColor selection;
    QColor selectedText;
    // Syntax colours, following GitHub's Primer palette.
    QColor synKeyword;
    QColor synType;
    QColor synBuiltin;
    QColor synString;
    QColor synNumber;
    QColor synComment;
    QColor synPreprocessor;
    QColor synAttribute;
    QColor synAdded;
    QColor synRemoved;
};

QColor colorForToken(const Colors &colors, CodeHighlighter::Token token)
{
    switch (token) {
    case CodeHighlighter::Token::Keyword:      return colors.synKeyword;
    case CodeHighlighter::Token::Type:         return colors.synType;
    case CodeHighlighter::Token::Builtin:      return colors.synBuiltin;
    case CodeHighlighter::Token::String:       return colors.synString;
    case CodeHighlighter::Token::Number:       return colors.synNumber;
    case CodeHighlighter::Token::Comment:      return colors.synComment;
    case CodeHighlighter::Token::Preprocessor: return colors.synPreprocessor;
    case CodeHighlighter::Token::Attribute:    return colors.synAttribute;
    case CodeHighlighter::Token::Added:        return colors.synAdded;
    case CodeHighlighter::Token::Removed:      return colors.synRemoved;
    case CodeHighlighter::Token::Plain:        break;
    }
    return QColor();
}

Colors colorsFor(bool dark)
{
    Colors c;
    if (dark) {
        c.text = QColor(0xd4, 0xda, 0xe0);
        c.background = QColor(0x1c, 0x20, 0x27);
        c.heading = QColor(0xf2, 0xf5, 0xf8);
        c.muted = QColor(0x8b, 0x96, 0xa3);
        c.link = QColor(0x6c, 0xb6, 0xff);
        c.codeBackground = QColor(0x26, 0x2c, 0x34);
        c.border = QColor(0x3a, 0x42, 0x4c);
        c.tableHeader = QColor(0x2c, 0x33, 0x3c);
        c.selection = QColor(0x2f, 0x5d, 0x94);
        c.selectedText = QColor(0xff, 0xff, 0xff);
        c.synKeyword = QColor(0xff, 0x7b, 0x72);
        c.synType = QColor(0xff, 0xa6, 0x57);
        c.synBuiltin = QColor(0xd2, 0xa8, 0xff);
        c.synString = QColor(0xa5, 0xd6, 0xff);
        c.synNumber = QColor(0x79, 0xc0, 0xff);
        c.synComment = QColor(0x8b, 0x94, 0x9e);
        c.synPreprocessor = QColor(0xff, 0x7b, 0x72);
        c.synAttribute = QColor(0x7e, 0xe7, 0x87);
        c.synAdded = QColor(0x56, 0xd3, 0x64);
        c.synRemoved = QColor(0xf8, 0x51, 0x49);
    } else {
        c.text = QColor(0x24, 0x29, 0x2f);
        c.background = QColor(0xff, 0xff, 0xff);
        c.heading = QColor(0x11, 0x14, 0x18);
        c.muted = QColor(0x6a, 0x73, 0x7d);
        c.link = QColor(0x03, 0x66, 0xd6);
        c.codeBackground = QColor(0xf3, 0xf5, 0xf7);
        c.border = QColor(0xd7, 0xda, 0xe0);
        c.tableHeader = QColor(0xf0, 0xf2, 0xf5);
        c.selection = QColor(0xac, 0xce, 0xf7);
        c.selectedText = QColor(0x11, 0x14, 0x18);
        c.synKeyword = QColor(0xcf, 0x22, 0x2e);
        c.synType = QColor(0x95, 0x38, 0x00);
        c.synBuiltin = QColor(0x82, 0x50, 0xdf);
        c.synString = QColor(0x0a, 0x30, 0x69);
        c.synNumber = QColor(0x05, 0x50, 0xae);
        c.synComment = QColor(0x6e, 0x77, 0x81);
        c.synPreprocessor = QColor(0xcf, 0x22, 0x2e);
        c.synAttribute = QColor(0x11, 0x63, 0x29);
        c.synAdded = QColor(0x11, 0x63, 0x29);
        c.synRemoved = QColor(0xa4, 0x0e, 0x26);
    }
    return c;
}

// Font size of h1..h6 relative to the body text.
const qreal kHeadingScale[7] = { 1.0, 1.9, 1.55, 1.3, 1.13, 1.0, 0.92 };

struct CharEdit
{
    int start;
    int length;
    QTextCharFormat format;
};

using BlockEdit = QPair<int, QTextBlockFormat>;

// Code arrives as one block per line. Fenced blocks are marked unbreakable and
// carry a fence character; indented blocks only carry the (empty) language
// property, so all three flags have to be checked.
bool isCodeBlock(const QTextBlockFormat &format)
{
    return format.hasProperty(QTextFormat::BlockCodeLanguage)
            || format.hasProperty(QTextFormat::BlockCodeFence)
            || format.nonBreakableLines();
}

// Consecutive fences sit next to each other in the block chain with nothing
// in between, so a run boundary is where the declared language changes rather
// than where code stops.
bool sameCodeRun(const QTextBlock &a, const QTextBlock &b)
{
    if (!a.isValid() || !b.isValid())
        return false;
    if (!isCodeBlock(a.blockFormat()) || !isCodeBlock(b.blockFormat()))
        return false;
    return a.blockFormat().stringProperty(QTextFormat::BlockCodeLanguage)
            == b.blockFormat().stringProperty(QTextFormat::BlockCodeLanguage);
}

bool startsCodeRun(const QTextBlock &block)
{
    return !sameCodeRun(block.previous(), block);
}

bool endsCodeRun(const QTextBlock &block)
{
    return !sameCodeRun(block, block.next());
}

bool isHorizontalRule(const QTextBlockFormat &format)
{
    return format.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
}

void setMonospace(QTextCharFormat *format, const QString &family, qreal pointSize)
{
    if (!family.isEmpty()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        format->setFontFamilies(QStringList(family));
#else
        format->setFontFamily(family);
#endif
    }
    format->setFontFixedPitch(true);
    format->setFontPointSize(pointSize);
}

// Formats cannot be applied while iterating: collect them, then commit.
void commitEdits(QTextDocument *doc, const QVector<BlockEdit> &blockEdits,
                 const QVector<CharEdit> &charEdits)
{
    if (blockEdits.isEmpty() && charEdits.isEmpty())
        return;

    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    for (const BlockEdit &edit : blockEdits) {
        cursor.setPosition(edit.first);
        cursor.setBlockFormat(edit.second);
    }
    for (const CharEdit &edit : charEdits) {
        cursor.setPosition(edit.start);
        cursor.setPosition(edit.start + edit.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(edit.format);
    }
    cursor.endEditBlock();
}

// Markdown tables come in borderless; give them grid lines and a header tint.
// Returns the document ranges the tables occupy, in document order, so the
// block pass can recognise cell content without walking frames per block.
void styleTables(QTextFrame *frame, const Colors &colors, qreal unit,
                 QVector<QPair<int, int>> *ranges)
{
    for (QTextFrame::iterator it = frame->begin(); !it.atEnd(); ++it) {
        QTextFrame *child = it.currentFrame();
        if (!child)
            continue;

        if (QTextTable *table = qobject_cast<QTextTable *>(child)) {
            ranges->append(qMakePair(table->firstPosition(), table->lastPosition()));
            QTextTableFormat format = table->format();
            format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
            format.setBorderBrush(colors.border);
            format.setBorder(1);
            format.setBorderCollapse(true);
            format.setCellSpacing(0);
            format.setCellPadding(qRound(unit * 0.6));
            format.setTopMargin(qRound(unit * 0.8));
            format.setBottomMargin(qRound(unit * 0.8));
            table->setFormat(format);

            for (int column = 0; column < table->columns(); ++column) {
                QTextTableCell cell = table->cellAt(0, column);
                if (!cell.isValid())
                    continue;
                QTextCharFormat cellFormat = cell.format();
                cellFormat.setBackground(colors.tableHeader);
                cell.setFormat(cellFormat);
            }
        }
        styleTables(child, colors, unit, ranges);
    }
}

// GitHub-style anchor id for a heading.
QString slugify(const QString &text)
{
    QString slug;
    slug.reserve(text.size());
    for (QChar ch : text) {
        if (ch.isLetterOrNumber())
            slug.append(ch.toLower());
        else if (ch == QLatin1Char('-') || ch == QLatin1Char('_'))
            slug.append(ch);
        else if (ch.isSpace() && !slug.endsWith(QLatin1Char('-')))
            slug.append(QLatin1Char('-'));
    }
    return slug;
}

// QFontInfo reports the family the engine actually resolves to, which works
// the same on Qt 5 and Qt 6 (QFontDatabase's methods are static only in Qt 6).
bool familyIsInstalled(const QString &family)
{
    return QFontInfo(QFont(family)).family().compare(family, Qt::CaseInsensitive) == 0;
}

// On some platforms QFontDatabase's "fixed font" is not actually fixed pitch
// (Qt 5 on a bare X11 session hands back a proportional family), so the result
// has to be verified before it is used.
QString resolveMonospaceFamily()
{
    const QFont systemFixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (QFontInfo(systemFixed).fixedPitch())
        return systemFixed.family();

    static const QStringList candidates = {
        QStringLiteral("Menlo"),            QStringLiteral("SF Mono"),
        QStringLiteral("Consolas"),         QStringLiteral("Cascadia Mono"),
        QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Liberation Mono"),
        QStringLiteral("Noto Sans Mono"),   QStringLiteral("Ubuntu Mono"),
        QStringLiteral("Courier New"),
    };
    for (const QString &candidate : candidates) {
        if (familyIsInstalled(candidate))
            return candidate;
    }

    QFont hinted;
    hinted.setStyleHint(QFont::Monospace);
    hinted.setFamily(QStringLiteral("monospace"));
    return QFontInfo(hinted).family();
}

bool looksLikeMarkdown(const QString &path)
{
    static const QStringList suffixes = {
        QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("mdown"),
        QStringLiteral("mkd"), QStringLiteral("mkdn"), QStringLiteral("mdwn"),
    };
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

// Defined further down; shared by the view and the printable copy.
void styleMarkdownDocument(QTextDocument *doc, const Colors &colors, qreal body,
                           const QString &monoFamily, qreal unit);
void scaleImagesToWidth(QTextDocument *doc, qreal available, QHash<QString, QSize> *cache);
QString tagFenceLanguages(const QString &markdown);
QString fenceLanguage(const QTextBlockFormat &format);
void fitCodeBlocksToWidth(QTextDocument *doc, qreal available, QPaintDevice *device);
QString withHardLineBreaks(const QString &markdown);
void convertSentinelsToLineBreaks(QTextDocument *doc);

} // namespace

MarkdownView::MarkdownView(QWidget *parent)
    : QTextBrowser(parent)
    , m_refitTimer(new QTimer(this))
    , m_zoomTimer(nullptr)
    , m_basePointSize(11.0)
{
    setOpenLinks(false); // links are routed through onAnchorClicked()
    setOpenExternalLinks(false);
    setReadOnly(true);
    setUndoRedoEnabled(false);
    setFrameShape(QFrame::NoFrame);
    setTabChangesFocus(true);
    setWordWrapMode(QTextOption::WordWrap);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard
                            | Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    document()->setUndoRedoEnabled(false);

    const qreal appPointSize = font().pointSizeF();
    if (appPointSize > 0)
        m_basePointSize = appPointSize;
    m_monoFamily = resolveMonospaceFamily();

    // Images are refitted to the viewport, so debounce window resizes.
    m_refitTimer->setSingleShot(true);
    m_refitTimer->setInterval(120);
    connect(m_refitTimer, &QTimer::timeout, this, &MarkdownView::scaleOversizedImages);

    m_zoomTimer = new QTimer(this);
    m_zoomTimer->setSingleShot(true);
    m_zoomTimer->setInterval(120);
    connect(m_zoomTimer, &QTimer::timeout, this, &MarkdownView::applyZoomToDocument);

    connect(this, &QTextBrowser::anchorClicked, this, &MarkdownView::onAnchorClicked);

    m_vim = new VimHandler(this, this);

    applyViewPalette();
}

QString MarkdownView::displayName() const
{
    if (!m_displayName.isEmpty())
        return m_displayName;
    if (!m_filePath.isEmpty())
        return QFileInfo(m_filePath).fileName();
    return QString();
}

qreal MarkdownView::bodyPointSize() const
{
    return m_basePointSize * m_zoom / 100.0;
}

bool MarkdownView::loadFile(const QString &path, bool keepScrollPosition)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QByteArray raw = file.readAll();
    if (file.error() != QFile::NoError)
        return false;

    const int scroll = verticalScrollBar()->value();
    m_filePath = QFileInfo(path).absoluteFilePath();
    m_displayName.clear();
    showMarkdown(QString::fromUtf8(raw), QFileInfo(m_filePath).absolutePath());

    if (keepScrollPosition)
        verticalScrollBar()->setValue(qMin(scroll, verticalScrollBar()->maximum()));
    return true;
}

void MarkdownView::showMarkdown(const QString &text, const QString &baseDir,
                                const QString &displayName)
{
    m_markdown = text;
    m_baseDir = baseDir;
    m_naturalImageSizes.clear();
    if (!displayName.isEmpty()) {
        m_displayName = displayName;
        m_filePath.clear();
    }
    render();
}

void MarkdownView::setSourceText(const QString &text)
{
    if (m_markdown == text)
        return;
    m_markdown = text;
    m_naturalImageSizes.clear();
    rerenderKeepingPlace();
}

void MarkdownView::setDarkMode(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    applyViewPalette();
    rerenderKeepingPlace();
}

void MarkdownView::setZoomPercent(int percent)
{
    const int clamped = qBound(50, percent, 400);
    if (clamped == m_zoom)
        return;
    m_zoom = clamped;
    // Coalesced here rather than in the caller, so holding the key down or
    // spinning the wheel costs one re-layout however the zoom was requested.
    m_zoomTimer->start();
    emit zoomChanged(m_zoom);
}

// Zoom changes the document's metrics but not its structure, so the Markdown
// does not have to be parsed again: re-running the styling pass with the new
// body size is several times cheaper, because the import and its first layout
// dominate a full render.
void MarkdownView::applyZoomToDocument()
{
    if (m_markdown.isEmpty())
        return;

    QTextDocument *doc = document();
    const QScrollBar *bar = verticalScrollBar();
    const qreal ratio = bar->maximum() > 0 ? qreal(bar->value()) / bar->maximum() : 0.0;
    const qreal body = bodyPointSize();

    QFont bodyFont = doc->defaultFont();
    bodyFont.setPointSizeF(body);

    setUpdatesEnabled(false);
    doc->setDefaultFont(bodyFont);
    doc->setDocumentMargin(qRound(body * 2.6));
    doc->setIndentWidth(qRound(body * 2.4));
    // The styling pass writes absolute sizes derived from the body size, so
    // running it again is idempotent rather than cumulative.
    styleDocument();
    scaleOversizedImages();
    setUpdatesEnabled(true);

    // Block positions did not move, so the outline needs no rebuild.
    document()->documentLayout()->documentSize();
    verticalScrollBar()->setValue(qRound(ratio * verticalScrollBar()->maximum()));
}

void MarkdownView::applyViewPalette()
{
    const Colors colors = colorsFor(m_dark);
    QPalette pal = palette();
    pal.setColor(QPalette::Base, colors.background);
    pal.setColor(QPalette::Window, colors.background);
    pal.setColor(QPalette::Text, colors.text);
    pal.setColor(QPalette::WindowText, colors.text);
    pal.setColor(QPalette::Link, colors.link);
    pal.setColor(QPalette::LinkVisited, colors.link);
    pal.setColor(QPalette::Highlight, colors.selection);
    pal.setColor(QPalette::HighlightedText, colors.selectedText);
    setPalette(pal);
}

void MarkdownView::render()
{
    if (m_zoomTimer)
        m_zoomTimer->stop(); // a full render already uses the current zoom
    QTextDocument *doc = document();
    const QUrl baseUrl = m_baseDir.isEmpty()
            ? QUrl()
            : QUrl::fromLocalFile(m_baseDir + QLatin1Char('/'));

    if (!m_baseDir.isEmpty())
        setSearchPaths(QStringList(m_baseDir));
    doc->setBaseUrl(baseUrl);

    const qreal body = bodyPointSize();
    QFont bodyFont = doc->defaultFont();
    bodyFont.setPointSizeF(body);
    doc->setDefaultFont(bodyFont);
    doc->setDocumentMargin(qRound(body * 2.6));
    // Lists indent by their level times this width, 40px by default, which
    // pushes list text uncomfortably far to the right.
    doc->setIndentWidth(qRound(body * 2.4));

    setUpdatesEnabled(false);
    QTextEdit::setMarkdown(sourceForRendering());
    if (m_hardLineBreaks)
        convertSentinelsToLineBreaks(doc);
    // The import clears the document, so restore document-level state.
    doc->setBaseUrl(baseUrl);
    doc->setUndoRedoEnabled(false);
    styleDocument();
    scaleOversizedImages();
    setUpdatesEnabled(true);

    emit documentRendered();
}

QString MarkdownView::sourceForRendering() const
{
    QString text = m_markdown;
    if (text.startsWith(QChar(0xFEFF))) // strip UTF-8 BOM
        text.remove(0, 1);
    text = tagFenceLanguages(text);
    return m_hardLineBreaks ? withHardLineBreaks(text) : text;
}

void MarkdownView::setHardLineBreaks(bool enabled)
{
    if (m_hardLineBreaks == enabled)
        return;
    m_hardLineBreaks = enabled;
    rerenderKeepingPlace();
}

// Printing needs a document that is independent of the on-screen one: always
// light (dark colours are invisible on paper), never zoomed, no page margin of
// its own, and laid out at the device's resolution.
QTextDocument *MarkdownView::createPrintDocument(QPagedPaintDevice *device,
                                                 qreal contentWidth) const
{
    QTextDocument *doc = new QTextDocument;
    if (device)
        doc->documentLayout()->setPaintDevice(device);

    QFont bodyFont = document()->defaultFont();
    bodyFont.setPointSizeF(m_basePointSize);
    doc->setDefaultFont(bodyFont);
    doc->setDocumentMargin(0);
    doc->setIndentWidth(qRound(m_basePointSize * 2.4 * (device && logicalDpiY() > 0
                                  ? qreal(device->logicalDpiY()) / logicalDpiY() : 1.0)));
    doc->setUndoRedoEnabled(false);
    if (!m_baseDir.isEmpty())
        doc->setBaseUrl(QUrl::fromLocalFile(m_baseDir + QLatin1Char('/')));

    doc->setMarkdown(sourceForRendering());
    if (m_hardLineBreaks)
        convertSentinelsToLineBreaks(doc);

    // Hand over the images the view already resolved; a bare QTextDocument has
    // no search paths of its own.
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat())
                continue;
            const QUrl name(fragment.charFormat().toImageFormat().name());
            if (name.isEmpty() || !doc->resource(QTextDocument::ImageResource, name).isNull())
                continue;
            const QVariant image = document()->resource(QTextDocument::ImageResource, name);
            if (!image.isNull())
                doc->addResource(QTextDocument::ImageResource, name, image);
        }
    }

    // Geometry is in device pixels, so the printer's higher resolution has to
    // be scaled in or the layout comes out cramped.
    const qreal dpiRatio = device && logicalDpiY() > 0
            ? qreal(device->logicalDpiY()) / logicalDpiY()
            : 1.0;
    styleMarkdownDocument(doc, colorsFor(false), m_basePointSize, m_monoFamily,
                          m_basePointSize * dpiRatio);
    if (contentWidth > 0) {
        doc->setTextWidth(contentWidth);
        scaleImagesToWidth(doc, contentWidth, nullptr);
        fitCodeBlocksToWidth(doc, contentWidth, device);
    }
    return doc;
}

void MarkdownView::rerenderKeepingPlace()
{
    if (m_markdown.isEmpty()) {
        render();
        return;
    }
    const QScrollBar *bar = verticalScrollBar();
    const qreal ratio = bar->maximum() > 0 ? qreal(bar->value()) / bar->maximum() : 0.0;

    render();

    document()->documentLayout()->documentSize(); // force layout so maximum() is final
    verticalScrollBar()->setValue(qRound(ratio * verticalScrollBar()->maximum()));
}

namespace {

// body is a font size in points; unit is the same distance expressed in the
// layout's device pixels, which is what block geometry is measured in.
void styleMarkdownDocument(QTextDocument *doc, const Colors &colors, qreal body,
                           const QString &monoFamily, qreal unit)
{
    const qreal codeSize = body * 0.92;
    const qreal headingUnit = unit / qMax(qreal(0.001), body); // px per point

    // One edit block around the whole pass: every table or cell format change
    // would otherwise relayout the entire document on its own.
    QTextCursor batch(doc);
    batch.beginEditBlock();

    QVector<QPair<int, int>> tableRanges;
    styleTables(doc->rootFrame(), colors, unit, &tableRanges);
    int tableIndex = 0;

    QVector<BlockEdit> blockEdits;
    QVector<CharEdit> charEdits;

    // Carried across the lines of one fenced block.
    QString codeLanguage;
    int codeState = 0;

    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        const QTextBlockFormat blockFormat = block.blockFormat();
        const int heading = blockFormat.headingLevel();
        const int quoteLevel = blockFormat.intProperty(QTextFormat::BlockQuoteLevel);
        const bool code = isCodeBlock(blockFormat);
        const qreal headingSize = heading > 0 ? body * kHeadingScale[qBound(1, heading, 6)] : body;

        QTextBlockFormat updated = blockFormat;
        bool blockChanged = false;

        // The importer leaks the last task-list marker onto later paragraphs.
        if (!block.textList() && blockFormat.marker() != QTextBlockFormat::MarkerType::NoMarker) {
            updated.setMarker(QTextBlockFormat::MarkerType::NoMarker);
            blockChanged = true;
        }

        if (heading > 0) {
            const bool first = block.position() == 0;
            const qreal headingPx = headingSize * headingUnit;
            updated.setTopMargin(qRound(headingPx * (first ? 0.2 : 1.15)));
            updated.setBottomMargin(qRound(headingPx * 0.45));
            blockChanged = true;
        } else if (code) {
            const bool firstLine = startsCodeRun(block);
            const bool lastLine = endsCodeRun(block);
            if (lastLine && block.text().isEmpty()) {
                // Qt 5's importer leaves an empty block after a fence; painting
                // it would add a stray stripe under the box.
                updated.setTopMargin(0);
                updated.setBottomMargin(qRound(unit * 0.7));
            } else {
                // One block per line, so only the outer edges get breathing
                // room; the shared background then reads as a single box.
                updated.setBackground(colors.codeBackground);
                updated.setTopMargin(firstLine ? qRound(unit * 0.7) : 0);
                updated.setBottomMargin(lastLine ? qRound(unit * 0.7) : 0);
                updated.setLineHeight(125, QTextBlockFormat::ProportionalHeight);
            }
            blockChanged = true;
        } else if (isHorizontalRule(blockFormat)) {
            updated.setTopMargin(qRound(unit * 1.1));
            updated.setBottomMargin(qRound(unit * 1.1));
            blockChanged = true;
        } else if (block.textList()) {
            updated.setTopMargin(qRound(unit * 0.15));
            updated.setBottomMargin(qRound(unit * 0.15));
            updated.setLineHeight(130, QTextBlockFormat::ProportionalHeight);
            blockChanged = true;
        } else if (block.text() == QString(QChar::ObjectReplacementCharacter)) {
            // A block holding just an image: proportional line height would
            // scale the image's own height and leave a large gap above it.
            updated.setTopMargin(qRound(unit * 0.6));
            updated.setBottomMargin(qRound(unit * 0.6));
            updated.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
            blockChanged = true;
        } else if (!block.text().isEmpty()) {
            // Blocks and tables are both visited in document order, so the
            // range list only needs a forward-moving cursor.
            while (tableIndex < tableRanges.size()
                   && tableRanges.at(tableIndex).second < block.position())
                ++tableIndex;
            // Table cells have their own padding; extra margins there just
            // make rows tall.
            const bool inTableCell = tableIndex < tableRanges.size()
                    && block.position() >= tableRanges.at(tableIndex).first;
            const qreal margin = inTableCell ? 0.0 : qRound(unit * 0.45);
            updated.setTopMargin(margin);
            updated.setBottomMargin(margin);
            updated.setLineHeight(inTableCell ? 120 : 140, QTextBlockFormat::ProportionalHeight);
            blockChanged = true;
        }

        if (quoteLevel > 0) {
            // The importer's own indent is scaled by indentWidth and ends up
            // far too deep, so drive the offset from the margin instead.
            updated.setIndent(0);
            updated.setLeftMargin(qRound(unit * 1.8 * quoteLevel));
            blockChanged = true;
        }

        if (blockChanged)
            blockEdits.append(BlockEdit(block.position(), updated));

        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || fragment.length() == 0)
                continue;

            QTextCharFormat format = fragment.charFormat();
            if (format.isImageFormat())
                continue;

            if (format.isAnchor())
                format.setForeground(colors.link);
            else if (heading > 0)
                format.setForeground(colors.heading);
            else if (quoteLevel > 0)
                format.setForeground(colors.muted);

            if (heading > 0) {
                // The importer sizes headings with a relative adjustment;
                // drop it so the explicit point size wins.
                format.clearProperty(QTextFormat::FontSizeAdjustment);
                format.setFontPointSize(headingSize);
                format.setFontWeight(QFont::Bold);
            }

            if (code) {
                setMonospace(&format, monoFamily, codeSize);
                format.clearForeground();
            } else if (format.fontFixedPitch()) { // inline `code` span
                setMonospace(&format, monoFamily, codeSize);
                format.setBackground(colors.codeBackground);
            }

            charEdits.append(CharEdit{fragment.position(), fragment.length(), format});
        }

        if (!code)
            continue;

        // Syntax colouring goes on after the plain code format, so the spans
        // win where they overlap.
        if (startsCodeRun(block)) {
            codeState = 0;
            codeLanguage = fenceLanguage(blockFormat);
        }
        if (!CodeHighlighter::isSupported(codeLanguage))
            continue;

        const QString text = block.text();
        const QVector<CodeHighlighter::Span> spans =
            CodeHighlighter::highlight(codeLanguage, text, &codeState);
        for (const CodeHighlighter::Span &span : spans) {
            const QColor color = colorForToken(colors, span.token);
            if (!color.isValid())
                continue;
            QTextCharFormat format;
            setMonospace(&format, monoFamily, codeSize);
            format.setForeground(color);
            if (span.token == CodeHighlighter::Token::Comment)
                format.setFontItalic(true);
            charEdits.append(CharEdit{block.position() + span.start, span.length, format});
        }
    }

    commitEdits(doc, blockEdits, charEdits);
    batch.endEditBlock();
}

// Shrinks images that are wider than the available width, keeping the aspect
// ratio, and restores their natural size when there is room again.
void scaleImagesToWidth(QTextDocument *doc, qreal available, QHash<QString, QSize> *cache)
{
    if (available <= 32)
        return;

    QVector<CharEdit> edits;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat())
                continue;

            QTextImageFormat image = fragment.charFormat().toImageFormat();
            const QString name = image.name();
            if (name.isEmpty())
                continue;

            QSize natural = cache ? cache->value(name) : QSize();
            if (natural.isEmpty()) {
                const QVariant resource = doc->resource(QTextDocument::ImageResource, QUrl(name));
                QImage loaded = qvariant_cast<QImage>(resource);
                if (loaded.isNull())
                    loaded = qvariant_cast<QPixmap>(resource).toImage();
                if (loaded.isNull())
                    continue;
                natural = loaded.size();
                if (cache)
                    cache->insert(name, natural);
            }

            qreal width = natural.width();
            qreal height = natural.height();
            if (width > available) {
                height = height * available / width;
                width = available;
            }
            if (qAbs(image.width() - width) < 1.0 && qAbs(image.height() - height) < 1.0)
                continue;

            image.setWidth(width);
            image.setHeight(height);
            edits.append(CharEdit{fragment.position(), fragment.length(), image});
        }
    }
    commitEdits(doc, QVector<BlockEdit>(), edits);
}

// Code blocks are laid out with unbreakable lines, which on paper means any
// line wider than the page is simply cut off. Shrink each fenced run just
// enough to fit; if even the smallest size is too narrow, let it wrap so that
// nothing is lost.
void fitCodeBlocksToWidth(QTextDocument *doc, qreal available, QPaintDevice *device)
{
    if (available <= 0)
        return;

    QVector<BlockEdit> blockEdits;
    QVector<CharEdit> charEdits;

    for (QTextBlock block = doc->begin(); block.isValid();) {
        if (!isCodeBlock(block.blockFormat())) {
            block = block.next();
            continue;
        }

        // Collect one fenced run and measure its widest line.
        QVector<QTextBlock> run;
        qreal widest = 0;
        for (QTextBlock line = block; line.isValid(); line = line.next()) {
            run.append(line);
            const QFontMetricsF metrics(line.charFormat().font(), device);
            widest = qMax(widest, metrics.horizontalAdvance(line.text()));
            if (endsCodeRun(line))
                break;
        }
        block = run.last().next();

        if (widest <= available || widest <= 0)
            continue;

        const qreal wanted = available / widest;
        const qreal factor = qMax(wanted, qreal(0.55));
        const bool stillTooWide = wanted < 0.55;

        for (const QTextBlock &line : run) {
            if (stillTooWide) {
                QTextBlockFormat format = line.blockFormat();
                format.setNonBreakableLines(false);
                blockEdits.append(BlockEdit(line.position(), format));
            }
            for (QTextBlock::iterator it = line.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                if (!fragment.isValid())
                    continue;
                QTextCharFormat format = fragment.charFormat();
                const qreal size = format.fontPointSize() > 0 ? format.fontPointSize()
                                                              : doc->defaultFont().pointSizeF();
                format.setFontPointSize(size * factor);
                charEdits.append(CharEdit{fragment.position(), fragment.length(), format});
            }
        }
    }

    commitEdits(doc, blockEdits, charEdits);
}

// Vertical tab: whitespace to the Markdown parser, but never used in prose.
const QChar kLineBreakSentinel = QChar(0x0b);

// Replaces the sentinels left by withHardLineBreaks() with Qt's own
// within-paragraph line separator. One character for one character, so
// positions stay valid while iterating.
void convertSentinelsToLineBreaks(QTextDocument *doc)
{
    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        const QString text = block.text();
        for (int i = text.indexOf(kLineBreakSentinel); i >= 0;
             i = text.indexOf(kLineBreakSentinel, i + 1)) {
            cursor.setPosition(block.position() + i);
            cursor.setPosition(block.position() + i + 1, QTextCursor::KeepAnchor);
            cursor.insertText(QString(QChar::LineSeparator));
        }
    }
    cursor.endEditBlock();
}

// Consecutive fences sit next to each other in the document with nothing in
// between, so two adjacent ```cpp blocks are indistinguishable: their
// backgrounds merge into one box and the first one's language leaks into the
// second. Tagging each fence's language makes them tell apart; the tag has to
// go inside the first word, because that is all the importer keeps.
QString tagFenceLanguages(const QString &markdown)
{
    QStringList lines = markdown.split(QLatin1Char('\n'));
    bool inFence = false;
    QString marker;
    int index = 0;

    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (!trimmed.startsWith(QLatin1String("```")) && !trimmed.startsWith(QLatin1String("~~~")))
            continue;
        if (inFence) {
            if (trimmed.startsWith(marker))
                inFence = false;
            continue;
        }
        inFence = true;
        marker = trimmed.left(3);
        lines[i] = lines.at(i) + QStringLiteral("-qmdv%1").arg(index++);
    }
    return lines.join(QLatin1Char('\n'));
}

// The language a fence declared, without the uniqueness tag.
QString fenceLanguage(const QTextBlockFormat &format)
{
    QString language = format.stringProperty(QTextFormat::BlockCodeLanguage)
                           .section(QLatin1Char(' '), 0, 0)
                           .trimmed();
    static const QRegularExpression tag(QStringLiteral("-qmdv\\d+$"));
    return language.remove(tag);
}

// True when a line opens a new Markdown block instead of continuing the
// paragraph above it.
bool startsMarkdownBlock(const QString &line)
{
    const QString text = line.trimmed();
    if (text.isEmpty())
        return true;
    static const QRegularExpression pattern(QStringLiteral(
        "^("
        "#{1,6}($|\\s)"            // ATX heading
        "|>"                       // block quote
        "|[-*+]($|\\s)"            // bullet list item
        "|\\d{1,9}[.)]($|\\s)"     // ordered list item
        "|```|~~~"                 // code fence
        "|\\|"                     // table row
        "|<"                       // raw HTML
        "|={2,}$"                  // setext underline
        "|-{3,}$|\\*{3,}$|_{3,}$"  // thematic break
        ")"));
    return pattern.match(text).hasMatch();
}

// True when a line is a block that finishes on that line, so the line after it
// can never be a continuation of it.
bool lineIsWholeBlock(const QString &line)
{
    const QString text = line.trimmed();
    static const QRegularExpression pattern(QStringLiteral(
        "^(#{1,6}($|\\s)|\\||={2,}$|-{3,}$|\\*{3,}$|_{3,}$)"));
    return pattern.match(text).hasMatch();
}

// Continuation lines are joined with a vertical tab, which the parser treats
// as ordinary whitespace, and which convertSentinelsToLineBreaks() turns into
// a real line separator once the document is built.
//
// The obvious alternatives do not work: Markdown's own two-space break makes
// the importer start a new block, so the paragraph spacing around it reads as
// a blank line, and joining with U+2028 directly stops "**bold**" at the end
// of a line from closing, because the parser only counts ASCII whitespace when
// deciding whether a delimiter run can close.
QString withHardLineBreaks(const QString &markdown)
{
    // A literal sentinel in the source would be indistinguishable from ours.
    const QStringList lines = QString(markdown).remove(kLineBreakSentinel)
                                      .split(QLatin1Char('\n'));
    QString result;
    result.reserve(markdown.size() + lines.size());

    bool inFence = false;
    bool joinToPrevious = false;
    QString fence;

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        const QString trimmed = line.trimmed();

        if (i > 0)
            result += joinToPrevious ? kLineBreakSentinel : QChar(QLatin1Char('\n'));
        // Leading indentation is meaningless once the lines are joined.
        result += joinToPrevious ? trimmed : line;
        joinToPrevious = false;

        if (inFence) {
            if (trimmed.startsWith(fence))
                inFence = false;
            continue;
        }
        if (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"))) {
            inFence = true;
            fence = trimmed.left(3);
            continue;
        }
        if (i + 1 >= lines.size())
            continue;

        const QString &next = lines.at(i + 1);
        const bool indentedCode = line.startsWith(QLatin1String("    "))
                || line.startsWith(QLatin1Char('\t'));
        joinToPrevious = !trimmed.isEmpty() && !indentedCode && !lineIsWholeBlock(line)
                && !startsMarkdownBlock(next);
    }
    return result;
}

} // namespace

void MarkdownView::styleDocument()
{
    const qreal body = bodyPointSize();
    styleMarkdownDocument(document(), colorsFor(m_dark), body, m_monoFamily, body);
}

void MarkdownView::scaleOversizedImages()
{
    if (m_refitting)
        return;

    QTextDocument *doc = document();
    const qreal margins = 2 * doc->documentMargin();
    const qreal available = (doc->textWidth() > 0 ? doc->textWidth() : viewport()->width()) - margins;

    // Relayout can toggle the scroll bar and re-enter through resizeEvent().
    m_refitting = true;
    scaleImagesToWidth(doc, available, &m_naturalImageSizes);
    m_refitting = false;
}

QVector<MarkdownView::Heading> MarkdownView::headings() const
{
    QVector<Heading> result;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const int level = block.blockFormat().headingLevel();
        const QString text = block.text().trimmed();
        if (level > 0 && !text.isEmpty()) {
            Heading heading;
            heading.level = qBound(1, level, 6);
            heading.text = text;
            heading.position = block.position();
            result.append(heading);
        }
    }
    return result;
}

int MarkdownView::headingAtViewportTop() const
{
    const int top = cursorForPosition(QPoint(2, 2)).position();
    int found = -1;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        if (block.position() > top)
            break;
        if (block.blockFormat().headingLevel() > 0)
            found = block.position();
    }
    return found;
}

void MarkdownView::goToPosition(int position)
{
    QTextCursor cursor(document());
    cursor.setPosition(qBound(0, position, qMax(0, document()->characterCount() - 1)));
    setTextCursor(cursor);

    const QRectF rect = document()->documentLayout()->blockBoundingRect(cursor.block());
    verticalScrollBar()->setValue(qMax(0, qRound(rect.top()) - 8));
}

int MarkdownView::positionForAnchor(const QString &anchor) const
{
    const QString wanted = slugify(anchor);
    if (wanted.isEmpty())
        return -1;
    const QVector<Heading> all = headings();
    for (const Heading &heading : all) {
        if (slugify(heading.text) == wanted)
            return heading.position;
    }
    return -1;
}

void MarkdownView::onAnchorClicked(const QUrl &url)
{
    if (url.isEmpty())
        return;

    // Same-document anchor: "#section".
    if (url.scheme().isEmpty() && url.path().isEmpty() && url.hasFragment()) {
        const int position = positionForAnchor(url.fragment());
        if (position >= 0)
            goToPosition(position);
        else
            scrollToAnchor(url.fragment());
        return;
    }

    QUrl target = url;
    if (url.isRelative() && !m_baseDir.isEmpty())
        target = QUrl::fromLocalFile(QDir(m_baseDir).absoluteFilePath(url.path()));

    if (target.isLocalFile()) {
        const QString path = target.toLocalFile();
        if (looksLikeMarkdown(path) && QFileInfo::exists(path)) {
            emit markdownLinkActivated(path);
            return;
        }
    }
    QDesktopServices::openUrl(target);
}

void MarkdownView::setVimNavigation(bool enabled)
{
    // The same engine as the editor, in its read-only guise: motions move a
    // real cursor, scrolling and visual selection work, and editing is
    // refused.
    m_vim->setEnabled(enabled);
}

void MarkdownView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const int delta = event->angleDelta().y();
        if (delta != 0)
            setZoomPercent(m_zoom + (delta > 0 ? 10 : -10));
        event->accept();
        return;
    }
    QTextBrowser::wheelEvent(event);
}

void MarkdownView::resizeEvent(QResizeEvent *event)
{
    QTextBrowser::resizeEvent(event);
    if (!m_naturalImageSizes.isEmpty() || !m_markdown.isEmpty())
        m_refitTimer->start();
}

bool MarkdownView::vimNavigation() const
{
    return m_vim && m_vim->isEnabled();
}
