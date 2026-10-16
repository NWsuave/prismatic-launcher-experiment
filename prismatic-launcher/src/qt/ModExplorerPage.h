#pragma once

#include <QWidget>
#include <functional>

#include "ModInventory.h"

class QComboBox;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;

class ModExplorerPage : public QWidget {
    Q_OBJECT
   public:
    explicit ModExplorerPage(QString dataRoot, QWidget* parent = nullptr, bool autoRefresh = true);
    ~ModExplorerPage() override;
    void setMutationAllowed(bool allowed);
    void setEnableCheck(std::function<bool(const QString&, const QString&)> check);
    bool isBusy() const;

   public slots:
    void refresh();

   signals:
    void installationsChanged();
    void errorOccurred(const QString& message);
    void busyChanged(bool busy);
    void dependencyInspectionRequested(const QString& profileId, const QString& uniqueId);

   private:
    void renderMods();
    void renderInstallations();
    void renderDetails();
    void updateActions();
    void changeSelected(bool remove);
    void setBusy(bool busy);
    void watchDirectories();
    std::optional<prismatic::ModInstallation> selectedInstallation() const;

    QString dataRoot_;
    prismatic::ModInventorySnapshot snapshot_;
    QList<prismatic::InventoryMod> visible_;
    bool busy_ = false;
    bool initialRefreshPending_ = true;
    bool mutationAllowed_ = true;
    bool refreshPending_ = false;
    std::function<bool(const QString&, const QString&)> enableCheck_;
    QLineEdit* search_ = nullptr;
    QComboBox* filter_ = nullptr;
    QTableWidget* mods_ = nullptr;
    QTableWidget* installations_ = nullptr;
    QPlainTextEdit* details_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* toggle_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* dependencies_ = nullptr;
    QPushButton* refresh_ = nullptr;
    QFileSystemWatcher* watcher_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
};
