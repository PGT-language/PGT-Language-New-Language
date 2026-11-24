#pragma once
#include <QProcess>
#include <QWidget>

class QLineEdit;
class QPlainTextEdit;

class OutputPanel : public QWidget
{
    Q_OBJECT

public:
    explicit OutputPanel(QWidget *parent = nullptr);

    void start(const QString &program, const QStringList &args, const QString &workDir);
    void stop();
    bool isRunning() const;
    void clear();
    void appendInfo(const QString &text);

signals:
    void locationActivated(const QString &file, int line, int column);
    void finished(int exitCode);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void appendText(const QString &text, const QColor &color);
    void sendInput();

    QPlainTextEdit *view_;
    QLineEdit *input_;
    QProcess *proc_;
    QString workDir_;
};
