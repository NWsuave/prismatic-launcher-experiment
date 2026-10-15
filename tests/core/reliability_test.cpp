#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

#include "GameLauncher.h"
#include "InstanceStore.h"
#include "ModManager.h"

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

QByteArray manifest(const QString& id) {
    return QJsonDocument(
               QJsonObject{
                   {"Name", "Fixture"}, {"Author", "Test"}, {"Version", "1.0.0"}, {"UniqueID", id}})
        .toJson(QJsonDocument::Compact);
}

struct ZipEntry {
    QByteArray path;
    QByteArray bytes;
    quint32 mode = 0100644;
};

void littleEndian(QByteArray& target, quint32 value, int length) {
    for (int i = 0; i < length; ++i) {
        target.append(static_cast<char>((value >> (i * 8)) & 0xff));
    }
}

quint32 crc32(const QByteArray& bytes) {
    quint32 crc = 0xffffffff;
    for (unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
        }
    }
    return crc ^ 0xffffffff;
}

// Write stored ZIP entries directly so hostile names and Unix link attributes are
// preserved exactly. No fixture executes code or reads outside its temporary root.
QByteArray zip(const QList<ZipEntry>& entries) {
    QByteArray local;
    QByteArray central;
    for (const auto& entry : entries) {
        const auto offset = static_cast<quint32>(local.size());
        const auto size = static_cast<quint32>(entry.bytes.size());
        const auto nameSize = static_cast<quint32>(entry.path.size());
        const auto crc = crc32(entry.bytes);
        littleEndian(local, 0x04034b50, 4);
        littleEndian(local, 20, 2);
        littleEndian(local, 0, 2);
        littleEndian(local, 0, 2);
        littleEndian(local, 0, 4);
        littleEndian(local, crc, 4);
        littleEndian(local, size, 4);
        littleEndian(local, size, 4);
        littleEndian(local, nameSize, 2);
        littleEndian(local, 0, 2);
        local += entry.path;
        local += entry.bytes;

        littleEndian(central, 0x02014b50, 4);
        littleEndian(central, 0x0314, 2);
        littleEndian(central, 20, 2);
        littleEndian(central, 0, 2);
        littleEndian(central, 0, 2);
        littleEndian(central, 0, 4);
        littleEndian(central, crc, 4);
        littleEndian(central, size, 4);
        littleEndian(central, size, 4);
        littleEndian(central, nameSize, 2);
        littleEndian(central, 0, 2);
        littleEndian(central, 0, 2);
        littleEndian(central, 0, 2);
        littleEndian(central, 0, 2);
        littleEndian(central, entry.mode << 16, 4);
        littleEndian(central, offset, 4);
        central += entry.path;
    }
    QByteArray result = local + central;
    littleEndian(result, 0x06054b50, 4);
    littleEndian(result, 0, 2);
    littleEndian(result, 0, 2);
    littleEndian(result, static_cast<quint32>(entries.size()), 2);
    littleEndian(result, static_cast<quint32>(entries.size()), 2);
    littleEndian(result, static_cast<quint32>(central.size()), 4);
    littleEndian(result, static_cast<quint32>(local.size()), 4);
    littleEndian(result, 0, 2);
    return result;
}
}  // namespace

class ReliabilityTests : public QObject {
    Q_OBJECT
   private slots:
    void validNestedZipInstallsMultipleMods() {
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto archive = fixture.filePath("bundle.zip");
        QVERIFY(writeFile(archive, zip({{"release/A/manifest.json", manifest("Fixture.A")},
                                        {"release/A/config.json", "{\"custom\":true}"},
                                        {"release/B/manifest.json", manifest("Fixture.B")}})));
        prismatic::ModManager manager(fixture.filePath("profile"));
        QString error;
        QVERIFY2(manager.installZip(archive, &error), qPrintable(error));
        const auto mods = manager.mods(&error);
        QCOMPARE(mods.size(), 2);
        const auto a = std::find_if(mods.begin(), mods.end(), [](const auto& mod) {
            return mod.manifest.uniqueId == "Fixture.A";
        });
        QVERIFY(a != mods.end());
        QCOMPARE(readFile(a->directory + "/config.json"), QByteArray("{\"custom\":true}"));
    }

    void hostileZipNames_data() {
        QTest::addColumn<QByteArray>("path");
        QTest::newRow("parent") << QByteArray("../escaped.txt");
        QTest::newRow("nested-parent") << QByteArray("safe/../../escaped.txt");
        QTest::newRow("normalized-parent") << QByteArray("safe/../escaped.txt");
        QTest::newRow("absolute") << QByteArray("absolute-fixture-target");
        QTest::newRow("windows-drive") << QByteArray("drive-fixture-target");
        QTest::newRow("windows-backslash") << QByteArray("..\\escaped.txt");
        QTest::newRow("windows-unc") << QByteArray("\\\\server\\share\\escaped.txt");
        QTest::newRow("alternate-stream") << QByteArray("safe/file:stream");
        QTest::newRow("windows-reserved") << QByteArray("Good/NUL.txt");
        QTest::newRow("trailing-dot") << QByteArray("Good/config.json.");
        QTest::newRow("trailing-space") << QByteArray("Good/config.json ");
    }

    void hostileZipNames() {
        QFETCH(QByteArray, path);
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        if (path == "absolute-fixture-target") path = fixture.filePath("escaped.txt").toUtf8();
        if (path == "drive-fixture-target") {
#ifdef Q_OS_WIN
            path = QDir::toNativeSeparators(fixture.filePath("escaped.txt")).toUtf8();
#else
            path = "C:" + fixture.filePath("escaped.txt").toUtf8();
#endif
        }
#ifdef Q_OS_WIN
        // Extended local paths exercise the UNC syntax without contacting a server.
        if (path.startsWith("\\\\")) {
            path = "\\\\?\\" + QDir::toNativeSeparators(fixture.filePath("escaped.txt")).toUtf8();
        }
#endif
        const auto archive = fixture.filePath("hostile.zip");
        QVERIFY(writeFile(
            archive, zip({{"Good/manifest.json", manifest("Fixture.Good")}, {path, "hostile"}})));
        prismatic::ModManager manager(fixture.filePath("profile"));
        QString error;
        QVERIFY(!manager.installZip(archive, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(manager.mods().isEmpty());
        QVERIFY(!QFileInfo::exists(fixture.filePath("escaped.txt")));
    }

    void zipSymlinkRejectedBeforeCommit() {
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto outside = fixture.filePath("outside/valuable.txt");
        QVERIFY(writeFile(outside, "keep"));
        const auto archive = fixture.filePath("links.zip");
        QVERIFY(writeFile(archive, zip({{"Good/manifest.json", manifest("Fixture.Good")},
                                        {"Good/link", "../../outside", 0120777},
                                        {"Good/link/valuable.txt", "overwrite"}})));
        prismatic::ModManager manager(fixture.filePath("profile"));
        QString error;
        QVERIFY(!manager.installZip(archive, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(manager.mods().isEmpty());
        QCOMPARE(readFile(outside), QByteArray("keep"));
    }

    void zipDuplicateIdRollsBackWholeBundle() {
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto archive = fixture.filePath("duplicates.zip");
        QVERIFY(writeFile(archive, zip({{"A/manifest.json", manifest("Fixture.Same")},
                                        {"B/manifest.json", manifest("fixture.same")}})));
        prismatic::ModManager manager(fixture.filePath("profile"));
        QString error;
        QVERIFY(!manager.installZip(archive, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(manager.mods().isEmpty());
    }

    void conflictingBundlePreservesExistingDisabledMod() {
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto source = fixture.filePath("original");
        QVERIFY(writeFile(source + "/manifest.json", manifest("Fixture.Existing")));
        QVERIFY(writeFile(source + "/config.json", "user-options"));
        prismatic::ModManager manager(fixture.filePath("profile"));
        QString error;
        QVERIFY2(manager.installFolder(source, &error), qPrintable(error));
        QVERIFY2(manager.setEnabled("Fixture.Existing", false, &error), qPrintable(error));
        const auto before = manager.mods();
        QCOMPARE(before.size(), 1);
        const auto originalDirectory = before.front().directory;
        const auto archive = fixture.filePath("conflict.zip");
        QVERIFY(
            writeFile(archive, zip({{"New/manifest.json", manifest("Fixture.New")},
                                    {"Conflict/manifest.json", manifest("Fixture.Existing")}})));
        QVERIFY(!manager.installZip(archive, &error));
        const auto after = manager.mods();
        QCOMPARE(after.size(), 1);
        QCOMPARE(after.front().manifest.uniqueId, QString("Fixture.Existing"));
        QVERIFY(!after.front().enabled);
        QCOMPARE(readFile(originalDirectory + "/config.json"), QByteArray("user-options"));
    }

    void corruptZipPayloadRejected() {
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        auto bytes = zip({{"Good/manifest.json", manifest("Fixture.Good")},
                          {"Good/data.txt", "UNIQUE_PAYLOAD"}});
        const auto index = bytes.indexOf("UNIQUE_PAYLOAD");
        QVERIFY(index >= 0);
        bytes[index] = 'X';  // Keep the original CRC to force a read error.
        const auto archive = fixture.filePath("corrupt.zip");
        QVERIFY(writeFile(archive, bytes));
        prismatic::ModManager manager(fixture.filePath("profile"));
        QString error;
        QVERIFY(!manager.installZip(archive, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(manager.mods().isEmpty());
    }

    void zipPathCollisions_data() {
        QTest::addColumn<QByteArray>("secondPath");
        QTest::newRow("same-name") << QByteArray("Good/config.json");
        QTest::newRow("case-collision") << QByteArray("Good/CONFIG.json");
    }

    void zipPathCollisions() {
        QFETCH(QByteArray, secondPath);
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto archive = fixture.filePath("collision.zip");
        QVERIFY(writeFile(archive, zip({{"Good/manifest.json", manifest("Fixture.Good")},
                                        {"Good/config.json", "keep"},
                                        {secondPath, "overwrite"}})));
        prismatic::ModManager manager(fixture.filePath("profile"));
        QString error;
        QVERIFY(!manager.installZip(archive, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(manager.mods().isEmpty());
    }

    void truncatedZipCannotInstallPartialMod() {
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        auto bytes = zip({{"Good/manifest.json", manifest("Fixture.Good")},
                          {"Good/data.txt", "TRUNCATED_PAYLOAD"}});
        const auto index = bytes.indexOf("TRUNCATED_PAYLOAD");
        QVERIFY(index >= 0);
        bytes.truncate(index + 2);
        const auto archive = fixture.filePath("truncated.zip");
        QVERIFY(writeFile(archive, bytes));
        prismatic::ModManager manager(fixture.filePath("profile"));
        QString error;
        QVERIFY(!manager.installZip(archive, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(manager.mods().isEmpty());
    }

    void zipMissingEndRecordRejected() {
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        auto bytes = zip({{"Good/manifest.json", manifest("Fixture.Good")}});
        bytes.chop(12);
        const auto archive = fixture.filePath("missing-tail.zip");
        QVERIFY(writeFile(archive, bytes));
        prismatic::ModManager manager(fixture.filePath("profile"));
        QString error;
        QVERIFY(!manager.installZip(archive, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(manager.mods().isEmpty());
    }

    void oversizedZipDeclarationRejectedWithoutExpansion() {
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        auto bytes = zip({{"Good/manifest.json", manifest("Fixture.Good")}});
        QByteArray oversized;
        littleEndian(oversized, 0x40000000, 4);
        bytes.replace(22, 4, oversized);
        const auto central = bytes.indexOf(QByteArray::fromHex("504b0102"));
        QVERIFY(central >= 0);
        bytes.replace(central + 24, 4, oversized);
        const auto archive = fixture.filePath("oversized-declaration.zip");
        QVERIFY(writeFile(archive, bytes));
        prismatic::ModManager manager(fixture.filePath("profile"));
        QString error;
        QVERIFY(!manager.installZip(archive, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(manager.mods().isEmpty());
    }

    void destinationWriteFailureDoesNotChangeSource() {
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto source = fixture.filePath("source");
        QVERIFY(writeFile(source + "/manifest.json", manifest("Fixture.New")));
        QVERIFY(writeFile(source + "/config.json", "unchanged"));
        const auto profile = fixture.filePath("profile");
        QVERIFY(writeFile(profile + "/Mods", "block-directory-creation"));
        prismatic::ModManager manager(profile);
        QString error;
        QVERIFY(!manager.installFolder(source, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readFile(source + "/config.json"), QByteArray("unchanged"));
        QCOMPARE(readFile(profile + "/Mods"), QByteArray("block-directory-creation"));
    }

    void staleStoreCannotOverwriteNewerRecords() {
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        prismatic::InstanceStore first(fixture.filePath("data"));
        prismatic::InstanceStore stale(fixture.filePath("data"));
        QString error;
        QVERIFY2(first.load(&error), qPrintable(error));
        QVERIFY2(stale.load(&error), qPrintable(error));
        QString firstId;
        QVERIFY2(first.create("First", "", &firstId, &error), qPrintable(error));
        QVERIFY(!stale.create("Stale", "", nullptr, &error));
        QVERIFY(!error.isEmpty());
        prismatic::InstanceStore fresh(fixture.filePath("data"));
        QVERIFY2(fresh.load(&error), qPrintable(error));
        QCOMPARE(fresh.instances().size(), 1);
        QVERIFY(fresh.find(firstId).has_value());
    }

    void failedStoreWriteKeepsMemoryAndDiskConsistent() {
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto data = fixture.filePath("data");
        prismatic::InstanceStore store(data);
        QString error;
        QVERIFY2(store.load(&error), qPrintable(error));
        QString id;
        QVERIFY2(store.create("Original", "", &id, &error), qPrintable(error));
        const auto original = readFile(data + "/instances.json");
        QVERIFY(QFile::rename(data + "/instances.json", data + "/saved.json"));
        QVERIFY(QDir().mkpath(data + "/instances.json"));
        auto edited = *store.find(id);
        edited.name = "Must not persist";
        QVERIFY(!store.update(edited, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(store.find(id)->name, QString("Original"));
        QCOMPARE(readFile(data + "/saved.json"), original);
    }

    void staleRemovalRestoresProfileAfterDatabaseConflict() {
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto data = fixture.filePath("data");
        prismatic::InstanceStore first(data);
        QString error, id;
        QVERIFY2(first.load(&error), qPrintable(error));
        QVERIFY2(first.create("Original", "", &id, &error), qPrintable(error));
        const auto config = first.modsPath(id) + "/config.json";
        QVERIFY(writeFile(config, "must-survive"));
        prismatic::InstanceStore stale(data);
        QVERIFY2(stale.load(&error), qPrintable(error));
        auto edited = *first.find(id);
        edited.name = "Newer";
        QVERIFY2(first.update(edited, &error), qPrintable(error));
        QVERIFY(!stale.remove(id, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readFile(config), QByteArray("must-survive"));
        prismatic::InstanceStore fresh(data);
        QVERIFY2(fresh.load(&error), qPrintable(error));
        QCOMPARE(fresh.find(id)->name, QString("Newer"));
        QCOMPARE(QDir(data + "/trash").entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
    }

    void removalRefusesReplacedProfileSymlink() {
#ifdef Q_OS_UNIX
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        prismatic::InstanceStore store(fixture.filePath("data"));
        QString error, id;
        QVERIFY2(store.load(&error), qPrintable(error));
        QVERIFY2(store.create("Original", "", &id, &error), qPrintable(error));
        const auto profile = store.profilePath(id);
        const auto outside = fixture.filePath("outside-profile");
        QVERIFY(writeFile(outside + "/config.json", "keep"));
        QVERIFY(QDir().rename(profile, fixture.filePath("saved-profile")));
        QVERIFY(QFile::link(outside, profile));
        QVERIFY(!store.remove(id, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(store.find(id).has_value());
        QCOMPARE(readFile(outside + "/config.json"), QByteArray("keep"));
#else
        QSKIP("Creating filesystem symlinks requires native privileges on Windows.");
#endif
    }

    void invalidProfileIdCannotRemoveSiblingData() {
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto sentinel = fixture.filePath("valuable/config.json");
        QVERIFY(writeFile(sentinel, "keep"));
        prismatic::InstanceStore store(fixture.filePath("data"));
        QString error;
        QVERIFY2(store.load(&error), qPrintable(error));
        QVERIFY(!store.remove("../../valuable", &error));
        QVERIFY(store.profilePath("../../valuable").isEmpty());
        QCOMPARE(readFile(sentinel), QByteArray("keep"));
    }

    void launchPlanRechecksDiskAndPreservesLiteralArguments() {
        QTemporaryDir fixture(QDir::currentPath() + "/reliability-XXXXXX");
        QVERIFY(fixture.isValid());
#ifdef Q_OS_WIN
        const auto executable = fixture.filePath("Game with spaces/Stardew Valley.exe");
#else
        const auto executable = fixture.filePath("Game with spaces/Stardew Valley");
#endif
        QVERIFY(writeFile(executable, "fixture: never executed"));
        QVERIFY(QFile::setPermissions(
            executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const auto installation =
            prismatic::GameLauncher::inspect(QFileInfo(executable).absolutePath());
        QVERIFY(installation.valid());
        prismatic::LaunchRequest request{
            installation, false, {}, {"two words", "$(never-run)", "; literal"}};
        request.installation.vanillaExecutable = fixture.filePath("forged-executable");
        QString error;
        const auto plan = prismatic::GameLauncher::prepareLaunch(request, &error);
        QVERIFY2(plan.has_value(), qPrintable(error));
        QCOMPARE(plan->program, executable);
        QCOMPARE(plan->arguments, request.arguments);
        QCOMPARE(plan->workingDirectory, QFileInfo(executable).absolutePath());
        QVERIFY(QFile::remove(executable));
        QVERIFY(!prismatic::GameLauncher::prepareLaunch(request, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(ReliabilityTests)
#include "reliability_test.moc"
