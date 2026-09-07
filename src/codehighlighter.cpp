#include "codehighlighter.h"

#include <QHash>
#include <QSet>
#include <QStringList>

namespace CodeHighlighter {
namespace {

enum State {
    StateNormal = 0,
    StateBlockComment,
    StateTripleSingle,
    StateTripleDouble,
};

enum Family {
    FamilyC,
    FamilyShell,
    FamilyPython,
    FamilyPerl,
    FamilySql,
    FamilyJson,
    FamilyYaml,
    FamilyIni,
    FamilyDiff,
    FamilyMarkup,
};

struct Rules
{
    Family family = FamilyC;
    QStringList lineComments;
    bool blockComments = false; // /* ... */
    bool tripleQuotes = false;  // ''' and """
    bool backticks = false;     // `command`
    bool sigils = false;        // $var, @list, %hash
    bool preprocessor = false;  // #include
    bool caseInsensitive = false;
    QSet<QString> keywords;
    QSet<QString> types;
    QSet<QString> builtins;
};

QSet<QString> words(const char *list)
{
    QSet<QString> result;
    for (const QString &word : QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts))
        result.insert(word);
    return result;
}

Rules cRules()
{
    Rules r;
    r.family = FamilyC;
    r.lineComments = QStringList{ QStringLiteral("//") };
    r.blockComments = true;
    r.preprocessor = true;
    r.backticks = true; // JavaScript template literals
    r.keywords = words(
        "alignas alignof and as asm async await break case catch class const consteval constexpr "
        "continue decltype def default defer delete do dyn else enum explicit export extends extern "
        "final finally fn for friend func function go goto if impl implements import in inline "
        "instanceof interface let loop match mod move mutable namespace new noexcept not nullptr "
        "operator or override package private protected public pub ref register return self sizeof "
        "static struct super switch template this throw throws trait try typedef typeid typename "
        "union unsafe using var virtual volatile where while with yield");
    r.types = words(
        "auto bool boolean byte char char16_t char32_t char8_t const_cast double dynamic_cast f32 "
        "f64 float i16 i32 i64 i8 int int16_t int32_t int64_t int8_t isize long map number Option "
        "Result set short signed size_t ssize_t static_cast str String string u16 u32 u64 u8 uint "
        "uint16_t uint32_t uint64_t uint8_t unsigned usize val vector void wchar_t");
    r.builtins = words("false NULL nullptr true undefined null nil");
    return r;
}

Rules shellRules()
{
    Rules r;
    r.family = FamilyShell;
    r.lineComments = QStringList{ QStringLiteral("#") };
    r.backticks = true;
    r.sigils = true;
    r.keywords = words(
        "if then else elif fi for while until do done case esac in function select time coproc "
        "break continue return exit local export readonly declare typeset unset shift eval exec "
        "source trap set unalias alias wait");
    r.builtins = words(
        "awk basename cat cd chmod chown cp cut date dirname echo env exec expr find grep head "
        "hostname id kill ln ls mkdir mv printf ps pwd read rm rmdir sed sleep sort ssh stat sudo "
        "tail tar tee test touch tr true false umask uname uniq wc which xargs systemctl service "
        "mysql mysqladmin mysqldump");
    return r;
}

Rules pythonRules()
{
    Rules r;
    r.family = FamilyPython;
    r.lineComments = QStringList{ QStringLiteral("#") };
    r.tripleQuotes = true;
    r.keywords = words(
        "and as assert async await break class continue def del elif else except finally for from "
        "global if import in is lambda nonlocal not or pass raise return try while with yield match "
        "case");
    r.builtins = words(
        "abs all any bool bytes callable chr dict dir enumerate eval filter float format frozenset "
        "getattr hasattr hash hex id input int isinstance issubclass iter len list map max min next "
        "object open ord print range repr reversed round self set setattr sorted str sum super "
        "tuple type vars zip");
    r.types = words("False None True NotImplemented Ellipsis");
    return r;
}

Rules perlRules()
{
    Rules r;
    r.family = FamilyPerl;
    r.lineComments = QStringList{ QStringLiteral("#") };
    r.sigils = true;
    r.backticks = true;
    r.keywords = words(
        "and BEGIN bless cmp defined delete do each else elsif END eq exists for foreach ge goto "
        "gt if last le local lt my ne next no not or our package qq qw redo ref require return sub "
        "unless undef until use wantarray while xor");
    r.builtins = words(
        "binmode chomp chop close closedir die each eof exec exit grep hex index join keys lc "
        "length map mkdir oct open opendir ord pop print printf push readdir reverse rindex rmdir "
        "scalar shift sort splice split sprintf sqrt sscanf substr system uc unlink unshift values "
        "wait warn");
    return r;
}

Rules sqlRules()
{
    Rules r;
    r.family = FamilySql;
    r.lineComments = QStringList{ QStringLiteral("--"), QStringLiteral("#") };
    r.blockComments = true;
    r.caseInsensitive = true;
    r.keywords = words(
        "add all alter and as asc begin between by case check column commit constraint create "
        "cross database default delete desc distinct drop else end exists foreign from full grant "
        "group having if in index inner insert into is join key left like limit not null offset on "
        "or order outer primary references rename replace revoke right rollback select set show "
        "table then truncate union unique update use using values when where with");
    r.types = words(
        "bigint binary bit blob boolean char date datetime decimal double float int integer json "
        "longtext numeric real smallint text time timestamp tinyint varbinary varchar");
    return r;
}

Rules jsonRules()
{
    Rules r;
    r.family = FamilyJson;
    r.builtins = words("true false null");
    return r;
}

Rules yamlRules()
{
    Rules r;
    r.family = FamilyYaml;
    r.lineComments = QStringList{ QStringLiteral("#") };
    r.builtins = words("true false null yes no on off ~");
    return r;
}

Rules iniRules()
{
    Rules r;
    r.family = FamilyIni;
    r.lineComments = QStringList{ QStringLiteral("#"), QStringLiteral(";") };
    r.builtins = words("true false yes no on off");
    return r;
}

Rules diffRules()
{
    Rules r;
    r.family = FamilyDiff;
    return r;
}

Rules markupRules()
{
    Rules r;
    r.family = FamilyMarkup;
    return r;
}

const QHash<QString, Rules> &languageTable()
{
    static const QHash<QString, Rules> table = [] {
        QHash<QString, Rules> map;
        const Rules c = cRules();
        for (const char *name : { "c", "cc", "cpp", "c++", "cxx", "h", "hpp", "cs", "csharp",
                                  "java", "js", "javascript", "jsx", "ts", "typescript", "tsx",
                                  "go", "golang", "rust", "rs", "swift", "kotlin", "kt", "php",
                                  "scala", "dart", "groovy", "objc" })
            map.insert(QString::fromLatin1(name), c);

        const Rules shell = shellRules();
        for (const char *name : { "sh", "bash", "zsh", "ksh", "shell", "console", "shell-session",
                                  "dockerfile", "docker", "makefile", "make", "cmake" })
            map.insert(QString::fromLatin1(name), shell);

        const Rules python = pythonRules();
        for (const char *name : { "python", "py", "python3" })
            map.insert(QString::fromLatin1(name), python);

        const Rules perl = perlRules();
        for (const char *name : { "perl", "pl", "pm", "raku" })
            map.insert(QString::fromLatin1(name), perl);

        const Rules sql = sqlRules();
        for (const char *name : { "sql", "mysql", "postgres", "postgresql", "plsql" })
            map.insert(QString::fromLatin1(name), sql);

        map.insert(QStringLiteral("json"), jsonRules());
        map.insert(QStringLiteral("json5"), jsonRules());

        const Rules yaml = yamlRules();
        map.insert(QStringLiteral("yaml"), yaml);
        map.insert(QStringLiteral("yml"), yaml);

        const Rules ini = iniRules();
        for (const char *name : { "ini", "toml", "conf", "cfg", "properties", "editorconfig" })
            map.insert(QString::fromLatin1(name), ini);

        const Rules diff = diffRules();
        for (const char *name : { "diff", "patch", "udiff" })
            map.insert(QString::fromLatin1(name), diff);

        const Rules markup = markupRules();
        for (const char *name : { "xml", "html", "xhtml", "svg", "qml-xml", "plist" })
            map.insert(QString::fromLatin1(name), markup);

        return map;
    }();
    return table;
}

QString normalise(const QString &language)
{
    // Fence info strings can carry extras, e.g. "```js title=demo".
    return language.section(QLatin1Char(' '), 0, 0).trimmed().toLower();
}

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

void addSpan(QVector<Span> *spans, int start, int length, Token token)
{
    if (length > 0 && token != Token::Plain)
        spans->append(Span{ start, length, token });
}

// Consumes a quoted run starting at `i`, honouring backslash escapes.
int scanQuoted(const QString &line, int i, QChar quote)
{
    const int n = line.size();
    int j = i + 1;
    while (j < n) {
        if (line.at(j) == QLatin1Char('\\')) {
            j += 2;
            continue;
        }
        if (line.at(j) == quote)
            return j + 1;
        ++j;
    }
    return n; // unterminated: colour to end of line
}

int scanNumber(const QString &line, int i)
{
    const int n = line.size();
    int j = i;
    if (line.at(j) == QLatin1Char('0') && j + 1 < n
        && (line.at(j + 1) == QLatin1Char('x') || line.at(j + 1) == QLatin1Char('X'))) {
        j += 2;
        while (j < n && (line.at(j).isLetterOrNumber()))
            ++j;
        return j;
    }
    while (j < n
           && (line.at(j).isDigit() || line.at(j) == QLatin1Char('.')
               || line.at(j) == QLatin1Char('_'))) {
        ++j;
    }
    // Trailing type suffixes such as 10UL or 1.5f.
    while (j < n && line.at(j).isLetter() && j - i < 24)
        ++j;
    return j;
}

Token classifyWord(const Rules &rules, const QString &word)
{
    const QString key = rules.caseInsensitive ? word.toLower() : word;
    if (rules.keywords.contains(key))
        return Token::Keyword;
    if (rules.types.contains(key))
        return Token::Type;
    if (rules.builtins.contains(key))
        return Token::Builtin;
    return Token::Plain;
}

void highlightDiff(const QString &line, QVector<Span> *spans)
{
    if (line.isEmpty())
        return;
    if (line.startsWith(QLatin1String("+++")) || line.startsWith(QLatin1String("---"))
        || line.startsWith(QLatin1String("diff ")) || line.startsWith(QLatin1String("index "))) {
        addSpan(spans, 0, line.size(), Token::Preprocessor);
    } else if (line.startsWith(QLatin1String("@@"))) {
        addSpan(spans, 0, line.size(), Token::Type);
    } else if (line.startsWith(QLatin1Char('+'))) {
        addSpan(spans, 0, line.size(), Token::Added);
    } else if (line.startsWith(QLatin1Char('-'))) {
        addSpan(spans, 0, line.size(), Token::Removed);
    }
}

// key: value / key = value, plus [sections] for INI.
int highlightKey(const Rules &rules, const QString &line, QVector<Span> *spans)
{
    int i = 0;
    while (i < line.size() && (line.at(i) == QLatin1Char(' ') || line.at(i) == QLatin1Char('\t')))
        ++i;
    if (i >= line.size())
        return i;

    if (rules.family == FamilyIni && line.at(i) == QLatin1Char('[')) {
        addSpan(spans, i, line.size() - i, Token::Type);
        return line.size();
    }
    if (rules.family == FamilyYaml && line.mid(i).startsWith(QLatin1String("- ")))
        i += 2;

    const int keyStart = i;
    while (i < line.size() && line.at(i) != QLatin1Char(':') && line.at(i) != QLatin1Char('=')
           && line.at(i) != QLatin1Char('#'))
        ++i;
    if (i < line.size() && (line.at(i) == QLatin1Char(':') || line.at(i) == QLatin1Char('='))
        && i > keyStart) {
        addSpan(spans, keyStart, i - keyStart, Token::Attribute);
        return i + 1;
    }
    return keyStart;
}

void highlightMarkup(const QString &line, QVector<Span> *spans, int *state)
{
    const int n = line.size();
    int i = 0;
    if (*state == StateBlockComment) {
        const int end = line.indexOf(QLatin1String("-->"));
        if (end < 0) {
            addSpan(spans, 0, n, Token::Comment);
            return;
        }
        addSpan(spans, 0, end + 3, Token::Comment);
        *state = StateNormal;
        i = end + 3;
    }
    while (i < n) {
        if (line.mid(i).startsWith(QLatin1String("<!--"))) {
            const int end = line.indexOf(QLatin1String("-->"), i);
            if (end < 0) {
                addSpan(spans, i, n - i, Token::Comment);
                *state = StateBlockComment;
                return;
            }
            addSpan(spans, i, end + 3 - i, Token::Comment);
            i = end + 3;
            continue;
        }
        if (line.at(i) == QLatin1Char('<')) {
            int j = i + 1;
            while (j < n && (isWordChar(line.at(j)) || line.at(j) == QLatin1Char('/')
                             || line.at(j) == QLatin1Char('!') || line.at(j) == QLatin1Char('?')
                             || line.at(j) == QLatin1Char(':') || line.at(j) == QLatin1Char('-')))
                ++j;
            addSpan(spans, i, j - i, Token::Keyword);
            i = j;
            continue;
        }
        if (line.at(i) == QLatin1Char('"') || line.at(i) == QLatin1Char('\'')) {
            const int end = scanQuoted(line, i, line.at(i));
            addSpan(spans, i, end - i, Token::String);
            i = end;
            continue;
        }
        if (isWordChar(line.at(i))) {
            int j = i;
            while (j < n && (isWordChar(line.at(j)) || line.at(j) == QLatin1Char('-')))
                ++j;
            if (j < n && line.at(j) == QLatin1Char('='))
                addSpan(spans, i, j - i, Token::Attribute);
            i = j;
            continue;
        }
        ++i;
    }
}

} // namespace

bool isSupported(const QString &language)
{
    const QString name = normalise(language);
    return !name.isEmpty() && languageTable().contains(name);
}

QVector<Span> highlight(const QString &language, const QString &line, int *state)
{
    QVector<Span> spans;
    const QString name = normalise(language);
    const auto entry = languageTable().constFind(name);
    if (entry == languageTable().constEnd())
        return spans;

    const Rules &rules = *entry;
    const int n = line.size();
    int i = 0;

    if (rules.family == FamilyDiff) {
        highlightDiff(line, &spans);
        return spans;
    }
    if (rules.family == FamilyMarkup) {
        highlightMarkup(line, &spans, state);
        return spans;
    }

    // Finish anything left open by the previous line.
    if (*state == StateBlockComment) {
        const int end = line.indexOf(QLatin1String("*/"));
        if (end < 0) {
            addSpan(&spans, 0, n, Token::Comment);
            return spans;
        }
        addSpan(&spans, 0, end + 2, Token::Comment);
        *state = StateNormal;
        i = end + 2;
    } else if (*state == StateTripleSingle || *state == StateTripleDouble) {
        const QString fence = *state == StateTripleSingle ? QStringLiteral("'''")
                                                          : QStringLiteral("\"\"\"");
        const int end = line.indexOf(fence);
        if (end < 0) {
            addSpan(&spans, 0, n, Token::String);
            return spans;
        }
        addSpan(&spans, 0, end + 3, Token::String);
        *state = StateNormal;
        i = end + 3;
    }

    if (i == 0 && (rules.family == FamilyYaml || rules.family == FamilyIni))
        i = highlightKey(rules, line, &spans);

    while (i < n) {
        const QChar c = line.at(i);

        bool consumed = false;
        for (const QString &marker : rules.lineComments) {
            if (!line.mid(i).startsWith(marker))
                continue;
            // "#!" on the first line and "#" inside a shell variable are not
            // comments; everything else to the end of the line is.
            addSpan(&spans, i, n - i, Token::Comment);
            return spans;
        }
        if (consumed)
            continue;

        if (rules.blockComments && c == QLatin1Char('/') && i + 1 < n
            && line.at(i + 1) == QLatin1Char('*')) {
            const int end = line.indexOf(QLatin1String("*/"), i + 2);
            if (end < 0) {
                addSpan(&spans, i, n - i, Token::Comment);
                *state = StateBlockComment;
                return spans;
            }
            addSpan(&spans, i, end + 2 - i, Token::Comment);
            i = end + 2;
            continue;
        }

        if (rules.tripleQuotes && (line.mid(i).startsWith(QLatin1String("'''"))
                                   || line.mid(i).startsWith(QLatin1String("\"\"\"")))) {
            const QString fence = line.mid(i, 3);
            const int end = line.indexOf(fence, i + 3);
            if (end < 0) {
                addSpan(&spans, i, n - i, Token::String);
                *state = fence.startsWith(QLatin1Char('\'')) ? StateTripleSingle
                                                             : StateTripleDouble;
                return spans;
            }
            addSpan(&spans, i, end + 3 - i, Token::String);
            i = end + 3;
            continue;
        }

        if (c == QLatin1Char('"') || c == QLatin1Char('\'')
            || (rules.backticks && c == QLatin1Char('`'))) {
            const int end = scanQuoted(line, i, c);
            addSpan(&spans, i, end - i, Token::String);
            i = end;
            continue;
        }

        if (rules.preprocessor && c == QLatin1Char('#') && spans.isEmpty()) {
            int j = i + 1;
            while (j < n && line.at(j).isLetter())
                ++j;
            addSpan(&spans, i, j - i, Token::Preprocessor);
            i = j;
            continue;
        }

        if (rules.sigils
            && (c == QLatin1Char('$') || c == QLatin1Char('@') || c == QLatin1Char('%'))) {
            int j = i + 1;
            if (j < n && (line.at(j) == QLatin1Char('{') || line.at(j) == QLatin1Char('(')))
                ++j;
            const int wordStart = j;
            while (j < n && (isWordChar(line.at(j)) || line.at(j) == QLatin1Char(':')))
                ++j;
            if (j > wordStart) {
                addSpan(&spans, i, j - i, Token::Type);
                i = j;
                continue;
            }
        }

        if (c.isDigit() && (i == 0 || !isWordChar(line.at(i - 1)))) {
            const int end = scanNumber(line, i);
            addSpan(&spans, i, end - i, Token::Number);
            i = end;
            continue;
        }

        if (isWordChar(c) && !c.isDigit()) {
            int j = i;
            while (j < n && isWordChar(line.at(j)))
                ++j;
            const QString word = line.mid(i, j - i);
            addSpan(&spans, i, j - i, classifyWord(rules, word));
            i = j;
            continue;
        }

        ++i;
    }
    return spans;
}

} // namespace CodeHighlighter
