#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace prismatic {

struct ModDependency {
    QString uniqueId;
    QString minimumVersion;
    bool isRequired = true;
};

struct ModManifest {
    QString name;
    QString author;
    QString version;
    QString uniqueId;
    QString description;
    QString minimumApiVersion;
    QString minimumGameVersion;
    QList<ModDependency> dependencies;
    std::optional<ModDependency> contentPackFor;
    QStringList updateKeys;
};

struct InstalledMod {
    ModManifest manifest;
    QString directory;
    bool enabled = true;
};

// Synchronous filesystem service. Run mutating operations on a worker thread.
// A profile owns Mods/ and DisabledMods/; it never owns the game's Mods folder.
class ModManager {
   public:
    explicit ModManager(QString profileRoot);
    static std::optional<ModManifest> parseManifest(const QString& path, QString* error = nullptr);
    QList<InstalledMod> mods(QString* error = nullptr) const;
    bool installFolder(const QString& source, QString* error = nullptr);
    bool installZip(const QString& source, QString* error = nullptr);
    // A downloaded dependency must contain exactly the approved mod/version.
    // Existing IDs are never replaced, even after approval.
    bool installDependencyZip(const QString& source, const QString& expectedId,
                              const QString& expectedVersion, QString* error = nullptr);
    bool setEnabled(const QString& uniqueId, bool enabled, QString* error = nullptr);
    // Call only after the user explicitly confirms removal.
    bool remove(const QString& uniqueId, QString* error = nullptr);

   private:
    QString root_;
    bool installStaged(const QString& staging, bool& preserveStaging, QString* error);
};

}  // namespace prismatic
