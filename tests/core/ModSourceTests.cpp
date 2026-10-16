#include <archive.h>
#include <archive_entry.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "ModSource.h"

using namespace prismatic;
namespace {
ModSource source() {
    return {"Test.Dependency",
            "Verified/Project",
            "Verified",
            QUrl("https://example.org/publisher"),
            QUrl("https://example.org/mod"),
            "^Dependency-[0-9.]+\\.zip$",
            "v",
            true};
}
QByteArray zip(const QString& id = "Test.Dependency",
               const QString& path = "Dependency/manifest.json") {
    QByteArray bytes(65536, '\0');
    size_t used = 0;
    auto* writer = archive_write_new();
    archive_write_set_format_zip(writer);
    archive_write_open_memory(writer, bytes.data(), static_cast<size_t>(bytes.size()), &used);
    const QByteArray manifest =
        QJsonDocument(
            QJsonObject{
                {"Name", "Dependency"}, {"Author", "Test"}, {"Version", "2.0"}, {"UniqueID", id}})
            .toJson();
    auto* entry = archive_entry_new();
    const QByteArray filename = path.toUtf8();
    archive_entry_set_pathname(entry, filename.constData());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, manifest.size());
    archive_write_header(writer, entry);
    archive_write_data(writer, manifest.constData(), static_cast<size_t>(manifest.size()));
    archive_entry_free(entry);
    archive_write_close(writer);
    archive_write_free(writer);
    bytes.resize(static_cast<qsizetype>(used));
    return bytes;
}
ModReleaseAsset asset(const QByteArray& bytes) {
    return {"Test.Dependency",
            "2.0",
            "Dependency-2.0.zip",
            QUrl("https://github.com/Verified/Project/releases/download/v2.0/Dependency-2.0.zip"),
            bytes.size(),
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(),
            "Verified/Project"};
}
QJsonObject release(const QByteArray& bytes) {
    const auto selected = asset(bytes);
    return {{"draft", false},
            {"prerelease", false},
            {"tag_name", "v2.0"},
            {"assets", QJsonArray{QJsonObject{
                           {"name", selected.name},
                           {"browser_download_url", selected.url.toString()},
                           {"size", static_cast<double>(bytes.size())},
                           {"state", "uploaded"},
                           {"digest", "sha256:" + QString::fromLatin1(selected.sha256)}}}}};
}
}  // namespace
class ModSourceTests : public QObject {
    Q_OBJECT
   private slots:
    void catalogValidation() {
        QString error;
        QVERIFY(!ModSourceCatalog::parse("{}", &error));
        QVERIFY(!ModSourceCatalog::parse(
            R"({"schemaVersion":1,"sources":[{"uniqueId":"Test.X","automatic":true}]})", &error));
        const auto bundled = ModSourceCatalog::bundled();
        QVERIFY(bundled.find("pathoschild.contentpatcher"));
        QVERIFY(!bundled.find("Arbitrary.Mod"));
    }
    void selectsOnlyUnambiguousCompatibleAssets() {
        const auto bytes = zip();
        auto json = release(bytes);
        GitHubReleaseProvider provider([&](const QUrl& url, qint64 maximum) {
            if (url.host() != "api.github.com" || maximum != 4LL * 1024 * 1024)
                return ModFetchResult{{}, "Wrong metadata request"};
            return ModFetchResult{QJsonDocument(QJsonArray{json}).toJson(), {}};
        });
        QString error;
        const auto resolved = provider.resolve(source(), "1.5", &error);
        QVERIFY2(resolved.has_value(), qPrintable(error));
        QCOMPARE(resolved->version, QString("2.0"));
        QVERIFY(!provider.resolve(source(), "3.0", &error));
        auto assets = json.value("assets").toArray();
        assets.append(assets.first());
        json["assets"] = assets;
        QVERIFY(!provider.resolve(source(), "1.0", &error));
        auto untrusted = source();
        untrusted.automatic = false;
        GitHubReleaseProvider forbiddenFetcher([](const QUrl&, qint64) {
            QTest::qFail("An untrusted source must never invoke its fetcher.", __FILE__, __LINE__);
            return ModFetchResult{{}, "Unexpected fetch"};
        });
        QVERIFY(!forbiddenFetcher.resolve(untrusted, {}, &error));
    }
    void approvalAndUnavailableDownloads() {
        const auto bytes = zip();
        QTemporaryDir fixture(QDir::currentPath() + "/source-test-XXXXXX");
        QVERIFY(fixture.isValid());
        int calls = 0;
        GitHubReleaseProvider provider([&](const QUrl&, qint64) {
            ++calls;
            return ModFetchResult{{}, "Unavailable fixture"};
        });
        QString error;
        QVERIFY(
            !provider.install(source(), asset(bytes), fixture.path() + "/profile", false, &error));
        QCOMPARE(calls, 0);
        QVERIFY(!QFileInfo::exists(fixture.path() + "/profile"));
        QVERIFY(!provider.resolve(source(), {}, &error));
        QVERIFY(
            !provider.install(source(), asset(bytes), fixture.path() + "/profile", true, &error));
        QVERIFY(!QFileInfo::exists(fixture.path() + "/profile"));
    }
    void installsThroughSafePipelineAndNeverReplaces() {
        const auto bytes = zip();
        QTemporaryDir fixture(QDir::currentPath() + "/source-test-XXXXXX");
        QVERIFY(fixture.isValid());
        GitHubReleaseProvider provider(
            [&](const QUrl&, qint64) { return ModFetchResult{bytes, {}}; });
        QString error;
        const auto profile = fixture.path() + "/profile";
        QVERIFY2(provider.install(source(), asset(bytes), profile, true, &error),
                 qPrintable(error));
        QCOMPARE(ModManager(profile).mods().size(), 1);
        QVERIFY(!provider.install(source(), asset(bytes), profile, true, &error));
        QCOMPARE(ModManager(profile).mods().first().manifest.version, QString("2.0"));
        QVERIFY(ModManager(fixture.path() + "/other-profile").mods().isEmpty());
    }
    void rejectedDownloadsLeaveExistingProfileUnchanged_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<bool>("badHash");
        QTest::newRow("wrong-id") << zip("Unexpected.Mod") << false;
        QTest::newRow("traversal") << zip("Test.Dependency", "../manifest.json") << false;
        QTest::newRow("invalid-zip") << QByteArray("not a zip") << false;
        QTest::newRow("checksum") << zip() << true;
    }
    void rejectedDownloadsLeaveExistingProfileUnchanged() {
        QFETCH(QByteArray, bytes);
        QFETCH(bool, badHash);
        QTemporaryDir fixture(QDir::currentPath() + "/source-test-XXXXXX");
        QVERIFY(fixture.isValid());
        const QString profile = fixture.path() + "/profile";
        QVERIFY(QDir().mkpath(profile + "/Mods"));
        QFile sentinel(profile + "/Mods/sentinel.txt");
        QVERIFY(sentinel.open(QIODevice::WriteOnly));
        sentinel.write("unchanged");
        sentinel.close();
        auto selected = asset(bytes);
        if (badHash) selected.sha256 = QByteArray(64, '0');
        GitHubReleaseProvider provider(
            [&](const QUrl&, qint64) { return ModFetchResult{bytes, {}}; });
        QString error;
        QVERIFY(!provider.install(source(), selected, profile, true, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(ModManager(profile).mods().isEmpty());
        QVERIFY(sentinel.open(QIODevice::ReadOnly));
        QCOMPARE(sentinel.readAll(), QByteArray("unchanged"));
        QCOMPARE(QDir(profile).entryList({".install-*"}, QDir::Dirs | QDir::Hidden).size(), 0);
    }
    void rejectsUnsafeTransportBeforeNetwork() {
        QVERIFY(
            !GitHubReleaseProvider::fetchHttps(QUrl("http://example.org/archive.zip"), 100).ok());
        // A valid request on the application thread is refused without opening a socket.
        QVERIFY(
            !GitHubReleaseProvider::fetchHttps(QUrl("https://example.org/archive.zip"), 100).ok());
    }
};
QTEST_GUILESS_MAIN(ModSourceTests)
#include "ModSourceTests.moc"
