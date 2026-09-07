#ifndef MARKDOWNVIEW_H
#define MARKDOWNVIEW_H

#include <QHash>
#include <QSize>
#include <QString>
#include <QTextBrowser>
#include <QVector>

class VimHandler;

QT_BEGIN_NAMESPACE
class QPagedPaintDevice;
class QTimer;
QT_END_NAMESPACE

// A read-only Markdown renderer on top of QTextDocument's built-in Markdown
// importer, so no third-party Markdown library is required.
//
// The importer only produces a bare document (heading levels, bold, code
// flags, tables). setDefaultStyleSheet() does not apply to imported Markdown -
// it is HTML-only - so the look is applied by walking the finished document
// and setting explicit character/block/table formats instead.
class MarkdownView : public QTextBrowser
{
    Q_OBJECT

public:
    explicit MarkdownView(QWidget *parent = nullptr);

    // Renders the file at path. Returns false and leaves the current content
    // in place if the file cannot be read.
    bool loadFile(const QString &path, bool keepScrollPosition = false);

    // Renders text directly; baseDir resolves relative images and links.
    void showMarkdown(const QString &text, const QString &baseDir = QString(),
                      const QString &displayName = QString());

    // Re-renders from edited source, keeping the file association and place.
    void setSourceText(const QString &text);
    QString sourceText() const { return m_markdown; }

    QString filePath() const { return m_filePath; }
    QString displayName() const;
    bool isEmpty() const { return m_markdown.isEmpty(); }

    void setDarkMode(bool dark);
    bool darkMode() const { return m_dark; }

    // Markdown collapses a single newline into a space. When enabled, every
    // newline in the source is rendered as a line break instead.
    void setHardLineBreaks(bool enabled);
    bool hardLineBreaks() const { return m_hardLineBreaks; }

    // vim-style reading keys: cursor motions, scrolling, visual selection,
    // [[/]] between headings, "/" for the find bar and * / # for the word
    // under the cursor.
    void setVimNavigation(bool enabled);
    bool vimNavigation() const;
    VimHandler *vim() const { return m_vim; }

    // A light-themed, unzoomed copy of the document for printing or PDF
    // export, laid out for \a device. The caller takes ownership.
    QTextDocument *createPrintDocument(QPagedPaintDevice *device, qreal contentWidth) const;

    int zoomPercent() const { return m_zoom; }
    void setZoomPercent(int percent);

    struct Heading {
        int level = 0;
        QString text;
        int position = 0;
    };
    QVector<Heading> headings() const;

    // Document position of the last heading at or above the top of the
    // viewport, or -1 when the document has no headings.
    int headingAtViewportTop() const;
    void goToPosition(int position);

signals:
    // A link pointing at another local Markdown file was clicked.
    void markdownLinkActivated(const QString &path);

    // Emitted after every (re-)render; document positions changed.
    void documentRendered();
    void zoomChanged(int percent);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;


private:
    void render();
    void rerenderKeepingPlace();
    void applyZoomToDocument();
    void applyViewPalette();
    void styleDocument();
    void scaleOversizedImages();
    QString sourceForRendering() const;
    void onAnchorClicked(const QUrl &url);
    int positionForAnchor(const QString &anchor) const;
    qreal bodyPointSize() const;

    QString m_filePath;
    QString m_displayName;
    QString m_baseDir;
    QString m_markdown;
    QString m_monoFamily;
    QHash<QString, QSize> m_naturalImageSizes;
    QTimer *m_refitTimer;
    QTimer *m_zoomTimer;
    qreal m_basePointSize;
    int m_zoom = 100;
    bool m_dark = false;
    bool m_hardLineBreaks = true;
    VimHandler *m_vim = nullptr;
    bool m_refitting = false;
};

#endif // MARKDOWNVIEW_H
