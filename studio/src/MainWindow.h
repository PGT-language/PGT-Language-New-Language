#pragma once
#include <QMainWindow>

class CodeEditor;
class CompilerPanel;
class OutputPanel;
class QAction;
class QFileSystemModel;
class QLabel;
class QListWidget;
class QMenu;
class QStackedWidget;
class QTabWidget;
class QTreeView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();

    void openProject(const QString &dir);
    CodeEditor *openFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void createWelcome();
    void updateRecentMenu();
    void updateTitle();
    void updateActions();
    void updateCursorInfo();

    void newProject();
    void openProjectDialog();
    void openFileDialog();
    void newFile();
    void createEntry(const QString &dir, bool folder);
    void showTreeMenu(const QPoint &pos);
    void renameEntry(const QModelIndex &index);
    void deleteEntry(const QModelIndex &index);
    void onEntryRenamed(const QString &dir, const QString &oldName, const QString &newName);
    QString selectedDir() const;
    bool saveEditor(CodeEditor *editor);
    bool saveAll();
    bool closeTab(int index);
    void showSettings();
    void about();

    QString compilerOrAsk();
    void build(bool runAfter);
    void runBinary();

    CodeEditor *currentEditor() const;
    CodeEditor *editorAt(int index) const;
    void refreshTabTitle(CodeEditor *editor);

    QString projectDir_;
    bool runAfterBuild_ = false;
    bool building_ = false;

    QStackedWidget *center_;
    QWidget *welcome_;
    QListWidget *welcomeRecent_;
    QLabel *welcomeRecentLabel_;
    QTabWidget *tabs_;
    QTreeView *tree_;
    QFileSystemModel *fsModel_;
    OutputPanel *output_;
    CompilerPanel *compilerPanel_;
    QLabel *cursorInfo_;
    QLabel *compilerInfo_;
    QMenu *recentMenu_;

    QAction *saveAct_, *saveAllAct_, *closeTabAct_, *newFileAct_;
    QAction *buildAct_, *runAct_, *stopAct_;
};
