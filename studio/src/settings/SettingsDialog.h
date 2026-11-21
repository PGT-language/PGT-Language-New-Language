#pragma once
#include <QDialog>

class QFontComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    void browseCompiler();
    void browseProjectsDir();
    void checkCompiler();
    void save();

    QLineEdit *compilerEdit_;
    QLabel *compilerStatus_;
    QFontComboBox *fontBox_;
    QSpinBox *fontSize_;
    QSpinBox *tabWidth_;
    QLineEdit *projectsDir_;
};
