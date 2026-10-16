#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtTest>

#include "InstanceStore.h"
#include "ModInventory.h"

using namespace prismatic;

namespace {
bool makeMod(const QString& root, const QString& id, const QString& version) {
    if (!QDir().mkpath(root)) return false;
    QFile file(root + "/manifest.json");
    const auto bytes = QJsonDocument(QJsonObject{{"Name", "Fixture Mod"},
                                                 {"Author", "Fixture Author"},
                                                 {"Version", version},
                                                 {"UniqueID", id}})
                           .toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}  // namespace

class ModInventoryTests : public QObject {
    Q_OBJECT
   private slots:
    void aggregatesCaseInsensitiveIdsAndRetainsProfileVersions() {
        QTemporaryDir fixture(QDir::currentPath() + "/inventory-test-XXXXXX");
        QVERIFY(fixture.isValid());
        InstanceStore store(fixture.path());
        QString error, first, second;
        QVERIFY(store.load(&error));
        QVERIFY(store.create("First", "", &first, &error));
        QVERIFY(store.create("Second", "", &second, &error));
        QVERIFY(makeMod(store.profilePath(first) + "/Mods/One", "Test.Mod", "1.0"));
        QVERIFY(makeMod(store.profilePath(second) + "/DisabledMods/Two", "test.mod", "2.0"));
        const auto inventory = ModInventory::scan(fixture.path());
        QVERIFY2(inventory.warnings.isEmpty(), qPrintable(inventory.warnings.join('\n')));
        QCOMPARE(inventory.mods.size(), 1);
        QCOMPARE(inventory.mods.first().installations.size(), 2);
        QCOMPARE(inventory.mods.first().versions(), QStringList({"1.0", "2.0"}));
        const auto enabled =
            ModInventory::filtered(inventory.mods, "fixture author", InstallationFilter::Enabled);
        QCOMPARE(enabled.size(), 1);
        QCOMPARE(enabled.first().installations.size(), 1);
        QCOMPARE(enabled.first().installations.first().profileId, first);
        const auto disabled =
            ModInventory::filtered(inventory.mods, "Second", InstallationFilter::Disabled);
        QCOMPARE(disabled.size(), 1);
        QCOMPARE(disabled.first().installations.first().profileId, second);
        QVERIFY(ModInventory::filtered(inventory.mods, "does not exist").isEmpty());
    }

    void invalidProfileManifestDoesNotHideHealthyProfiles() {
        QTemporaryDir fixture(QDir::currentPath() + "/inventory-test-XXXXXX");
        QVERIFY(fixture.isValid());
        InstanceStore store(fixture.path());
        QString error, broken, healthy;
        QVERIFY(store.load(&error));
        QVERIFY(store.create("Broken", "", &broken, &error));
        QVERIFY(store.create("Healthy", "", &healthy, &error));
        QVERIFY(makeMod(store.profilePath(broken) + "/Mods/Bad", "Invalid.Mod", "1.0"));
        QFile manifest(store.profilePath(broken) + "/Mods/Bad/manifest.json");
        QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(manifest.write("{broken"), 7);
        manifest.close();
        QVERIFY(makeMod(store.profilePath(healthy) + "/Mods/Good", "Healthy.Mod", "3.0"));
        QVERIFY(makeMod(store.profilePath(broken) + "/Mods/StillGood", "Retained.Mod", "1.0"));
        const auto inventory = ModInventory::scan(fixture.path());
        QCOMPARE(inventory.mods.size(), 2);
        QStringList ids;
        for (const auto& mod : inventory.mods) ids.append(mod.uniqueId);
        QVERIFY(ids.contains("Healthy.Mod"));
        QVERIFY(ids.contains("Retained.Mod"));
        QCOMPARE(inventory.warnings.size(), 1);
        QVERIFY(inventory.warnings.first().contains("Broken"));
    }

    void respectsProfileLocksAndRejectsLinkedInstallations() {
        QTemporaryDir fixture(QDir::currentPath() + "/inventory-test-XXXXXX");
        QVERIFY(fixture.isValid());
        InstanceStore store(fixture.path());
        QString error, profile;
        QVERIFY(store.load(&error));
        QVERIFY(store.create("Profile", "", &profile, &error));
        const QString root = store.profilePath(profile);
        QVERIFY(makeMod(root + "/Mods/Good", "Test.Good", "1.0"));
        QLockFile lock(root + ".mods.lock");
        QVERIFY(lock.tryLock(0));
        const auto busy = ModInventory::scan(fixture.path());
        QVERIFY(busy.mods.isEmpty());
        QVERIFY(busy.warnings.join('\n').contains("busy"));
        lock.unlock();
#ifndef Q_OS_WIN
        QVERIFY(makeMod(fixture.path() + "/outside", "Test.Outside", "1.0"));
        QVERIFY(QFile::link(fixture.path() + "/outside", root + "/Mods/Linked"));
        const auto inventory = ModInventory::scan(fixture.path());
        QCOMPARE(inventory.mods.size(), 1);
        QCOMPARE(inventory.mods.first().uniqueId, QString("Test.Good"));
        QVERIFY(!inventory.warnings.isEmpty());
        for (const auto& path : inventory.watchedPaths) QVERIFY(!path.contains("Linked"));
#endif
    }
};

QTEST_GUILESS_MAIN(ModInventoryTests)
#include "ModInventoryTests.moc"
