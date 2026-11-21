#include "OutputPanel.h"

#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTextBlock>
#include <QVBoxLayout>

OutputPanel::OutputPanel(QWidget *parent) : QWidget(parent), proc_(new QProcess(this))
{
    view_ = new QPlainTextEdit;
    view_->setReadOnly(true);
    view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono("monospace");
    mono.setStyleHint(QFont::Monospace);
    view_->setFont(mono);
    view_->viewport()->installEventFilter(this);

    input_ = new QLineEdit;
    input_->setPlaceholderText("Ввод для запущенной программы (Enter — отправить)");
    input_->setEnabled(false);
    connect(input_, &QLineEdit::returnPressed, this, &OutputPanel::sendInput);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view_);
    layout->addWidget(input_);

    connect(proc_, &QProcess::readyReadStandardOutput, this, [this] {
        appendText(QString::fromUtf8(proc_->readAllStandardOutput()), palette().color(QPalette::Text));
    });
    connect(proc_, &QProcess::readyReadStandardError, this, [this] {
        appendText(QString::fromUtf8(proc_->readAllStandardError()), QColor("#c0392b"));
    });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
        {
            appendInfo("Не удалось запустить: " + proc_->errorString());
            input_->setEnabled(false);
            emit finished(-1);
        }
    });
    connect(proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        input_->setEnabled(false);
        appendInfo(status == QProcess::CrashExit ? "Процесс аварийно завершился"
                                                 : QString("Процесс завершился с кодом %1").arg(code));
        emit finished(status == QProcess::CrashExit ? -1 : code);
    });
}

void OutputPanel::start(const QString &program, const QStringList &args, const QString &workDir)
{
    stop();
    workDir_ = workDir;
    appendInfo("$ " + QFileInfo(program).fileName() + " " + args.join(' '));
    proc_->setWorkingDirectory(workDir);
    proc_->start(program, args);
    input_->setEnabled(true);
    input_->setFocus();
}

void OutputPanel::stop()
{
    if (proc_->state() == QProcess::NotRunning)
        return;
    proc_->kill();
    proc_->waitForFinished(2000);
}

bool OutputPanel::isRunning() const
{
    return proc_->state() != QProcess::NotRunning;
}

void OutputPanel::clear()
{
    view_->clear();
}

void OutputPanel::appendInfo(const QString &text)
{
    appendText(text + "\n", palette().color(QPalette::PlaceholderText));
}

void OutputPanel::appendText(const QString &text, const QColor &color)
{
    QTextCursor cursor(view_->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(color);
    cursor.insertText(text, fmt);
    view_->setTextCursor(cursor);
    view_->ensureCursorVisible();
}

void OutputPanel::sendInput()
{
    if (!isRunning())
        return;
    QString line = input_->text();
    input_->clear();
    appendText(line + "\n", QColor("#2980b9"));
    proc_->write((line + "\n").toUtf8());
}

bool OutputPanel::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == view_->viewport() && event->type() == QEvent::MouseButtonDblClick)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        QString line = view_->cursorForPosition(me->position().toPoint()).block().text();
        static const QRegularExpression re(R"(^(.+?):(\d+):(\d+): error:)");
        auto m = re.match(line);
        if (m.hasMatch())
        {
            QString file = m.captured(1);
            if (QFileInfo(file).isRelative())
                file = QDir(workDir_).filePath(file);
            emit locationActivated(QDir::cleanPath(file), m.captured(2).toInt(), m.captured(3).toInt());
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}
