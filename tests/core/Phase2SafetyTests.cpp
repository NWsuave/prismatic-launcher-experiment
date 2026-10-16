#include <archive.h>
#include <archive_entry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "InstanceStore.h"
#include "ModManager.h"
#include "ModSource.h"

using namespace prismatic;

namespace {
bool writeFile(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray manifest(const QString& id, const QString& version) {
    return QJsonDocument(QJsonObject{{"Name", "Safety fixture"},
                                     {"Author", "Tests"},
                                     {"Version", version},
                                     {"UniqueID", id}})
        .toJson();
}

bool makeMod(const QString& directory, const QString& id, const QString& version) {
    return writeFile(directory + "/manifest.json", manifest(id, version)) &&
           writeFile(directory + "/config.json", version.toUtf8());
}

bool writeZip(const QString& path, const QList<QPair<QByteArray, QByteArray>>& entries) {
    std::unique_ptr<archive, decltype(&archive_write_free)> writer(archive_write_new(),
                                                                   archive_write_free);
    if (!writer || archive_write_set_format_zip(writer.get()) != ARCHIVE_OK) return false;
    QByteArray bytes(65536, '\0');
    size_t size = 0;
    if (archive_write_open_memory(writer.get(), bytes.data(), bytes.size(), &size) != ARCHIVE_OK)
        return false;
    for (const auto& item : entries) {
        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(),
                                                                            archive_entry_free);
        if (!entry) return false;
        archive_entry_set_pathname_utf8(entry.get(), item.first.constData());
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_perm(entry.get(), 0644);
        archive_entry_set_size(entry.get(), item.second.size());
        if (archive_write_header(writer.get(), entry.get()) != ARCHIVE_OK ||
            archive_write_data(writer.get(), item.second.constData(), item.second.size()) !=
                item.second.size() ||
            archive_write_finish_entry(writer.get()) != ARCHIVE_OK)
            return false;
    }
    if (archive_write_close(writer.get()) != ARCHIVE_OK) return false;
    return writeFile(path, bytes.left(static_cast<qsizetype>(size)));
}
}  // namespace

class Phase2SafetyTests : public QObject {
    Q_OBJECT
   private slots:
    void profileMutationsNeverChangeOtherVersionsOrGameMods() {
        QTemporaryDir fixture(QDir::currentPath() + "/phase2-safety-XXXXXX");
        QVERIFY(fixture.isValid());
        const QString game = fixture.filePath("shared-game");
        QVERIFY(writeFile(game + "/Mods/sentinel.txt", "original-game-mods"));
        QVERIFY(writeFile(game + "/Saves/sentinel.txt", "original-shared-save"));
        InstanceStore store(fixture.filePath("data"));
        QString error, firstId, secondId;
        QVERIFY2(store.load(&error), qPrintable(error));
        QVERIFY2(store.create("First", game, &firstId, &error), qPrintable(error));
        QVERIFY2(store.create("Second", game, &secondId, &error), qPrintable(error));
        QVERIFY(makeMod(fixture.filePath("release1"), "Fixture.Framework", "1.0.0"));
        QVERIFY(makeMod(fixture.filePath("release2"), "Fixture.Framework", "2.0.0"));
        ModManager first(store.profilePath(firstId)), second(store.profilePath(secondId));
        QVERIFY2(first.installFolder(fixture.filePath("release1"), &error), qPrintable(error));
        QVERIFY2(second.installFolder(fixture.filePath("release2"), &error), qPrintable(error));
        const auto secondBefore = second.mods();
        QCOMPARE(secondBefore.size(), 1);
        QVERIFY2(first.setEnabled("Fixture.Framework", false, &error), qPrintable(error));
        QCOMPARE(second.mods().first().manifest.version, QString("2.0.0"));
        QVERIFY(second.mods().first().enabled);
        QVERIFY2(first.remove("Fixture.Framework", &error), qPrintable(error));
        QVERIFY(first.mods().isEmpty());
        QCOMPARE(second.mods().first().directory, secondBefore.first().directory);
        QCOMPARE(readFile(secondBefore.first().directory + "/config.json"), QByteArray("2.0.0"));
        QCOMPARE(readFile(game + "/Mods/sentinel.txt"), QByteArray("original-game-mods"));
        QCOMPARE(readFile(game + "/Saves/sentinel.txt"), QByteArray("original-shared-save"));
    }

    void rejectedMultiModZipPreservesEveryProfileAndCleansStaging() {
        QTemporaryDir fixture(QDir::currentPath() + "/phase2-safety-XXXXXX");
        QVERIFY(fixture.isValid());
        QVERIFY(makeMod(fixture.filePath("release"), "Fixture.Existing", "1.0.0"));
        const QString firstPath = fixture.filePath("first");
        ModManager first(firstPath), second(fixture.filePath("second"));
        QString error;
        QVERIFY2(first.installFolder(fixture.filePath("release"), &error), qPrintable(error));
        QVERIFY2(second.installFolder(fixture.filePath("release"), &error), qPrintable(error));
        const QString firstConfig = first.mods().first().directory + "/config.json";
        const QString secondConfig = second.mods().first().directory + "/config.json";
        QVERIFY(writeFile(firstConfig, "first-profile-options"));
        QVERIFY(writeFile(secondConfig, "second-profile-options"));
        const QString zip = fixture.filePath("invalid-bundle.zip");
        QVERIFY(writeZip(zip, {{"Valid/manifest.json", manifest("Fixture.New", "2.0.0")},
                               {"Valid/config.json", "would-be-new-options"},
                               {"Invalid/manifest.json", "{\"UniqueID\":false}"}}));
        QVERIFY(!first.installZip(zip, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(first.mods().size(), 1);
        QCOMPARE(second.mods().size(), 1);
        QCOMPARE(readFile(firstConfig), QByteArray("first-profile-options"));
        QCOMPARE(readFile(secondConfig), QByteArray("second-profile-options"));
        QVERIFY(QDir(firstPath)
                    .entryList({".install-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
                    .isEmpty());
    }

    void occupiedDisabledDestinationNeverOverwritesFiles() {
        QTemporaryDir fixture(QDir::currentPath() + "/phase2-safety-XXXXXX");
        QVERIFY(fixture.isValid());
        QVERIFY(makeMod(fixture.filePath("source"), "Fixture.Collision", "1.0.0"));
        const QString profile = fixture.filePath("profile");
        ModManager manager(profile);
        QString error;
        QVERIFY2(manager.installFolder(fixture.filePath("source"), &error), qPrintable(error));
        const QString existing = manager.mods().first().directory;
        const QString blocked = profile + "/DisabledMods/Fixture.Collision";
        QVERIFY(writeFile(blocked, "unrelated-user-file"));
        QVERIFY(!manager.setEnabled("Fixture.Collision", false, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readFile(blocked), QByteArray("unrelated-user-file"));
        QCOMPARE(readFile(existing + "/config.json"), QByteArray("1.0.0"));
        QCOMPARE(manager.mods().size(), 1);
        QVERIFY(manager.mods().first().enabled);
    }

    void higherArchiveVersionThanApprovedReleaseIsRejected() {
        QTemporaryDir fixture(QDir::currentPath() + "/phase2-safety-XXXXXX");
        QVERIFY(fixture.isValid());
        QVERIFY(makeMod(fixture.filePath("existing"), "Fixture.Existing", "1.0.0"));
        const QString firstRoot = fixture.filePath("first");
        ModManager first(firstRoot), second(fixture.filePath("second"));
        QString error;
        QVERIFY2(first.installFolder(fixture.filePath("existing"), &error), qPrintable(error));
        QVERIFY2(second.installFolder(fixture.filePath("existing"), &error), qPrintable(error));
        const QString zip = fixture.filePath("mismatched-release.zip");
        QVERIFY(
            writeZip(zip, {{"Framework/manifest.json", manifest("Fixture.Framework", "9.0.0")}}));
        const auto bytes = readFile(zip);
        ModSource source;
        source.uniqueId = "Fixture.Framework";
        source.repository = "Fixture/Framework";
        source.publisher = "Fixture publisher";
        source.identityEvidence = QUrl("https://github.com/Fixture/Framework");
        source.officialPage = source.identityEvidence;
        source.assetPattern = "^Framework-[0-9.]+\\.zip$";
        source.automatic = true;
        ModReleaseAsset asset{
            source.uniqueId,
            "1.0.0",
            "Framework-1.0.0.zip",
            QUrl(
                "https://github.com/Fixture/Framework/releases/download/1.0.0/Framework-1.0.0.zip"),
            bytes.size(),
            {},
            source.repository};
        int fetches = 0;
        GitHubReleaseProvider provider([&](const QUrl&, qint64) {
            ++fetches;
            return ModFetchResult{bytes, {}};
        });
        QVERIFY(!provider.install(source, asset, firstRoot, true, &error));
        QCOMPARE(fetches, 1);
        QVERIFY(!error.isEmpty());
        const auto firstInstalled = first.mods();
        const auto secondInstalled = second.mods();
        QCOMPARE(firstInstalled.size(), 1);
        QCOMPARE(secondInstalled.size(), 1);
        QCOMPARE(firstInstalled.first().manifest.uniqueId, QString("Fixture.Existing"));
        QCOMPARE(readFile(firstInstalled.first().directory + "/config.json"), QByteArray("1.0.0"));
        QCOMPARE(readFile(secondInstalled.first().directory + "/config.json"), QByteArray("1.0.0"));
    }

    void linkedRemovalTrashCannotReachAnotherProfile() {
#ifndef Q_OS_UNIX
        QSKIP("Creating filesystem symlinks requires native privileges on Windows.");
#else
        QTemporaryDir fixture(QDir::currentPath() + "/phase2-safety-XXXXXX");
        QVERIFY(fixture.isValid());
        QVERIFY(makeMod(fixture.filePath("source"), "Fixture.Protected", "1.0.0"));
        const QString firstPath = fixture.filePath("first");
        const QString secondPath = fixture.filePath("second");
        ModManager first(firstPath), second(secondPath);
        QString error;
        QVERIFY2(first.installFolder(fixture.filePath("source"), &error), qPrintable(error));
        QVERIFY2(second.installFolder(fixture.filePath("source"), &error), qPrintable(error));
        const QString firstConfig = first.mods().first().directory + "/config.json";
        const QString secondConfig = second.mods().first().directory + "/config.json";
        QVERIFY(QFile::link(secondPath + "/Mods", firstPath + "/RemovedMods"));
        QVERIFY(!first.remove("Fixture.Protected", &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(first.mods().size(), 1);
        QCOMPARE(second.mods().size(), 1);
        QCOMPARE(readFile(firstConfig), QByteArray("1.0.0"));
        QCOMPARE(readFile(secondConfig), QByteArray("1.0.0"));
#endif
    }
};

QTEST_GUILESS_MAIN(Phase2SafetyTests)
#include "Phase2SafetyTests.moc"
