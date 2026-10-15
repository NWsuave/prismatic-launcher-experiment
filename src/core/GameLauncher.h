#pragma once

#include <QList>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <optional>

class QProcess;

namespace prismatic {

struct GameInstallation {
    QString gameDirectory;
    QString vanillaExecutable;
    QString smapiExecutable;
    QStringList diagnostics;

    [[nodiscard]] bool valid() const { return !vanillaExecutable.isEmpty(); }
    [[nodiscard]] bool hasSmapi() const { return !smapiExecutable.isEmpty(); }
};

struct LaunchRequest {
    GameInstallation installation;
    bool useSmapi = false;
    QString modsDirectory;
    QStringList arguments;
};

struct LaunchPlan {
    QString program;
    QStringList arguments;
    QString workingDirectory;
    QProcessEnvironment environment;
};

class GameLauncher final : public QObject {
    Q_OBJECT

   public:
    explicit GameLauncher(QObject* parent = nullptr);
    ~GameLauncher() override;

    [[nodiscard]] static GameInstallation inspect(const QString& directory);
    [[nodiscard]] static QList<GameInstallation> discover();
    // Re-inspects the installation and returns only absolute executable paths.
    // A modded launch requires an existing, separate absolute Mods directory.
    [[nodiscard]] static std::optional<LaunchPlan> prepareLaunch(const LaunchRequest& request,
                                                                 QString* error = nullptr);

    // Reports dispatch success; started/error signals report OS launch outcome.
    bool start(const LaunchRequest& request);
    [[nodiscard]] bool running() const;

   signals:
    void started();
    void output(const QString& text);
    void error(const QString& message);
    void finished(int exitCode);

   private:
    QProcess* process_ = nullptr;
};

}  // namespace prismatic
