#pragma once
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <vector>

// Syntax highlighting for .pgt files.
class PgtHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit PgtHighlighter(QTextDocument *doc);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    std::vector<Rule> rules_;
    QTextCharFormat commentFormat_;
    QTextCharFormat stringFormat_;
};
