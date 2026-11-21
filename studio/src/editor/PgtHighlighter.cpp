#include "editor/PgtHighlighter.h"

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
    const QStringList keywords = {"package", "fn",     "return",  "if",        "else",  "while",
                                  "for",     "in",     "break",   "continue",  "struct", "interface",
                                  "const",   "switch", "case",    "default",   "extern"};
    const QStringList types = {"int", "int64", "float", "char", "string", "bool", "void"};
    const QStringList constants = {"true", "false", "nil"};
    const QStringList builtins = {
        "print",     "println",  "input",      "inputln",   "len",        "append",     "make",
        "new",       "free",     "str",        "chars",     "parseInt",   "parseInt64", "parseFloat",
        "substr",    "contains", "indexOf",    "startsWith", "endsWith",  "upper",      "lower",
        "abs",       "min",      "max",        "sqrt",      "pow",        "floor",      "ceil",
        "round",     "trunc",    "sin",        "cos",       "tan",        "log",        "exp",
        "readFile",  "writeFile", "appendFile", "fileExists", "rand",     "srand",      "exit",
        "panic"};

    // Later rules win, so the most specific ones come last.
    rules_.push_back({QRegularExpression("\\b[A-Z]\\w*\\b"), makeFormat(QColor("#2b7a78"))});
    rules_.push_back({QRegularExpression("\\b\\w+(?=\\s*\\()"), makeFormat(QColor("#3867d6"))});
    rules_.push_back({QRegularExpression(words(builtins) + "(?=\\s*\\()"), makeFormat(QColor("#8854d0"))});
    rules_.push_back({QRegularExpression(words(types)), makeFormat(QColor("#0f8b8d"), true)});
    rules_.push_back({QRegularExpression(words(keywords)), makeFormat(QColor("#c0392b"), true)});
    rules_.push_back({QRegularExpression("\\bvar\\*"), makeFormat(QColor("#c0392b"), true)});
    rules_.push_back({QRegularExpression("#use\\b"), makeFormat(QColor("#d35400"), true)});
    rules_.push_back({QRegularExpression(words(constants)), makeFormat(QColor("#d35400"))});
    rules_.push_back({QRegularExpression("\\b(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\\d+(?:\\.\\d+)?)\\b"),
                      makeFormat(QColor("#d35400"))});

    stringFormat_ = makeFormat(QColor("#27ae60"));
    commentFormat_ = makeFormat(QColor("#7f8c8d"), false, true);
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

    // Strings, chars and comments are scanned by hand so that `//` inside "a//b" is not a comment.
    // Block state 1 means "inside /* */ that started on an earlier line".
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
