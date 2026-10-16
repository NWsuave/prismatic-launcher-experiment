#pragma once

#include <QList>
#include <QStringList>

#include "ModManager.h"

namespace prismatic {

struct ModInstallation {
    QString profileId;
    QString profileName;
    QString profileRoot;
    InstalledMod mod;
};

struct InventoryMod {
    QString uniqueId;
    QList<ModInstallation> installations;
    QStringList versions() const;
};

struct ModInventorySnapshot {
    QList<InventoryMod> mods;
    QStringList warnings;
    QStringList watchedPaths;
};

enum class InstallationFilter { All, Enabled, Disabled };

// A read-only index. Every installation still belongs to one isolated profile.
class ModInventory {
   public:
    static ModInventorySnapshot scan(const QString& dataRoot);
    // State filters retain only matching installations, so actions always identify a profile.
    static QList<InventoryMod> filtered(const QList<InventoryMod>& mods, const QString& query,
                                        InstallationFilter filter = InstallationFilter::All);
};

}  // namespace prismatic
