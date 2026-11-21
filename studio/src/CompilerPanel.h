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

// Shows what the compiler sees in the current file: the tokens and the syntax tree.
// Runs `pgt build - --debug=json` on the editor text (unsaved changes included) after each edit.
class CompilerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CompilerPanel(QWidget *parent = nullptr);

    void setEditor(CodeEditor *editor);
    // Builds the project token graph and switches to its tab.
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
