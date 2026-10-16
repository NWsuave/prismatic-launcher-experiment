#include "DependencyResolver.h"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <functional>
#include <tuple>

namespace prismatic {
namespace {
struct Version {
    QStringList core;
    QStringList pre;
};
std::optional<Version> version(const QString& text) {
    static const QRegularExpression syntax(
        QStringLiteral("^(0|[1-9][0-9]*)(?:\\.(0|[1-9][0-9]*)){0,3}(?:-([0-9A-Za-z-]+(?:\\.[0-9A-"
                       "Za-z-]+)*))?(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?$"));
    if (text.size() > 256 || !syntax.match(text).hasMatch()) return std::nullopt;
    const auto base = text.section('+', 0, 0);
    Version result{base.section('-', 0, 0).split('.'), {}};
    const auto dash = base.indexOf('-');
    if (dash >= 0) result.pre = base.mid(dash + 1).split('.');
    static const QRegularExpression numeric(QStringLiteral("^[0-9]+$"));
    if (std::any_of(result.pre.cbegin(), result.pre.cend(), [](const auto& identifier) {
            return identifier.size() > 1 && identifier.startsWith('0') &&
                   numeric.match(identifier).hasMatch();
        }))
        return std::nullopt;
    while (result.core.size() < 4) result.core.append("0");
    return result;
}
int digits(const QString& a, const QString& b) {
    if (a.size() != b.size()) return a.size() > b.size() ? 1 : -1;
    const int cmp = QString::compare(a, b, Qt::CaseSensitive);
    return cmp == 0 ? 0 : (cmp > 0 ? 1 : -1);
}
QString key(const QString& id) { return id.toCaseFolded(); }
}  // namespace
std::optional<int> DependencyResolver::compareVersions(const QString& left, const QString& right) {
    const auto a = version(left), b = version(right);
    if (!a || !b) return std::nullopt;
    for (qsizetype i = 0; i < a->core.size(); ++i) {
        const int cmp = digits(a->core[i], b->core[i]);
        if (cmp) return cmp;
    }
    if (a->pre.isEmpty() || b->pre.isEmpty())
        return a->pre.isEmpty() == b->pre.isEmpty() ? 0 : (a->pre.isEmpty() ? 1 : -1);
    static const QRegularExpression numeric(QStringLiteral("^[0-9]+$"));
    for (qsizetype i = 0; i < std::min(a->pre.size(), b->pre.size()); ++i) {
        const bool an = numeric.match(a->pre[i]).hasMatch(),
                   bn = numeric.match(b->pre[i]).hasMatch();
        const int cmp =
            an && bn ? digits(a->pre[i], b->pre[i])
                     : (an != bn ? (an ? -1 : 1)
                                 : QString::compare(a->pre[i], b->pre[i], Qt::CaseSensitive));
        if (cmp) return cmp > 0 ? 1 : -1;
    }
    return a->pre.size() == b->pre.size() ? 0 : (a->pre.size() > b->pre.size() ? 1 : -1);
}
bool DependencyResolution::hasBlockingIssues() const {
    return std::any_of(diagnostics.cbegin(), diagnostics.cend(), [](const auto& d) {
        return d.required && d.status != DependencyStatus::Satisfied &&
               d.status != DependencyStatus::MissingOptional &&
               d.status != DependencyStatus::UnknownRuntimeVersion;
    });
}
QString DependencyResolution::summary() const {
    QStringList lines;
    for (const auto& d : diagnostics)
        if (d.status != DependencyStatus::Satisfied) lines.append(d.message);
    return lines.join('\n');
}
DependencyResolution DependencyResolver::resolve(const QList<InstalledMod>& profileMods,
                                                 const QList<ModManifest>& metadata,
                                                 const QString& smapiVersion,
                                                 const QString& gameVersion) {
    DependencyResolution result;
    QMap<QString, InstalledMod> installed;
    QMap<QString, ModManifest> known;
    for (const auto& m : metadata) known.insert(key(m.uniqueId), m);
    for (const auto& m : profileMods) {
        const QString id = key(m.manifest.uniqueId);
        if (installed.contains(id))
            result.diagnostics.append({DependencyStatus::DuplicateInstalled,
                                       m.manifest.uniqueId,
                                       m.manifest.uniqueId,
                                       {},
                                       {},
                                       true,
                                       "Duplicate installed ID: " + m.manifest.uniqueId});
        installed.insert(id, m);
        known.insert(id, m.manifest);
    }
    QMap<QString, DependencyRequest> requests;
    QSet<QString> visited, active;
    std::function<void(const QString&, int)> walk = [&](const QString& id, int depth) {
        if (depth > 256 || active.contains(id)) {
            result.diagnostics.append({DependencyStatus::Circular,
                                       id,
                                       id,
                                       {},
                                       {},
                                       true,
                                       "Circular or excessively deep dependency graph at " + id});
            return;
        }
        if (visited.contains(id) || !known.contains(id)) return;
        visited.insert(id);
        active.insert(id);
        const auto manifest = known.value(id);
        for (const auto& runtime :
             {std::make_tuple(manifest.minimumApiVersion, smapiVersion,
                              DependencyStatus::SmapiTooOld, QString("SMAPI")),
              std::make_tuple(manifest.minimumGameVersion, gameVersion,
                              DependencyStatus::GameTooOld, QString("Stardew Valley"))}) {
            const auto& minimum = std::get<0>(runtime);
            if (minimum.isEmpty()) continue;
            const auto& current = std::get<1>(runtime);
            DependencyStatus status = DependencyStatus::Satisfied;
            if (current.isEmpty())
                status = DependencyStatus::UnknownRuntimeVersion;
            else {
                const auto cmp = compareVersions(current, minimum);
                if (!cmp)
                    status = DependencyStatus::InvalidVersion;
                else if (*cmp < 0)
                    status = std::get<2>(runtime);
            }
            if (status != DependencyStatus::Satisfied)
                result.diagnostics.append(
                    {status, manifest.uniqueId, std::get<3>(runtime), minimum, current, true,
                     QString("%1 requires %2 %3 or later (%4).")
                         .arg(manifest.uniqueId, std::get<3>(runtime), minimum,
                              current.isEmpty() ? QString("installed version unknown") : current)});
        }
        auto dependencies = manifest.dependencies;
        if (manifest.contentPackFor) dependencies.append(*manifest.contentPackFor);
        // Merge duplicate declarations without weakening required/minimum semantics.
        QMap<QString, ModDependency> unique;
        for (const auto& dep : dependencies) {
            const auto target = key(dep.uniqueId);
            if (!unique.contains(target))
                unique.insert(target, dep);
            else {
                auto& merged = unique[target];
                merged.isRequired |= dep.isRequired;
                if (!dep.minimumVersion.isEmpty()) {
                    const auto cmp = compareVersions(dep.minimumVersion, merged.minimumVersion);
                    if (merged.minimumVersion.isEmpty() || !cmp || *cmp > 0)
                        merged.minimumVersion = dep.minimumVersion;
                }
            }
        }
        for (const auto& dep : unique) {
            const QString target = key(dep.uniqueId);
            if (dep.isRequired || (installed.contains(target) && installed[target].enabled))
                result.graph[id].append(target);
            DependencyStatus status = DependencyStatus::Satisfied;
            QString current;
            if (!dep.minimumVersion.isEmpty() && !version(dep.minimumVersion))
                status = DependencyStatus::InvalidVersion;
            else if (!installed.contains(target))
                status = dep.isRequired ? DependencyStatus::MissingRequired
                                        : DependencyStatus::MissingOptional;
            else {
                const auto& found = installed[target];
                current = found.manifest.version;
                if (!found.enabled)
                    status = DependencyStatus::Disabled;
                else if (!dep.minimumVersion.isEmpty()) {
                    const auto cmp = compareVersions(current, dep.minimumVersion);
                    if (!cmp)
                        status = DependencyStatus::InvalidVersion;
                    else if (*cmp < 0)
                        status = DependencyStatus::Outdated;
                }
            }
            QString state;
            switch (status) {
                case DependencyStatus::Satisfied:
                    state = "satisfied";
                    break;
                case DependencyStatus::MissingRequired:
                    state = "missing in this profile";
                    break;
                case DependencyStatus::MissingOptional:
                    state = "optional, not installed";
                    break;
                case DependencyStatus::Disabled:
                    state = "disabled in this profile";
                    break;
                case DependencyStatus::Outdated:
                    state = "installed version " + current + " is too old";
                    break;
                default:
                    state = "invalid version metadata";
                    break;
            }
            result.diagnostics.append(
                {status, manifest.uniqueId, dep.uniqueId, dep.minimumVersion, current,
                 dep.isRequired,
                 QString("%1 → %2%3: %4")
                     .arg(manifest.uniqueId, dep.uniqueId,
                          dep.minimumVersion.isEmpty() ? QString() : " >= " + dep.minimumVersion,
                          state)});
            if (dep.isRequired && status == DependencyStatus::MissingRequired) {
                auto& request = requests[target];
                if (request.uniqueId.isEmpty()) request.uniqueId = dep.uniqueId;
                if (!request.requiredBy.contains(manifest.uniqueId))
                    request.requiredBy.append(manifest.uniqueId);
                const auto cmp = compareVersions(dep.minimumVersion, request.minimumVersion);
                if (request.minimumVersion.isEmpty() || (cmp && *cmp > 0))
                    request.minimumVersion = dep.minimumVersion;
            }
            if (dep.isRequired || (installed.contains(target) && installed[target].enabled))
                walk(target, depth + 1);
        }
        active.remove(id);
    };
    for (const auto& m : profileMods)
        if (m.enabled) walk(key(m.manifest.uniqueId), 0);
    result.requests = requests.values();
    return result;
}
}  // namespace prismatic
