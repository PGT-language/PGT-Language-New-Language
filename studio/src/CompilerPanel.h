#pragma once
#include <QPointer>
#include <QWidget>

class CodeEditor;
class ProjectGraphView;
class QTabWidget;
class QLabel;
class QProcess;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QJsonObject;

class CompilerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CompilerPanel(QWidget *parent = nullptr);

    void setEditor(CodeEditor *editor);
    void analyzeProject(const QString &projectDir);

signals:
    void locationActivated(const QString &file, int line, int column);

private:
    void scheduleRefresh();
    void refresh();
    void showResult(const QByteArray &json);
    void showMessage(const QString &text, bool isError);
    void addAstNode(QTreeWidgetItem *parent, const QJsonObject &node);
    void jumpTo(QTreeWidgetItem *item);
    void syncWithCursor();

    QPointer<CodeEditor> editor_;
    QLabel *status_;
    QTreeWidget *tokens_;
    QTreeWidget *ast_;
    QTabWidget *tabs_;
    ProjectGraphView *graph_;
    QProcess *proc_;
    QTimer *debounce_;
    QMetaObject::Connection textConn_, cursorConn_;
};
