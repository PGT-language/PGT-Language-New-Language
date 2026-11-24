#pragma once
#include <QFont>
#include <QString>
#include <QStringList>

namespace Settings
{
    QString compilerPath();
    void setCompilerPath(const QString &path);

    QString resolvedCompiler();

    QFont editorFont();
    void setEditorFont(const QFont &font);

    int tabWidth();
    void setTabWidth(int spaces);

    QString projectsDir();
    void setProjectsDir(const QString &dir);

    QStringList recentProjects();
    void addRecentProject(const QString &dir);
}
