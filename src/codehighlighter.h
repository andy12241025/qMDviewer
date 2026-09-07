#ifndef CODEHIGHLIGHTER_H
#define CODEHIGHLIGHTER_H

#include <QString>
#include <QVector>

// Small dependency-free syntax highlighter for fenced code blocks.
//
// Code arrives one line per text block, so lines are scanned individually and
// constructs that span lines (block comments, triple-quoted strings) are
// carried over through the state parameter. Languages that are not recognised
// produce no spans at all, which leaves unlabelled fences - log excerpts,
// ASCII diagrams - untouched.
namespace CodeHighlighter {

enum class Token {
    Plain,
    Keyword,
    Type,
    Builtin,
    String,
    Number,
    Comment,
    Preprocessor,
    Attribute,
    Added,
    Removed,
};

struct Span
{
    int start = 0;
    int length = 0;
    Token token = Token::Plain;
};

// Language names are fence info strings such as "cpp", "sh" or "yaml".
bool isSupported(const QString &language);

// Pass 0 in *state for the first line of a block and keep feeding the same
// variable back in for the following lines.
QVector<Span> highlight(const QString &language, const QString &line, int *state);

} // namespace CodeHighlighter

#endif // CODEHIGHLIGHTER_H
