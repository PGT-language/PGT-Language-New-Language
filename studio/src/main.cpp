#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("PGT Studio");
    QApplication::setOrganizationName("pgt");
    QApplication::setApplicationVersion(PGT_STUDIO_VERSION);

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
