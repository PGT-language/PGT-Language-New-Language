#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QTranslator>

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    QApplication app(argc, argv);
    QApplication::setApplicationName("PGT Studio");
    QApplication::setOrganizationName("pgt");
    QApplication::setApplicationVersion(PGT_STUDIO_VERSION);
    Theme::apply(app);

    QTranslator qtTranslator;
    if (qtTranslator.load("qtbase_ru", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    QCommandLineParser parser;
    parser.setApplicationDescription("PGT Studio");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("path", "a project directory or a file to open");
    parser.process(app);

    MainWindow window;
    window.show();

    for (const QString &arg : parser.positionalArguments())
    {
        QFileInfo info(arg);
        if (info.isDir())
            window.openProject(info.absoluteFilePath());
        else if (info.isFile())
            window.openFile(info.absoluteFilePath());
    }
    return app.exec();
}
