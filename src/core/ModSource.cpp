#include "ModSource.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <utility>

namespace prismatic {
namespace {
constexpr qint64 archiveLimit = 256LL * 1024 * 1024;
constexpr qint64 metadataLimit = 4LL * 1024 * 1024;
bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}
bool https(const QUrl& url) {
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty() &&
           url.userInfo().isEmpty() && (url.port() == -1 || url.port() == 443) &&
           url.fragment().isEmpty();
}
bool repositoryValid(const QString& repository) {
    static const QRegularExpression syntax(
        "^[A-Za-z0-9][A-Za-z0-9_.-]{0,99}/[A-Za-z0-9][A-Za-z0-9_.-]{0,99}$");
    return syntax.match(repository).hasMatch() && !repository.contains("..");
}
bool assetUrl(const QUrl& url, const QString& repository) {
    return https(url) && url.host().compare("github.com", Qt::CaseInsensitive) == 0 &&
           url.path().startsWith('/' + repository + "/releases/download/", Qt::CaseInsensitive) &&
           !url.path().contains("/../") && url.query().isEmpty();
}
}  // namespace
std::optional<ModSourceCatalog> ModSourceCatalog::parse(const QByteArray& bytes, QString* error) {
    if (error) error->clear();
    if (bytes.size() > 1024 * 1024) {
        fail(error, "Source catalog exceeds 1 MiB.");
        return {};
    }
    QJsonParseError jsonError;
    const auto doc = QJsonDocument::fromJson(bytes, &jsonError);
    if (!doc.isObject() || jsonError.error != QJsonParseError::NoError ||
        doc.object().value("schemaVersion").toInt() != 1 ||
        !doc.object().value("sources").isArray()) {
        fail(error, "Invalid source catalog schema.");
        return {};
    }
    ModSourceCatalog catalog;
    QSet<QString> ids;
    const auto list = doc.object().value("sources").toArray();
    if (list.size() > 1000) {
        fail(error, "Too many source catalog entries.");
        return {};
    }
    static const QRegularExpression idSyntax("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,199}$");
    for (const auto& item : list) {
        if (!item.isObject()) {
            fail(error, "Invalid source catalog entry.");
            return {};
        }
        const auto object = item.toObject();
        ModSource source;
        source.uniqueId = object.value("uniqueId").toString();
        source.repository = object.value("repository").toString();
        source.publisher = object.value("publisher").toString();
        source.identityEvidence = QUrl(object.value("identityEvidence").toString());
        source.officialPage = QUrl(object.value("officialPage").toString());
        source.assetPattern = object.value("assetPattern").toString();
        source.tagPrefix = object.value("tagPrefix").toString();
        source.automatic = object.value("automatic").toBool(false);
        const QRegularExpression pattern(source.assetPattern);
        if (!idSyntax.match(source.uniqueId).hasMatch() ||
            ids.contains(source.uniqueId.toCaseFolded()) || source.publisher.isEmpty() ||
            source.publisher.size() > 200 || !https(source.identityEvidence) ||
            !https(source.officialPage) ||
            (source.automatic &&
             (!repositoryValid(source.repository) || !pattern.isValid() ||
              source.assetPattern.size() > 200 || !source.assetPattern.startsWith('^') ||
              !source.assetPattern.endsWith('$') || source.tagPrefix.size() > 100))) {
            fail(error, "Invalid or duplicate source catalog policy.");
            return {};
        }
        ids.insert(source.uniqueId.toCaseFolded());
        catalog.sources_.append(source);
    }
    return catalog;
}
std::optional<ModSourceCatalog> ModSourceCatalog::load(const QString& path, QString* error) {
    if (error) error->clear();
    if (path.contains(QChar::Null) ||
        QDir::fromNativeSeparators(path).split('/').contains(QStringLiteral(".."))) {
        fail(error, "Unsafe source catalog path.");
        return {};
    }
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile() || info.size() > 1024 * 1024) {
        fail(error, "Source catalog is missing, linked, or too large.");
        return {};
    }
    QString ancestor = info.absolutePath();
    while (!ancestor.isEmpty()) {
        const QFileInfo parent(ancestor);
        if (parent.isSymLink()
#ifdef Q_OS_WIN
            || parent.isJunction()
#endif
        ) {
            fail(error, "Linked source catalog ancestors are not allowed.");
            return {};
        }
        if (parent.absolutePath() == ancestor) break;
        ancestor = parent.absolutePath();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, "Cannot read source catalog.");
        return {};
    }
    return parse(file.read(1024 * 1024 + 1), error);
}
ModSourceCatalog ModSourceCatalog::bundled() {
    QFile file(":/prismatic/catalog/sources.json");
    if (file.open(QIODevice::ReadOnly)) {
        if (const auto catalog = parse(file.readAll())) return *catalog;
    }
    // Safe fallback if a consumer links core without the application resources.
    ModSourceCatalog catalog;
    ModSource source;
    source.uniqueId = "Pathoschild.ContentPatcher";
    source.publisher = "Pathoschild";
    source.officialPage = QUrl("https://www.nexusmods.com/stardewvalley/mods/1915");
    source.identityEvidence = QUrl("https://stardewvalleywiki.com/Modding:Content_Patcher");
    catalog.sources_.append(source);
    return catalog;
}
std::optional<ModSource> ModSourceCatalog::find(const QString& uniqueId) const {
    const auto found = std::find_if(sources_.cbegin(), sources_.cend(), [&](const auto& source) {
        return source.uniqueId.compare(uniqueId, Qt::CaseInsensitive) == 0;
    });
    if (found != sources_.cend()) return *found;
    return {};
}
QList<ModSource> ModSourceCatalog::entries() const { return sources_; }
GitHubReleaseProvider::GitHubReleaseProvider(ModFetcher fetcher)
    : fetcher_(fetcher ? std::move(fetcher) : fetchHttps) {}
ModFetchResult GitHubReleaseProvider::fetchHttps(const QUrl& url, qint64 maximumBytes) {
    if (!https(url) || maximumBytes <= 0 || maximumBytes > archiveLimit)
        return {{}, "Unsafe download URL or limit."};
    if (QCoreApplication::instance() &&
        QThread::currentThread() == QCoreApplication::instance()->thread())
        return {{}, "Network downloads must run on a worker thread."};
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "PrismaticLauncher/0.2");
    request.setRawHeader("Accept", "application/vnd.github+json");
    // No redirects: asset handling below explicitly permits only GitHub CDN hosts.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QByteArray bytes;
    QString error;
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
        error = "Download timed out.";
        reply->abort();
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        const QByteArray chunk = reply->read(maximumBytes - bytes.size() + 1);
        bytes += chunk;
        const auto declared = reply->header(QNetworkRequest::ContentLengthHeader);
        if (bytes.size() > maximumBytes ||
            (declared.isValid() && declared.toLongLong() > maximumBytes)) {
            error = "Download exceeds its size limit.";
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    deadline.start(60000);
    loop.exec();
    deadline.stop();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (!redirect.isEmpty() && error.isEmpty()) {
        // GitHub asset URLs redirect once to their dedicated binary CDN. API
        // redirects, repository renames, and arbitrary redirect hosts fail closed.
        const QUrl destination = url.resolved(redirect);
        const QString host = destination.host().toLower();
        if (url.host() == "github.com" && url.path().contains("/releases/download/") &&
            https(destination) &&
            (host == "release-assets.githubusercontent.com" ||
             host == "objects.githubusercontent.com"))
            return fetchHttps(destination, maximumBytes);
        return {{}, "Untrusted download redirect."};
    }
    if (error.isEmpty() && (reply->error() != QNetworkReply::NoError || status != 200))
        error = "Download failed: " + reply->errorString();
    if (error.isEmpty()) {
        bytes += reply->read(maximumBytes - bytes.size() + 1);
        if (bytes.size() > maximumBytes) error = "Download exceeds its size limit.";
    }
    return {error.isEmpty() ? bytes : QByteArray(), error};
}
std::optional<ModReleaseAsset> GitHubReleaseProvider::resolve(const ModSource& source,
                                                              const QString& minimumVersion,
                                                              QString* error) {
    if (error) error->clear();
    if (!source.automatic || !repositoryValid(source.repository) || source.publisher.isEmpty() ||
        !https(source.identityEvidence) || !https(source.officialPage)) {
        fail(error, "Automatic download unavailable; use the official project page.");
        return {};
    }
    const QRegularExpression pattern(source.assetPattern);
    if (!pattern.isValid() || !source.assetPattern.startsWith('^') ||
        !source.assetPattern.endsWith('$')) {
        fail(error, "No validated asset naming policy.");
        return {};
    }
    const QUrl api("https://api.github.com/repos/" + source.repository + "/releases?per_page=100");
    const auto response = fetcher_(api, metadataLimit);
    if (!response.ok() || response.bytes.size() > metadataLimit) {
        fail(error, response.error.isEmpty() ? QString("Release metadata exceeds limit.")
                                             : response.error);
        return {};
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(response.bytes, &parseError);
    if (!doc.isArray() || parseError.error != QJsonParseError::NoError) {
        fail(error, "Invalid release metadata.");
        return {};
    }
    std::optional<ModReleaseAsset> best;
    for (const auto& item : doc.array()) {
        const auto release = item.toObject();
        if (!release.value("draft").isBool() || !release.value("prerelease").isBool() ||
            release.value("draft").toBool() || release.value("prerelease").toBool())
            continue;
        const QString tag = release.value("tag_name").toString();
        if (!tag.startsWith(source.tagPrefix)) continue;
        QString version = tag.mid(source.tagPrefix.size());
        if (version.startsWith('v')) version.remove(0, 1);
        const auto valid = DependencyResolver::compareVersions(version, version);
        const auto compatible = minimumVersion.isEmpty()
                                    ? std::optional<int>(0)
                                    : DependencyResolver::compareVersions(version, minimumVersion);
        if (!valid || !compatible || *compatible < 0) continue;
        QList<ModReleaseAsset> candidates;
        for (const auto& assetItem : release.value("assets").toArray()) {
            const auto data = assetItem.toObject();
            const QString name = data.value("name").toString();
            const QUrl url(data.value("browser_download_url").toString());
            const double size = data.value("size").toDouble(-1);
            if (!pattern.match(name).hasMatch() || !name.endsWith(".zip", Qt::CaseInsensitive) ||
                name.contains('/') || name.contains('\\') || !assetUrl(url, source.repository) ||
                url.fileName() != name || !data.value("size").isDouble() || !std::isfinite(size) ||
                size <= 0 || size > archiveLimit || std::floor(size) != size ||
                data.value("state").toString() != "uploaded")
                continue;
            const QString digest = data.value("digest").toString();
            QByteArray hash;
            if (!digest.isEmpty()) {
                static const QRegularExpression sha("^sha256:([a-fA-F0-9]{64})$");
                const auto match = sha.match(digest);
                if (!match.hasMatch()) continue;
                hash = match.captured(1).toLatin1().toLower();
            }
            candidates.append({source.uniqueId, version, name, url, static_cast<qint64>(size), hash,
                               source.repository});
        }
        // Ambiguous releases require a catalog policy update instead of guessing.
        if (candidates.size() != 1) continue;
        if (!best || *DependencyResolver::compareVersions(version, best->version) > 0)
            best = candidates.first();
    }
    if (!best) fail(error, "No unambiguous compatible release asset; manual download required.");
    return best;
}
bool GitHubReleaseProvider::install(const ModSource& source, const ModReleaseAsset& asset,
                                    const QString& profileRoot, bool userApproved, QString* error) {
    if (error) error->clear();
    if (!userApproved) return fail(error, "Explicit user approval is required before downloading.");
    if (!source.automatic || source.publisher.isEmpty() || !https(source.identityEvidence) ||
        asset.uniqueId.compare(source.uniqueId, Qt::CaseInsensitive) != 0 ||
        asset.repository != source.repository || !repositoryValid(source.repository) ||
        !assetUrl(asset.url, source.repository) || asset.url.fileName() != asset.name ||
        !QRegularExpression(source.assetPattern).match(asset.name).hasMatch() || asset.size <= 0 ||
        asset.size > archiveLimit ||
        !DependencyResolver::compareVersions(asset.version, asset.version))
        return fail(error, "Downloaded asset does not match the approved source policy.");
    QString scanError;
    const auto installed = ModManager(profileRoot).mods(&scanError);
    if (!scanError.isEmpty()) return fail(error, scanError);
    if (std::any_of(installed.cbegin(), installed.cend(), [&](const auto& mod) {
            return mod.manifest.uniqueId.compare(asset.uniqueId, Qt::CaseInsensitive) == 0;
        }))
        return fail(error,
                    "Dependency already installed; replacement requires manual removal approval.");
    const auto response = fetcher_(asset.url, asset.size);
    if (!response.ok()) return fail(error, response.error);
    if (response.bytes.size() != asset.size) return fail(error, "Downloaded asset size mismatch.");
    if (!asset.sha256.isEmpty() &&
        QCryptographicHash::hash(response.bytes, QCryptographicHash::Sha256).toHex() !=
            asset.sha256)
        return fail(error, "Downloaded asset SHA-256 mismatch.");
    // Keep downloaded executables in a private disposable directory. Only the
    // archive installer may write into the profile after all validation passes.
    // macOS commonly exposes /var (and /tmp) through a system symlink. Resolve
    // the trusted OS temporary root before the strict archive path validation.
    const QString temporaryRoot = QDir(QDir::tempPath()).canonicalPath();
    if (temporaryRoot.isEmpty()) return fail(error, "Cannot locate download staging storage.");
    QTemporaryDir staging(temporaryRoot + "/prismatic-download-XXXXXX");
    if (!staging.isValid()) return fail(error, "Cannot create dependency download staging.");
    QFile file(staging.path() + "/dependency.zip");
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
        file.write(response.bytes) != response.bytes.size() || !file.flush())
        return fail(error, "Cannot stage downloaded dependency.");
    file.close();
    return ModManager(profileRoot)
        .installDependencyZip(file.fileName(), asset.uniqueId, asset.version, error);
}
}  // namespace prismatic
