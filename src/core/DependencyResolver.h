#pragma once

#include <QMap>

#include "ModManager.h"

namespace prismatic {

enum class DependencyStatus {
    Satisfied,
    MissingRequired,
    MissingOptional,
    Disabled,
    Outdated,
    InvalidVersion,
    Circular,
    DuplicateInstalled,
    SmapiTooOld,
    GameTooOld,
    UnknownRuntimeVersion
};

struct DependencyDiagnostic {
    DependencyStatus status = DependencyStatus::Satisfied;
    QString modId;
    QString dependencyId;
    QString minimumVersion;
    QString installedVersion;
    bool required = true;
    QString message;
};

struct DependencyRequest {
    QString uniqueId;
    QString minimumVersion;
    QStringList requiredBy;
};

struct DependencyResolution {
    QList<DependencyDiagnostic> diagnostics;
    QList<DependencyRequest> requests;
    QMap<QString, QStringList> graph;
    bool hasBlockingIssues() const;
    QString summary() const;
};

class DependencyResolver {
   public:
    // Only profileMods satisfy requirements. Metadata supplies transitive edges,
    // never installed status. Empty runtime versions report unknown compatibility.
    static DependencyResolution resolve(const QList<InstalledMod>& profileMods,
                                        const QList<ModManifest>& metadata = {},
                                        const QString& smapiVersion = {},
                                        const QString& gameVersion = {});
    // SMAPI-compatible numeric version cores and SemVer prerelease precedence.
    // Invalid/uncomparable input returns nullopt rather than guessing.
    static std::optional<int> compareVersions(const QString& left, const QString& right);
};

}  // namespace prismatic
