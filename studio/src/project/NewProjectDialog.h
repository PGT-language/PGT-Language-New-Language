#pragma once
#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;

// Creates a project with `pgt new <name> -t <template>`.
class NewProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewProjectDialog(const QString &compiler, QWidget *parent = nullptr);

    // The directory of the created project (valid after exec() returned Accepted).
    QString projectDir() const { return projectDir_; }

private:
    void loadTemplates();
    void updatePreview();
    void create();

    QString compiler_;
    QLineEdit *name_;
    QLineEdit *location_;
    QComboBox *template_;
    QLabel *preview_;
    QLabel *error_;
    QString projectDir_;
};
