#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtTest>

#include "InstanceStore.h"

using namespace prismatic;

class InstanceStoreTests : public QObject {
    Q_OBJECT
   private slots:
    void initTestCase() { QVERIFY(QDir().mkpath("build/fixtures")); }

    void persistsCrudAndSelection() {
        QTemporaryDir dir(QDir::currentPath() + "/build/fixtures/instances-XXXXXX");
        QVERIFY(dir.isValid());
        InstanceStore store(dir.path());
        QString error, id;
        QVERIFY2(store.load(&error), qPrintable(error));
        QVERIFY(!store.create("   ", "", nullptr, &error));
        QVERIFY(!store.create("Farm", "relative/game", nullptr, &error));
        QVERIFY2(store.create("  Farm  ", dir.path(), &id, &error), qPrintable(error));
        QCOMPARE(store.find(id)->name, "Farm");
        QVERIFY(QDir(store.modsPath(id)).exists());
        auto instance = *store.find(id);
        instance.name = "Renamed farm";
        instance.useSmapi = false;
        instance.launchArguments = QStringList{"--example", "two words"};
        QVERIFY2(store.update(instance, &error), qPrintable(error));
        InstanceStore reopened(dir.path());
        QVERIFY2(reopened.load(&error), qPrintable(error));
        QCOMPARE(reopened.instances().size(), 1);
        QCOMPARE(reopened.selectedId(), id);
        QCOMPARE(reopened.find(id)->name, instance.name);
        QCOMPARE(reopened.find(id)->useSmapi, false);
        QCOMPARE(reopened.find(id)->launchArguments, instance.launchArguments);
        QVERIFY(reopened.remove(id, &error));
        QVERIFY(reopened.instances().isEmpty());
        QVERIFY(!QDir(reopened.profilePath(id)).exists());
        QCOMPARE(QDir(dir.filePath("trash")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(),
                 1);
    }

    void duplicatePreservesConfigurationAndIsolatesFiles() {
        QTemporaryDir dir(QDir::currentPath() + "/build/fixtures/duplicate-XXXXXX");
        InstanceStore store(dir.path());
        QVERIFY(store.load());
        QString id, copy;
        QVERIFY(store.create("Original", "", &id));
        QFile config(QDir(store.modsPath(id)).filePath("config.json"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        QCOMPARE(config.write("private settings"), 16);
        config.close();
        QVERIFY(store.duplicate(id, "Copy", &copy));
        QVERIFY(copy != id);
        QFile copied(QDir(store.modsPath(copy)).filePath("config.json"));
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(copied.readAll(), QByteArray("private settings"));
        QVERIFY(store.select(copy));
        QCOMPARE(store.selectedId(), copy);
        QVERIFY(!store.select("../../outside"));
        QVERIFY(store.profilePath("../../outside").isEmpty());
        QVERIFY(!store.remove("../../outside"));
    }

    void corruptionRecoversLastValidBackup() {
        QTemporaryDir dir(QDir::currentPath() + "/build/fixtures/recovery-XXXXXX");
        InstanceStore store(dir.path());
        QVERIFY(store.load());
        QString id;
        QVERIFY(store.create("Before", "", &id));
        auto instance = *store.find(id);
        instance.name = "After";
        QVERIFY(store.update(instance));
        QFile primary(dir.filePath("instances.json"));
        QVERIFY(primary.open(QIODevice::WriteOnly | QIODevice::Truncate));
        primary.write("{broken");
        primary.close();
        InstanceStore recovered(dir.path());
        QString warning;
        QVERIFY(recovered.load(&warning));
        QVERIFY(!warning.isEmpty());
        QCOMPARE(recovered.find(id)->name, "Before");
        QVERIFY(recovered.update(*recovered.find(id)));
    }

    void staleAndFailedWritersLeaveStateUntouched() {
        QTemporaryDir dir(QDir::currentPath() + "/build/fixtures/writes-XXXXXX");
        InstanceStore first(dir.path()), stale(dir.path());
        QVERIFY(first.load());
        QVERIFY(stale.load());
        QVERIFY(first.create("First", ""));
        QString error;
        QVERIFY(!stale.create("Stale", "", nullptr, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(stale.instances().isEmpty());
        InstanceStore invalid(dir.filePath("instances.json"));
        QVERIFY(!invalid.load(&error));
    }

    void duplicateRejectsSymlinkAndKeepsSource() {
#ifdef Q_OS_UNIX
        QTemporaryDir dir(QDir::currentPath() + "/build/fixtures/symlink-XXXXXX");
        InstanceStore store(dir.path());
        QVERIFY(store.load());
        QString id;
        QVERIFY(store.create("Original", "", &id));
        QVERIFY(QDir().mkpath(dir.filePath("outside")));
        QVERIFY(QFile::link(dir.filePath("outside"), QDir(store.modsPath(id)).filePath("escape")));
        QString error;
        QVERIFY(!store.duplicate(id, "Unsafe copy", nullptr, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(store.instances().size(), 1);
        QVERIFY(QDir(dir.filePath("outside")).exists());
#endif
    }

    void refusesToDowngradeFutureSchema() {
        QTemporaryDir dir(QDir::currentPath() + "/build/fixtures/future-XXXXXX");
        InstanceStore store(dir.path());
        QVERIFY(store.load());
        QString id;
        QVERIFY(store.create("Current", "", &id));
        QVERIFY(store.select(id));
        QFile file(dir.filePath("instances.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto object = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        object["schemaVersion"] = 2;
        const auto future = QJsonDocument(object).toJson();
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(future), future.size());
        file.close();
        InstanceStore older(dir.path());
        QString error;
        QVERIFY(!older.load(&error));
        QVERIFY(!older.create("Overwrite", "", nullptr, &error));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), future);
    }

    void profileMutationRespectsModOperationLock() {
        QTemporaryDir dir(QDir::currentPath() + "/build/fixtures/mod-lock-XXXXXX");
        InstanceStore store(dir.path());
        QVERIFY(store.load());
        QString id;
        QVERIFY(store.create("Busy profile", "", &id));
        QLockFile lock(store.profilePath(id) + ".mods.lock");
        lock.setStaleLockTime(0);
        QVERIFY(lock.tryLock(0));
        QString error;
        QVERIFY(!store.duplicate(id, "Busy copy", nullptr, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!store.remove(id, &error));
        QVERIFY(QDir(store.profilePath(id)).exists());
        lock.unlock();
        QString copy;
        QVERIFY(store.duplicate(id, "Available copy", &copy));
        QVERIFY(!QFileInfo::exists(store.profilePath(copy) + ".mods.lock"));
        QVERIFY(store.remove(id));
    }

    void damagedRecordsRemainRecoverableAcrossRepeatedSaves() {
        QTemporaryDir dir(QDir::currentPath() + "/build/fixtures/partial-recovery-XXXXXX");
        InstanceStore store(dir.path());
        QVERIFY(store.load());
        QString goodId, damagedId;
        QVERIFY(store.create("Healthy", "", &goodId));
        QVERIFY(store.create("Recoverable profile", "", &damagedId));
        QFile file(dir.filePath("instances.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto object = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        auto records = object["instances"].toArray();
        auto damaged = records[1].toObject();
        damaged["name"] = "";
        records[1] = damaged;
        object["instances"] = records;
        const auto raw = QJsonDocument(object).toJson();
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(raw), raw.size());
        file.close();
        InstanceStore recovered(dir.path());
        QString warning;
        QVERIFY(recovered.load(&warning));
        QVERIFY(!warning.isEmpty());
        QCOMPARE(recovered.instances().size(), 1);
        QVERIFY(recovered.select(goodId));
        QVERIFY(recovered.select(goodId));
        const auto snapshots =
            QDir(dir.path()).entryList({"instances.recovery-*.json"}, QDir::Files);
        QCOMPARE(snapshots.size(), 1);
        QFile snapshot(dir.filePath(snapshots.first()));
        QVERIFY(snapshot.open(QIODevice::ReadOnly));
        QCOMPARE(snapshot.readAll(), raw);
        QVERIFY(QDir(store.profilePath(damagedId)).exists());
    }

    void missingProfileStillRespectsConcurrentOperationLock() {
        QTemporaryDir dir(QDir::currentPath() + "/build/fixtures/missing-locked-XXXXXX");
        InstanceStore store(dir.path());
        QVERIFY(store.load());
        QString id;
        QVERIFY(store.create("Temporarily moved", "", &id));
        QLockFile lock(store.profilePath(id) + ".mods.lock");
        lock.setStaleLockTime(0);
        QVERIFY(lock.tryLock(0));
        QVERIFY(QDir().rename(store.profilePath(id), dir.filePath("moved-profile")));
        QString error;
        QVERIFY(!store.remove(id, &error));
        QVERIFY(store.find(id).has_value());
        QVERIFY(!error.isEmpty());
    }
};

QTEST_APPLESS_MAIN(InstanceStoreTests)
#include "InstanceStoreTests.moc"
