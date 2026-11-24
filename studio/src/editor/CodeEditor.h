#pragma once
#include <QPlainTextEdit>

class LineNumberArea;

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit CodeEditor(QWidget *parent = nullptr);

    bool load(const QString &path, QString *error);
    bool save(QString *error);

    QString filePath() const { return path_; }
    void setFilePath(const QString &path) { path_ = path; }
    QString displayName() const;
    void applySettings();
    void goToLine(int line, int column = 1);

    int lineNumberAreaWidth() const;
    void paintLineNumbers(QPaintEvent *event);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine();
    QString indentUnit() const;

    LineNumberArea *lineNumbers_;
    QString path_;
};
