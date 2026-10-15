#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QLockFile>
#include <QMessageBox>

#include "AtomicFile.h"
#include "LauncherWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Prismatic Launcher");
    QApplication::setOrganizationName("Prismatic");
    QApplication::setStyle("Fusion");
    QApplication::setFont(QFont("Sans Serif", 10));

    QCommandLineParser parser;
    parser.setApplicationDescription("Prismatic Launcher — local Stardew Valley mod profiles");
    parser.addHelpOption();
    parser.addVersionOption();
    QApplication::setApplicationVersion("0.2.0");
    parser.addOption({"config", "Use an alternate INI configuration file.", "file"});
    parser.addOption(
        {"data-dir", "Store launcher profiles and settings in this directory.", "directory"});
    parser.process(app);

    QFile stylesheet(":/ui/style.qss");
    if (stylesheet.open(QIODevice::ReadOnly)) app.setStyleSheet(stylesheet.readAll());

    const auto requestedData = parser.isSet("data-dir")
                                   ? parser.value("data-dir")
                                   : qEnvironmentVariable("PRISMATIC_DATA_DIR");
    const auto dataDirectory =
        QDir::cleanPath(requestedData.isEmpty() ? QApplication::applicationDirPath() + "/data"
                                                : QFileInfo(requestedData).absoluteFilePath());
    QString pathError;
    if (!prismatic::validateStoragePath(dataDirectory, &pathError) ||
        !prismatic::validateStoragePath(dataDirectory + "/.launcher-session.lock", &pathError) ||
        !QDir().mkpath(dataDirectory) || !QFileInfo(dataDirectory).isWritable()) {
        QMessageBox::critical(nullptr, "Cannot open launcher data",
                              "Choose a writable launcher data directory with --data-dir.\n" +
                                  dataDirectory + "\n" + pathError);
        return 1;
    }
    QLockFile sessionLock(dataDirectory + "/.launcher-session.lock");
    if (!sessionLock.tryLock(0)) {
        QMessageBox::critical(nullptr, "Launcher data is in use",
                              "Another launcher is using this data directory. Close it before "
                              "opening the same profiles, or choose a different --data-dir.");
        return 1;
    }
    const auto configFile = parser.isSet("config")
                                ? QFileInfo(parser.value("config")).absoluteFilePath()
                                : dataDirectory + "/launcher.ini";
    LauncherWindow window(configFile, nullptr, dataDirectory);
    window.show();
    return QApplication::exec();
}
