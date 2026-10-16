#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QLockFile>
#include <QMessageBox>

namespace {
#ifdef Q_OS_MACOS
void useBundledQtPlugins(char* argv[]) {
    const auto executable = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    QDir contents(QFileInfo(executable).absolutePath());
    if (contents.dirName() != "MacOS" || !contents.cdUp() || contents.dirName() != "Contents")
        return;
    const auto plugins = contents.filePath("PlugIns");
    if (QFileInfo::exists(plugins + "/platforms/libqcocoa.dylib"))
        qputenv("QT_PLUGIN_PATH", QFile::encodeName(plugins));
}
#endif

void configureParser(QCommandLineParser& parser) {
    parser.setApplicationDescription("Prismatic Launcher — local Stardew Valley mod profiles");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"config", "Use an alternate INI configuration file.", "file"});
    parser.addOption(
        {"data-dir", "Store launcher profiles and settings in this directory.", "directory"});
}
}  // namespace

#include "AtomicFile.h"
#include "DataPaths.h"
#include "LauncherWindow.h"

int main(int argc, char* argv[]) {
    {
        QCoreApplication commandLineApp(argc, argv);
        QCoreApplication::setApplicationName("Prismatic Launcher");
        QCoreApplication::setApplicationVersion("0.2.0");
        QCommandLineParser commandLineParser;
        configureParser(commandLineParser);
        commandLineParser.process(commandLineApp);
    }
#ifdef Q_OS_MACOS
    useBundledQtPlugins(argv);
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName("Prismatic Launcher");
    QApplication::setOrganizationName("Prismatic");
    QApplication::setStyle("Fusion");
    QApplication::setFont(QFont("Sans Serif", 10));

    QCommandLineParser parser;
    configureParser(parser);
    QApplication::setApplicationVersion("0.2.0");
    parser.process(app);

    QFile stylesheet(":/ui/style.qss");
    if (stylesheet.open(QIODevice::ReadOnly)) app.setStyleSheet(stylesheet.readAll());

    const auto requestedData = parser.isSet("data-dir")
                                   ? parser.value("data-dir")
                                   : qEnvironmentVariable("PRISMATIC_DATA_DIR");
    const auto dataDirectory = requestedData.isEmpty()
                                   ? prismatic::defaultDataDirectory()
                                   : QFileInfo(requestedData).absoluteFilePath();
    QString pathError;
    if (requestedData.isEmpty() &&
        !prismatic::migrateLegacyData(QApplication::applicationDirPath() + "/data", dataDirectory,
                                      &pathError)) {
        QMessageBox::critical(nullptr, "Cannot migrate launcher data", pathError);
        return 1;
    }
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
