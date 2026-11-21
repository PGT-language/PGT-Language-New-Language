#include "settings/Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QSettings>
#include <QStandardPaths>

namespace Settings
{

    QString compilerPath()
    {
        return QSettings().value("compiler/path").toString();
    }

    void setCompilerPath(const QString &path)
    {
        QSettings().setValue("compiler/path", path.trimmed());
    }

    QString resolvedCompiler()
    {
        QString configured = compilerPath();
        if (!configured.isEmpty())
            return QFileInfo(configured).isExecutable() ? configured : QString();

        QString inPath = QStandardPaths::findExecutable("pgt");
        if (!inPath.isEmpty())
            return inPath;

        // A development checkout: studio/build/pgt-studio next to build/pgt.
        QDir dir(QCoreApplication::applicationDirPath());
        for (const QString &rel : {"pgt", "../../build/pgt", "../build/pgt"})
        {
            QFileInfo f(dir.filePath(rel));
            if (f.isFile() && f.isExecutable())
                return f.canonicalFilePath();
        }
        return {};
    }

    QFont editorFont()
    {
        QFont fallback = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        fallback.setPointSize(11);
        QFont f = QSettings().value("editor/font", fallback).value<QFont>();
        f.setStyleHint(QFont::Monospace);
        return f;
    }

    void setEditorFont(const QFont &font)
    {
        QSettings().setValue("editor/font", font);
    }

    int tabWidth()
    {
        return QSettings().value("editor/tabWidth", 4).toInt();
    }

    void setTabWidth(int spaces)
    {
        QSettings().setValue("editor/tabWidth", spaces);
    }

    QString projectsDir()
    {
        return QSettings().value("projects/dir", QDir::homePath()).toString();
    }

    void setProjectsDir(const QString &dir)
    {
        QSettings().setValue("projects/dir", dir);
    }

    QStringList recentProjects()
    {
        return QSettings().value("projects/recent").toStringList();
    }

    void addRecentProject(const QString &dir)
    {
        QStringList list = recentProjects();
        list.removeAll(dir);
        list.prepend(dir);
        while (list.size() > 10)
            list.removeLast();
        QSettings().setValue("projects/recent", list);
    }

}
