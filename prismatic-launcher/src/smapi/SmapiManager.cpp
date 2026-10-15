
#include "SmapiManager.h"

/*
 * Installs SMAPI using the attached installer
 * @param the game directory
 * @param installer directory
 * @returns the result of the installation
 */
InstallResult SmapiManager::install(const QString& gameDirectory,
                                    const QString& installerDirectory) const {
    return installer_->install(gameDirectory, installerDirectory);
}

/*
 * Verfies the installation of SMAPI using attached launcher
 * @param the game directory
 * @returns whether SMAPI is intalled
 */
bool SmapiManager::isInstalled(const QString& gameDirectory) const {
    return installer_->isInstalled(gameDirectory);
}

/*
 * Launches the game using the attached launcher
 * @param the game directory
 * @returns the result of the launch
 */
LaunchResult SmapiManager::launch(const QString& gameDirectory,
                                  const std::wstring& arguments) const {
    if (!installer_->isInstalled(gameDirectory)) {
        return {.status = LaunchStatus::SmapiNotInstalled,
                .message = "SMAPI is not installed or its installation is incomplete."};
    }

#ifdef _WIN32
    const bool launched =
        launcher_->launch(std::filesystem::path(gameDirectory.toStdWString()), arguments);

    return launched ? LaunchResult{.status = LaunchStatus::Success,
                                   .message = "Stardew Valley started through SMAPI."}
                    : LaunchResult{.status = LaunchStatus::LaunchFailed,
                                   .message = "Unable to start Stardew Valley through SMAPI."};
#else
    return {.status = LaunchStatus::UnsupportedPlatform,
            .message = "SMAPI launching is not implemented for this platform yet."};
#endif
}