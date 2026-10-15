
#include "SmapiInstaller.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <optional>

struct InstallerCommand {
    QString program;
    QStringList arguments;
};

/*
 * Resolves the installer command for different platforms
 * @param the installer directory
 * @param error message
 * @return the installer command
 */
std::optional<InstallerCommand> resolveInstallerCommand(const QString& installerDirectory,
                                                        QString* errorMessage) {
    const QDir packageDir(installerDirectory);

    if (!packageDir.exists()) {
        *errorMessage = "The SMAPI installer directory does not exist.";
        return std::nullopt;
    }

// For Windows
#if defined(Q_OS_WIN)
    const QString launcher = packageDir.filePath("install on Windows.exe");

    if (!QFileInfo::exists(launcher)) {
        *errorMessage = "This folder does not contain the Windows SMAPI installer.";
        return std::nullopt;
    }

    return InstallerCommand{.program = launcher, .arguments = {}};
// For MACOS
#elif defined(Q_OS_MACOS)
    const QString launcher = packageDir.filePath("install on macOS.command");

    if (!QFileInfo(launcher).isFile()) {
        *errorMessage = "This folder does not contain the macOS SMAPI installer.";
        return std::nullopt;
    }

    return InstallerCommand{.program = "/bin/sh", .arguments = {launcher}};
// For Linux
#else
    const QString launcher = packageDir.filePath("install on Linux.sh");

    if (!QFileInfo(launcher).isFile()) {
        *errorMessage = "This folder does not contain the Linux SMAPI installer.";
        return std::nullopt;
    }

    return InstallerCommand{.program = "/bin/sh", .arguments = {launcher}};
#endif
}

/*
 * Installs SMAPI using installers
 * @param the game directory
 * @param the location of the installer
 */
InstallResult SmapiInstaller::install(const QString& gameDirectory,
                                      const QString& installerPath) const {
    const QDir gameDir(gameDirectory);

    // Validate game directory
    if (!gameDir.exists()) {
        return {.status = InstallStatus::InvalidGameDirectory,
                .message = "The Stardew Valley game directory does not exist."};
    }

    // Resolve Command
    QString resolverError;
    const auto command = resolveInstallerCommand(installerPath, &resolverError);

    if (!command) {
        return {.status = InstallStatus::InvalidInstaller, .message = resolverError};
    }

    // Command arguments
    QStringList arguments = command->arguments;
    arguments << "--no-prompt" << "--install" << "--game-path" << gameDir.absolutePath();

    QProcess process;
    process.setWorkingDirectory(QDir(installerPath).absolutePath());
    process.setProgram(command->program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();

    if (!process.waitForStarted()) {
        return {.status = InstallStatus::ProcessStartFailed,
                .message = process.errorString(),
                .standardError = QString::fromUtf8(process.readAllStandardError())};
    }

    process.waitForFinished(-1);

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    const QString error = QString::fromUtf8(process.readAllStandardError());

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {.status = InstallStatus::InstallerFailed,
                .message = "The SMAPI installer did not complete successfully.",
                .exitCode = process.exitCode(),
                .standardOutput = output,
                .standardError = error};
    }

    if (!isInstalled(gameDir.absolutePath())) {
        return {.status = InstallStatus::VerificationFailed,
                .message = "SMAPI finished, but its installed files could not be verified.",
                .exitCode = process.exitCode(),
                .standardOutput = output,
                .standardError = error};
    }

    return {.status = InstallStatus::Success,
            .message = "SMAPI installed successfully.",
            .exitCode = process.exitCode(),
            .standardOutput = output,
            .standardError = error};
}

/*
 * this namsepace uses QDir functions to determine if a directory and file exist
 * used in the isInstalled function
 */
namespace {
bool hasFile(const QDir& directory, const QString& name) {
    return QFileInfo(directory.filePath(name)).isFile();
}

bool hasDirectory(const QDir& directory, const QString& name) {
    return QFileInfo(directory.filePath(name)).isDir();
}
}  // namespace

/*
 * Checks if SMAPI is already installed
 * @param the game directory, expects directory to be containing the game launcher
 * @return If SMAPI is installed
 */
bool SmapiInstaller::isInstalled(const QString& gameDirectory) const {
    const QDir gameDir(gameDirectory);

    if (!gameDir.exists()) {
        return false;
    }

    // checks for metadata and smapi-internal
    const bool hasMetadata = hasFile(gameDir, "StardewModdingAPI.metadata.json");
    const bool hasInternalFiles = hasDirectory(gameDir, "smapi-internal");

// Checks if the operating system is Windows
#if defined(Q_OS_WIN)
    return hasFile(gameDir, "StardewModdingAPI.exe") && hasMetadata && hasInternalFiles;
#else
    // SMAPI on Linux/macOS preserves the original launcher and replaces StardewValley with its
    // launcher.
    return hasFile(gameDir, "StardewModdingAPI") && hasFile(gameDir, "StardewValley") &&
           hasFile(gameDir, "StardewValley-original") && hasMetadata && hasInternalFiles;
#endif
}
