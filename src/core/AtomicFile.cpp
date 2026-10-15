#include "AtomicFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

#ifdef Q_OS_UNIX
#include <unistd.h>

#include <cstdio>
#endif

namespace prismatic {

bool validateStoragePath(const QString& path, QString* error) {
    if (error) error->clear();
    if (path.isEmpty() || path.contains(QChar::Null) ||
        QDir::fromNativeSeparators(path).split('/').contains("..")) {
        if (error) *error = "The storage path is empty or contains invalid characters.";
        return false;
    }
    QString part = QFileInfo(path).absoluteFilePath();
    while (true) {
        const QFileInfo info(part);
        bool linked = info.isSymLink();
#ifdef Q_OS_WIN
        linked = linked || info.isJunction();
#endif
        if (linked) {
            if (error) *error = "Storage paths cannot contain symbolic links or junctions.";
            return false;
        }
        const QString parent = info.absolutePath();
        if (parent == part) break;
        part = parent;
    }
    return true;
}

bool writeFileAtomically(const QString& path, const QByteArray& bytes, QString* error) {
    if (error) error->clear();
    const auto fail = [error](const QString& reason) {
        if (error) *error = reason;
        return false;
    };
    if (!validateStoragePath(path, error)) return false;
    const QFileInfo target(path);
    if (path.isEmpty() || path.contains(QChar::Null) || target.isSymLink() ||
        (target.exists() && !target.isFile()))
        return fail("Choose a regular destination file without symbolic links.");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return fail("Could not open atomic save file: " + file.errorString());
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return fail("Could not write atomic save file: " + file.errorString());
    }
    if (file.commit()) return true;
#ifdef Q_OS_UNIX
    // Some restricted /proc mounts prevent Qt from naming an O_TMPFILE inode.
    // A named sibling plus POSIX rename preserves atomic replacement there.
    const QString temporary = path + ".save-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QFile named(temporary);
    if (!named.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail("Could not create atomic save file: " + named.errorString());
    const bool written = named.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) &&
                         named.write(bytes) == bytes.size() && named.flush() &&
                         ::fsync(named.handle()) == 0;
    named.close();
    if (!written || ::rename(QFile::encodeName(temporary).constData(),
                             QFile::encodeName(path).constData()) != 0) {
        named.remove();
        return fail("Could not atomically replace the destination file.");
    }
    return true;
#else
    return fail("Could not commit atomic save file: " + file.errorString());
#endif
}

}  // namespace prismatic
