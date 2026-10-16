#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "DataPaths.h"

namespace {
bool put(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}  // namespace

class DataPathsTests : public QObject {
    Q_OBJECT
   private slots:
    void standardDefault() {
        QCOMPARE(prismatic::defaultDataDirectory(),
                 QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    }
    void migrationCopiesAndRetainsOriginal() {
        QTemporaryDir temp(QDir::currentPath() + "/migration-XXXXXX");
        QVERIFY(temp.isValid());
        const auto legacy = temp.filePath("old/data");
        const auto target = temp.filePath("new/data");
        QVERIFY(put(legacy + "/instances.json", "registry"));
        QVERIFY(put(legacy + "/profiles/fixture/Mods/Test/config.json", "config"));
        QString error;
        QVERIFY2(prismatic::migrateLegacyData(legacy, target, &error), qPrintable(error));
        QCOMPARE(read(target + "/instances.json"), QByteArray("registry"));
        QCOMPARE(read(target + "/profiles/fixture/Mods/Test/config.json"), QByteArray("config"));
        QCOMPARE(read(legacy + "/instances.json"), QByteArray("registry"));
        QVERIFY(put(legacy + "/instances.json", "changed"));
        QVERIFY(prismatic::migrateLegacyData(legacy, target, &error));
        QCOMPARE(read(target + "/instances.json"), QByteArray("registry"));
    }
    void existingDestinationIsNeverMerged() {
        QTemporaryDir temp(QDir::currentPath() + "/migration-XXXXXX");
        QVERIFY(put(temp.filePath("old/instances.json"), "old"));
        QVERIFY(put(temp.filePath("new/launcher.ini"), "keep"));
        QVERIFY(prismatic::migrateLegacyData(temp.filePath("old"), temp.filePath("new")));
        QVERIFY(!QFileInfo::exists(temp.filePath("new/instances.json")));
        QCOMPARE(read(temp.filePath("new/launcher.ini")), QByteArray("keep"));
    }
    void activeLegacySessionPreventsMigration() {
        QTemporaryDir temp(QDir::currentPath() + "/migration-XXXXXX");
        const auto legacy = temp.filePath("old");
        QVERIFY(put(legacy + "/instances.json", "registry"));
        QLockFile lock(legacy + "/.launcher-session.lock");
        QVERIFY(lock.tryLock());
        QString error;
        QVERIFY(!prismatic::migrateLegacyData(legacy, temp.filePath("new"), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(temp.filePath("new")));
    }
    void symbolicLinksAreRejectedWithoutPartialDestination() {
#ifdef Q_OS_UNIX
        QTemporaryDir temp(QDir::currentPath() + "/migration-XXXXXX");
        const auto legacy = temp.filePath("old");
        QVERIFY(put(legacy + "/instances.json", "registry"));
        QVERIFY(put(temp.filePath("private"), "private"));
        QVERIFY(QFile::link(temp.filePath("private"), legacy + "/escape"));
        QString error;
        QVERIFY(!prismatic::migrateLegacyData(legacy, temp.filePath("new"), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(temp.filePath("new")));
        QCOMPARE(read(temp.filePath("private")), QByteArray("private"));
#endif
    }
};
QTEST_GUILESS_MAIN(DataPathsTests)
#include "DataPathsTests.moc"
