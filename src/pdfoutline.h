#ifndef PDFOUTLINE_H
#define PDFOUTLINE_H

#include <QString>
#include <QVector>

// Qt's QPdfWriter can lay out pages but cannot write a document outline - the
// bookmark tree that PDF viewers show in their sidebar. This adds one to a
// finished file as an incremental update, which appends new objects and a new
// cross-reference section without touching the bytes Qt wrote.
namespace PdfOutline {

struct Bookmark
{
    int level = 1;           // 1 for a top-level entry, 2 for its children, ...
    QString title;
    int pageIndex = 0;       // zero-based page in the file
    qreal topPoints = 0;     // scroll target, in points up from the page bottom
};

// Returns false and leaves the file untouched if it cannot be parsed.
bool addTo(const QString &filePath, const QVector<Bookmark> &bookmarks);

} // namespace PdfOutline

#endif // PDFOUTLINE_H
