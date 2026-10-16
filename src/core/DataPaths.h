#pragma once

#include <QString>

namespace prismatic {
QString defaultDataDirectory();
// Copies executable-relative legacy data only into an absent destination. The
// source is retained; unsafe trees or active legacy sessions leave it untouched.
bool migrateLegacyData(const QString& legacy, const QString& destination, QString* error = nullptr);
}  // namespace prismatic
