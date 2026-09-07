#include "pdfoutline.h"

#include <QByteArray>
#include <QFile>
#include <QRegularExpression>
#include <algorithm>
#include <QVector>

namespace PdfOutline {
namespace {

// One entry of the outline tree, resolved into object numbers and links.
struct Item
{
    int object = 0;
    int level = 1;
    QString title;
    int pageIndex = 0;
    qreal topPoints = 0;
    int parent = -1;
    int previous = -1;
    int next = -1;
    int firstChild = -1;
    int lastChild = -1;
    int descendants = 0;
};

// PDF text strings are safest as UTF-16BE hex, which needs no escaping and
// carries the em-dashes and arrows that show up in headings.
QByteArray pdfString(const QString &text)
{
    QByteArray out("<FEFF");
    for (const QChar ch : text)
        out += QByteArray::number(ch.unicode(), 16).rightJustified(4, '0').toUpper();
    return out + ">";
}

QByteArray number(qreal value)
{
    return QByteArray::number(value, 'f', 2);
}

// Body of the object numbered `object`, i.e. what sits between "obj" and
// "endobj". Empty when the object cannot be found.
QByteArray objectBody(const QByteArray &pdf, int object)
{
    const QByteArray needle = QByteArray::number(object) + " 0 obj";
    int at = -1;
    for (int from = 0;; ) {
        at = pdf.indexOf(needle, from);
        if (at < 0)
            return QByteArray();
        // Must be the start of a line, not the tail of "123 0 obj".
        if (at == 0 || pdf.at(at - 1) == '\n' || pdf.at(at - 1) == '\r')
            break;
        from = at + needle.size();
    }
    const int start = at + needle.size();
    const int end = pdf.indexOf("endobj", start);
    if (end < 0)
        return QByteArray();
    return pdf.mid(start, end - start);
}

int referenceIn(const QByteArray &dictionary, const char *key)
{
    const QRegularExpression pattern(QStringLiteral("%1\\s+(\\d+)\\s+0\\s+R")
                                         .arg(QRegularExpression::escape(
                                             QString::fromLatin1(key))));
    const auto match = pattern.match(QString::fromLatin1(dictionary));
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

// The trailer nearest the end of the file wins; earlier ones belong to
// previous revisions.
QByteArray lastTrailer(const QByteArray &pdf)
{
    const int at = pdf.lastIndexOf("trailer");
    if (at < 0)
        return QByteArray();
    const int open = pdf.indexOf("<<", at);
    const int close = pdf.indexOf(">>", open);
    if (open < 0 || close < 0)
        return QByteArray();
    return pdf.mid(open, close - open + 2);
}

QVector<int> pageObjects(const QByteArray &pdf, int pagesObject)
{
    QVector<int> pages;
    const QByteArray body = objectBody(pdf, pagesObject);
    const int open = body.indexOf("/Kids");
    if (open < 0)
        return pages;
    const int start = body.indexOf('[', open);
    const int end = body.indexOf(']', start);
    if (start < 0 || end < 0)
        return pages;

    const QString kids = QString::fromLatin1(body.mid(start + 1, end - start - 1));
    static const QRegularExpression reference(QStringLiteral("(\\d+)\\s+0\\s+R"));
    auto it = reference.globalMatch(kids);
    while (it.hasNext())
        pages.append(it.next().captured(1).toInt());
    return pages;
}

void linkTree(QVector<Item> *items)
{
    QVector<int> stack; // indices of the open ancestors
    for (int i = 0; i < items->size(); ++i) {
        Item &item = (*items)[i];
        while (!stack.isEmpty() && (*items)[stack.last()].level >= item.level)
            stack.removeLast();

        if (!stack.isEmpty()) {
            const int parent = stack.last();
            item.parent = parent;
            if ((*items)[parent].firstChild < 0)
                (*items)[parent].firstChild = i;
            const int previous = (*items)[parent].lastChild;
            if (previous >= 0) {
                (*items)[previous].next = i;
                item.previous = previous;
            }
            (*items)[parent].lastChild = i;
        } else {
            // Top level: link against the previous top-level entry.
            for (int j = i - 1; j >= 0; --j) {
                if ((*items)[j].parent < 0) {
                    (*items)[j].next = i;
                    item.previous = j;
                    break;
                }
            }
        }
        stack.append(i);
    }

    // Children come after their parents, so counting backwards fills in the
    // totals in one pass.
    for (int i = items->size() - 1; i >= 0; --i) {
        const Item &item = (*items)[i];
        if (item.parent >= 0)
            (*items)[item.parent].descendants += 1 + item.descendants;
    }
}

} // namespace

bool addTo(const QString &filePath, const QVector<Bookmark> &bookmarks)
{
    if (bookmarks.isEmpty())
        return false;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray pdf = file.readAll();
    file.close();

    const int lastStartxref = pdf.lastIndexOf("startxref");
    const QByteArray trailer = lastTrailer(pdf);
    if (lastStartxref < 0 || trailer.isEmpty())
        return false;

    const QByteArray previousOffset =
        QByteArray::number(QString::fromLatin1(pdf.mid(lastStartxref + 9, 32))
                               .trimmed()
                               .section(QRegularExpression(QStringLiteral("\\s")), 0, 0)
                               .toLongLong());

    const int rootObject = referenceIn(trailer, "/Root");
    const int infoObject = referenceIn(trailer, "/Info");
    if (rootObject < 0)
        return false;

    const QByteArray catalog = objectBody(pdf, rootObject);
    const int pagesObject = referenceIn(catalog, "/Pages");
    if (catalog.isEmpty() || pagesObject < 0)
        return false;

    const QVector<int> pages = pageObjects(pdf, pagesObject);
    if (pages.isEmpty())
        return false;

    const QRegularExpression sizePattern(QStringLiteral("/Size\\s+(\\d+)"));
    const auto sizeMatch = sizePattern.match(QString::fromLatin1(trailer));
    if (!sizeMatch.hasMatch())
        return false;
    int nextObject = sizeMatch.captured(1).toInt();

    // Object numbers: the outline root first, then one per bookmark.
    const int outlineObject = nextObject++;
    QVector<Item> items;
    items.reserve(bookmarks.size());
    for (const Bookmark &bookmark : bookmarks) {
        Item item;
        item.object = nextObject++;
        item.level = qMax(1, bookmark.level);
        item.title = bookmark.title;
        item.pageIndex = qBound(0, bookmark.pageIndex, pages.size() - 1);
        item.topPoints = bookmark.topPoints;
        items.append(item);
    }
    linkTree(&items);

    int rootCount = 0;
    int firstTop = -1;
    int lastTop = -1;
    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i).parent >= 0)
            continue;
        rootCount += 1 + items.at(i).descendants;
        if (firstTop < 0)
            firstTop = i;
        lastTop = i;
    }
    if (firstTop < 0)
        return false;

    // Build the appended revision, recording where each object starts.
    QByteArray addition;
    QVector<QPair<int, qint64>> offsets; // object number -> byte offset
    const qint64 base = pdf.size();

    auto appendObject = [&](int object, const QByteArray &body) {
        offsets.append(qMakePair(object, base + addition.size()));
        addition += QByteArray::number(object) + " 0 obj\n" + body + "\nendobj\n";
    };

    // The catalog has to be reissued so it can point at the outline.
    QByteArray newCatalog = catalog.trimmed();
    const int openDict = newCatalog.indexOf("<<");
    if (openDict < 0)
        return false;
    newCatalog.insert(openDict + 2,
                      "\n/Outlines " + QByteArray::number(outlineObject) + " 0 R"
                      "\n/PageMode /UseOutlines");
    appendObject(rootObject, newCatalog);

    appendObject(outlineObject,
                 "<<\n/Type /Outlines"
                 "\n/First " + QByteArray::number(items.at(firstTop).object) + " 0 R"
                 "\n/Last " + QByteArray::number(items.at(lastTop).object) + " 0 R"
                 "\n/Count " + QByteArray::number(rootCount) + "\n>>");

    for (const Item &item : items) {
        QByteArray body = "<<\n/Title " + pdfString(item.title);
        body += "\n/Parent " + QByteArray::number(item.parent >= 0
                                                      ? items.at(item.parent).object
                                                      : outlineObject) + " 0 R";
        if (item.previous >= 0)
            body += "\n/Prev " + QByteArray::number(items.at(item.previous).object) + " 0 R";
        if (item.next >= 0)
            body += "\n/Next " + QByteArray::number(items.at(item.next).object) + " 0 R";
        if (item.firstChild >= 0) {
            body += "\n/First " + QByteArray::number(items.at(item.firstChild).object) + " 0 R";
            body += "\n/Last " + QByteArray::number(items.at(item.lastChild).object) + " 0 R";
            // Positive: the branch shows up expanded.
            body += "\n/Count " + QByteArray::number(item.descendants);
        }
        body += "\n/Dest [" + QByteArray::number(pages.at(item.pageIndex))
                + " 0 R /XYZ null " + number(item.topPoints) + " null]";
        body += "\n>>";
        appendObject(item.object, body);
    }

    // Cross-reference section for just the objects this revision touches.
    std::sort(offsets.begin(), offsets.end(),
              [](const QPair<int, qint64> &a, const QPair<int, qint64> &b) {
                  return a.first < b.first;
              });

    const qint64 xrefOffset = base + addition.size();
    QByteArray xref = "xref\n0 1\n0000000000 65535 f \n";
    int index = 0;
    while (index < offsets.size()) {
        int end = index;
        while (end + 1 < offsets.size() && offsets.at(end + 1).first == offsets.at(end).first + 1)
            ++end;
        xref += QByteArray::number(offsets.at(index).first) + " "
                + QByteArray::number(end - index + 1) + "\n";
        for (int i = index; i <= end; ++i) {
            xref += QByteArray::number(offsets.at(i).second).rightJustified(10, '0')
                    + " 00000 n \n";
        }
        index = end + 1;
    }

    QByteArray tail = "trailer\n<<\n/Size " + QByteArray::number(nextObject)
            + "\n/Root " + QByteArray::number(rootObject) + " 0 R";
    if (infoObject >= 0)
        tail += "\n/Info " + QByteArray::number(infoObject) + " 0 R";
    tail += "\n/Prev " + previousOffset + "\n>>\nstartxref\n"
            + QByteArray::number(xrefOffset) + "\n%%EOF\n";

    if (!file.open(QIODevice::Append))
        return false;
    const bool ok = file.write(addition) == addition.size()
            && file.write(xref) == xref.size()
            && file.write(tail) == tail.size();
    file.close();
    return ok;
}

} // namespace PdfOutline
