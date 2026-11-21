#include "project/NewProjectDialog.h"

#include "settings/Settings.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

NewProjectDialog::NewProjectDialog(const QString &compiler, QWidget *parent)
    : QDialog(parent), compiler_(compiler)
{
    setWindowTitle("Новый проект");
    setMinimumWidth(520);

    name_ = new QLineEdit("hello");
    name_->setValidator(
        new QRegularExpressionValidator(QRegularExpression("[A-Za-z_][A-Za-z0-9_-]*"), name_));
    location_ = new QLineEdit(Settings::projectsDir());
    auto *browse = new QPushButton("Обзор…");
    connect(browse, &QPushButton::clicked, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(this, "Где создать проект", location_->text());
        if (!dir.isEmpty())
            location_->setText(dir);
    });
    auto *locationRow = new QHBoxLayout;
    locationRow->addWidget(location_, 1);
    locationRow->addWidget(browse);

    template_ = new QComboBox;
    preview_ = new QLabel;
    preview_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    error_ = new QLabel;
    error_->setWordWrap(true);
    error_->setStyleSheet("color: #c0392b");

    auto *form = new QFormLayout;
    form->addRow("Имя:", name_);
    form->addRow("Папка:", locationRow);
    form->addRow("Шаблон:", template_);
    form->addRow("Будет создан:", preview_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Создать");
    connect(buttons, &QDialogButtonBox::accepted, this, &NewProjectDialog::create);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(error_);
    layout->addStretch();
    layout->addWidget(buttons);

    connect(name_, &QLineEdit::textChanged, this, &NewProjectDialog::updatePreview);
    connect(location_, &QLineEdit::textChanged, this, &NewProjectDialog::updatePreview);
    loadTemplates();
    updatePreview();
}

void NewProjectDialog::loadTemplates()
{
    // `pgt new --list` prints "  name        description" per line.
    QProcess proc;
    proc.start(compiler_, {"new", "--list"});
    if (proc.waitForFinished(5000) && proc.exitCode() == 0)
    {
        const QStringList lines = QString::fromUtf8(proc.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines)
        {
            QString t = line.trimmed();
            int sp = t.indexOf(' ');
            QString name = sp < 0 ? t : t.left(sp);
            QString desc = sp < 0 ? QString() : t.mid(sp).trimmed();
            template_->addItem(desc.isEmpty() ? name : name + " — " + desc, name);
        }
    }
    if (template_->count() == 0)
    {
        template_->addItem("hello", "hello");
        error_->setText("Не удалось получить список шаблонов от компилятора: проверьте путь в настройках.");
    }
}

void NewProjectDialog::updatePreview()
{
    preview_->setText(QDir(location_->text()).filePath(name_->text()));
}

void NewProjectDialog::create()
{
    QString name = name_->text();
    QDir location(location_->text());
    if (name.isEmpty())
    {
        error_->setText("Введите имя проекта.");
        return;
    }
    if (!location.exists() && !QDir().mkpath(location.path()))
    {
        error_->setText("Не удалось создать папку " + location.path());
        return;
    }

    QProcess proc;
    proc.setWorkingDirectory(location.path());
    proc.start(compiler_, {"new", name, "-t", template_->currentData().toString()});
    if (!proc.waitForFinished(10000))
    {
        error_->setText("Компилятор не ответил: " + proc.errorString());
        return;
    }
    if (proc.exitCode() != 0)
    {
        error_->setText(QString::fromUtf8(proc.readAllStandardError()).trimmed());
        return;
    }
    projectDir_ = location.filePath(name);
    Settings::setProjectsDir(location.path());
    accept();
}
