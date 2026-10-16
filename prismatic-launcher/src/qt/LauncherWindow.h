#pragma once

#include <QMainWindow>
#include <QStringList>
#include <functional>
#include <memory>

#include "InstanceStore.h"
#include "LauncherConfig.h"
#include "ModManager.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QPlainTextEdit;
class QComboBox;
class QTableWidget;
class QProgressBar;
class QCloseEvent;
class ModExplorerPage;
class ThemeManager;
namespace prismatic {
class GameLauncher;
}

class LauncherWindow : public QMainWindow {
   public:
    explicit LauncherWindow(const QString& configFile, QWidget* parent = nullptr,
                            const QString& dataDirectory = {});
    ~LauncherWindow() override;

   protected:
    void closeEvent(QCloseEvent* event) override;

   private:
    QWidget* createLibrary();
    QWidget* createDetails();
    QWidget* createModsPage();
    QWidget* createLogsPage();
    QWidget* createSettingsPage();
    QWidget* createModpacksPage();
    void addInstance();
    void renameInstance();
    void duplicateInstance();
    void removeInstance();
    void reloadInstances();
    std::optional<prismatic::Instance> selectedInstance() const;
    void openInstance(int tab = 0);
    void launchGame();
    void saveSettings();
    void refreshPathStatus();
    void filterInstances(const QString& query);
    void refreshMods();
    void installMods(bool archive);
    void toggleMod();
    void removeMod();
    void runTask(const QString& description, std::function<QString()> operation,
                 std::function<void()> complete = {});
    bool canChange();
    void updateControls();
    void appendLog(const QString& message);
    void renderLogs();
    QString redacted(const QString& text) const;
    void reportError(const QString& message);
    void inspectDependencies(const QString& profileId);
    bool confirmDependencies(const QString& profileId, const QString& enablingId = {});

    LauncherConfig config_;
    QString dataDirectory_;
    std::unique_ptr<prismatic::InstanceStore> store_;
    prismatic::GameLauncher* launcher_ = nullptr;
    std::unique_ptr<ThemeManager> themes_;
    ModExplorerPage* explorer_ = nullptr;
    bool explorerActivated_ = false;
    bool busy_ = false;
    bool refreshing_ = false;
    QStringList logLines_;
    QString dependencyPromptProfileId_;
    QList<prismatic::InstalledMod> installedMods_;
    QStackedWidget* pages_ = nullptr;
    QStackedWidget* detailPages_ = nullptr;
    QListWidget* instances_ = nullptr;
    QListWidget* navigation_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLineEdit* gamePath_ = nullptr;
    QPlainTextEdit* arguments_ = nullptr;
    QLineEdit* logFilter_ = nullptr;
    QLabel* detailTitle_ = nullptr;
    QLabel* pathStatus_ = nullptr;
    QLabel* instanceCount_ = nullptr;
    QLabel* modStatus_ = nullptr;
    QPlainTextEdit* logs_ = nullptr;
    QComboBox* launchMode_ = nullptr;
    QTableWidget* mods_ = nullptr;
    QProgressBar* progress_ = nullptr;
};
