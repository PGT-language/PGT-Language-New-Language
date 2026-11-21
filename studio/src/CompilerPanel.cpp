#include "CompilerPanel.h"

#include "Theme.h"
#include "editor/CodeEditor.h"
#include "settings/Settings.h"

#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
    constexpr int LineRole = Qt::UserRole;
    constexpr int ColRole = Qt::UserRole + 1;

    QTreeWidget *makeTree(const QStringList &headers)
    {
        auto *t = new QTreeWidget;
        t->setHeaderLabels(headers);
        t->setFrameShape(QFrame::NoFrame);
        t->setUniformRowHeights(true);
        t->setFont(Settings::editorFont());
        t->header()->setStretchLastSection(false);
        t->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int i = 1; i < headers.size(); ++i)
            t->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
        return t;
    }

    void setPos(QTreeWidgetItem *item, int line, int col)
    {
        item->setData(0, LineRole, line);
        item->setData(0, ColRole, col);
    }

    // Token kinds grouped by color, the same palette as the editor.
    QColor tokenColor(const QString &kind)
    {
        if (kind.startsWith("Kw"))
            return kind == "KwTrue" || kind == "KwFalse" || kind == "KwNil" ? Theme::synConstant
                                                                             : Theme::synKeyword;
        if (kind == "Ident")
            return Theme::text;
        if (kind == "String" || kind == "Char")
            return Theme::synString;
        if (kind == "Int" || kind == "Float")
            return Theme::synConstant;
        if (kind == "Eof")
            return Theme::textFaint;
        return Theme::textDim;
    }
}

CompilerPanel::CompilerPanel(QWidget *parent)
    : QWidget(parent), proc_(new QProcess(this)), debounce_(new QTimer(this))
{
    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->setContentsMargins(8, 4, 8, 4);
    status_->setCursor(Qt::PointingHandCursor);

    tokens_ = makeTree({"Текст", "Вид", "Позиция"});
    tokens_->setRootIsDecorated(false);
    ast_ = makeTree({"Узел", "Вид", "Позиция"});

    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);
    tabs->addTab(ast_, "Дерево");
    tabs->addTab(tokens_, "Токены");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(status_);
    layout->addWidget(tabs);

    debounce_->setSingleShot(true);
    debounce_->setInterval(400);
    connect(debounce_, &QTimer::timeout, this, &CompilerPanel::refresh);

    connect(tokens_, &QTreeWidget::itemClicked, this, &CompilerPanel::jumpTo);
    connect(ast_, &QTreeWidget::itemClicked, this, &CompilerPanel::jumpTo);
    connect(proc_, &QProcess::finished, this, [this] {
        QByteArray out = proc_->readAllStandardOutput();
        if (out.trimmed().isEmpty())
            showMessage("Компилятор не вернул данные: " + QString::fromUtf8(proc_->readAllStandardError()).trimmed(),
                        true);
        else
            showResult(out);
    });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            showMessage("Не удалось запустить компилятор: " + proc_->errorString(), true);
    });

    showMessage("Откройте .pgt файл", false);
}

void CompilerPanel::setEditor(CodeEditor *editor)
{
    if (editor_ == editor)
        return;
    disconnect(textConn_);
    disconnect(cursorConn_);
    editor_ = editor;
    if (editor_)
    {
        textConn_ = connect(editor_, &QPlainTextEdit::textChanged, this, &CompilerPanel::scheduleRefresh);
        cursorConn_ = connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, &CompilerPanel::syncWithCursor);
    }
    refresh();
}

void CompilerPanel::scheduleRefresh()
{
    debounce_->start();
}

void CompilerPanel::refresh()
{
    if (!editor_ || !editor_->filePath().endsWith(".pgt"))
    {
        tokens_->clear();
        ast_->clear();
        showMessage("Откройте .pgt файл", false);
        return;
    }
    QString compiler = Settings::resolvedCompiler();
    if (compiler.isEmpty())
    {
        showMessage("Компилятор не найден — укажите путь в настройках", true);
        return;
    }
    if (proc_->state() != QProcess::NotRunning)
    {
        proc_->kill();
        proc_->waitForFinished(1000);
    }
    proc_->start(compiler, {"build", "-", "--debug=json"});
    proc_->write(editor_->toPlainText().toUtf8());
    proc_->closeWriteChannel();
}

void CompilerPanel::showMessage(const QString &text, bool isError)
{
    QPalette p = status_->palette();
    p.setColor(QPalette::WindowText, isError ? Theme::error : Theme::textDim);
    status_->setPalette(p);
    status_->setText(text);
    status_->setProperty("line", 0);
}

void CompilerPanel::showResult(const QByteArray &json)
{
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (perr.error != QJsonParseError::NoError)
    {
        showMessage("Не удалось разобрать ответ компилятора: " + perr.errorString(), true);
        return;
    }
    QJsonObject file = doc.object()["files"].toArray().first().toObject();

    // Keep the expanded state readable: rebuild and expand everything.
    tokens_->setUpdatesEnabled(false);
    ast_->setUpdatesEnabled(false);
    tokens_->clear();
    ast_->clear();

    const QJsonArray toks = file["tokens"].toArray();
    for (const QJsonValue &v : toks)
    {
        QJsonObject t = v.toObject();
        QString kind = t["kind"].toString();
        QString text = t["text"].toString();
        if (kind == "String")
            text = "\"" + text + "\"";
        else if (kind == "Char")
            text = "'" + text + "'";
        auto *item = new QTreeWidgetItem(tokens_, {text.replace('\n', "\\n").replace('\t', "\\t"), kind,
                                                   QString("%1:%2").arg(t["line"].toInt()).arg(t["col"].toInt())});
        item->setForeground(0, tokenColor(kind));
        item->setForeground(1, Theme::textFaint);
        item->setForeground(2, Theme::textFaint);
        setPos(item, t["line"].toInt(), t["col"].toInt());
    }

    if (file.contains("ast"))
    {
        addAstNode(nullptr, file["ast"].toObject());
        ast_->expandAll();
    }

    tokens_->setUpdatesEnabled(true);
    ast_->setUpdatesEnabled(true);

    if (file.contains("error"))
    {
        QJsonObject e = file["error"].toObject();
        int line = e["line"].toInt(), col = e["col"].toInt();
        showMessage(QString("✖ %1:%2: %3").arg(line).arg(col).arg(e["message"].toString()), true);
        status_->setProperty("line", line);
        status_->setProperty("col", col);
    }
    else
    {
        showMessage(QString("✔ %1 токенов, синтаксис в порядке").arg(toks.size()), false);
        QPalette p = status_->palette();
        p.setColor(QPalette::WindowText, Theme::success);
        status_->setPalette(p);
    }
    syncWithCursor();
}

void CompilerPanel::addAstNode(QTreeWidgetItem *parent, const QJsonObject &node)
{
    QString pos = node["line"].toInt() ? QString("%1:%2").arg(node["line"].toInt()).arg(node["col"].toInt()) : "";
    QStringList cols{node["label"].toString(), node["kind"].toString(), pos};
    auto *item = parent ? new QTreeWidgetItem(parent, cols) : new QTreeWidgetItem(ast_, cols);
    QString kind = node["kind"].toString();
    QColor color = Theme::text;
    if (kind == "Function" || kind == "Method" || kind == "Extern")
        color = Theme::synFunction;
    else if (kind == "Struct" || kind == "Interface" || kind == "Field" || kind == "Type")
        color = Theme::synType;
    else if (kind.endsWith("Lit"))
        color = kind == "StringLit" || kind == "CharLit" ? Theme::synString : Theme::synConstant;
    else if (kind == "Part" || kind == "Block")
        color = Theme::textDim;
    else if (kind == "Import" || kind == "Program")
        color = Theme::synDirective;
    else if (kind != "Name" && kind != "Call" && kind != "Selector" && kind != "Index" && kind != "FieldInit")
        color = Theme::synKeyword;
    item->setForeground(0, color);
    item->setForeground(1, Theme::textFaint);
    item->setForeground(2, Theme::textFaint);
    setPos(item, node["line"].toInt(), node["col"].toInt());
    for (const QJsonValue &kid : node["children"].toArray())
        addAstNode(item, kid.toObject());
}

void CompilerPanel::jumpTo(QTreeWidgetItem *item)
{
    int line = item->data(0, LineRole).toInt();
    if (editor_ && line > 0)
        editor_->goToLine(line, item->data(0, ColRole).toInt());
}

void CompilerPanel::syncWithCursor()
{
    if (!editor_ || tokens_->topLevelItemCount() == 0)
        return;
    QTextCursor c = editor_->textCursor();
    int line = c.blockNumber() + 1, col = c.positionInBlock() + 1;
    // The last token that starts at or before the cursor on the same line.
    QTreeWidgetItem *best = nullptr;
    for (int i = 0; i < tokens_->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *it = tokens_->topLevelItem(i);
        int l = it->data(0, LineRole).toInt();
        if (l > line)
            break;
        if (l == line && it->data(0, ColRole).toInt() <= col)
            best = it;
    }
    if (best)
    {
        QSignalBlocker block(tokens_);
        tokens_->setCurrentItem(best);
        tokens_->scrollToItem(best);
    }
}
