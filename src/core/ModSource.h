#pragma once

#include <QByteArray>
#include <QUrl>
#include <functional>

#include "DependencyResolver.h"

namespace prismatic {

struct ModSource {
    QString uniqueId;
    QString repository;  // Reviewed owner/repo, never derived from search/update keys.
    QString publisher;
    QUrl identityEvidence;
    QUrl officialPage;
    QString assetPattern;
    QString tagPrefix;
    bool automatic = false;
};

class ModSourceCatalog {
   public:
    static ModSourceCatalog bundled();
    // A locally maintained trust policy. Loading does not fetch any URL.
    static std::optional<ModSourceCatalog> load(const QString& path, QString* error = nullptr);
    static std::optional<ModSourceCatalog> parse(const QByteArray& bytes, QString* error = nullptr);
    std::optional<ModSource> find(const QString& uniqueId) const;
    QList<ModSource> entries() const;

   private:
    QList<ModSource> sources_;
};

struct ModFetchResult {
    QByteArray bytes;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
// Fetchers must enforce limit/timeouts; injected local fixtures never use a socket.
using ModFetcher = std::function<ModFetchResult(const QUrl&, qint64)>;

struct ModReleaseAsset {
    QString uniqueId;
    QString version;
    QString name;
    QUrl url;
    qint64 size = 0;
    QByteArray sha256;  // Optional GitHub publisher-provided digest, hex.
    QString repository;
};

class ModSourceProvider {
   public:
    virtual ~ModSourceProvider() = default;
    virtual std::optional<ModReleaseAsset> resolve(const ModSource& source,
                                                   const QString& minimumVersion,
                                                   QString* error = nullptr) = 0;
};

class GitHubReleaseProvider final : public ModSourceProvider {
   public:
    explicit GitHubReleaseProvider(ModFetcher fetcher = {});
    // The caller must obtain explicit consent before resolve (network) or install.
    std::optional<ModReleaseAsset> resolve(const ModSource& source, const QString& minimumVersion,
                                           QString* error = nullptr) override;
    // Worker-thread service. Explicit approval is mandatory; no replacements.
    bool install(const ModSource& source, const ModReleaseAsset& asset, const QString& profileRoot,
                 bool userApproved, QString* error = nullptr);
    static ModFetchResult fetchHttps(const QUrl& url, qint64 maximumBytes);

   private:
    ModFetcher fetcher_;
};

}  // namespace prismatic
