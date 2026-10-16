#include "GameLauncher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <memory>

namespace prismatic {
namespace {
constexpr qint64 kOutputChunk = 16384;

QString executableIn(const QString& directory, const QStringList& names) {
    for (const auto& name : names) {
        const QFileInfo candidate(QDir(directory).filePath(name));
        if (candidate.isFile() && candidate.isReadable()
#ifndef Q_OS_WIN
            && candidate.isExecutable()
#endif
        ) {
            return candidate.absoluteFilePath();
        }
    }
    return {};
}

bool pathInside(const QString& path, const QString& parent) {
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    const auto prefix = parent.endsWith('/') ? parent : parent + '/';
    return path.compare(parent, sensitivity) == 0 || path.startsWith(prefix, sensitivity);
}

bool hasSymlinkAncestor(const QString& path) {
    if (QDir::fromNativeSeparators(path).split('/').contains("..")) return true;
    auto current = QFileInfo(path).absoluteFilePath();
    for (;;) {
        const QFileInfo entry(current);
        if (entry.isSymLink()
#ifdef Q_OS_WIN
            || entry.isJunction()
#endif
        )
            return true;
        const auto parent = entry.absolutePath();
        if (parent == current) return false;
        current = parent;
    }
}

QString normalizedGameDirectory(const QString& directory) {
    if (directory.trimmed().isEmpty() || directory.contains(QChar::Null)) return {};
    QFileInfo selected(directory);
    if (!selected.isDir()) return {};
    QString path = selected.canonicalFilePath();
#ifdef Q_OS_MACOS
    const QStringList locations{"Contents/MacOS", "Stardew Valley.app/Contents/MacOS"};
    for (const auto& relative : locations) {
        const QFileInfo nested(QDir(path).filePath(relative));
        if (nested.isDir()) return nested.canonicalFilePath();
    }
#endif
    return path;
}
}  // namespace

GameInstallation GameLauncher::inspect(const QString& directory) {
    GameInstallation result;
    result.gameDirectory = normalizedGameDirectory(directory);
    if (result.gameDirectory.isEmpty()) {
        result.diagnostics << QStringLiteral("Choose an existing Stardew Valley game directory.");
        return result;
    }
#ifdef Q_OS_WIN
    result.vanillaExecutable = executableIn(result.gameDirectory, {"Stardew Valley.exe"});
    result.smapiExecutable = executableIn(result.gameDirectory, {"StardewModdingAPI.exe"});
#else
    result.smapiExecutable = executableIn(result.gameDirectory, {"StardewModdingAPI"});
    // SMAPI replaces the StardewValley script. Prefer the actual native apphost
    // or the installer's original-script backup for a guaranteed vanilla launch.
    result.vanillaExecutable =
        executableIn(result.gameDirectory, {"StardewValley-original", "Stardew Valley"});
    const auto wrapper = executableIn(result.gameDirectory, {"StardewValley"});
    const bool installedWrapper =
        QFileInfo::exists(QDir(result.gameDirectory).filePath("StardewValley-original")) ||
        QFileInfo::exists(QDir(result.gameDirectory).filePath("StardewModdingAPI.dll")) ||
        QFileInfo::exists(QDir(result.gameDirectory).filePath("StardewModdingAPI.exe")) ||
        QFileInfo(QDir(result.gameDirectory).filePath("smapi-internal")).isDir();
    if (result.smapiExecutable.isEmpty() && installedWrapper) result.smapiExecutable = wrapper;
    if (result.vanillaExecutable.isEmpty() && result.smapiExecutable.isEmpty() && !installedWrapper)
        result.vanillaExecutable = wrapper;
#endif
    if (!result.valid()) {
        result.diagnostics << QStringLiteral(
            "No executable vanilla game was found. Check the directory and executable permissions; "
            "a replaced SMAPI launcher cannot be used as vanilla.");
    }
    if (!result.hasSmapi()) {
        result.diagnostics << QStringLiteral(
            "SMAPI was not detected. Install SMAPI separately to launch this profile's mods.");
    }
    return result;
}

QList<GameInstallation> GameLauncher::discover() {
    QStringList roots;
    const auto home = QDir::homePath();
#ifdef Q_OS_WIN
    for (const auto& variable : {"ProgramFiles(x86)", "ProgramFiles"}) {
        const auto base = qEnvironmentVariable(variable);
        if (!base.isEmpty()) roots << QDir(base).filePath("Steam");
    }
    const auto local = qEnvironmentVariable("LOCALAPPDATA");
    if (!local.isEmpty()) roots << QDir(local).filePath("Steam");
#elif defined(Q_OS_MACOS)
    roots << home + "/Library/Application Support/Steam";
#else
    roots << home + "/.local/share/Steam" << home + "/.steam/steam" << home + "/.steam/root"
          << home + "/.var/app/com.valvesoftware.Steam/data/Steam";
    const auto dataHome = qEnvironmentVariable("XDG_DATA_HOME");
    if (!dataHome.isEmpty()) roots << QDir(dataHome).filePath("Steam");
#endif
    QStringList libraries = roots;
    // Read only Steam's own bounded library metadata; never recursively search
    // the user's home directory or scan arbitrary mounted drives.
    const QRegularExpression libraryPath(QStringLiteral("\"path\"\\s*\"((?:\\\\.|[^\"\\\\])*)\""));
    for (const auto& root : roots) {
        QFile metadata(QDir(root).filePath("steamapps/libraryfolders.vdf"));
        if (!metadata.open(QIODevice::ReadOnly) || metadata.size() > 1024 * 1024) continue;
        const auto contents = QString::fromUtf8(metadata.readAll());
        auto matches = libraryPath.globalMatch(contents);
        while (matches.hasNext()) {
            auto path = matches.next().captured(1);
            path.replace("\\\\", "\\");
            path.replace("\\\"", "\"");
            if (QDir::isAbsolutePath(path)) libraries << path;
        }
    }
    QList<GameInstallation> found;
    QSet<QString> seen;
    for (const auto& library : libraries) {
        const auto installation =
            inspect(QDir(library).filePath("steamapps/common/Stardew Valley"));
        if ((!installation.valid() && !installation.hasSmapi()) ||
            seen.contains(installation.gameDirectory))
            continue;
        seen.insert(installation.gameDirectory);
        found << installation;
    }
    return found;
}

std::optional<LaunchPlan> GameLauncher::prepareLaunch(const LaunchRequest& request,
                                                      QString* error) {
    if (error) error->clear();
    const auto fail = [error](const QString& message) -> std::optional<LaunchPlan> {
        if (error) *error = message;
        return std::nullopt;
    };
    const auto installation = inspect(request.installation.gameDirectory);
    const auto program =
        request.useSmapi ? installation.smapiExecutable : installation.vanillaExecutable;
    if (program.isEmpty()) {
        return fail(request.useSmapi
                        ? QStringLiteral("SMAPI executable not found or not executable.")
                        : QStringLiteral("Vanilla game executable not found or not executable."));
    }
    if (request.arguments.size() > 256)
        return fail(QStringLiteral("Too many launch arguments (maximum 256)."));
    for (const auto& argument : request.arguments) {
        if (argument.contains(QChar::Null) || argument.size() > 32768)
            return fail(QStringLiteral("Launch arguments contain invalid or oversized text."));
        if (argument.compare("--mods-path", Qt::CaseInsensitive) == 0 ||
            argument.startsWith("--mods-path=", Qt::CaseInsensitive))
            return fail(QStringLiteral("The launcher manages --mods-path for profile isolation."));
    }
    LaunchPlan plan{program, request.arguments, installation.gameDirectory,
                    QProcessEnvironment::systemEnvironment()};
    // AppImage deployment paths belong to the launcher, not the external game.
    if (plan.environment.value("PRISMATIC_BUNDLED_ENV") == "1") {
        for (const auto* name : {"LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                                 "QML2_IMPORT_PATH"}) {
            const auto original = QString("PRISMATIC_ORIGINAL_") + name;
            const auto value = plan.environment.value(original);
            if (value.isEmpty())
                plan.environment.remove(name);
            else
                plan.environment.insert(name, value);
            plan.environment.remove(original);
        }
        plan.environment.remove("PRISMATIC_BUNDLED_ENV");
    }
    // Do not inherit a global override that could load another profile's mods.
    plan.environment.remove("SMAPI_MODS_PATH");
    if (request.useSmapi) {
        const QFileInfo mods(request.modsDirectory);
        if (!QDir::isAbsolutePath(request.modsDirectory) ||
            request.modsDirectory.contains(QChar::Null) || !mods.isDir() ||
            hasSymlinkAncestor(request.modsDirectory))
            return fail(QStringLiteral(
                "The profile Mods path must be an existing absolute directory, not a symlink."));
        const auto profilePath = mods.canonicalFilePath();
        const QFileInfo sharedMods(QDir(installation.gameDirectory).filePath("Mods"));
        const auto sharedPath =
            sharedMods.exists() ? sharedMods.canonicalFilePath() : sharedMods.absoluteFilePath();
        if (profilePath.isEmpty() || profilePath == installation.gameDirectory ||
            pathInside(profilePath, sharedPath) || pathInside(sharedPath, profilePath))
            return fail(QStringLiteral(
                "Use a separate profile Mods directory outside the game's shared Mods directory."));
        plan.arguments << "--mods-path" << profilePath;
        plan.environment.insert("SMAPI_MODS_PATH", profilePath);
        plan.environment.insert("SMAPI_USE_CURRENT_SHELL", "true");
    }
    return plan;
}

GameLauncher::GameLauncher(QObject* parent) : QObject(parent) {}

GameLauncher::~GameLauncher() {
    if (!process_) return;
    // QProcess's destructor kills its child. A window's lifetime must never
    // terminate a game: the parentless process instead deletes itself on exit.
    process_->disconnect(this);
    if (process_->state() == QProcess::NotRunning) process_->deleteLater();
}

bool GameLauncher::start(const LaunchRequest& request) {
    if (running()) {
        emit error(QStringLiteral("A game is already running from this launcher."));
        return false;
    }
    QString message;
    const auto plan = prepareLaunch(request, &message);
    if (!plan) {
        emit error(message);
        return false;
    }
    auto* child = new QProcess;
    process_ = child;
    child->setProcessChannelMode(QProcess::MergedChannels);
    child->setWorkingDirectory(plan->workingDirectory);
    child->setProcessEnvironment(plan->environment);
    connect(child, &QProcess::started, this, &GameLauncher::started);
    // Fixed-size reads bound individual GUI log events. Always drain output,
    // including after the launcher's window has been destroyed.
    connect(child, &QProcess::readyReadStandardOutput, child,
            [launcher = QPointer<GameLauncher>(this), child,
             decoder = std::make_shared<QStringDecoder>(QStringDecoder::Utf8)] {
                while (child->bytesAvailable() > 0) {
                    const auto bytes = child->read(kOutputChunk);
                    if (bytes.isEmpty()) break;
                    const QString decoded = (*decoder)(bytes);
                    if (launcher && !decoded.isEmpty()) emit launcher->output(decoded);
                }
            });
    connect(
        child, &QProcess::errorOccurred, child,
        [launcher = QPointer<GameLauncher>(this), child](QProcess::ProcessError failure) {
            if (launcher)
                emit launcher->error(QStringLiteral("Game process: %1").arg(child->errorString()));
            if (failure == QProcess::FailedToStart) {
                if (launcher && launcher->process_ == child) launcher->process_ = nullptr;
                child->deleteLater();
            }
        });
    connect(child, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, child](int exitCode, QProcess::ExitStatus) {
                if (process_ == child) process_ = nullptr;
                emit finished(exitCode);
            });
    connect(child, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), child,
            &QObject::deleteLater);
    child->start(plan->program, plan->arguments, QIODevice::ReadOnly);
    return true;
}

bool GameLauncher::running() const { return process_ && process_->state() != QProcess::NotRunning; }

}  // namespace prismatic
