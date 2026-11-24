#include "settings/SettingsDialog.h"

#include "Theme.h"
#include "settings/Settings.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Настройки");
    setMinimumWidth(580);

    auto *compilerBox = new QGroupBox("Компилятор PGT");
    compilerEdit_ = new QLineEdit(Settings::compilerPath());
    QString detected = Settings::resolvedCompiler();
    compilerEdit_->setPlaceholderText(detected.isEmpty() ? "путь к pgt" : "автоматически: " + detected);
    auto *browse = new QPushButton("Обзор…");
    auto *check = new QPushButton("Проверить");
    browse->setAutoDefault(false);
    check->setAutoDefault(false);
    connect(browse, &QPushButton::clicked, this, &SettingsDialog::browseCompiler);
    connect(check, &QPushButton::clicked, this, &SettingsDialog::checkCompiler);

    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(compilerEdit_, 1);
    pathRow->addWidget(browse);
    pathRow->addWidget(check);

    auto *hint = new QLabel("Пустое поле — искать pgt в PATH и рядом со студией.");
    QPalette dim = hint->palette();
    dim.setColor(QPalette::WindowText, Theme::textDim);
    hint->setPalette(dim);

    compilerStatus_ = new QLabel;
    compilerStatus_->setWordWrap(true);
    compilerStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *compilerLayout = new QVBoxLayout(compilerBox);
    compilerLayout->addLayout(pathRow);
    compilerLayout->addWidget(hint);
    compilerLayout->addWidget(compilerStatus_);

    auto *editorBox = new QGroupBox("Редактор");
    QFont font = Settings::editorFont();
    fontBox_ = new QFontComboBox;
    fontBox_->setFontFilters(QFontComboBox::MonospacedFonts);
    fontBox_->setCurrentFont(font);
    fontSize_ = new QSpinBox;
    fontSize_->setRange(6, 48);
    fontSize_->setValue(font.pointSize() > 0 ? font.pointSize() : 11);
    tabWidth_ = new QSpinBox;
    tabWidth_->setRange(1, 16);
    tabWidth_->setValue(Settings::tabWidth());
    tabWidth_->setSuffix(" пробела");

    auto *editorLayout = new QFormLayout(editorBox);
    editorLayout->addRow("Шрифт:", fontBox_);
    editorLayout->addRow("Размер:", fontSize_);
    editorLayout->addRow("Отступ (Tab):", tabWidth_);

    auto *projectsBox = new QGroupBox("Проекты");
    projectsDir_ = new QLineEdit(Settings::projectsDir());
    auto *browseDir = new QPushButton("Обзор…");
    browseDir->setAutoDefault(false);
    connect(browseDir, &QPushButton::clicked, this, &SettingsDialog::browseProjectsDir);
    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(projectsDir_, 1);
    dirRow->addWidget(browseDir);
    auto *projectsLayout = new QFormLayout(projectsBox);
    projectsLayout->addRow("Папка по умолчанию:", dirRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Сохранить");
    buttons->button(QDialogButtonBox::Cancel)->setText("Отмена");
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 16);
    layout->addWidget(compilerBox);
    layout->addWidget(editorBox);
    layout->addWidget(projectsBox);
    layout->addStretch();
    layout->addWidget(buttons);
}

void SettingsDialog::browseCompiler()
{
    QString start = compilerEdit_->text().isEmpty() ? Settings::resolvedCompiler() : compilerEdit_->text();
    QString path = QFileDialog::getOpenFileName(this, "Компилятор PGT", start);
    if (!path.isEmpty())
    {
        compilerEdit_->setText(path);
        checkCompiler();
    }
}

void SettingsDialog::browseProjectsDir()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Папка проектов", projectsDir_->text());
    if (!dir.isEmpty())
        projectsDir_->setText(dir);
}

void SettingsDialog::setStatus(const QString &text, bool ok)
{
    compilerStatus_->setText(QString("<span style='color:%1'>%2 %3</span>")
                                 .arg(Theme::hex(ok ? Theme::success : Theme::error), ok ? "✔" : "✖",
                                      text.toHtmlEscaped().replace("\n", "<br>")));
}

void SettingsDialog::checkCompiler()
{
    QString path = compilerEdit_->text().trimmed();
    if (path.isEmpty())
        path = Settings::resolvedCompiler();
    if (path.isEmpty())
    {
        setStatus("Компилятор не найден.", false);
        return;
    }

    QProcess proc;
    proc.start(path, {"--version"});
    if (!proc.waitForFinished(5000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        setStatus("Не удалось запустить " + path + ": " + proc.errorString(), false);
        return;
    }
    setStatus(QString::fromUtf8(proc.readAllStandardOutput()).trimmed(), true);
}

void SettingsDialog::save()
{
    Settings::setCompilerPath(compilerEdit_->text());
    QFont font = fontBox_->currentFont();
    font.setPointSize(fontSize_->value());
    Settings::setEditorFont(font);
    Settings::setTabWidth(tabWidth_->value());
    Settings::setProjectsDir(projectsDir_->text().trimmed());
    accept();
}
