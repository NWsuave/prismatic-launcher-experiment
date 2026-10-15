#include "InstanceStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>

#include "AtomicFile.h"

namespace prismatic {
namespace {
constexpr qint64 MaxStoreBytes = 8 * 1024 * 1024;

bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}

bool validId(const QString& id) {
    return !QUuid(id).isNull() && QUuid(id).toString(QUuid::WithoutBraces) == id;
}

bool safePath(const QString& path) {
    return QDir::isAbsolutePath(path) && validateStoragePath(path);
}

bool readFile(const QString& path, QByteArray* bytes, QString* error) {
    if (!safePath(path)) return fail(error, "Storage paths cannot contain symbolic links.");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    if (file.size() > MaxStoreBytes)
        return fail(error, "Instance database exceeds the size limit.");
    *bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return fail(error, file.errorString());
    return true;
}

bool atomicWrite(const QString& path, const QByteArray& bytes, QString* error) {
    if (!safePath(path)) return fail(error, "Storage paths cannot contain symbolic links.");
    return writeFileAtomically(path, bytes, error);
}

bool parse(const QByteArray& data, QList<Instance>* instances, QString* selected,
           QString* warning) {
    const auto document = QJsonDocument::fromJson(data);
    if (!document.isObject()) return false;
    const auto object = document.object();
    if (object.value("schemaVersion").toInt(-1) != 1 || !object.value("instances").isArray() ||
        !object.value("selectedId").isString())
        return false;
    QSet<QString> seen;
    bool skipped = false;
    for (const auto& value : object.value("instances").toArray()) {
        const auto record = value.toObject();
        Instance instance;
        instance.id = record.value("id").toString();
        instance.name = record.value("name").toString().trimmed();
        instance.gamePath = record.value("gamePath").toString();
        instance.useSmapi = record.value("useSmapi").toBool(true);
        bool validArguments = record.value("launchArguments").isArray();
        for (const auto& argument : record.value("launchArguments").toArray()) {
            if (!argument.isString() || argument.toString().contains(QChar::Null) ||
                argument.toString().size() > 32768)
                validArguments = false;
            instance.launchArguments.append(argument.toString());
        }
        const bool valid =
            validId(instance.id) && !seen.contains(instance.id) && !instance.name.isEmpty() &&
            instance.name.size() <= 120 &&
            !instance.name.contains(QRegularExpression("[\\x00-\\x1f\\x7f]")) &&
            record.value("gamePath").isString() && !instance.gamePath.contains(QChar::Null) &&
            (instance.gamePath.isEmpty() || QDir::isAbsolutePath(instance.gamePath)) &&
            record.value("useSmapi").isBool() && validArguments &&
            instance.launchArguments.size() <= 128;
        if (!valid) {
            skipped = true;
            continue;
        }
        seen.insert(instance.id);
        instances->append(instance);
    }
    *selected = object.value("selectedId").toString();
    if (!selected->isEmpty() && !seen.contains(*selected)) {
        *selected = instances->isEmpty() ? QString() : instances->first().id;
        skipped = true;
    }
    if (skipped && warning)
        *warning =
            "Invalid instance records were skipped; original data will be retained in a recovery "
            "snapshot on the next save.";
    return true;
}

QByteArray serialize(const QList<Instance>& instances, const QString& selected) {
    QJsonArray records;
    for (const auto& instance : instances) {
        records.append(
            QJsonObject{{"id", instance.id},
                        {"name", instance.name},
                        {"gamePath", instance.gamePath},
                        {"useSmapi", instance.useSmapi},
                        {"launchArguments", QJsonArray::fromStringList(instance.launchArguments)}});
    }
    return QJsonDocument(
               QJsonObject{{"schemaVersion", 1}, {"selectedId", selected}, {"instances", records}})
        .toJson();
}

bool copyTree(const QString& source, const QString& destination, int depth, int* entries,
              qint64* bytes, QString* error) {
    if (depth > 48 || !safePath(source) || !safePath(destination))
        return fail(error, "Profile contains unsafe links or excessive directory nesting.");
    if (!QDir().mkpath(destination)) return fail(error, "Could not create duplicate profile.");
    const auto children = QDir(source).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                     QDir::Hidden | QDir::System);
    for (const auto& child : children) {
        if (++*entries > 100000 || child.isSymLink())
            return fail(error, "Profile contains symbolic links or too many files.");
        const QString target = QDir(destination).filePath(child.fileName());
        if (child.isDir()) {
            if (!copyTree(child.absoluteFilePath(), target, depth + 1, entries, bytes, error))
                return false;
        } else if (child.isFile()) {
            QFile input(child.absoluteFilePath()), output(target);
            if (!input.open(QIODevice::ReadOnly) ||
                !output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                return fail(error, "Could not open profile file for copying.");
            while (!input.atEnd()) {
                const QByteArray chunk = input.read(65536);
                *bytes += chunk.size();
                if (input.error() != QFileDevice::NoError || chunk.isEmpty() ||
                    *bytes > 16LL * 1024 * 1024 * 1024 || output.write(chunk) != chunk.size())
                    return fail(error, "Could not copy profile file, or profile exceeds 16 GiB.");
            }
            if (!output.flush()) return fail(error, "Could not flush copied profile file.");
            output.setPermissions(child.permissions());
        } else {
            return fail(error, "Profile contains an unsupported special file.");
        }
    }
    return true;
}
}  // namespace

InstanceStore::InstanceStore(const QString& rootPath)
    : m_root(rootPath.isEmpty() ? QString() : QDir(rootPath).absolutePath()) {}

bool InstanceStore::prepare(QString* error) const {
    if (!safePath(m_root))
        return fail(error, "Instance storage must be a directory without symbolic links.");
    for (const auto& name : {QString(), QString("profiles"), QString("trash")}) {
        const QString path = name.isEmpty() ? m_root : QDir(m_root).filePath(name);
        if (!safePath(path) || !QDir().mkpath(path))
            return fail(error, "Could not create safe instance storage directories.");
    }
    for (const auto& name : {"instances.json", "instances.json.bak", "instances.lock"}) {
        if (!safePath(QDir(m_root).filePath(name)))
            return fail(error, "Instance database paths cannot be symbolic links.");
    }
    return true;
}

bool InstanceStore::load(QString* error) {
    if (error) error->clear();
    m_loaded = false;
    if (!prepare(error)) return false;
    QLockFile lock(QDir(m_root).filePath("instances.lock"));
    if (!lock.tryLock(0)) return fail(error, "Instance database is busy; try again.");
    const QString primary = QDir(m_root).filePath("instances.json");
    const QString backup = primary + ".bak";
    QByteArray snapshot;
    const bool exists = QFileInfo::exists(primary);
    QList<Instance> loaded;
    QString selected, warning;
    const bool readOk = !exists || readFile(primary, &snapshot, error);
    if (exists && !readOk) return false;
    const auto schema = QJsonDocument::fromJson(snapshot).object().value("schemaVersion");
    if (schema.isDouble() && schema.toDouble() > 1)
        return fail(error,
                    "Instance database was created by a newer launcher. Upgrade the launcher to "
                    "open it safely.");
    if ((exists && !parse(snapshot, &loaded, &selected, &warning)) ||
        (!exists && QFileInfo::exists(backup))) {
        QByteArray backupData;
        loaded.clear();
        if (!readFile(backup, &backupData, error) ||
            !parse(backupData, &loaded, &selected, &warning))
            return fail(error,
                        "Instance database is damaged and no valid backup is available. Original "
                        "files were preserved.");
        warning =
            "Recovered instance records from the previous backup; the damaged database is "
            "preserved until you save.";
    }
    m_instances = loaded;
    m_selected = selected;
    m_diskSnapshot = snapshot;
    m_diskExists = exists;
    m_loaded = true;
    if (error) *error = warning;
    return true;
}

QList<Instance> InstanceStore::instances() const { return m_instances; }

std::optional<Instance> InstanceStore::find(const QString& id) const {
    const auto found = std::find_if(m_instances.cbegin(), m_instances.cend(),
                                    [&id](const auto& instance) { return instance.id == id; });
    return found == m_instances.cend() ? std::nullopt : std::optional<Instance>(*found);
}

QString InstanceStore::selectedId() const { return m_selected; }

QString InstanceStore::profilePath(const QString& id) const {
    return validId(id) ? QDir(m_root).filePath("profiles/" + id) : QString();
}

QString InstanceStore::modsPath(const QString& id) const {
    const QString profile = profilePath(id);
    return profile.isEmpty() ? QString() : QDir(profile).filePath("Mods");
}

bool InstanceStore::validate(const Instance& instance, QString* error) {
    if (!validId(instance.id) || instance.name.trimmed().isEmpty() ||
        instance.name.trimmed().size() > 120 ||
        instance.name.contains(QRegularExpression("[\\x00-\\x1f\\x7f]")))
        return fail(error,
                    "Instance name must contain 1 to 120 characters without control characters.");
    if (!instance.gamePath.isEmpty() &&
        (!QDir::isAbsolutePath(instance.gamePath) || instance.gamePath.contains(QChar::Null) ||
         !QFileInfo(instance.gamePath).isDir()))
        return fail(error, "Game directory must be an existing absolute directory, or left empty.");
    if (instance.launchArguments.size() > 128) return fail(error, "Too many launch arguments.");
    if (std::any_of(instance.launchArguments.cbegin(), instance.launchArguments.cend(),
                    [](const auto& argument) {
                        return argument.size() > 32768 || argument.contains(QChar::Null);
                    }))
        return fail(error, "Launch argument is invalid or too long.");
    return true;
}

bool InstanceStore::commit(const QList<Instance>& instances, const QString& selected,
                           QString* error) {
    if (error) error->clear();
    if (!m_loaded) return fail(error, "Load the instance database before editing it.");
    if (!prepare(error)) return false;
    QLockFile lock(QDir(m_root).filePath("instances.lock"));
    if (!lock.tryLock(0)) return fail(error, "Instance database is busy; try again.");
    const QString primary = QDir(m_root).filePath("instances.json");
    QByteArray current;
    const bool exists = QFileInfo::exists(primary);
    if (exists && !readFile(primary, &current, error)) return false;
    if (exists != m_diskExists || current != m_diskSnapshot)
        return fail(error, "Instance database changed in another window; reload before editing.");
    QList<Instance> oldInstances;
    QString oldSelected, parseWarning;
    const bool parsed = exists && parse(current, &oldInstances, &oldSelected, &parseWarning);
    if (exists && (!parsed || !parseWarning.isEmpty())) {
        // Keep damaged bytes independently of the rotating backup. Otherwise a
        // second ordinary save would erase records which were skipped on load.
        const QString digest = QString::fromLatin1(
            QCryptographicHash::hash(current, QCryptographicHash::Sha256).toHex());
        const QString recovery = QDir(m_root).filePath("instances.recovery-" + digest + ".json");
        if (QFileInfo::exists(recovery)) {
            QByteArray retained;
            if (!readFile(recovery, &retained, error)) return false;
            if (retained != current)
                return fail(error,
                            "An existing recovery snapshot conflicts with the damaged database.");
        } else if (!atomicWrite(recovery, current, error)) {
            return false;
        }
    }
    if (parsed) {
        if (!atomicWrite(primary + ".bak", current, error)) return false;
    }
    const QByteArray data = serialize(instances, selected);
    if (data.size() > MaxStoreBytes)
        return fail(error, "Instance database exceeds the size limit.");
    if (!atomicWrite(primary, data, error)) return false;
    m_instances = instances;
    m_selected = selected;
    m_diskSnapshot = data;
    m_diskExists = true;
    return true;
}

bool InstanceStore::create(const QString& name, const QString& gamePath, QString* createdId,
                           QString* error) {
    Instance instance{
        QUuid::createUuid().toString(QUuid::WithoutBraces), name.trimmed(), gamePath, true, {}};
    if (!m_loaded) return fail(error, "Load the instance database before editing it.");
    if (!validate(instance, error) || !prepare(error)) return false;
    const QString profile = profilePath(instance.id);
    if (QFileInfo::exists(profile)) return fail(error, "Profile directory already exists.");
    if (!QDir().mkpath(QDir(profile).filePath("Mods")) ||
        !QDir().mkpath(QDir(profile).filePath("DisabledMods"))) {
        QDir(profile).removeRecursively();
        return fail(error, "Could not create profile directories.");
    }
    auto next = m_instances;
    next.append(instance);
    if (!commit(next, m_selected.isEmpty() ? instance.id : m_selected, error)) {
        QDir(profile).removeRecursively();
        return false;
    }
    if (createdId) *createdId = instance.id;
    return true;
}

bool InstanceStore::update(const Instance& instance, QString* error) {
    if (!find(instance.id)) return fail(error, "Instance does not exist.");
    if (!validate(instance, error)) return false;
    auto next = m_instances;
    for (auto& record : next) {
        if (record.id == instance.id) {
            record = instance;
            record.name = record.name.trimmed();
        }
    }
    return commit(next, m_selected, error);
}

bool InstanceStore::duplicate(const QString& id, const QString& name, QString* createdId,
                              QString* error) {
    const auto source = find(id);
    if (!source) return fail(error, "Instance does not exist.");
    Instance copy = *source;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name = name.trimmed();
    if (!validate(copy, error) || !prepare(error)) return false;
    const QString original = profilePath(id), destination = profilePath(copy.id);
    if (!QFileInfo(original).isDir() || QFileInfo::exists(destination))
        return fail(error, "Source profile is missing, or destination already exists.");
    const QString lockPath = original + ".mods.lock";
    if (!safePath(lockPath)) return fail(error, "Profile lock path contains a symbolic link.");
    QLockFile lock(lockPath);
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return fail(error, "Profile is busy with another mod operation.");
    int entries = 0;
    qint64 bytes = 0;
    if (!copyTree(original, destination, 0, &entries, &bytes, error)) {
        QDir(destination).removeRecursively();
        return false;
    }
    auto next = m_instances;
    next.append(copy);
    if (!commit(next, m_selected, error)) {
        QDir(destination).removeRecursively();
        return false;
    }
    if (createdId) *createdId = copy.id;
    return true;
}

bool InstanceStore::remove(const QString& id, QString* error) {
    if (!find(id)) return fail(error, "Instance does not exist.");
    if (!prepare(error)) return false;
    const QString profile = profilePath(id);
    const QString trash = QDir(m_root).filePath("trash/" + id + "-" +
                                                QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!safePath(profile)) return fail(error, "Profile directory contains a symbolic link.");
    const bool exists = QFileInfo::exists(profile);
    const QString lockPath = profile + ".mods.lock";
    if (!safePath(lockPath)) return fail(error, "Profile lock path contains a symbolic link.");
    QLockFile lock(lockPath);
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return fail(error, "Profile is busy with another mod operation.");
    if (exists && !QDir().rename(profile, trash))
        return fail(error, "Could not move profile to the recovery trash.");
    auto next = m_instances;
    for (qsizetype i = 0; i < next.size(); ++i) {
        if (next[i].id == id) {
            next.removeAt(i);
            break;
        }
    }
    const QString selected =
        m_selected == id ? (next.isEmpty() ? QString() : next.first().id) : m_selected;
    if (!commit(next, selected, error)) {
        if (exists && !QDir().rename(trash, profile))
            return fail(error, "Database save failed. Profile remains recoverable at " + trash);
        return false;
    }
    return true;
}

bool InstanceStore::select(const QString& id, QString* error) {
    if (!find(id)) return fail(error, "Instance does not exist.");
    return commit(m_instances, id, error);
}

}  // namespace prismatic
