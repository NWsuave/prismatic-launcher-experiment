#include "DataPaths.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "AtomicFile.h"

namespace prismatic {
QString defaultDataDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

bool migrateLegacyData(const QString& legacy, const QString& destination, QString* error) {
    if (error) error->clear();
    const auto fail = [error](const QString& text) {
        if (error) *error = text;
        return false;
    };
    if (!validateStoragePath(legacy, error) || !validateStoragePath(destination, error))
        return false;
    const auto source = QFileInfo(legacy).absoluteFilePath();
    const auto target = QFileInfo(destination).absoluteFilePath();
    if (source == target || QFileInfo::exists(target)) return true;
    if (!QFileInfo::exists(source + "/instances.json") &&
        !QFileInfo::exists(source + "/instances.json.bak"))
        return true;
    if (target.startsWith(source + '/') || source.startsWith(target + '/'))
        return fail("Legacy and new data directories must not contain one another.");
    const auto parent = QFileInfo(target).absolutePath();
    if (!QDir().mkpath(parent)) return fail("Cannot create the application-data parent directory.");
    QLockFile migrationLock(target + ".migration.lock");
    if (!migrationLock.tryLock(0)) return fail("Another launcher is migrating application data.");
    if (QFileInfo::exists(target)) return true;
    QLockFile sourceLock(source + "/.launcher-session.lock");
    if (!sourceLock.tryLock(0))
        return fail("Close the launcher using the legacy data directory before migration.");
    QTemporaryDir staging(parent + "/.prismatic-migration-XXXXXX");
    if (!staging.isValid()) return fail("Cannot create application-data migration staging.");
    QDirIterator entries(source,
                         QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                         QDirIterator::Subdirectories);
    qint64 bytes = 0;
    qsizetype count = 0;
    while (entries.hasNext()) {
        entries.next();
        const auto info = entries.fileInfo();
        const auto relative = QDir(source).relativeFilePath(info.absoluteFilePath());
        if (relative == ".launcher-session.lock") continue;
        if (++count > 100000 || !validateStoragePath(info.absoluteFilePath(), error))
            return fail("Legacy data exceeds migration limits or contains an unsafe link: " +
                        relative);
        const auto output = staging.filePath(relative);
        if (info.isDir()) {
            if (!QDir().mkpath(output)) return fail("Cannot copy legacy directory: " + relative);
        } else if (info.isFile()) {
            constexpr qint64 maxBytes = 20LL * 1024 * 1024 * 1024;
            if (info.size() < 0 || info.size() > maxBytes - bytes)
                return fail(
                    "Legacy data exceeds the 20 GiB automatic migration limit. Use --data-dir.");
            QFile input(info.absoluteFilePath());
            QFile copied(output);
            if (!QDir().mkpath(QFileInfo(output).absolutePath()) ||
                !input.open(QIODevice::ReadOnly) ||
                !copied.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                return fail("Cannot copy legacy file: " + relative + ": " + input.errorString());
            while (!input.atEnd()) {
                const auto chunk = input.read(65536);
                if (input.error() != QFileDevice::NoError || chunk.size() > maxBytes - bytes ||
                    copied.write(chunk) != chunk.size())
                    return fail("Cannot copy legacy file within migration limits: " + relative);
                bytes += chunk.size();
            }
            if (!copied.setPermissions(info.permissions()) || !copied.flush())
                return fail("Cannot preserve legacy file: " + relative);
        } else {
            return fail("Legacy data contains a non-regular file: " + relative);
        }
    }
    // Same-filesystem rename publishes a complete tree. Never merge or replace data.
    if (!QDir().rename(staging.path(), target))
        return fail("Cannot publish migrated data; the original data has been retained.");
    staging.setAutoRemove(false);
    return true;
}
}  // namespace prismatic
