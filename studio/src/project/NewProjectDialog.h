#pragma once
#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;

class NewProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewProjectDialog(const QString &compiler, QWidget *parent = nullptr);

    QString projectDir() const { return projectDir_; }

private:
    void loadTemplates();
    void updatePreview();
    void showError(const QString &text);
    void create();

    QString compiler_;
    QLineEdit *name_;
    QLineEdit *location_;
    QComboBox *template_;
    QLabel *preview_;
    QLabel *error_;
    QString projectDir_;
};
