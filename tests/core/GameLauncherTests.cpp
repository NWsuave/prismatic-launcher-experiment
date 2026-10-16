#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <cstdio>

#include "GameLauncher.h"

using namespace prismatic;

namespace {
QString vanillaName() {
#ifdef Q_OS_WIN
    return QStringLiteral("Stardew Valley.exe");
#else
    return QStringLiteral("Stardew Valley");
#endif
}

QString smapiName() {
#ifdef Q_OS_WIN
    return QStringLiteral("StardewModdingAPI.exe");
#else
    return QStringLiteral("StardewModdingAPI");
#endif
}

bool executable(const QString& path, const QByteArray& contents = "fixture, never executed") {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() &&
           file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                               QFileDevice::ExeOwner);
}
}  // namespace

class GameLauncherTests final : public QObject {
    Q_OBJECT

   private slots:
    void invalidDirectory();
    void inspectAndPlan();
    void bundleEnvironmentDoesNotLeakIntoGame();
    void nativeWrapperNeverUsedForVanilla();
    void rejectUnsafeProfilesAndArguments();
    void inspectMacBundle();
    void discoverSteamLibrary();
    void processFailureIsAsynchronous();
    void capturesOutputAndArguments();
    void destroyingLauncherKeepsChildAlive();
};

void GameLauncherTests::invalidDirectory() {
    QVERIFY(!GameLauncher::inspect(QString()).valid());
    QString error;
    QVERIFY(!GameLauncher::prepareLaunch({}, &error));
    QVERIFY(!error.isEmpty());
}

void GameLauncherTests::inspectAndPlan() {
    QTemporaryDir fixture(QDir::currentPath() + "/launcher-fixture-XXXXXX");
    QVERIFY(fixture.isValid());
    QVERIFY(executable(fixture.filePath(vanillaName())));
    QVERIFY(executable(fixture.filePath(smapiName())));
    QVERIFY(QDir().mkpath(fixture.filePath("profile/Mods")));
    const auto installation = GameLauncher::inspect(fixture.path());
    QVERIFY(installation.valid());
    QVERIFY(installation.hasSmapi());
    LaunchRequest request{installation, true, fixture.filePath("profile/Mods"), {"--test", "a b"}};
    QString error;
    auto plan = GameLauncher::prepareLaunch(request, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->program, installation.smapiExecutable);
    QCOMPARE(plan->workingDirectory, QFileInfo(fixture.path()).canonicalFilePath());
    QVERIFY(plan->arguments.contains("a b"));
    QCOMPARE(plan->arguments.last(), QFileInfo(request.modsDirectory).canonicalFilePath());
    QCOMPARE(plan->environment.value("SMAPI_MODS_PATH"), plan->arguments.last());
    request.useSmapi = false;
    plan = GameLauncher::prepareLaunch(request, &error);
    QVERIFY(plan.has_value());
    QCOMPARE(plan->program, installation.vanillaExecutable);
    QVERIFY(!plan->arguments.contains("--mods-path"));
}

void GameLauncherTests::bundleEnvironmentDoesNotLeakIntoGame() {
    const QStringList names{"LD_LIBRARY_PATH",
                            "QT_PLUGIN_PATH",
                            "QT_QPA_PLATFORM_PLUGIN_PATH",
                            "QML2_IMPORT_PATH",
                            "PRISMATIC_BUNDLED_ENV",
                            "PRISMATIC_ORIGINAL_LD_LIBRARY_PATH",
                            "PRISMATIC_ORIGINAL_QT_PLUGIN_PATH",
                            "PRISMATIC_ORIGINAL_QT_QPA_PLATFORM_PLUGIN_PATH",
                            "PRISMATIC_ORIGINAL_QML2_IMPORT_PATH"};
    struct Restore {
        QMap<QByteArray, QByteArray> values;
        ~Restore() {
            for (auto it = values.cbegin(); it != values.cend(); ++it) {
                if (it.value().isNull())
                    qunsetenv(it.key().constData());
                else
                    qputenv(it.key().constData(), it.value());
            }
        }
    } restore;
    for (const auto& name : names) restore.values.insert(name.toUtf8(), qgetenv(qPrintable(name)));
    qputenv("PRISMATIC_BUNDLED_ENV", "1");
    qputenv("LD_LIBRARY_PATH", "/bundle/usr/lib:/user/lib");
    qputenv("PRISMATIC_ORIGINAL_LD_LIBRARY_PATH", "/user/lib");
    qputenv("QT_PLUGIN_PATH", "/bundle/usr/plugins");
    qputenv("PRISMATIC_ORIGINAL_QT_PLUGIN_PATH", "");
    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", "/bundle/usr/plugins/platforms");
    qputenv("PRISMATIC_ORIGINAL_QT_QPA_PLATFORM_PLUGIN_PATH", "");
    qputenv("QML2_IMPORT_PATH", "/bundle/usr/qml");
    qputenv("PRISMATIC_ORIGINAL_QML2_IMPORT_PATH", "");
    QTemporaryDir fixture(QDir::currentPath() + "/launcher-fixture-XXXXXX");
    QVERIFY(fixture.isValid());
    QVERIFY(executable(fixture.filePath(vanillaName())));
    auto plan = GameLauncher::prepareLaunch({GameLauncher::inspect(fixture.path()), false, {}, {}});
    QVERIFY(plan.has_value());
    QCOMPARE(plan->environment.value("LD_LIBRARY_PATH"), QString("/user/lib"));
    QVERIFY(!plan->environment.contains("QT_PLUGIN_PATH"));
    QVERIFY(!plan->environment.contains("QT_QPA_PLATFORM_PLUGIN_PATH"));
    QVERIFY(!plan->environment.contains("QML2_IMPORT_PATH"));
    QVERIFY(!plan->environment.contains("PRISMATIC_BUNDLED_ENV"));
    QVERIFY(!plan->environment.contains("PRISMATIC_ORIGINAL_LD_LIBRARY_PATH"));
}

void GameLauncherTests::nativeWrapperNeverUsedForVanilla() {
#ifdef Q_OS_WIN
    QSKIP("The replaced launcher wrapper is specific to Unix.");
#else
    QTemporaryDir fixture(QDir::currentPath() + "/launcher-fixture-XXXXXX");
    QVERIFY(fixture.isValid());
    QVERIFY(executable(fixture.filePath("StardewValley")));
    QVERIFY(executable(fixture.filePath("StardewValley-original")));
    QVERIFY(executable(fixture.filePath("StardewModdingAPI")));
    auto installation = GameLauncher::inspect(fixture.path());
    QCOMPARE(installation.vanillaExecutable, fixture.filePath("StardewValley-original"));
    QVERIFY(QFile::remove(fixture.filePath("StardewValley-original")));
    installation = GameLauncher::inspect(fixture.path());
    QVERIFY(!installation.valid());
    QVERIFY(installation.hasSmapi());
    QVERIFY(!GameLauncher::prepareLaunch({installation, false, {}, {}}));
    QVERIFY(QFile::remove(fixture.filePath("StardewModdingAPI")));
    // Older Mono-based SMAPI installations use an .exe even on Unix. A
    // missing original-script backup must not turn that wrapper into vanilla.
    QVERIFY(executable(fixture.filePath("StardewModdingAPI.exe")));
    installation = GameLauncher::inspect(fixture.path());
    QVERIFY(!installation.valid());
    QVERIFY(installation.hasSmapi());
#endif
}

void GameLauncherTests::rejectUnsafeProfilesAndArguments() {
    QTemporaryDir fixture(QDir::currentPath() + "/launcher-fixture-XXXXXX");
    QVERIFY(fixture.isValid());
    QVERIFY(executable(fixture.filePath(vanillaName())));
    QVERIFY(executable(fixture.filePath(smapiName())));
    QVERIFY(QDir().mkpath(fixture.filePath("Mods")));
    QVERIFY(QDir().mkpath(fixture.filePath("profile/Mods")));
    auto installation = GameLauncher::inspect(fixture.path());
    LaunchRequest request{installation, true, fixture.filePath("profile/Mods"), {}};
    const QStringList hostile{"--mods-path", "--mods-path=elsewhere", "--MODS-PATH",
                              QString::fromLatin1("a\0b", 3)};
    for (const auto& argument : hostile) {
        request.arguments = {argument};
        QVERIFY(!GameLauncher::prepareLaunch(request));
    }
    request.arguments.clear();
    request.modsDirectory = "relative/Mods";
    QVERIFY(!GameLauncher::prepareLaunch(request));
    request.modsDirectory = fixture.filePath("Mods");
    QVERIFY(!GameLauncher::prepareLaunch(request));
    request.modsDirectory = fixture.path();
    QVERIFY(!GameLauncher::prepareLaunch(request));
    request.modsDirectory = fixture.filePath("profile/../profile/Mods");
    QVERIFY(!GameLauncher::prepareLaunch(request));
#ifndef Q_OS_WIN
    QVERIFY(QFile::link(fixture.filePath("Mods"), fixture.filePath("profile/link")));
    request.modsDirectory = fixture.filePath("profile/link");
    QVERIFY(!GameLauncher::prepareLaunch(request));
    QVERIFY(QDir().mkpath(fixture.filePath("outside/Mods")));
    QVERIFY(QFile::link(fixture.filePath("outside"), fixture.filePath("profile/ancestor")));
    request.modsDirectory = fixture.filePath("profile/ancestor/Mods");
    QVERIFY(!GameLauncher::prepareLaunch(request));
#endif
}

void GameLauncherTests::inspectMacBundle() {
#ifdef Q_OS_MACOS
    QTemporaryDir fixture(QDir::currentPath() + "/launcher-fixture-XXXXXX");
    QVERIFY(fixture.isValid());
    QVERIFY(QDir().mkpath(fixture.filePath("Stardew Valley.app/Contents/MacOS")));
    const auto game = fixture.filePath("Stardew Valley.app/Contents/MacOS");
    QVERIFY(executable(game + "/StardewValley"));
    QCOMPARE(GameLauncher::inspect(fixture.path()).gameDirectory, game);
#else
    QSKIP("Native macOS bundle normalization is specific to macOS.");
#endif
}

void GameLauncherTests::discoverSteamLibrary() {
#if defined(Q_OS_LINUX)
    QTemporaryDir fixture(QDir::currentPath() + "/launcher-fixture-XXXXXX");
    QVERIFY(fixture.isValid());
    struct RestoreEnvironment {
        QByteArray home = qgetenv("HOME");
        QByteArray data = qgetenv("XDG_DATA_HOME");
        ~RestoreEnvironment() {
            if (home.isNull())
                qunsetenv("HOME");
            else
                qputenv("HOME", home);
            if (data.isNull())
                qunsetenv("XDG_DATA_HOME");
            else
                qputenv("XDG_DATA_HOME", data);
        }
    } restore;
    qputenv("HOME", fixture.path().toUtf8());
    qputenv("XDG_DATA_HOME", fixture.filePath(".local/share").toUtf8());
    // Ensure discovery cannot inspect any actual user installation.
    QCOMPARE(QDir::homePath(), fixture.path());
    const auto steam = fixture.filePath(".local/share/Steam");
    const auto game = fixture.filePath("external library/steamapps/common/Stardew Valley");
    QVERIFY(QDir().mkpath(steam + "/steamapps"));
    QVERIFY(QDir().mkpath(game));
    QVERIFY(executable(game + '/' + vanillaName()));
    QFile libraries(steam + "/steamapps/libraryfolders.vdf");
    QVERIFY(libraries.open(QIODevice::WriteOnly));
    libraries.write("\"libraryfolders\" { \"1\" { \"path\" \"" +
                    fixture.filePath("external library").toUtf8() + "\" } }");
    libraries.close();
    const auto found = GameLauncher::discover();
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().gameDirectory, game);
#else
    QSKIP("Steam library fixture currently targets Linux environment conventions.");
#endif
}

void GameLauncherTests::processFailureIsAsynchronous() {
    QTemporaryDir fixture(QDir::currentPath() + "/launcher-fixture-XXXXXX");
    QVERIFY(fixture.isValid());
    // Deliberately not a valid binary: no downloaded/game/mod executable runs.
    QVERIFY(executable(fixture.filePath(vanillaName())));
    GameLauncher launcher;
    QSignalSpy errors(&launcher, &GameLauncher::error);
    QVERIFY(launcher.start({GameLauncher::inspect(fixture.path()), false, {}, {}}));
    QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
    QVERIFY(!launcher.running());
}

void GameLauncherTests::capturesOutputAndArguments() {
    QTemporaryDir fixture(QDir::currentPath() + "/launcher-fixture-XXXXXX");
    QVERIFY(fixture.isValid());
    // Execute only our own trusted test helper, never a game or mod binary.
    QFile helper(QCoreApplication::applicationFilePath());
    QVERIFY2(helper.open(QIODevice::ReadOnly), qPrintable(helper.errorString()));
    QVERIFY(executable(fixture.filePath(vanillaName()), helper.readAll()));
    GameLauncher launcher;
    QSignalSpy output(&launcher, &GameLauncher::output);
    QSignalSpy finished(&launcher, &GameLauncher::finished);
    const QString literalArgument = "space and ; $(never execute)";
    QVERIFY(launcher.start({GameLauncher::inspect(fixture.path()),
                            false,
                            {},
                            {"--prismatic-fixture-child", literalArgument}}));
    QVERIFY(!launcher.start({GameLauncher::inspect(fixture.path()), false, {}, {}}));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
    QCOMPARE(finished.first().first().toInt(), 0);
    QString text;
    for (const auto& event : output) text += event.first().toString();
    QVERIFY(text.contains(literalArgument));
    QVERIFY(text.contains(fixture.path()));
    QVERIFY(text.contains("fixture stderr"));
    QVERIFY(!launcher.running());
}

void GameLauncherTests::destroyingLauncherKeepsChildAlive() {
    QTemporaryDir fixture(QDir::currentPath() + "/launcher-fixture-XXXXXX");
    QVERIFY(fixture.isValid());
    QFile helper(QCoreApplication::applicationFilePath());
    QVERIFY2(helper.open(QIODevice::ReadOnly), qPrintable(helper.errorString()));
    QVERIFY(executable(fixture.filePath(vanillaName()), helper.readAll()));
    auto* launcher = new GameLauncher;
    QSignalSpy started(launcher, &GameLauncher::started);
    QVERIFY(launcher->start({GameLauncher::inspect(fixture.path()),
                             false,
                             {},
                             {"--prismatic-fixture-child", "survived"}}));
    QTRY_COMPARE_WITH_TIMEOUT(started.size(), 1, 5000);
    delete launcher;
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(fixture.filePath("child-finished")), 5000);
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().value(1) == "--prismatic-fixture-child") {
        QFile output;
        output.open(stdout, QIODevice::WriteOnly);
        output.write(QDir::currentPath().toUtf8() + '\n');
        output.write(application.arguments().value(2).toUtf8() + '\n');
        output.flush();
        QFile errors;
        errors.open(stderr, QIODevice::WriteOnly);
        errors.write("fixture stderr\n");
        errors.flush();
        QThread::msleep(150);
        QFile marker("child-finished");
        return marker.open(QIODevice::WriteOnly) ? 0 : 1;
    }
    GameLauncherTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "GameLauncherTests.moc"
