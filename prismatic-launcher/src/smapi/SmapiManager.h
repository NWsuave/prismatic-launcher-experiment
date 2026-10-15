
#ifndef SMAPI_MANAGER
#define SMAPI_MANAGER

#include <QString>
#include <memory>

#include "SmapiInstaller.h"
#include "SmapiLauncher.h"

enum class LaunchStatus { Success, SmapiNotInstalled, LaunchFailed, UnsupportedPlatform };

/*
 * Describes the result of launching SMAPI
 */
struct LaunchResult {
    LaunchStatus status;
    QString message;

    /*
     * Returns if the launch has succeeded
     */
    [[nodiscard]] bool succeeded() const { return status == LaunchStatus::Success; }
};

/*
 * Manages SMAPI installation and launching by using the respective classes
 */
class SmapiManager {
   public:
    SmapiManager(std::unique_ptr<SmapiInstaller> installer,
                 std::unique_ptr<SmapiLauncher> launcher);

    [[nodiscard]] bool isInstalled(const QString& gameDirectory) const;

    [[nodiscard]] InstallResult install(const QString& gameDirectory,
                                        const QString& installerDirectory) const;
    [[nodiscard]] LaunchResult launch(const QString& gameDirectory,
                                      const std::wstring& arguments = L"") const;

   private:
    std::unique_ptr<SmapiInstaller> installer_;
    std::unique_ptr<SmapiLauncher> launcher_;
};

#endif