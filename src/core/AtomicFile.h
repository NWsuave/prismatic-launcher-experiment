#pragma once

#include <QByteArray>
#include <QString>

namespace prismatic {

// Reject links anywhere along a launcher-owned path before creating files.
bool validateStoragePath(const QString& path, QString* error = nullptr);

// Replaces a regular file atomically using a sibling temporary file. Never
// enables direct writes to the destination; failed writes preserve its bytes.
bool writeFileAtomically(const QString& path, const QByteArray& bytes, QString* error = nullptr);

}  // namespace prismatic
