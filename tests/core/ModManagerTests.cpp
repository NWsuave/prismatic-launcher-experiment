#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtTest>

#include "ModManager.h"

using namespace prismatic;

namespace {
bool writeFile(const QString& path, const QByteArray& data) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
bool makeMod(const QString& root, const QString& id,
             const QString& version = QStringLiteral("1.0")) {
    return writeFile(root + "/manifest.json", QJsonDocument(QJsonObject{{"Name", "Fixture"},
                                                                        {"Author", "Tester"},
                                                                        {"Version", version},
                                                                        {"UniqueID", id}})
                                                  .toJson()) &&
           writeFile(root + "/config.json", "{\"preserve\":true}");
}
}  // namespace

class ModManagerTests : public QObject {
    Q_OBJECT
   private slots:
    void acceptsCommentedManifests();
    void rejectsInvalidManifests();
    void installsNestedMultipleModsAndPreservesConfiguration();
    void rejectsConflictsWithoutChangingExistingMods();
    void rejectsSymbolicLinks();
    void rejectsInvalidProfileAndMissingMod();
    void profileLockPreventsConcurrentChanges();
    void removalRetainsRecoverableUserConfiguration();
    void sourceContainingProfileIsRejected();
    void lockedRemovedProfileIsNeverRecreated();
    void rejectsParentSegmentsBeforeResolvingLinks();
};

void ModManagerTests::acceptsCommentedManifests() {
    QTemporaryDir fixture(QDir::currentPath() + "/mod-test-XXXXXX");
    QVERIFY(fixture.isValid());
    const auto path = fixture.path() + "/manifest.json";
    const QByteArray json = R"({
        // SMAPI manifests allow comments and trailing commas.
        "Name": "A // literal", "Author": "Test /* literal */",
        "Version": "1.2.3-beta", "UniqueID": "Test.Mod", /* comment */
    })";
    QVERIFY(writeFile(path, json));
    QString error;
    const auto manifest = ModManager::parseManifest(path, &error);
    QVERIFY2(manifest.has_value(), qPrintable(error));
    QCOMPARE(manifest->name, QString("A // literal"));
    QCOMPARE(manifest->author, QString("Test /* literal */"));
    QCOMPARE(manifest->uniqueId, QString("Test.Mod"));
}

void ModManagerTests::rejectsInvalidManifests() {
    QTemporaryDir fixture(QDir::currentPath() + "/mod-test-XXXXXX");
    QVERIFY(fixture.isValid());
    const auto path = fixture.path() + "/manifest.json";
    QString error;
    const QByteArray wrongType = R"({"Name":"X","Author":"Y","Version":1,"UniqueID":"a.b"})";
    QVERIFY(writeFile(path, wrongType));
    QVERIFY(!ModManager::parseManifest(path, &error));
    QVERIFY(!error.isEmpty());
    const QByteArray invalidId =
        R"({"Name":"X","Author":"Y","Version":"1","UniqueID":"../escape"})";
    QVERIFY(writeFile(path, invalidId));
    QVERIFY(!ModManager::parseManifest(path, &error));
    QVERIFY(writeFile(path, "{ /* unfinished"));
    QVERIFY(!ModManager::parseManifest(path, &error));
}

void ModManagerTests::installsNestedMultipleModsAndPreservesConfiguration() {
    QTemporaryDir fixture(QDir::currentPath() + "/mod-test-XXXXXX");
    QVERIFY(fixture.isValid());
    const auto source = fixture.path() + "/download";
    QVERIFY(makeMod(source + "/wrapper/first", "Test.First"));
    QVERIFY(makeMod(source + "/wrapper/second", "Test.Second"));
    ModManager manager(fixture.path() + "/profile");
    QString error;
    QVERIFY2(manager.installFolder(source, &error), qPrintable(error));
    auto mods = manager.mods(&error);
    QCOMPARE(mods.size(), 2);
    QVERIFY2(manager.setEnabled("test.first", false, &error), qPrintable(error));
    mods = manager.mods(&error);
    for (const auto& mod : mods) {
        if (mod.manifest.uniqueId == "Test.First") {
            QVERIFY(!mod.enabled);
            QFile config(mod.directory + "/config.json");
            QVERIFY(config.open(QIODevice::ReadOnly));
            QCOMPARE(config.readAll(), QByteArray("{\"preserve\":true}"));
        }
    }
    QVERIFY(manager.setEnabled("Test.First", true, &error));
    QVERIFY(manager.remove("Test.Second", &error));
    QCOMPARE(manager.mods(&error).size(), 1);
    QVERIFY(QFileInfo::exists(source + "/wrapper/second/config.json"));
}

void ModManagerTests::rejectsConflictsWithoutChangingExistingMods() {
    QTemporaryDir fixture(QDir::currentPath() + "/mod-test-XXXXXX");
    QVERIFY(fixture.isValid());
    QVERIFY(makeMod(fixture.path() + "/original", "Test.Existing"));
    ModManager manager(fixture.path() + "/profile");
    QString error;
    QVERIFY(manager.installFolder(fixture.path() + "/original", &error));
    QVERIFY(makeMod(fixture.path() + "/bundle/new", "Test.New"));
    QVERIFY(makeMod(fixture.path() + "/bundle/conflict", "test.existing", "2.0"));
    QVERIFY(!manager.installFolder(fixture.path() + "/bundle", &error));
    QVERIFY(!error.isEmpty());
    const auto mods = manager.mods(&error);
    QCOMPARE(mods.size(), 1);
    QCOMPARE(mods.first().manifest.version, QString("1.0"));
}

void ModManagerTests::rejectsSymbolicLinks() {
#ifdef Q_OS_WIN
    QSKIP("Creating symbolic links requires Windows developer privileges.");
#else
    QTemporaryDir fixture(QDir::currentPath() + "/mod-test-XXXXXX");
    QVERIFY(fixture.isValid());
    QVERIFY(makeMod(fixture.path() + "/source", "Test.Link"));
    QVERIFY(writeFile(fixture.path() + "/outside", "must not follow"));
    QVERIFY(QFile::link(fixture.path() + "/outside", fixture.path() + "/source/link"));
    ModManager manager(fixture.path() + "/profile");
    QString error;
    QVERIFY(!manager.installFolder(fixture.path() + "/source", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(manager.mods(&error).isEmpty());
#endif
}

void ModManagerTests::rejectsInvalidProfileAndMissingMod() {
    QTemporaryDir fixture(QDir::currentPath() + "/mod-test-XXXXXX");
    QVERIFY(fixture.isValid());
    QVERIFY(makeMod(fixture.path() + "/source", "Test.Valid"));
    QVERIFY(writeFile(fixture.path() + "/blocked", "file blocks profile"));
    ModManager blocked(fixture.path() + "/blocked");
    QString error;
    QVERIFY(!blocked.installFolder(fixture.path() + "/source", &error));
    QVERIFY(!error.isEmpty());
    ModManager manager(fixture.path() + "/profile");
    QVERIFY(!manager.remove("missing", &error));
    QVERIFY(!manager.setEnabled("../source", true, &error));
    QVERIFY(QFileInfo::exists(fixture.path() + "/source/manifest.json"));
}

void ModManagerTests::profileLockPreventsConcurrentChanges() {
    QTemporaryDir fixture(QDir::currentPath() + "/mod-test-XXXXXX");
    QVERIFY(fixture.isValid());
    const auto profile = fixture.path() + "/profile";
    QVERIFY(makeMod(fixture.path() + "/source", "Test.Locked"));
    ModManager manager(profile);
    QString error;
    QVERIFY2(manager.installFolder(fixture.path() + "/source", &error), qPrintable(error));
    QLockFile lock(profile + ".mods.lock");
    QVERIFY(lock.tryLock(0));
    QVERIFY(!manager.setEnabled("Test.Locked", false, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!manager.remove("Test.Locked", &error));
    lock.unlock();
    const auto installed = manager.mods(&error);
    QCOMPARE(installed.size(), 1);
    QVERIFY(installed.first().enabled);
}

void ModManagerTests::removalRetainsRecoverableUserConfiguration() {
    QTemporaryDir fixture(QDir::currentPath() + "/mod-test-XXXXXX");
    QVERIFY(fixture.isValid());
    const auto profile = fixture.path() + "/profile";
    QVERIFY(makeMod(fixture.path() + "/source", "Test.Removed"));
    ModManager manager(profile);
    QString error;
    QVERIFY(manager.installFolder(fixture.path() + "/source", &error));
    QVERIFY(manager.remove("Test.Removed", &error));
    QVERIFY(manager.mods(&error).isEmpty());
    const auto removed =
        QDir(profile + "/RemovedMods").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QCOMPARE(removed.size(), 1);
    QFile config(profile + "/RemovedMods/" + removed.first() + "/config.json");
    QVERIFY(config.open(QIODevice::ReadOnly));
    QCOMPARE(config.readAll(), QByteArray("{\"preserve\":true}"));
}

void ModManagerTests::sourceContainingProfileIsRejected() {
    QTemporaryDir fixture(QDir::currentPath() + "/mod-test-XXXXXX");
    QVERIFY(fixture.isValid());
    QVERIFY(makeMod(fixture.path(), "Test.Parent"));
    ModManager manager(fixture.path() + "/profile");
    QString error;
    QVERIFY(!manager.installFolder(fixture.path(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(manager.mods().isEmpty());
    QVERIFY(QFileInfo::exists(fixture.path() + "/config.json"));
}

void ModManagerTests::lockedRemovedProfileIsNeverRecreated() {
    QTemporaryDir fixture(QDir::currentPath() + "/mod-test-XXXXXX");
    QVERIFY(fixture.isValid());
    const auto profile = fixture.path() + "/profile";
    const auto source = fixture.path() + "/source";
    QVERIFY(makeMod(source, "Test.ConcurrentRemoval"));
    ModManager manager(profile);
    QString error;
    QVERIFY(manager.installFolder(source, &error));
    QLockFile lock(profile + ".mods.lock");
    QVERIFY(lock.tryLock(0));
    // Model InstanceStore's first removal step while its commit is pending.
    QVERIFY(QDir().rename(profile, fixture.path() + "/saved-profile"));
    QVERIFY(!manager.setEnabled("Test.ConcurrentRemoval", false, &error));
    QVERIFY(!QFileInfo::exists(profile));
    QVERIFY(!manager.remove("Test.ConcurrentRemoval", &error));
    QVERIFY(!QFileInfo::exists(profile));
    QVERIFY(!manager.installFolder(source, &error));
    QVERIFY(!QFileInfo::exists(profile));
    const auto zip = fixture.path() + "/fixture.zip";
    QVERIFY(writeFile(zip, "archive reading must not be reached while locked"));
    QVERIFY(!manager.installZip(zip, &error));
    QVERIFY(!QFileInfo::exists(profile));
    QVERIFY(QDir().rename(fixture.path() + "/saved-profile", profile));
}

void ModManagerTests::rejectsParentSegmentsBeforeResolvingLinks() {
#ifdef Q_OS_WIN
    QSKIP("Creating symbolic links requires Windows developer privileges.");
#else
    QTemporaryDir fixture(QDir::currentPath() + "/mod-test-XXXXXX");
    QVERIFY(fixture.isValid());
    QVERIFY(QDir().mkpath(fixture.path() + "/safe"));
    QVERIFY(QDir().mkpath(fixture.path() + "/outside/child"));
    QVERIFY(makeMod(fixture.path() + "/outside", "Test.Outside"));
    QVERIFY(QFile::link(fixture.path() + "/outside/child", fixture.path() + "/safe/link"));
    const auto rawPath = fixture.path() + "/safe/link/../manifest.json";
    QString error;
    QVERIFY(!ModManager::parseManifest(rawPath, &error));
    QVERIFY(!error.isEmpty());
    ModManager manager(fixture.path() + "/profile");
    QVERIFY(!manager.installFolder(fixture.path() + "/safe/link/..", &error));
    QVERIFY(!QFileInfo::exists(fixture.path() + "/profile"));
    QVERIFY(writeFile(fixture.path() + "/outside/archive.zip", "fixture: must not read"));
    QVERIFY(!manager.installZip(fixture.path() + "/safe/link/../archive.zip", &error));
    QVERIFY(!QFileInfo::exists(fixture.path() + "/profile"));
#endif
}

QTEST_GUILESS_MAIN(ModManagerTests)
#include "ModManagerTests.moc"
