#ifndef MARKDOWNEDITOR_H
#define MARKDOWNEDITOR_H

#include <QPlainTextEdit>

class VimHandler;

QT_BEGIN_NAMESPACE
class QPaintEvent;
class QWheelEvent;
class QResizeEvent;
class QSyntaxHighlighter;
QT_END_NAMESPACE

// Plain-text source editor for the Markdown being previewed: monospaced, with
// an optional line-number gutter, light highlighting of the Markdown itself,
// and an optional vim mode.
class MarkdownEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit MarkdownEditor(QWidget *parent = nullptr);

    VimHandler *vim() const { return m_vim; }
    void setVimEnabled(bool enabled);
    bool isVimEnabled() const;

    void setLineNumbersVisible(bool visible);
    bool lineNumbersVisible() const { return m_lineNumbersVisible; }

    void setDarkMode(bool dark);
    void setBasePointSize(qreal points);
    qreal basePointSize() const { return m_basePointSize; }

    int zoomPercent() const { return m_zoom; }
    void setZoomPercent(int percent);

signals:
    void zoomChanged(int percent);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    friend class LineNumberArea;
    void paintLineNumbers(QPaintEvent *event);
    int lineNumberAreaWidth() const;
    void updateViewportMargins();
    void applyFont();
    void highlightCurrentLine();
    void applyPalette();

    QWidget *m_lineNumbers = nullptr;
    QSyntaxHighlighter *m_highlighter = nullptr;
    VimHandler *m_vim = nullptr;
    bool m_lineNumbersVisible = true;
    bool m_dark = false;
    int m_zoom = 100;
    qreal m_basePointSize = 11.0;
};

#endif // MARKDOWNEDITOR_H
