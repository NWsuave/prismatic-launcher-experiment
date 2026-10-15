#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "AtomicFile.h"

using namespace prismatic;

namespace {
QByteArray read(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
}  // namespace

class AtomicFileTests final : public QObject {
    Q_OBJECT

   private slots:
    void replacesExistingFile();
    void rejectsInvalidDestinations();
    void rejectsSymlinkDestinationsAndAncestors();
    void failedReplacementPreservesOtherData();
};

void AtomicFileTests::replacesExistingFile() {
    QTemporaryDir fixture(QDir::currentPath() + "/atomic-fixture-XXXXXX");
    QVERIFY(fixture.isValid());
    const auto target = fixture.filePath("state.json");
    QString error = "stale error";
    QVERIFY2(writeFileAtomically(target, "first", &error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(read(target), QByteArray("first"));
    const QByteArray replacement(262144, '\x7f');
    QVERIFY2(writeFileAtomically(target, replacement, &error), qPrintable(error));
    QCOMPARE(read(target), replacement);
    QVERIFY2(writeFileAtomically(target, {}, &error), qPrintable(error));
    QCOMPARE(QFileInfo(target).size(), 0);
    QCOMPARE(QDir(fixture.path()).entryList(QDir::Files | QDir::Hidden).size(), 1);
#ifdef Q_OS_UNIX
    const auto permissions = QFileInfo(target).permissions();
    QVERIFY(permissions.testFlag(QFileDevice::ReadOwner));
    QVERIFY(permissions.testFlag(QFileDevice::WriteOwner));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadGroup));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadOther));
#endif
}

void AtomicFileTests::rejectsInvalidDestinations() {
    QTemporaryDir fixture(QDir::currentPath() + "/atomic-fixture-XXXXXX");
    QVERIFY(fixture.isValid());
    QString error;
    QVERIFY(!writeFileAtomically({}, "bad", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!writeFileAtomically(fixture.path(), "bad", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!writeFileAtomically(fixture.filePath("missing/file"), "bad", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!writeFileAtomically(QString::fromLatin1("bad\0path", 8), "bad", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(QDir(fixture.path())
                .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)
                .isEmpty());
}

void AtomicFileTests::rejectsSymlinkDestinationsAndAncestors() {
#ifdef Q_OS_UNIX
    QTemporaryDir fixture(QDir::currentPath() + "/atomic-fixture-XXXXXX");
    QVERIFY(fixture.isValid());
    const auto outside = fixture.filePath("outside");
    QVERIFY(QDir().mkpath(outside));
    const auto original = outside + "/original";
    QVERIFY(writeFileAtomically(original, "preserved"));
    const auto leaf = fixture.filePath("linked-file");
    QVERIFY(QFile::link(original, leaf));
    QVERIFY(!writeFileAtomically(leaf, "overwritten"));
    QCOMPARE(read(original), QByteArray("preserved"));
    const auto parent = fixture.filePath("linked-parent");
    QVERIFY(QFile::link(outside, parent));
    QVERIFY(!writeFileAtomically(parent + "/original", "overwritten"));
    QCOMPARE(read(original), QByteArray("preserved"));
    QVERIFY(!writeFileAtomically(parent + "/new-file", "escaped"));
    QVERIFY(!QFileInfo::exists(outside + "/new-file"));
    // Lexically collapsing '..' before checking links can hide a symlink
    // component while the operating system still resolves it during a save.
    QVERIFY(!writeFileAtomically(parent + "/../escaped", "escaped"));
    QVERIFY(!QFileInfo::exists(fixture.filePath("escaped")));
#else
    QSKIP("Symbolic-link creation needs platform-specific privileges on Windows.");
#endif
}

void AtomicFileTests::failedReplacementPreservesOtherData() {
    QTemporaryDir fixture(QDir::currentPath() + "/atomic-fixture-XXXXXX");
    QVERIFY(fixture.isValid());
    const auto blocked = fixture.filePath("blocked");
    QVERIFY(QDir().mkpath(blocked));
    const auto existing = blocked + "/original";
    QVERIFY(writeFileAtomically(existing, "preserved"));
    QString error;
    // A directory cannot be atomically replaced with bytes, even when empty.
    QVERIFY(!writeFileAtomically(blocked, "replacement", &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(read(existing), QByteArray("preserved"));
    QCOMPARE(QDir(blocked).entryList(QDir::Files | QDir::Hidden).size(), 1);
}

QTEST_GUILESS_MAIN(AtomicFileTests)
#include "AtomicFileTests.moc"
