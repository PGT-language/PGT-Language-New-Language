#include "editor/PgtHighlighter.h"

#include "Theme.h"

namespace
{

    QTextCharFormat makeFormat(const QColor &color, bool bold = false, bool italic = false)
    {
        QTextCharFormat f;
        f.setForeground(color);
        if (bold)
            f.setFontWeight(QFont::Bold);
        f.setFontItalic(italic);
        return f;
    }

    QString words(const QStringList &list)
    {
        return "\\b(?:" + list.join('|') + ")\\b";
    }

}

PgtHighlighter::PgtHighlighter(QTextDocument *doc) : QSyntaxHighlighter(doc)
{
    const QStringList keywords = {"package", "fn",     "return", "if",      "else",   "while",
                                  "for",     "in",     "break",  "continue", "struct", "interface",
                                  "const",   "switch", "case",   "default", "extern", "map"};
    const QStringList types = {"int", "int64", "uint64", "float", "char", "byte", "string", "bool", "void", "any"};
    const QStringList constants = {"true", "false", "nil", "iota"};
    const QStringList builtins = {
        "len",         "append",       "make",       "new",         "free",         "str",
        "chars",       "parseInt",     "parseInt64", "parseFloat",  "substr",       "contains",
        "indexOf",     "startsWith",   "endsWith",   "upper",       "lower",        "abs",
        "min",         "max",          "sqrt",       "pow",         "floor",        "ceil",
        "round",       "trunc",        "sin",        "cos",         "tan",          "log",
        "exp",         "readFile",     "writeFile",  "appendFile",  "fileExists",   "rand",
        "srand",       "exit",         "panic",      "has",         "delete"};

    rules_.push_back({QRegularExpression("\\b[A-Z]\\w*\\b"), makeFormat(Theme::synExported)});
    rules_.push_back({QRegularExpression("\\b\\w+(?=\\s*\\()"), makeFormat(Theme::synFunction)});
    rules_.push_back({QRegularExpression(words(builtins) + "(?=\\s*\\()"), makeFormat(Theme::synBuiltin)});
    rules_.push_back({QRegularExpression(words(types)), makeFormat(Theme::synType)});
    rules_.push_back({QRegularExpression(words(keywords)), makeFormat(Theme::synKeyword, true)});
    rules_.push_back({QRegularExpression("\\basm(?=\\s*\\{)"), makeFormat(Theme::synKeyword, true)});
    rules_.push_back({QRegularExpression("\\bvar[*&]"), makeFormat(Theme::synKeyword, true)});
    rules_.push_back({QRegularExpression("\\bmalloc(?=\\s*\\()"), makeFormat(Theme::synKeyword, true)});
    rules_.push_back({QRegularExpression("#use\\b"), makeFormat(Theme::synDirective, true)});
    rules_.push_back({QRegularExpression(words(constants)), makeFormat(Theme::synConstant)});
    rules_.push_back({QRegularExpression("\\b(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\\d+(?:\\.\\d+)?)\\b"),
                      makeFormat(Theme::synConstant)});

    stringFormat_ = makeFormat(Theme::synString);
    commentFormat_ = makeFormat(Theme::synComment, false, true);
}

void PgtHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : rules_)
    {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext())
        {
            auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), rule.format);
        }
    }

    int i = 0;
    const int n = text.size();
    if (previousBlockState() == 1)
    {
        int end = text.indexOf("*/");
        if (end < 0)
        {
            setFormat(0, n, commentFormat_);
            setCurrentBlockState(1);
            return;
        }
        setFormat(0, end + 2, commentFormat_);
        i = end + 2;
    }
    setCurrentBlockState(0);

    while (i < n)
    {
        QChar c = text[i];
        if (c == '"' || c == '\'')
        {
            int start = i++;
            while (i < n && text[i] != c)
                i += (text[i] == '\\') ? 2 : 1;
            i = qMin(i + 1, n);
            setFormat(start, i - start, stringFormat_);
        }
        else if (c == '/' && i + 1 < n && text[i + 1] == '/')
        {
            setFormat(i, n - i, commentFormat_);
            return;
        }
        else if (c == '/' && i + 1 < n && text[i + 1] == '*')
        {
            int end = text.indexOf("*/", i + 2);
            if (end < 0)
            {
                setFormat(i, n - i, commentFormat_);
                setCurrentBlockState(1);
                return;
            }
            setFormat(i, end + 2 - i, commentFormat_);
            i = end + 2;
        }
        else
        {
            ++i;
        }
    }
}
