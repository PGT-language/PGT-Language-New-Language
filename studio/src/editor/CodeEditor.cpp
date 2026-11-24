#include "editor/CodeEditor.h"

#include "Theme.h"
#include "editor/PgtHighlighter.h"
#include "settings/Settings.h"

#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSaveFile>
#include <QTextBlock>

class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor *editor) : QWidget(editor), editor_(editor) {}
    QSize sizeHint() const override { return {editor_->lineNumberAreaWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent *event) override { editor_->paintLineNumbers(event); }

private:
    CodeEditor *editor_;
};

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent), lineNumbers_(new LineNumberArea(this))
{
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameShape(QFrame::NoFrame);
    QPalette p = palette();
    p.setColor(QPalette::Base, Theme::editor);
    p.setColor(QPalette::Text, Theme::text);
    p.setColor(QPalette::Highlight, Theme::selection);
    p.setColor(QPalette::HighlightedText, Theme::text);
    setPalette(p);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    applySettings();
    highlightCurrentLine();
}

bool CodeEditor::load(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        *error = f.errorString();
        return false;
    }
    path_ = QFileInfo(path).canonicalFilePath();
    if (path_.endsWith(".pgt") || path_.endsWith(".mod"))
        new PgtHighlighter(document());
    setPlainText(QString::fromUtf8(f.readAll()));
    document()->setModified(false);
    return true;
}

bool CodeEditor::save(QString *error)
{
    QSaveFile f(path_);
    if (!f.open(QIODevice::WriteOnly) || f.write(toPlainText().toUtf8()) < 0 || !f.commit())
    {
        *error = f.errorString();
        return false;
    }
    document()->setModified(false);
    return true;
}

QString CodeEditor::displayName() const
{
    return QFileInfo(path_).fileName();
}

void CodeEditor::applySettings()
{
    QFont font = Settings::editorFont();
    setFont(font);
    setTabStopDistance(QFontMetricsF(font).horizontalAdvance(' ') * Settings::tabWidth());
    updateLineNumberAreaWidth();
}

void CodeEditor::goToLine(int line, int column)
{
    QTextBlock block = document()->findBlockByNumber(qMax(0, line - 1));
    if (!block.isValid())
        return;
    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                        qMin(qMax(0, column - 1), block.length() - 1));
    setTextCursor(cursor);
    centerCursor();
    setFocus();
}

QString CodeEditor::indentUnit() const
{
    return QString(Settings::tabWidth(), ' ');
}

void CodeEditor::keyPressEvent(QKeyEvent *event)
{
    QTextCursor cursor = textCursor();

    if (event->key() == Qt::Key_Tab && !cursor.hasSelection())
    {
        cursor.insertText(indentUnit());
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        QString line = cursor.block().text();
        QString indent;
        for (QChar c : line)
        {
            if (c != ' ' && c != '\t')
                break;
            indent += c;
        }
        QString before = line.left(cursor.positionInBlock()).trimmed();
        if (before.endsWith('{') || before.endsWith('('))
            indent += indentUnit();
        cursor.insertText("\n" + indent);
        setTextCursor(cursor);
        return;
    }
    if (event->text() == "}")
    {
        QString line = cursor.block().text();
        if (line.trimmed().isEmpty() && line.size() >= indentUnit().size() &&
            cursor.positionInBlock() == line.size())
        {
            cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, indentUnit().size());
            cursor.insertText("}");
            return;
        }
    }
    QPlainTextEdit::keyPressEvent(event);
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = QString::number(qMax(1, blockCount())).size();
    return 24 + fontMetrics().horizontalAdvance('9') * qMax(3, digits);
}

void CodeEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        lineNumbers_->scroll(0, dy);
    else
        lineNumbers_->update(0, rect.y(), lineNumbers_->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    QRect cr = contentsRect();
    lineNumbers_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::highlightCurrentLine()
{
    QTextEdit::ExtraSelection sel;
    sel.format.setBackground(Theme::currentLine);
    sel.format.setProperty(QTextFormat::FullWidthSelection, true);
    sel.cursor = textCursor();
    sel.cursor.clearSelection();
    setExtraSelections({sel});
}

void CodeEditor::paintLineNumbers(QPaintEvent *event)
{
    QPainter painter(lineNumbers_);
    painter.fillRect(event->rect(), Theme::editor);

    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    int current = textCursor().blockNumber();

    while (block.isValid() && top <= event->rect().bottom())
    {
        if (block.isVisible() && bottom >= event->rect().top())
        {
            if (number == current)
                painter.fillRect(0, top, lineNumbers_->width(), bottom - top, Theme::currentLine);
            painter.setPen(number == current ? Theme::text : Theme::textFaint);
            painter.drawText(0, top, lineNumbers_->width() - 14, fontMetrics().height(), Qt::AlignRight,
                             QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}
