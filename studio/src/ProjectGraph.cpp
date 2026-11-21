#include "ProjectGraph.h"

#include "Theme.h"
#include "settings/Settings.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
    constexpr int PathRole = Qt::UserRole;     // absolute file path to open on click
    constexpr int LineRole = Qt::UserRole + 1;
    constexpr int ColRole = Qt::UserRole + 2;
    constexpr int TokensRole = Qt::UserRole + 3; // display path of a not yet filled token stream

    // Draws "main part\tsecondary part": the main part in the item color, the rest dimmed.
    class TwoToneDelegate : public QStyledItemDelegate
    {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;

        void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
        {
            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);
            QString text = opt.text;
            opt.text.clear();
            QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
            style->drawControl(QStyle::CE_ItemViewItem, &opt, p, opt.widget);

            QRect r = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget).adjusted(2, 0, 0, 0);
            QString main = text.section('\t', 0, 0);
            QString extra = text.section('\t', 1);
            QVariant fg = index.data(Qt::ForegroundRole);
            QColor mainColor = fg.isValid() ? fg.value<QBrush>().color() : Theme::text;

            p->save();
            p->setFont(opt.font);
            p->setPen(mainColor);
            p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, main);
            if (!extra.isEmpty())
            {
                int x = opt.fontMetrics.horizontalAdvance(main) + 10;
                p->setPen(Theme::textDim);
                p->drawText(r.adjusted(x, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, extra);
            }
            p->restore();
        }

        QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
        {
            QSize s = QStyledItemDelegate::sizeHint(option, index);
            return {s.width() + 16, s.height() + 4};
        }
    };

    QString tokensText(int n)
    {
        int m10 = n % 10, m100 = n % 100;
        const char *word = (m10 == 1 && m100 != 11) ? "токен"
                           : (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) ? "токена"
                                                                                  : "токенов";
        return QString("%1 %2").arg(n).arg(word);
    }

    QTreeWidgetItem *item(QTreeWidgetItem *parent, const QString &text, const QColor &color = Theme::text,
                          bool bold = false)
    {
        auto *it = new QTreeWidgetItem(parent, {text});
        it->setForeground(0, color);
        if (bold)
        {
            QFont f = it->font(0);
            f.setBold(true);
            it->setFont(0, f);
        }
        return it;
    }

    QTreeWidgetItem *section(QTreeWidgetItem *parent, const QString &text)
    {
        return item(parent, text, Theme::synKeyword, true);
    }

    void setLocation(QTreeWidgetItem *it, const QString &abs, int line, int col)
    {
        it->setData(0, PathRole, abs);
        it->setData(0, LineRole, line);
        it->setData(0, ColRole, col);
    }
}

ProjectGraphView::ProjectGraphView(QWidget *parent) : QWidget(parent), proc_(new QProcess(this))
{
    title_ = new QLabel("Граф токенов проекта");
    QFont tf = title_->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.35);
    tf.setBold(true);
    title_->setFont(tf);
    subtitle_ = new QLabel;
    QPalette dim = subtitle_->palette();
    dim.setColor(QPalette::WindowText, Theme::textDim);
    subtitle_->setPalette(dim);
    subtitle_->setWordWrap(true);

    clearBtn_ = new QPushButton("Очистить");
    connect(clearBtn_, &QPushButton::clicked, this, &ProjectGraphView::clear);

    auto *titles = new QVBoxLayout;
    titles->setSpacing(2);
    titles->addWidget(title_);
    titles->addWidget(subtitle_);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(12, 10, 12, 8);
    header->addLayout(titles, 1);
    header->addWidget(clearBtn_, 0, Qt::AlignTop);

    tree_ = new QTreeWidget;
    tree_->setHeaderHidden(true);
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setItemDelegate(new TwoToneDelegate(tree_));
    tree_->setIndentation(18);
    tree_->setAnimated(true);
    connect(tree_, &QTreeWidget::itemExpanded, this, &ProjectGraphView::fillTokens);
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it) {
        QString path = it->data(0, PathRole).toString();
        if (!path.isEmpty())
            emit locationActivated(path, qMax(1, it->data(0, LineRole).toInt()), qMax(1, it->data(0, ColRole).toInt()));
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(tree_);

    connect(proc_, &QProcess::finished, this, [this] {
        QByteArray out = proc_->readAllStandardOutput();
        if (out.trimmed().isEmpty())
        {
            subtitle_->setText("Компилятор не вернул данные: " +
                               QString::fromUtf8(proc_->readAllStandardError()).trimmed());
            return;
        }
        showResult(out);
    });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            subtitle_->setText("Не удалось запустить компилятор: " + proc_->errorString());
    });
    clear();
}

void ProjectGraphView::clear()
{
    tree_->clear();
    files_.clear();
    subtitle_->setText("Нажмите «Отладка» (F6), чтобы построить граф проекта");
}

void ProjectGraphView::analyze(const QString &projectDir)
{
    QString compiler = Settings::resolvedCompiler();
    if (compiler.isEmpty() || projectDir.isEmpty())
    {
        subtitle_->setText(compiler.isEmpty() ? "Компилятор не найден — укажите путь в настройках"
                                              : "Откройте проект");
        return;
    }
    if (proc_->state() != QProcess::NotRunning)
    {
        proc_->kill();
        proc_->waitForFinished(1000);
    }
    subtitle_->setText("Анализ проекта…");
    proc_->setWorkingDirectory(projectDir);
    proc_->start(compiler, {"build", ".", "--debug=project"});
}

void ProjectGraphView::showResult(const QByteArray &json)
{
    QJsonParseError err;
    QJsonObject root = QJsonDocument::fromJson(json, &err).object();
    if (err.error != QJsonParseError::NoError)
    {
        subtitle_->setText("Не удалось разобрать ответ компилятора: " + err.errorString());
        return;
    }
    tree_->clear();
    files_.clear();
    const QJsonArray files = root["files"].toArray();
    for (const QJsonValue &v : files)
        files_.insert(v.toObject()["path"].toString(), v.toObject());

    QString entry = root["entry"].toString();
    int total = root["totalTokens"].toInt();
    int edges = root["importEdges"].toInt();
    const QJsonArray unresolved = root["unresolved"].toArray();
    subtitle_->setText(QString("%1 файлов · %2 · %3 импортов").arg(files.size()).arg(tokensText(total)).arg(edges));

    tree_->setUpdatesEnabled(false);
    auto *top = new QTreeWidgetItem(tree_, {entry + "\tграф зависимостей"});
    top->setForeground(0, Theme::accent);
    QFont bold = top->font(0);
    bold.setBold(true);
    top->setFont(0, bold);

    // ---- overview
    QTreeWidgetItem *overview = section(top, "Обзор проекта");
    item(overview, "Точка входа\t" + entry);
    item(overview, "Загружено файлов\t" + QString::number(files.size()));
    item(overview, "Всего токенов\t" + QString::number(total));
    item(overview, "Связей импорта\t" + QString::number(edges));
    QTreeWidgetItem *unres = item(overview, "Нерешённых импортов\t" + QString::number(unresolved.size()),
                                  unresolved.isEmpty() ? Theme::text : Theme::error);
    for (const QJsonValue &u : unresolved)
        item(unres, "✖ " + u.toString(), Theme::error);

    // ---- dependency tree
    QTreeWidgetItem *deps = section(top, "Дерево зависимостей");
    QStringList path;
    QSet<QString> shown;
    addDependencies(deps, entry, path, shown);

    // ---- files
    QTreeWidgetItem *filesItem = section(top, QString("Файлы (%1)").arg(files.size()));
    for (const QJsonValue &v : files)
    {
        QJsonObject f = v.toObject();
        addFile(filesItem, f, f["path"].toString() == entry);
    }

    top->setExpanded(true);
    overview->setExpanded(true);
    unres->setExpanded(true);
    deps->setExpanded(true);
    filesItem->setExpanded(true);
    tree_->expandItem(deps);
    for (int i = 0; i < deps->childCount(); ++i)
        tree_->expandRecursively(tree_->indexFromItem(deps->child(i)), 2);
    tree_->setUpdatesEnabled(true);
}

void ProjectGraphView::addDependencies(QTreeWidgetItem *parent, const QString &file, QStringList &path,
                                       QSet<QString> &shown)
{
    QJsonObject f = files_.value(file);
    QTreeWidgetItem *node = item(parent, file + "\t" + tokensText(f["tokenCount"].toInt()),
                                 file.startsWith("std/") ? Theme::synType : Theme::synExported);
    setLocation(node, f["absPath"].toString(), 1, 1);

    if (path.contains(file))
    {
        node->setText(0, file + "\t↺ цикл импорта");
        node->setForeground(0, Theme::warning);
        return;
    }
    if (shown.contains(file))
    {
        // Every file is expanded once; later mentions only point back to it.
        node->setText(0, node->text(0) + " · показан выше");
        return;
    }
    shown.insert(file);
    path.append(file);
    for (const QJsonValue &v : f["imports"].toArray())
    {
        QJsonObject imp = v.toObject();
        if (!imp["resolved"].toBool())
        {
            item(node, "✖ \"" + imp["path"].toString() + "\"\tне найден", Theme::error);
            continue;
        }
        for (const QJsonValue &dep : imp["files"].toArray())
        {
            addDependencies(node, dep.toString(), path, shown);
            node->child(node->childCount() - 1)->setToolTip(0, "#use \"" + imp["path"].toString() + "\"");
        }
    }
    path.removeLast();
}

void ProjectGraphView::addFile(QTreeWidgetItem *parent, const QJsonObject &f, bool expand)
{
    QString display = f["path"].toString();
    QString abs = f["absPath"].toString();
    QTreeWidgetItem *fileItem = item(parent, display + "\t" + tokensText(f["tokenCount"].toInt()),
                                     display.startsWith("std/") ? Theme::synType : Theme::synExported);
    setLocation(fileItem, abs, 1, 1);

    if (f.contains("error"))
    {
        QJsonObject e = f["error"].toObject();
        QTreeWidgetItem *err = item(fileItem,
                                    QString("✖ %1:%2\t%3").arg(e["line"].toInt()).arg(e["col"].toInt()).arg(e["message"].toString()),
                                    Theme::error);
        setLocation(err, abs, e["line"].toInt(), e["col"].toInt());
    }

    QTreeWidgetItem *ov = section(fileItem, "Обзор");
    item(ov, "Пакет\t" + f["package"].toString());
    item(ov, "Строк\t" + QString::number(f["lines"].toInt()));
    item(ov, "Функций\t" + QString::number(f["functions"].toInt()));
    item(ov, "Импортов\t" + QString::number(f["imports"].toArray().size()));
    item(ov, "Токенов\t" + QString::number(f["tokenCount"].toInt()));
    QJsonObject cats = f["categories"].toObject();
    for (auto it = cats.begin(); it != cats.end(); ++it)
        item(ov, "   " + it.key() + "\t" + QString::number(it.value().toInt()), Theme::textDim);

    if (f.contains("ast"))
    {
        QTreeWidgetItem *astItem = section(fileItem, "Дерево разбора (AST)");
        addAst(astItem, f["ast"].toObject(), abs);
        if (expand)
        {
            astItem->setExpanded(true);
            for (int i = 0; i < astItem->childCount(); ++i)
                astItem->child(i)->setExpanded(true);
        }
    }

    QTreeWidgetItem *stream = section(fileItem, "Поток токенов");
    stream->setData(0, TokensRole, display);
    item(stream, "…", Theme::textFaint); // placeholder: filled when expanded
    if (expand)
    {
        fileItem->setExpanded(true);
        ov->setExpanded(true);
    }
}

void ProjectGraphView::addAst(QTreeWidgetItem *parent, const QJsonObject &node, const QString &absPath)
{
    QString kind = node["kind"].toString();
    QColor color = Theme::text;
    if (kind == "Function" || kind == "Method" || kind == "Extern")
        color = Theme::synFunction;
    else if (kind == "Struct" || kind == "Interface" || kind == "Field")
        color = Theme::synType;
    else if (kind == "Import")
        color = Theme::synDirective;
    else if (kind == "Section" || kind == "Part" || kind == "Block")
        color = Theme::textDim;
    else if (kind.endsWith("Lit"))
        color = kind == "StringLit" || kind == "CharLit" ? Theme::synString : Theme::synConstant;
    else if (kind == "Program")
        color = Theme::synKeyword;

    // The top Program node itself is not repeated; its sections go straight under "AST".
    QTreeWidgetItem *it = parent;
    if (kind != "Program" || parent->parent() == nullptr)
    {
        it = item(parent, node["label"].toString() + (kind == "Section" ? "" : "\t" + kind), color);
        if (node["line"].toInt() > 0)
            setLocation(it, absPath, node["line"].toInt(), node["col"].toInt());
    }
    else
    {
        item(parent, node["label"].toString(), Theme::synKeyword);
    }
    for (const QJsonValue &kid : node["children"].toArray())
        addAst(it, kid.toObject(), absPath);
}

void ProjectGraphView::fillTokens(QTreeWidgetItem *stream)
{
    QString display = stream->data(0, TokensRole).toString();
    if (display.isEmpty())
        return;
    stream->setData(0, TokensRole, QString());
    delete stream->takeChild(0);

    QJsonObject f = files_.value(display);
    QString abs = f["absPath"].toString();
    tree_->setUpdatesEnabled(false);
    for (const QJsonValue &v : f["tokens"].toArray())
    {
        QJsonObject t = v.toObject();
        QString kind = t["kind"].toString();
        QString text = t["text"].toString().replace('\n', "\\n").replace('\t', "\\t");
        if (kind == "String")
            text = "\"" + text + "\"";
        else if (kind == "Char")
            text = "'" + text + "'";
        QColor color = kind.startsWith("Kw") ? Theme::synKeyword
                       : kind == "Ident"     ? Theme::text
                       : kind == "String" || kind == "Char" ? Theme::synString
                       : kind == "Int" || kind == "Float"   ? Theme::synConstant
                                                            : Theme::textDim;
        QString pos = QString("%1:%2").arg(t["line"].toInt()).arg(t["col"].toInt());
        QTreeWidgetItem *it = item(stream, (text.isEmpty() ? "EOF" : text) + "\t" + kind + "  " + pos, color);
        setLocation(it, abs, t["line"].toInt(), t["col"].toInt());
    }
    tree_->setUpdatesEnabled(true);
}
