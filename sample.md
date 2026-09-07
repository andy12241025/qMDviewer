# Sample document

A page that exercises everything the renderer handles. Open it with
`qmdviewer sample.md`, then edit it in another editor and watch the view
follow along.

## Inline formatting

Regular text with **bold**, *italic*, ***both***, ~~struck through~~,
`inline code`, and a [link to the Qt docs](https://doc.qt.io/qt-6/qtextdocument.html).
Autolinks work too: <https://commonmark.org>.

An in-document link jumps to [Tables](#tables).

## Lists

1. Ordered items
2. Second item
   1. Nested ordered
   2. Another
3. Third

- Unordered item
- With a nested list
  - Deeper
    - Deeper still

Task lists (a GitHub dialect extension):

- [x] Render Markdown without extra dependencies
- [x] Reload when the file changes on disk
- [ ] Syntax highlighting inside code fences

## Code

Inline `QTextDocument::setMarkdown()` versus a fenced block:

```cpp
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MarkdownView view;
    view.loadFile(QStringLiteral("sample.md"));
    view.show();
    return app.exec();
}
```

An indented block works as well:

    $ cmake -B build -DCMAKE_BUILD_TYPE=Release
    $ cmake --build build

## Tables

| Feature | Shortcut | Notes |
| --- | --- | --- |
| Open | `Ctrl+O` | Drag and drop also works |
| Find | `Ctrl+F` | `F3` / `Shift+F3` cycle matches |
| Reload | `F5` | Automatic when the file changes |
| Zoom | `Ctrl+ +` / `Ctrl+ -` | Or `Ctrl` plus the mouse wheel |
| Outline | `Ctrl+Shift+O` | Follows the scroll position |

## Quotes

> A blockquote is indented and dimmed.
>
> > Nested quotes indent further.

## Rules and headings

---

### Third level

#### Fourth level

##### Fifth level

###### Sixth level

Paragraphs keep comfortable line spacing so long passages stay readable, which
matters more in a viewer than in an editor. Images are scaled down to fit the
window width and scaled back up when the window grows.
