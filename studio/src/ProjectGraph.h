#pragma once
#include <QJsonObject>
#include <QWidget>

class QLabel;
class QProcess;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// "Граф токенов проекта": every file the program loads (the standard library included),
// who imports whom, how many tokens each file weighs, its syntax tree and token stream.
// Filled from `pgt build <dir> --debug=project`.
class ProjectGraphView : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectGraphView(QWidget *parent = nullptr);

    void analyze(const QString &projectDir);
    void clear();

signals:
    void locationActivated(const QString &file, int line, int column);

private:
    void showResult(const QByteArray &json);
    void addDependencies(QTreeWidgetItem *parent, const QString &file, QStringList &path, QSet<QString> &shown);
    void addFile(QTreeWidgetItem *parent, const QJsonObject &file, bool expand);
    void addAst(QTreeWidgetItem *parent, const QJsonObject &node, const QString &absPath);
    void fillTokens(QTreeWidgetItem *streamItem);

    QLabel *title_;
    QLabel *subtitle_;
    QPushButton *clearBtn_;
    QTreeWidget *tree_;
    QProcess *proc_;
    QHash<QString, QJsonObject> files_; // by display path
};
