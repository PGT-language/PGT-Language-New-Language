#pragma once
#include <QJsonObject>
#include <QWidget>

class QLabel;
class QProcess;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

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
    QHash<QString, QJsonObject> files_;
};
