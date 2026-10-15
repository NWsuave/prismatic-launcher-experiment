#ifndef SMAPI_INSTALLER
#define SMAPI_INSTALLER

#include <QString>
#include <QStringList>
#include <optional>

/*
 * InstallStatus
 * provides enums for status of the installation process
 */
enum class InstallStatus {
    Success,
    InvalidGameDirectory,
    InvalidInstaller,
    ProcessStartFailed,
    InstallerFailed,
    VerificationFailed
};

/*
 * InstallResult
 * keeps track of the isntallation result
 */
struct InstallResult {
    InstallStatus status;
    QString message;
    int exitCode = -1;
    QString standardOutput;
    QString standardError;

    /*
     * Compares the status of the installation to success
     * @return if the installation succeeded
     */
    [[nodiscard]] bool succeeded() const { return status == InstallStatus::Success; }
};

/*
 * SmapiInstaller
 * handles SMAPI installation process and verifying installation
 */
class SmapiInstaller {
   public:
    [[nodiscard]] bool isInstalled(const QString& gameDirectory) const;
    [[nodiscard]] InstallResult install(const QString& gameDirectory,
                                        const QString& installerPath) const;
};

#endif