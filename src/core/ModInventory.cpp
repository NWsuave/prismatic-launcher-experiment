#include "ModInventory.h"

#include <QDir>
#include <QLockFile>
#include <QMap>
#include <QSet>
#include <algorithm>

#include "AtomicFile.h"
#include "InstanceStore.h"

namespace prismatic {
QStringList InventoryMod::versions() const {
    QStringList result;
    for (const auto& installation : installations) result.append(installation.mod.manifest.version);
    result.removeDuplicates();
    result.sort(Qt::CaseInsensitive);
    return result;
}

ModInventorySnapshot ModInventory::scan(const QString& dataRoot) {
    ModInventorySnapshot result;
    InstanceStore store(dataRoot);
    QString error;
    if (!store.load(&error)) {
        result.warnings.append(error);
        return result;
    }
    const auto watch = [&result](const QString& path) {
        if (validateStoragePath(path) && QFileInfo::exists(path)) result.watchedPaths.append(path);
    };
    // Parent directories contain short-lived registry/profile lock files. Watching
    // those would make an inventory scan trigger another inventory scan forever.
    watch(dataRoot + "/instances.json");
    QMap<QString, InventoryMod> groups;
    for (const auto& profile : store.instances()) {
        const QString profileRoot = store.profilePath(profile.id);
        if (!validateStoragePath(profileRoot, &error) ||
            !validateStoragePath(profileRoot + ".mods.lock", &error)) {
            result.warnings.append(QStringLiteral("%1: %2").arg(profile.name, error));
            continue;
        }
        watch(profileRoot);
        QLockFile lock(profileRoot + ".mods.lock");
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0)) {
            result.warnings.append(
                QStringLiteral("%1: profile mods are busy; refresh to retry.").arg(profile.name));
            continue;
        }
        QSet<QString> ids;
        for (const auto& name : {QStringLiteral("Mods"), QStringLiteral("DisabledMods")}) {
            const QString directory = profileRoot + '/' + name;
            if (!validateStoragePath(directory, &error)) {
                result.warnings.append(QStringLiteral("%1: %2").arg(profile.name, error));
                continue;
            }
            watch(directory);
            const auto entries = QDir(directory).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot |
                                                               QDir::Hidden | QDir::System);
            for (const auto& entry : entries) {
                const QString path = entry.absoluteFilePath() + "/manifest.json";
                watch(path);
                const auto manifest = ModManager::parseManifest(path, &error);
                if (!manifest) {
                    result.warnings.append(
                        QStringLiteral("%1 / %2: %3").arg(profile.name, entry.fileName(), error));
                    continue;
                }
                const QString id = manifest->uniqueId.toCaseFolded();
                if (ids.contains(id))
                    result.warnings.append(
                        QStringLiteral("%1: duplicate installed mod ID %2; repair this profile "
                                       "before changing mods.")
                            .arg(profile.name, manifest->uniqueId));
                ids.insert(id);
                auto& group = groups[id];
                if (group.uniqueId.isEmpty()) group.uniqueId = manifest->uniqueId;
                group.installations.append({profile.id,
                                            profile.name,
                                            profileRoot,
                                            {*manifest, entry.absoluteFilePath(), name == "Mods"}});
            }
        }
    }
    result.mods = groups.values();
    std::sort(result.mods.begin(), result.mods.end(), [](const auto& left, const auto& right) {
        return QString::compare(left.installations.first().mod.manifest.name,
                                right.installations.first().mod.manifest.name,
                                Qt::CaseInsensitive) < 0;
    });
    return result;
}

QList<InventoryMod> ModInventory::filtered(const QList<InventoryMod>& mods, const QString& query,
                                           InstallationFilter filter) {
    QList<InventoryMod> result;
    const QString needle = query.trimmed();
    for (const auto& mod : mods) {
        InventoryMod group{mod.uniqueId, {}};
        for (const auto& installation : mod.installations) {
            if (filter == InstallationFilter::Enabled && !installation.mod.enabled) continue;
            if (filter == InstallationFilter::Disabled && installation.mod.enabled) continue;
            const auto& manifest = installation.mod.manifest;
            const QString searchable =
                QStringList{manifest.uniqueId, manifest.name,        manifest.author,
                            manifest.version,  manifest.description, installation.profileName}
                    .join('\n');
            if (needle.isEmpty() || searchable.contains(needle, Qt::CaseInsensitive))
                group.installations.append(installation);
        }
        if (!group.installations.isEmpty()) result.append(group);
    }
    return result;
}
}  // namespace prismatic
