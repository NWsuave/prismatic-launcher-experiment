#include "ModManager.h"

#include <archive.h>
#include <archive_entry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <memory>

#include "DependencyResolver.h"

namespace prismatic {
namespace {
constexpr qint64 maxFileBytes = 256LL * 1024 * 1024;
constexpr qint64 maxTotalBytes = 1024LL * 1024 * 1024;
constexpr int maxEntries = 10000;
constexpr int maxDepth = 64;

QString pathKey(const QString& path) {
    return path.normalized(QString::NormalizationForm_C).toCaseFolded();
}

bool isLinked(const QFileInfo& info) {
#ifdef Q_OS_WIN
    return info.isSymLink() || info.isJunction();
#else
    return info.isSymLink();
#endif
}

bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}

bool safePathSyntax(const QString& path, QString* error) {
    // Inspect the raw spelling before QFileInfo removes parent components.
    // On a symlinked path, lexical normalization changes what the OS opens.
    if (path.contains(QChar::Null) ||
        QDir::fromNativeSeparators(path).split('/').contains(QStringLiteral("..")))
        return fail(error, QStringLiteral("Parent path components and NUL bytes are not allowed."));
    return true;
}

// Validate every existing ancestor, including the profile itself. Never create
// directories through a user-provided symlink or a Windows junction.
bool safeAncestors(const QString& path, QString* error) {
    if (!safePathSyntax(path, error)) return false;
    QString current = QFileInfo(path).absoluteFilePath();
    while (!current.isEmpty()) {
        const QFileInfo info(current);
        if (isLinked(info))
            return fail(
                error,
                QStringLiteral("Symbolic links and junctions are not allowed: %1").arg(current));
        if (info.exists() && !info.isDir())
            return fail(error, QStringLiteral("A file blocks the directory: %1").arg(current));
        const QString parent = info.absolutePath();
        if (parent == current) break;
        current = parent;
    }
    return true;
}

bool prepareProfile(const QString& root, QString* error) {
    if (root.isEmpty()) return fail(error, QStringLiteral("The mod profile path is empty."));
    if (!safeAncestors(root, error)) return false;
    if (!QDir().mkpath(root)) return fail(error, QStringLiteral("Cannot create the mod profile."));
    for (const QString& name : {QStringLiteral("Mods"), QStringLiteral("DisabledMods")}) {
        const QString path = root + '/' + name;
        if (!safeAncestors(path, error)) return false;
        if (!QDir().mkpath(path)) return fail(error, QStringLiteral("Cannot create %1.").arg(name));
    }
    return true;
}

// The lock is beside the profile so InstanceStore can move the whole profile
// while holding it. Prepare only the parent before locking: recreating a moved
// profile here would prevent InstanceStore from restoring it after a failed save.
bool prepareLockParent(const QString& root, QString* error) {
    if (root.isEmpty()) return fail(error, QStringLiteral("The mod profile path is empty."));
    if (!safeAncestors(root, error)) return false;
    if (!QDir().mkpath(QFileInfo(root).absolutePath()))
        return fail(error, QStringLiteral("Cannot create the mod profile parent directory."));
    return true;
}

bool lockProfile(QLockFile& lock, const QString& root, QString* error) {
    const QFileInfo lockInfo(root + ".mods.lock");
    if (isLinked(lockInfo))
        return fail(error, QStringLiteral("The profile lock is an unsafe symbolic link."));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return fail(error, QStringLiteral("The mod profile is busy or cannot be locked."));
    return true;
}

// This also rejects platform-specific aliases so archives behave consistently
// on Linux, Windows and macOS. Dot components may not hide a traversal attempt.
bool safeRelative(const QString& path, QString* error) {
    if (path.isEmpty() || path.size() > 1024 || path.startsWith('/') || path.contains('\\'))
        return fail(error, QStringLiteral("Unsafe archive or folder path: %1").arg(path));
    const auto parts = path.split('/');
    if (parts.size() > maxDepth)
        return fail(error, QStringLiteral("The mod directory nesting is too deep."));
    static const QRegularExpression forbidden(QStringLiteral("[\\x00-\\x1f<>:\"|?*]"));
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    const bool unsafe = std::any_of(parts.cbegin(), parts.cend(), [](const QString& part) {
        return part.isEmpty() || part == "." || part == ".." || part.size() > 200 ||
               part.endsWith('.') || part.endsWith(' ') || part.contains(forbidden) ||
               reserved.match(part).hasMatch();
    });
    if (unsafe) return fail(error, QStringLiteral("Unsafe archive or folder path: %1").arg(path));
    return true;
}

struct Budget {
    int entries = 0;
    qint64 bytes = 0;
};

bool copyTree(const QString& source, const QString& destination, Budget& budget, int depth,
              QString* error) {
    if (depth > maxDepth)
        return fail(error, QStringLiteral("The mod directory nesting is too deep."));
    const QFileInfo sourceInfo(source);
    if (isLinked(sourceInfo))
        return fail(error, QStringLiteral("Mod folders may not contain symbolic links."));
    if (!sourceInfo.isDir())
        return fail(error, QStringLiteral("The selected mod folder is not a directory."));
    if (!QDir().mkpath(destination))
        return fail(error, QStringLiteral("Cannot create the staging directory."));
    const auto entries = QDir(source).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
    QSet<QString> names;
    for (const QFileInfo& entry : entries) {
        if (++budget.entries > maxEntries)
            return fail(error, QStringLiteral("The mod contains too many files."));
        const QString name = entry.fileName();
        if (!safeRelative(name, error)) return false;
        if (names.contains(pathKey(name)))
            return fail(error, QStringLiteral("The mod contains conflicting filenames."));
        names.insert(pathKey(name));
        if (isLinked(entry))
            return fail(error, QStringLiteral("Mod folders may not contain symbolic links."));
        const QString target = destination + '/' + name;
        if (entry.isDir()) {
            if (!copyTree(entry.absoluteFilePath(), target, budget, depth + 1, error)) return false;
        } else if (entry.isFile()) {
            if (entry.size() > maxFileBytes || entry.size() > maxTotalBytes - budget.bytes)
                return fail(error, QStringLiteral("The mod exceeds the installation size limit."));
            QFile input(entry.absoluteFilePath());
            QFile output(target);
            if (!input.open(QIODevice::ReadOnly) ||
                !output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                return fail(error, QStringLiteral("Cannot copy a mod file into staging."));
            qint64 fileBytes = 0;
            while (!input.atEnd()) {
                const QByteArray chunk = input.read(65536);
                if (chunk.isEmpty() && input.error() != QFileDevice::NoError)
                    return fail(error, QStringLiteral("Reading a mod file failed."));
                fileBytes += chunk.size();
                budget.bytes += chunk.size();
                if (fileBytes > maxFileBytes || budget.bytes > maxTotalBytes)
                    return fail(error,
                                QStringLiteral("The mod exceeds the installation size limit."));
                if (output.write(chunk) != chunk.size())
                    return fail(error, QStringLiteral("Writing a staged mod file failed."));
            }
            if (!output.flush())
                return fail(error, QStringLiteral("Flushing a staged mod file failed."));
        } else {
            return fail(error, QStringLiteral(
                                   "Mod folders may contain only regular files and directories."));
        }
    }
    return true;
}

// Strip comments without touching their appearance inside JSON strings, then
// strip trailing commas outside strings. QJsonDocument still validates JSON.
QByteArray manifestJson(const QByteArray& source, bool& valid) {
    QByteArray clean;
    bool quoted = false;
    bool escaped = false;
    valid = true;
    for (qsizetype i = 0; i < source.size(); ++i) {
        const char c = source[i];
        if (quoted) {
            clean += c;
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"') {
            quoted = true;
            clean += c;
        } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            i += 2;
            while (i < source.size() && source[i] != '\n' && source[i] != '\r') ++i;
            clean += '\n';
        } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            i += 2;
            while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/')) ++i;
            if (i + 1 >= source.size()) {
                valid = false;
                return {};
            }
            ++i;
            clean += ' ';
        } else
            clean += c;
    }
    quoted = false;
    escaped = false;
    for (qsizetype i = 0; i < clean.size(); ++i) {
        const char c = clean[i];
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == ',') {
            qsizetype next = i + 1;
            while (next < clean.size() && (clean[next] == ' ' || clean[next] == '\n' ||
                                           clean[next] == '\r' || clean[next] == '\t'))
                ++next;
            if (next < clean.size() && (clean[next] == '}' || clean[next] == ']')) clean[i] = ' ';
        }
    }
    return clean;
}

QList<InstalledMod> scan(const QString& root, QString* error) {
    QList<InstalledMod> result;
    QSet<QString> ids;
    for (const QString& name : {QStringLiteral("Mods"), QStringLiteral("DisabledMods")}) {
        const QString directory = root + '/' + name;
        if (!safeAncestors(directory, error)) return {};
        const auto entries = QDir(directory).entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
        for (const QFileInfo& entry : entries) {
            if (isLinked(entry)) {
                fail(error, QStringLiteral("A mod directory is an unsafe symbolic link."));
                return {};
            }
            if (!entry.isDir())
                continue;  // Preserve unrelated files without treating them as mods.
            const QString manifestPath = entry.absoluteFilePath() + "/manifest.json";
            if (!QFileInfo::exists(manifestPath)) continue;
            auto manifest = ModManager::parseManifest(manifestPath, error);
            if (!manifest) return {};
            const QString id = manifest->uniqueId.toCaseFolded();
            if (ids.contains(id)) {
                fail(error,
                     QStringLiteral("Duplicate installed mod ID: %1").arg(manifest->uniqueId));
                return {};
            }
            ids.insert(id);
            result.append({*manifest, entry.absoluteFilePath(), name == "Mods"});
        }
    }
    return result;
}

bool discover(const QString& directory, QList<InstalledMod>& found, int depth, QString* error) {
    if (depth > maxDepth) return fail(error, QStringLiteral("The mod nesting is too deep."));
    const QString path = directory + "/manifest.json";
    const bool hasManifest = QFileInfo::exists(path);
    const qsizetype previous = found.size();
    const auto children =
        QDir(directory).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    if (!std::all_of(children.cbegin(), children.cend(), [&](const QFileInfo& child) {
            return discover(child.absoluteFilePath(), found, depth + 1, error);
        }))
        return false;
    if (hasManifest) {
        if (found.size() != previous)
            return fail(error,
                        QStringLiteral(
                            "A mod contains another mod manifest. Install these mods separately."));
        const auto manifest = ModManager::parseManifest(path, error);
        if (!manifest) return false;
        found.append({*manifest, directory, true});
    }
    return true;
}

struct ArchiveInput {
    QFile file;
    QByteArray buffer = QByteArray(65536, '\0');
};

la_ssize_t readArchive(struct archive* reader, void* client, const void** buffer) {
    auto* input = static_cast<ArchiveInput*>(client);
    const qint64 count = input->file.read(input->buffer.data(), input->buffer.size());
    if (count < 0) archive_set_error(reader, EIO, "Reading the ZIP failed");
    *buffer = input->buffer.constData();
    return static_cast<la_ssize_t>(count);
}

la_int64_t seekArchive(struct archive* reader, void* client, la_int64_t offset, int whence) {
    auto* input = static_cast<ArchiveInput*>(client);
    qint64 base = 0;
    if (whence == SEEK_CUR)
        base = input->file.pos();
    else if (whence == SEEK_END)
        base = input->file.size();
    else if (whence != SEEK_SET)
        return -1;
    // Avoid signed overflow even for malicious offsets in ZIP metadata.
    if ((offset < 0 && offset < -base) ||
        (offset > 0 && offset > std::numeric_limits<qint64>::max() - base) ||
        !input->file.seek(base + offset)) {
        archive_set_error(reader, EIO, "Seeking in the ZIP failed");
        return -1;
    }
    return input->file.pos();
}

bool extractZip(const QString& source, const QString& target, QString* error) {
    ArchiveInput input;
    input.file.setFileName(source);
    if (!input.file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Cannot read the selected ZIP archive."));
    std::unique_ptr<struct archive, decltype(&archive_read_free)> reader(archive_read_new(),
                                                                         archive_read_free);
    if (!reader) return fail(error, QStringLiteral("Cannot initialize the ZIP reader."));
    archive_read_support_filter_none(reader.get());
    // Require the central directory: a streaming ZIP reader can otherwise
    // accept a file whose entire central directory was truncated away.
    archive_read_support_format_zip_seekable(reader.get());
    archive_read_set_seek_callback(reader.get(), seekArchive);
    if (archive_read_open(reader.get(), &input, nullptr, readArchive, nullptr) != ARCHIVE_OK)
        return fail(error, QStringLiteral("The selected file is not a readable ZIP archive."));
    if (!QDir().mkpath(target))
        return fail(error, QStringLiteral("Cannot create ZIP staging directory."));
    QSet<QString> files;
    QSet<QString> directories;
    QMap<QString, QString> spellings;
    Budget budget;
    struct archive_entry* entry = nullptr;
    int status = ARCHIVE_OK;
    while ((status = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        if (++budget.entries > maxEntries)
            return fail(error, QStringLiteral("The ZIP contains too many entries."));
        const char* raw = archive_entry_pathname_utf8(entry);
        if (!raw) raw = archive_entry_pathname(entry);
        if (!raw) return fail(error, QStringLiteral("The ZIP contains an unnamed entry."));
        QString path = QString::fromUtf8(raw);
        while (path.endsWith('/')) path.chop(1);
        // A conventional leading ./ does not change the destination.
        while (path.startsWith("./")) path.remove(0, 2);
        if (!safeRelative(path, error)) return false;
        const auto type = archive_entry_filetype(entry);
        if (archive_entry_symlink(entry) || archive_entry_hardlink(entry) ||
            archive_entry_is_encrypted(entry) > 0 || (type != AE_IFREG && type != AE_IFDIR))
            return fail(error, QStringLiteral(
                                   "The ZIP contains a link, special file, or encrypted entry."));
        const QString key = pathKey(path);
        const auto originalParts = path.split('/');
        QString originalPrefix;
        for (const auto& component : originalParts) {
            if (!originalPrefix.isEmpty()) originalPrefix += '/';
            originalPrefix += component;
            const QString prefixKey = pathKey(originalPrefix);
            if (spellings.contains(prefixKey) && spellings.value(prefixKey) != originalPrefix)
                return fail(
                    error,
                    QStringLiteral("The ZIP contains filenames that alias on another platform."));
            spellings.insert(prefixKey, originalPrefix);
        }
        const auto components = key.split('/');
        QString parent;
        for (qsizetype i = 0; i + 1 < components.size(); ++i) {
            if (!parent.isEmpty()) parent += '/';
            parent += components[i];
            if (files.contains(parent))
                return fail(error, QStringLiteral("ZIP entries conflict with a parent file."));
            directories.insert(parent);
        }
        const QString outputPath = target + '/' + path;
        if (type == AE_IFDIR) {
            if (archive_entry_size(entry) != 0)
                return fail(error,
                            QStringLiteral("A ZIP directory contains unexpected file data."));
            if (files.contains(key))
                return fail(error, QStringLiteral("ZIP entries have conflicting paths."));
            directories.insert(key);
            if (!QDir().mkpath(outputPath))
                return fail(error, QStringLiteral("Cannot create an extracted directory."));
            continue;
        }
        if (files.contains(key) || directories.contains(key))
            return fail(error, QStringLiteral("The ZIP contains duplicate or conflicting paths."));
        files.insert(key);
        const auto declared = archive_entry_size(entry);
        if (declared < 0 || declared > maxFileBytes || declared > maxTotalBytes - budget.bytes)
            return fail(error, QStringLiteral("The ZIP exceeds the installation size limit."));
        if (!QDir().mkpath(QFileInfo(outputPath).absolutePath()))
            return fail(error, QStringLiteral("Cannot create an extracted directory."));
        QFile output(outputPath);
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return fail(error, QStringLiteral("Cannot create an extracted file."));
        char buffer[65536];
        qint64 fileBytes = 0;
        la_ssize_t count = 0;
        while ((count = archive_read_data(reader.get(), buffer, sizeof(buffer))) > 0) {
            fileBytes += count;
            budget.bytes += count;
            if (fileBytes > maxFileBytes || budget.bytes > maxTotalBytes)
                return fail(error, QStringLiteral("The ZIP exceeds the installation size limit."));
            if (output.write(buffer, count) != count)
                return fail(error, QStringLiteral("Writing an extracted file failed."));
        }
        if (count < 0 || fileBytes != declared)
            return fail(error, QStringLiteral("The ZIP contains corrupted or truncated data."));
        if (!output.flush())
            return fail(error, QStringLiteral("Flushing an extracted file failed."));
    }
    if (status != ARCHIVE_EOF)
        return fail(error, QStringLiteral("The ZIP archive is corrupted or incomplete."));
    return true;
}
}  // namespace

ModManager::ModManager(QString profileRoot)
    : root_(profileRoot.trimmed().isEmpty()
                ? QString()
                : QDir::cleanPath(QFileInfo(profileRoot).absoluteFilePath())) {}

std::optional<ModManifest> ModManager::parseManifest(const QString& path, QString* error) {
    if (error) error->clear();
    if (!safePathSyntax(path, error)) return std::nullopt;
    const QFileInfo info(path);
    if (isLinked(info) || !info.isFile() || info.size() > 1024 * 1024 ||
        !safeAncestors(info.absolutePath(), error)) {
        fail(error, QStringLiteral("The manifest is missing, unsafe, or exceeds 1 MiB."));
        return std::nullopt;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("Cannot read the mod manifest."));
        return std::nullopt;
    }
    const QByteArray bytes = file.read(1024 * 1024 + 1);
    if (bytes.size() > 1024 * 1024 || file.error() != QFileDevice::NoError) {
        fail(error, QStringLiteral("Cannot read the mod manifest within its size limit."));
        return std::nullopt;
    }
    bool valid = true;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(manifestJson(bytes, valid), &parseError);
    if (!valid || !document.isObject() || parseError.error != QJsonParseError::NoError) {
        fail(error,
             QStringLiteral("Invalid JSON in mod manifest: %1").arg(parseError.errorString()));
        return std::nullopt;
    }
    const auto object = document.object();
    auto field = [&](const QString& name) -> QString {
        QString result;
        int matches = 0;
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (it.key().compare(name, Qt::CaseInsensitive) == 0) {
                ++matches;
                if (it.value().isString()) result = it.value().toString().trimmed();
            }
        }
        if (matches != 1 || result.size() > 512 || result.contains(QChar::Null)) return {};
        return result;
    };
    ModManifest manifest;
    manifest.name = field("Name");
    manifest.author = field("Author");
    manifest.version = field("Version");
    manifest.uniqueId = field("UniqueID");
    static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,199}$"));
    if (manifest.name.isEmpty() || manifest.author.isEmpty() || manifest.version.isEmpty() ||
        !validId.match(manifest.uniqueId).hasMatch() || !safeRelative(manifest.uniqueId, nullptr)) {
        fail(error, QStringLiteral(
                        "The manifest needs valid Name, Author, Version, and UniqueID strings."));
        return std::nullopt;
    }
    bool metadataValid = true;
    auto value = [&](const QJsonObject& source, const QString& name) -> QJsonValue {
        QJsonValue found(QJsonValue::Undefined);
        for (auto it = source.begin(); it != source.end(); ++it) {
            if (it.key().compare(name, Qt::CaseInsensitive) != 0) continue;
            if (!found.isUndefined()) metadataValid = false;
            found = it.value();
        }
        return found;
    };
    auto text = [&](const QJsonObject& source, const QString& name, int limit = 512) {
        const auto item = value(source, name);
        if (item.isUndefined() || item.isNull()) return QString();
        if (!item.isString() || item.toString().size() > limit ||
            item.toString().contains(QChar::Null))
            metadataValid = false;
        return item.toString().trimmed();
    };
    auto dependency = [&](const QJsonValue& item, bool contentPack) -> ModDependency {
        if (!item.isObject()) metadataValid = false;
        const auto data = item.toObject();
        ModDependency dep{text(data, "UniqueID"), text(data, "MinimumVersion"), true};
        if (!validId.match(dep.uniqueId).hasMatch() || !safeRelative(dep.uniqueId, nullptr))
            metadataValid = false;
        const auto required = value(data, "IsRequired");
        if (!required.isUndefined()) {
            if (!required.isBool() || contentPack)
                metadataValid = false;
            else
                dep.isRequired = required.toBool();
        }
        if (!dep.minimumVersion.isEmpty() &&
            !DependencyResolver::compareVersions(dep.minimumVersion, dep.minimumVersion))
            metadataValid = false;
        return dep;
    };
    manifest.description = text(object, "Description", 8192);
    manifest.minimumApiVersion = text(object, "MinimumApiVersion");
    manifest.minimumGameVersion = text(object, "MinimumGameVersion");
    for (const auto& runtime : {manifest.minimumApiVersion, manifest.minimumGameVersion})
        if (!runtime.isEmpty() && !DependencyResolver::compareVersions(runtime, runtime))
            metadataValid = false;
    const auto dependencies = value(object, "Dependencies");
    if (!dependencies.isUndefined() && !dependencies.isNull()) {
        if (!dependencies.isArray() || dependencies.toArray().size() > 1000) metadataValid = false;
        for (const auto& item : dependencies.toArray())
            manifest.dependencies.append(dependency(item, false));
    }
    const auto contentPack = value(object, "ContentPackFor");
    if (!contentPack.isUndefined() && !contentPack.isNull()) {
        manifest.contentPackFor = dependency(contentPack, true);
        if (!text(object, "EntryDll").isEmpty()) metadataValid = false;
    }
    const auto updates = value(object, "UpdateKeys");
    if (!updates.isUndefined() && !updates.isNull()) {
        if (!updates.isArray() || updates.toArray().size() > 100) metadataValid = false;
        for (const auto& item : updates.toArray()) {
            if (!item.isString() || item.toString().size() > 512 ||
                item.toString().contains(QChar::Null))
                metadataValid = false;
            else
                manifest.updateKeys.append(item.toString().trimmed());
        }
    }
    if (!metadataValid ||
        !DependencyResolver::compareVersions(manifest.version, manifest.version)) {
        fail(error,
             QStringLiteral(
                 "Invalid SMAPI dependency, content pack, update key, or version metadata."));
        return std::nullopt;
    }
    return manifest;
}

QList<InstalledMod> ModManager::mods(QString* error) const {
    if (error) error->clear();
    if (root_.isEmpty()) {
        fail(error, QStringLiteral("The mod profile path is empty."));
        return {};
    }
    if (!safeAncestors(root_, error)) return {};
    if (!QFileInfo::exists(root_)) return {};
    QLockFile lock(root_ + ".mods.lock");
    if (!lockProfile(lock, root_, error)) return {};
    return scan(root_, error);
}

bool ModManager::installStaged(const QString& staging, bool& preserveStaging, QString* error) {
    preserveStaging = false;
    QList<InstalledMod> candidates;
    if (!discover(staging + "/content", candidates, 0, error)) return false;
    if (candidates.isEmpty())
        return fail(error, QStringLiteral("No mod manifest.json files were found."));
    QString scanError;
    const auto installed = scan(root_, &scanError);
    if (!scanError.isEmpty()) return fail(error, scanError);
    QSet<QString> ids;
    for (const auto& mod : installed) ids.insert(mod.manifest.uniqueId.toCaseFolded());
    QSet<QString> targets;
    for (const auto& item :
         QDir(root_ + "/Mods")
             .entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        targets.insert(pathKey(item.fileName()));
    for (const auto& mod : candidates) {
        const QString id = mod.manifest.uniqueId.toCaseFolded();
        if (ids.contains(id))
            return fail(
                error,
                QStringLiteral(
                    "Duplicate mod ID or version conflict: %1 (%2). Existing files were preserved.")
                    .arg(mod.manifest.uniqueId, mod.manifest.version));
        if (targets.contains(id))
            return fail(error, QStringLiteral(
                                   "A file or directory already occupies the mod destination: %1")
                                   .arg(mod.manifest.uniqueId));
        ids.insert(id);
        targets.insert(id);
    }
    QList<QPair<QString, QString>> moved;
    for (const auto& mod : candidates) {
        const QString target = root_ + "/Mods/" + mod.manifest.uniqueId;
        if (!QDir().rename(mod.directory, target)) {
            bool recovered = true;
            for (auto it = moved.crbegin(); it != moved.crend(); ++it)
                if (!QDir().rename(it->second, it->first)) recovered = false;
            preserveStaging = !recovered;
            return fail(error,
                        recovered
                            ? QStringLiteral(
                                  "Installing the mods failed. The installation was rolled back.")
                            : QStringLiteral(
                                  "Installation failed and rollback was incomplete. Recovery "
                                  "staging was retained at %1; original downloads are unchanged.")
                                  .arg(staging));
        }
        moved.append({mod.directory, target});
    }
    return true;
}

bool ModManager::installFolder(const QString& source, QString* error) {
    if (error) error->clear();
    if (source.trimmed().isEmpty() || !safeAncestors(source, error))
        return fail(error, QStringLiteral("Select a regular mod folder without linked ancestors."));
    if (!prepareLockParent(root_, error)) return false;
    QLockFile lock(root_ + ".mods.lock");
    if (!lockProfile(lock, root_, error)) return false;
    if (!prepareProfile(root_, error)) return false;
    const QString sourcePath = QFileInfo(source).canonicalFilePath();
    const QString profilePath = QFileInfo(root_).canonicalFilePath();
    const QString sourcePrefix = sourcePath.endsWith('/') ? sourcePath : sourcePath + '/';
#ifdef Q_OS_WIN
    constexpr auto pathSensitivity = Qt::CaseInsensitive;
#else
    constexpr auto pathSensitivity = Qt::CaseSensitive;
#endif
    if (sourcePath.isEmpty() || sourcePath.compare(profilePath, pathSensitivity) == 0 ||
        profilePath.startsWith(sourcePrefix, pathSensitivity))
        return fail(error, QStringLiteral("The mod source may not contain the profile directory."));
    QTemporaryDir staging(root_ + "/.install-XXXXXX");
    if (!staging.isValid())
        return fail(error, QStringLiteral("Cannot create the installation staging directory."));
    Budget budget;
    if (!copyTree(sourcePath, staging.path() + "/content", budget, 0, error)) return false;
    bool preserveStaging = false;
    const bool result = installStaged(staging.path(), preserveStaging, error);
    if (preserveStaging) staging.setAutoRemove(false);
    return result;
}

bool ModManager::installZip(const QString& source, QString* error) {
    if (error) error->clear();
    if (!safePathSyntax(source, error)) return false;
    const QFileInfo input(source);
    if (!input.isFile() || isLinked(input) || !safeAncestors(input.absolutePath(), error))
        return fail(error, QStringLiteral("Select a regular ZIP file without linked ancestors."));
    if (!prepareLockParent(root_, error)) return false;
    QLockFile lock(root_ + ".mods.lock");
    if (!lockProfile(lock, root_, error)) return false;
    if (!prepareProfile(root_, error)) return false;
    QTemporaryDir staging(root_ + "/.install-XXXXXX");
    if (!staging.isValid())
        return fail(error, QStringLiteral("Cannot create ZIP staging directory."));
    if (!extractZip(source, staging.path() + "/content", error)) return false;
    bool preserveStaging = false;
    const bool result = installStaged(staging.path(), preserveStaging, error);
    if (preserveStaging) staging.setAutoRemove(false);
    return result;
}

bool ModManager::installDependencyZip(const QString& source, const QString& expectedId,
                                      const QString& expectedVersion, QString* error) {
    if (error) error->clear();
    if (expectedId.isEmpty()) return fail(error, QStringLiteral("No approved dependency ID."));
    if (!safePathSyntax(source, error)) return false;
    const QFileInfo input(source);
    if (!input.isFile() || isLinked(input) || !safeAncestors(input.absolutePath(), error))
        return fail(error, QStringLiteral("Select a regular dependency ZIP file."));
    if (!prepareLockParent(root_, error)) return false;
    QLockFile lock(root_ + ".mods.lock");
    if (!lockProfile(lock, root_, error) || !prepareProfile(root_, error)) return false;
    QTemporaryDir staging(root_ + "/.install-XXXXXX");
    if (!staging.isValid()) return fail(error, QStringLiteral("Cannot stage the dependency."));
    if (!extractZip(source, staging.path() + "/content", error)) return false;
    QList<InstalledMod> candidates;
    if (!discover(staging.path() + "/content", candidates, 0, error)) return false;
    if (candidates.size() != 1 ||
        candidates.first().manifest.uniqueId.compare(expectedId, Qt::CaseInsensitive) != 0)
        return fail(error,
                    QStringLiteral("The archive must contain exactly the approved dependency."));
    if (!expectedVersion.isEmpty()) {
        const auto cmp = DependencyResolver::compareVersions(candidates.first().manifest.version,
                                                             expectedVersion);
        if (!cmp || *cmp != 0)
            return fail(error, QStringLiteral("The archive dependency version is incompatible."));
    }
    bool preserve = false;
    const bool success = installStaged(staging.path(), preserve, error);
    if (preserve) staging.setAutoRemove(false);
    return success;
}

bool ModManager::setEnabled(const QString& uniqueId, bool enabled, QString* error) {
    if (error) error->clear();
    if (!prepareLockParent(root_, error)) return false;
    QLockFile lock(root_ + ".mods.lock");
    if (!lockProfile(lock, root_, error)) return false;
    if (!prepareProfile(root_, error)) return false;
    QString scanError;
    const auto installed = scan(root_, &scanError);
    if (!scanError.isEmpty()) return fail(error, scanError);
    for (const auto& mod : installed) {
        if (mod.manifest.uniqueId.compare(uniqueId, Qt::CaseInsensitive) != 0) continue;
        if (mod.enabled == enabled) return true;
        const QString parent = root_ + (enabled ? "/Mods" : "/DisabledMods");
        const QString name = QFileInfo(mod.directory).fileName();
        const auto entries = QDir(parent).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                        QDir::Hidden | QDir::System);
        if (std::any_of(entries.cbegin(), entries.cend(), [&](const QFileInfo& entry) {
                return pathKey(entry.fileName()) == pathKey(name);
            }))
            return fail(
                error,
                QStringLiteral("The destination already contains a conflicting mod directory."));
        if (!QDir().rename(mod.directory, parent + '/' + name))
            return fail(error,
                        QStringLiteral("Cannot move the mod. No existing files were replaced."));
        return true;
    }
    return fail(error, QStringLiteral("The selected mod no longer exists."));
}

bool ModManager::remove(const QString& uniqueId, QString* error) {
    if (error) error->clear();
    if (!prepareLockParent(root_, error)) return false;
    QLockFile lock(root_ + ".mods.lock");
    if (!lockProfile(lock, root_, error)) return false;
    if (!prepareProfile(root_, error)) return false;
    QString scanError;
    const auto installed = scan(root_, &scanError);
    if (!scanError.isEmpty()) return fail(error, scanError);
    for (const auto& mod : installed) {
        if (mod.manifest.uniqueId.compare(uniqueId, Qt::CaseInsensitive) != 0) continue;
        // Retain removed mods, including user configuration, in a recoverable
        // profile-local trash directory; never recursively delete user files.
        const QString trash = root_ + "/RemovedMods";
        if (!safeAncestors(trash, error) || !QDir().mkpath(trash))
            return fail(error,
                        QStringLiteral("Cannot create the recoverable removed-mod directory."));
        const QString target = trash + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!QDir().rename(mod.directory, target))
            return fail(
                error,
                QStringLiteral("Cannot move the mod to the recoverable removed-mod directory."));
        return true;
    }
    return fail(error, QStringLiteral("The selected mod no longer exists."));
}
}  // namespace prismatic
