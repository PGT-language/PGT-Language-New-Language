#pragma once
#include <QFont>
#include <QString>
#include <QStringList>

// All persistent options of the studio (stored with QSettings).
namespace Settings
{
    // The path written in the settings; may be empty.
    QString compilerPath();
    void setCompilerPath(const QString &path);

    // The compiler that will actually be used: the configured one, otherwise `pgt` from PATH
    // or a build next to the studio. Empty when nothing was found.
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
