#include "MainWindow.h"

#include "CompilerPanel.h"
#include "OutputPanel.h"
#include "Theme.h"
#include "editor/CodeEditor.h"
#include "project/NewProjectDialog.h"
#include "settings/Settings.h"
#include "settings/SettingsDialog.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow()
{
    resize(1200, 800);

    // ---- editor tabs and the welcome page
    tabs_ = new QTabWidget;
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setDocumentMode(true);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(tabs_, &QTabWidget::currentChanged, this, [this] {
        updateActions();
        updateCursorInfo();
    });

    createWelcome();
    center_ = new QStackedWidget;
    center_->addWidget(welcome_);
    center_->addWidget(tabs_);
    setCentralWidget(center_);

    // ---- project tree
    fsModel_ = new QFileSystemModel(this);
    // Renaming happens only when asked for (context menu or F2), never on a click.
    fsModel_->setReadOnly(false);
    connect(fsModel_, &QFileSystemModel::fileRenamed, this, &MainWindow::onEntryRenamed);
    tree_ = new QTreeView;
    tree_->setModel(fsModel_);
    tree_->setHeaderHidden(true);
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    for (int c = 1; c < fsModel_->columnCount(); ++c)
        tree_->hideColumn(c);
    connect(tree_, &QTreeView::customContextMenuRequested, this, &MainWindow::showTreeMenu);
    auto openIndex = [this](const QModelIndex &index) {
        if (index.isValid() && !fsModel_->isDir(index))
            openFile(fsModel_->filePath(index));
    };
    connect(tree_, &QTreeView::clicked, this, openIndex);
    connect(tree_, &QTreeView::activated, this, openIndex);

    // Keyboard shortcuts of the tree, active only while it has focus.
    auto treeShortcut = [this](const QKeySequence &key, auto handler) {
        auto *a = new QAction(tree_);
        a->setShortcut(key);
        a->setShortcutContext(Qt::WidgetShortcut);
        connect(a, &QAction::triggered, this, handler);
        tree_->addAction(a);
    };
    treeShortcut(QKeySequence(Qt::Key_F2), [this] { renameEntry(tree_->currentIndex()); });
    treeShortcut(QKeySequence::Delete, [this] { deleteEntry(tree_->currentIndex()); });
    auto *treeDock = new QDockWidget("ПРОЕКТ");
    treeDock->setObjectName("projectDock");
    treeDock->setWidget(tree_);
    treeDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::LeftDockWidgetArea, treeDock);

    // ---- output
    output_ = new OutputPanel;
    connect(output_, &OutputPanel::locationActivated, this, [this](const QString &file, int line, int col) {
        if (CodeEditor *e = openFile(file))
            e->goToLine(line, col);
    });
    connect(output_, &OutputPanel::finished, this, [this](int code) {
        if (building_)
        {
            building_ = false;
            if (code == 0 && runAfterBuild_)
                runBinary();
            else if (code != 0)
                statusBar()->showMessage("Сборка не удалась (двойной клик по ошибке — перейти к месту)", 8000);
            else
                statusBar()->showMessage("Сборка завершена", 4000);
        }
        updateActions();
    });
    auto *outDock = new QDockWidget("ВЫВОД");
    outDock->setObjectName("outputDock");
    outDock->setWidget(output_);
    outDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::BottomDockWidgetArea, outDock);
    compilerPanel_ = new CompilerPanel;
    auto *compilerDock = new QDockWidget("КОМПИЛЯТОР");
    compilerDock->setObjectName("compilerDock");
    compilerDock->setWidget(compilerPanel_);
    compilerDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::RightDockWidgetArea, compilerDock);
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { compilerPanel_->setEditor(currentEditor()); });

    resizeDocks({treeDock, compilerDock}, {240, 360}, Qt::Horizontal);
    resizeDocks({outDock}, {220}, Qt::Vertical);

    // ---- status bar
    cursorInfo_ = new QLabel;
    compilerInfo_ = new QLabel;
    statusBar()->addPermanentWidget(cursorInfo_);
    statusBar()->addPermanentWidget(compilerInfo_);

    createActions();
    treeDock->toggleViewAction()->setText("Панель проекта");
    outDock->toggleViewAction()->setText("Панель вывода");
    compilerDock->toggleViewAction()->setText("Токены и дерево разбора");
    QMenu *view = menuBar()->addMenu("&Вид");
    view->addAction(treeDock->toggleViewAction());
    view->addAction(outDock->toggleViewAction());
    view->addAction(compilerDock->toggleViewAction());
    menuBar()->addMenu("&Справка")->addAction("О программе", this, &MainWindow::about);

    QSettings s;
    restoreGeometry(s.value("window/geometry").toByteArray());
    restoreState(s.value("window/state").toByteArray());

    updateRecentMenu();
    updateTitle();
    updateActions();
}

void MainWindow::createActions()
{
    QMenu *file = menuBar()->addMenu("&Файл");
    file->addAction("Новый проект…", QKeySequence("Ctrl+Shift+N"), this, &MainWindow::newProject);
    file->addAction("Открыть проект…", QKeySequence("Ctrl+Shift+O"), this, &MainWindow::openProjectDialog);
    recentMenu_ = file->addMenu("Недавние проекты");
    file->addSeparator();
    newFileAct_ = file->addAction("Новый файл…", QKeySequence::New, this, &MainWindow::newFile);
    file->addAction("Открыть файл…", QKeySequence::Open, this, &MainWindow::openFileDialog);
    saveAct_ = file->addAction("Сохранить", QKeySequence::Save, this, [this] {
        if (CodeEditor *e = currentEditor())
            saveEditor(e);
    });
    saveAllAct_ = file->addAction("Сохранить всё", QKeySequence("Ctrl+Shift+S"), this, &MainWindow::saveAll);
    closeTabAct_ =
        file->addAction("Закрыть вкладку", QKeySequence::Close, this, [this] { closeTab(tabs_->currentIndex()); });
    file->addSeparator();
    file->addAction("Настройки…", QKeySequence("Ctrl+,"), this, &MainWindow::showSettings);
    file->addSeparator();
    file->addAction("Выход", QKeySequence::Quit, this, &QWidget::close);

    QMenu *buildMenu = menuBar()->addMenu("&Сборка");
    buildAct_ = buildMenu->addAction("Собрать", QKeySequence("Ctrl+B"), this, [this] { build(false); });
    runAct_ = buildMenu->addAction("Запустить", QKeySequence("F5"), this, [this] { build(true); });
    stopAct_ = buildMenu->addAction("Остановить", QKeySequence("Shift+F5"), this, [this] { output_->stop(); });

    auto *bar = addToolBar("Сборка");
    bar->setObjectName("buildToolbar");
    bar->setMovable(false);
    bar->addAction(saveAllAct_);
    bar->addSeparator();
    bar->addAction(buildAct_);
    bar->addAction(runAct_);
    bar->addAction(stopAct_);
}

void MainWindow::createWelcome()
{
    welcome_ = new QWidget;
    welcome_->setAutoFillBackground(true);
    QPalette bg = welcome_->palette();
    bg.setColor(QPalette::Window, Theme::editor);
    welcome_->setPalette(bg);

    auto *title = new QLabel(QString("<div style='font-size:34pt; font-weight:800; color:%1'>PGT "
                                     "<span style='color:%2'>Studio</span></div>")
                                 .arg(Theme::hex(Theme::accent), Theme::hex(Theme::text)));
    title->setAlignment(Qt::AlignCenter);
    auto *subtitle = new QLabel(
        QString("<span style='color:%1'>Среда разработки для языка PGT</span>").arg(Theme::hex(Theme::textDim)));
    subtitle->setAlignment(Qt::AlignCenter);

    auto *newBtn = new QPushButton("Новый проект");
    newBtn->setObjectName("primary");
    auto *openBtn = new QPushButton("Открыть проект");
    for (QPushButton *b : {newBtn, openBtn})
        b->setMinimumWidth(220);
    connect(newBtn, &QPushButton::clicked, this, &MainWindow::newProject);
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::openProjectDialog);

    welcomeRecentLabel_ =
        new QLabel(QString("<span style='color:%1'>НЕДАВНИЕ ПРОЕКТЫ</span>").arg(Theme::hex(Theme::textDim)));
    welcomeRecent_ = new QListWidget;
    welcomeRecent_->setFixedWidth(520);
    welcomeRecent_->setMaximumHeight(220);
    connect(welcomeRecent_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { openProject(item->data(Qt::UserRole).toString()); });

    auto *layout = new QVBoxLayout(welcome_);
    layout->addStretch();
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(24);
    layout->addWidget(newBtn, 0, Qt::AlignHCenter);
    layout->addWidget(openBtn, 0, Qt::AlignHCenter);
    layout->addSpacing(28);
    layout->addWidget(welcomeRecentLabel_, 0, Qt::AlignHCenter);
    layout->addWidget(welcomeRecent_, 0, Qt::AlignHCenter);
    layout->addStretch();
}

void MainWindow::updateRecentMenu()
{
    recentMenu_->clear();
    welcomeRecent_->clear();
    const QStringList recent = Settings::recentProjects();
    for (const QString &dir : recent)
    {
        if (!QFileInfo(dir).isDir())
            continue;
        recentMenu_->addAction(dir, this, [this, dir] { openProject(dir); });
        auto *item = new QListWidgetItem(QFileInfo(dir).fileName() + "    " + dir);
        item->setData(Qt::UserRole, dir);
        welcomeRecent_->addItem(item);
    }
    recentMenu_->setEnabled(!recentMenu_->isEmpty());
    welcomeRecent_->setVisible(welcomeRecent_->count() > 0);
    welcomeRecentLabel_->setVisible(welcomeRecent_->count() > 0);
}

void MainWindow::updateTitle()
{
    QString title = "PGT Studio";
    if (!projectDir_.isEmpty())
        title = QFileInfo(projectDir_).fileName() + " — " + title;
    setWindowTitle(title);

    QString compiler = Settings::resolvedCompiler();
    compilerInfo_->setText(compiler.isEmpty() ? "компилятор не найден" : "pgt: " + compiler);
}

void MainWindow::updateActions()
{
    bool hasEditor = currentEditor() != nullptr;
    bool running = output_->isRunning();
    saveAct_->setEnabled(hasEditor);
    saveAllAct_->setEnabled(tabs_->count() > 0);
    closeTabAct_->setEnabled(hasEditor);
    newFileAct_->setEnabled(!projectDir_.isEmpty());
    buildAct_->setEnabled(!projectDir_.isEmpty() && !running);
    runAct_->setEnabled(!projectDir_.isEmpty() && !running);
    stopAct_->setEnabled(running);
    center_->setCurrentWidget(tabs_->count() > 0 ? static_cast<QWidget *>(tabs_) : welcome_);
}

void MainWindow::updateCursorInfo()
{
    CodeEditor *e = currentEditor();
    if (!e)
    {
        cursorInfo_->clear();
        return;
    }
    QTextCursor c = e->textCursor();
    cursorInfo_->setText(QString("Стр %1, стлб %2").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1));
}

// ------------------------------------------------------------------ projects and files

void MainWindow::openProject(const QString &dir)
{
    QFileInfo info(dir);
    if (!info.isDir())
    {
        QMessageBox::warning(this, "Открыть проект", "Папка не найдена: " + dir);
        return;
    }
    if (!saveAll())
        return;
    while (tabs_->count() > 0)
        delete tabs_->widget(0);

    projectDir_ = info.canonicalFilePath();
    fsModel_->setRootPath(projectDir_);
    tree_->setRootIndex(fsModel_->index(projectDir_));
    Settings::addRecentProject(projectDir_);
    updateRecentMenu();
    updateTitle();

    QString main = QDir(projectDir_).filePath("main.pgt");
    if (QFileInfo::exists(main))
        openFile(main);
    updateActions();
    output_->clear();
    output_->appendInfo("Проект: " + projectDir_);
}

void MainWindow::newProject()
{
    QString compiler = compilerOrAsk();
    if (compiler.isEmpty())
        return;
    NewProjectDialog dlg(compiler, this);
    if (dlg.exec() == QDialog::Accepted)
        openProject(dlg.projectDir());
}

void MainWindow::openProjectDialog()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Открыть проект",
                                                    projectDir_.isEmpty() ? Settings::projectsDir() : projectDir_);
    if (!dir.isEmpty())
        openProject(dir);
}

void MainWindow::openFileDialog()
{
    QString path = QFileDialog::getOpenFileName(this, "Открыть файл",
                                                projectDir_.isEmpty() ? Settings::projectsDir() : projectDir_,
                                                "PGT (*.pgt *.mod);;Все файлы (*)");
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::newFile()
{
    createEntry(selectedDir(), false);
}

QString MainWindow::selectedDir() const
{
    QModelIndex index = tree_->currentIndex();
    if (!index.isValid() || projectDir_.isEmpty())
        return projectDir_;
    QString path = fsModel_->filePath(index);
    return fsModel_->isDir(index) ? path : QFileInfo(path).absolutePath();
}

void MainWindow::createEntry(const QString &dir, bool folder)
{
    bool ok = false;
    QString name = QInputDialog::getText(this, folder ? "Новая папка" : "Новый файл",
                                         folder ? "Имя папки:" : "Имя файла:", QLineEdit::Normal,
                                         folder ? "" : "file.pgt", &ok)
                       .trimmed();
    if (!ok || name.isEmpty())
        return;
    QString path = QDir(dir).filePath(name);
    if (QFileInfo::exists(path))
    {
        QMessageBox::warning(this, "Создать", "Уже существует: " + name);
        return;
    }
    if (folder)
    {
        if (!QDir().mkpath(path))
            QMessageBox::warning(this, "Новая папка", "Не удалось создать папку " + path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(this, "Новый файл", "Не удалось создать файл: " + f.errorString());
        return;
    }
    if (path.endsWith(".pgt"))
    {
        // A new file belongs to the package of its directory.
        QString dirName = QFileInfo(path).absoluteDir().canonicalPath();
        QString pkg = dirName == projectDir_ ? "main" : QFileInfo(dirName).fileName();
        f.write(QString("package %1\n\n").arg(pkg).toUtf8());
    }
    f.close();
    openFile(path);
}

void MainWindow::showTreeMenu(const QPoint &pos)
{
    if (projectDir_.isEmpty())
        return;
    QModelIndex index = tree_->indexAt(pos);
    tree_->setCurrentIndex(index);
    QString path = index.isValid() ? fsModel_->filePath(index) : projectDir_;
    bool isDir = !index.isValid() || fsModel_->isDir(index);
    QString dir = isDir ? path : QFileInfo(path).absolutePath();

    QMenu menu(this);
    if (index.isValid() && !isDir)
        menu.addAction("Открыть", this, [this, path] { openFile(path); });
    menu.addAction("Новый файл…", this, [this, dir] { createEntry(dir, false); });
    menu.addAction("Новая папка…", this, [this, dir] { createEntry(dir, true); });
    if (index.isValid())
    {
        menu.addSeparator();
        QAction *rename = menu.addAction("Переименовать", this, [this, index] { renameEntry(index); });
        rename->setShortcut(QKeySequence(Qt::Key_F2));
        QAction *del = menu.addAction("Удалить", this, [this, index] { deleteEntry(index); });
        del->setShortcut(QKeySequence::Delete);
    }
    menu.addSeparator();
    menu.addAction("Копировать путь", this, [path] { QApplication::clipboard()->setText(path); });
    menu.addAction("Копировать относительный путь", this, [this, path] {
        QApplication::clipboard()->setText(QDir(projectDir_).relativeFilePath(path));
    });
    menu.addAction("Показать в файловом менеджере", this,
                   [dir] { QDesktopServices::openUrl(QUrl::fromLocalFile(dir)); });
    menu.exec(tree_->viewport()->mapToGlobal(pos));
}

void MainWindow::renameEntry(const QModelIndex &index)
{
    if (index.isValid())
        tree_->edit(index); // inline editing; the model reports the result through fileRenamed
}

void MainWindow::onEntryRenamed(const QString &dir, const QString &oldName, const QString &newName)
{
    QString oldPath = QDir(dir).filePath(oldName);
    QString newPath = QDir(dir).filePath(newName);
    for (int i = 0; i < tabs_->count(); ++i)
    {
        CodeEditor *e = editorAt(i);
        QString p = e->filePath();
        if (p == oldPath || p.startsWith(oldPath + "/"))
        {
            e->setFilePath(newPath + p.mid(oldPath.size()));
            refreshTabTitle(e);
            tabs_->setTabToolTip(i, e->filePath());
        }
    }
}

void MainWindow::deleteEntry(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    QString path = fsModel_->filePath(index);
    bool isDir = fsModel_->isDir(index);
    auto answer = QMessageBox::question(
        this, "Удалить", QString("Удалить %1 «%2»?\nОн будет перемещён в корзину.")
                             .arg(isDir ? "папку" : "файл", QFileInfo(path).fileName()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;

    // Tabs of the deleted files are closed without asking: the files are gone.
    for (int i = tabs_->count() - 1; i >= 0; --i)
    {
        QString p = editorAt(i)->filePath();
        if (p == path || p.startsWith(path + "/"))
            delete tabs_->widget(i);
    }
    if (!QFile::moveToTrash(path))
    {
        // Never delete for good without a second, explicit confirmation.
        auto permanently = QMessageBox::warning(this, "Удалить",
                                            "Не удалось переместить в корзину.\nУдалить «" +
                                                QFileInfo(path).fileName() + "» навсегда?",
                                            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (permanently == QMessageBox::Yes && !(isDir ? QDir(path).removeRecursively() : QFile::remove(path)))
            QMessageBox::warning(this, "Удалить", "Не удалось удалить " + path);
    }
    updateActions();
}

CodeEditor *MainWindow::openFile(const QString &path)
{
    QString canonical = QFileInfo(path).canonicalFilePath();
    for (int i = 0; i < tabs_->count(); ++i)
    {
        if (editorAt(i)->filePath() == canonical)
        {
            tabs_->setCurrentIndex(i);
            return editorAt(i);
        }
    }

    auto *editor = new CodeEditor;
    QString error;
    if (!editor->load(path, &error))
    {
        delete editor;
        QMessageBox::warning(this, "Открыть файл", "Не удалось открыть " + path + ":\n" + error);
        return nullptr;
    }
    int index = tabs_->addTab(editor, editor->displayName());
    tabs_->setTabToolTip(index, editor->filePath());
    tabs_->setCurrentIndex(index);
    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this, editor] { refreshTabTitle(editor); });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::updateCursorInfo);
    editor->setFocus();
    updateActions();
    return editor;
}

bool MainWindow::saveEditor(CodeEditor *editor)
{
    QString error;
    if (!editor->save(&error))
    {
        QMessageBox::warning(this, "Сохранить", "Не удалось сохранить " + editor->filePath() + ":\n" + error);
        return false;
    }
    statusBar()->showMessage("Сохранено: " + editor->displayName(), 3000);
    return true;
}

bool MainWindow::saveAll()
{
    for (int i = 0; i < tabs_->count(); ++i)
        if (editorAt(i)->document()->isModified() && !saveEditor(editorAt(i)))
            return false;
    return true;
}

bool MainWindow::closeTab(int index)
{
    CodeEditor *editor = editorAt(index);
    if (!editor)
        return true;
    if (editor->document()->isModified())
    {
        auto answer = QMessageBox::question(this, "Закрыть файл", "Сохранить изменения в " + editor->displayName() + "?",
                                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !saveEditor(editor)))
            return false;
    }
    delete editor;
    updateActions();
    return true;
}

void MainWindow::refreshTabTitle(CodeEditor *editor)
{
    int index = tabs_->indexOf(editor);
    if (index >= 0)
        tabs_->setTabText(index, editor->displayName() + (editor->document()->isModified() ? " ●" : ""));
}

CodeEditor *MainWindow::currentEditor() const
{
    return qobject_cast<CodeEditor *>(tabs_->currentWidget());
}

CodeEditor *MainWindow::editorAt(int index) const
{
    return qobject_cast<CodeEditor *>(tabs_->widget(index));
}

// ------------------------------------------------------------------ build and run

QString MainWindow::compilerOrAsk()
{
    QString compiler = Settings::resolvedCompiler();
    if (!compiler.isEmpty())
        return compiler;
    auto answer = QMessageBox::warning(this, "Компилятор не найден", "Укажите путь к компилятору pgt в настройках.",
                                       QMessageBox::Open | QMessageBox::Cancel);
    if (answer == QMessageBox::Open)
        showSettings();
    return Settings::resolvedCompiler();
}

void MainWindow::build(bool runAfter)
{
    if (projectDir_.isEmpty() || output_->isRunning())
        return;
    QString compiler = compilerOrAsk();
    if (compiler.isEmpty() || !saveAll())
        return;
    output_->clear();
    building_ = true;
    runAfterBuild_ = runAfter;
    output_->start(compiler, {"build", "."}, projectDir_);
    updateActions();
}

void MainWindow::runBinary()
{
    // `pgt build <dir>` names the executable after the directory. It is started directly
    // (not through `pgt run`) so that "Stop" kills the program itself.
    QString exe = QDir(projectDir_).filePath(QFileInfo(projectDir_).fileName());
    if (!QFileInfo(exe).isExecutable())
    {
        output_->appendInfo("Исполняемый файл не найден: " + exe);
        return;
    }
    output_->start(exe, {}, projectDir_);
    updateActions();
}

// ------------------------------------------------------------------ misc

void MainWindow::showSettings()
{
    SettingsDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    for (int i = 0; i < tabs_->count(); ++i)
        editorAt(i)->applySettings();
    updateTitle();
}

void MainWindow::about()
{
    QMessageBox::about(this, "О программе",
                       QString("<h3>PGT Studio %1</h3><p>Среда разработки для языка PGT.</p><p>Qt %2</p>")
                           .arg(PGT_STUDIO_VERSION, qVersion()));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (int i = 0; i < tabs_->count(); ++i)
    {
        CodeEditor *e = editorAt(i);
        if (!e->document()->isModified())
            continue;
        tabs_->setCurrentIndex(i);
        auto answer = QMessageBox::question(this, "Выход", "Сохранить изменения в " + e->displayName() + "?",
                                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !saveEditor(e)))
        {
            event->ignore();
            return;
        }
    }
    output_->stop();
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state", saveState());
    event->accept();
}
