#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

#include "DependencyResolver.h"

using namespace prismatic;
namespace {
InstalledMod mod(const QString& id, const QString& version = "1.0", bool enabled = true) {
    ModManifest manifest;
    manifest.name = id;
    manifest.author = "Fixture";
    manifest.uniqueId = id;
    manifest.version = version;
    return {manifest, {}, enabled};
}
}  // namespace
class DependencyResolverTests : public QObject {
    Q_OBJECT
   private slots:
    void versions() {
        QCOMPARE(*DependencyResolver::compareVersions("1.10", "1.2.0"), 1);
        QCOMPARE(*DependencyResolver::compareVersions("1.0-beta.2", "1.0-beta.11"), -1);
        QCOMPARE(*DependencyResolver::compareVersions("1.0", "1.0-rc"), 1);
        QCOMPARE(*DependencyResolver::compareVersions("1.0+build", "1.0"), 0);
        QVERIFY(!DependencyResolver::compareVersions("unknown", "1"));
        QVERIFY(!DependencyResolver::compareVersions("1.0-beta.02", "1.0-beta.2"));
        QCOMPARE(*DependencyResolver::compareVersions("1.0+build.02", "1.0"), 0);
    }
    void profileIsolationAndOptional() {
        auto a = mod("Test.A");
        a.manifest.dependencies = {{"Test.B", "2.0", true}, {"Test.Optional", {}, false}};
        const auto otherProfile = mod("Test.B", "2.0");
        auto result = DependencyResolver::resolve({a}, {otherProfile.manifest});
        QVERIFY(result.hasBlockingIssues());
        QCOMPARE(result.requests.size(), 1);
        QCOMPARE(result.requests.first().uniqueId, QString("Test.B"));
        result = DependencyResolver::resolve({a, otherProfile});
        QVERIFY(!result.hasBlockingIssues());
        QVERIFY(result.requests.isEmpty());
    }
    void disabledAndOutdated() {
        auto a = mod("Test.A");
        a.manifest.dependencies = {{"Test.B", "2.0", true}};
        auto result = DependencyResolver::resolve({a, mod("Test.B", "2.0", false)});
        QCOMPARE(result.diagnostics.first().status, DependencyStatus::Disabled);
        QVERIFY(result.hasBlockingIssues());
        result = DependencyResolver::resolve({a, mod("Test.B", "1.0")});
        QCOMPARE(result.diagnostics.first().status, DependencyStatus::Outdated);
    }
    void optionalDependencyNeverBecomesRequired() {
        auto a = mod("Test.A");
        a.manifest.dependencies = {{"Test.B", "2.0", false}};
        QVERIFY(!DependencyResolver::resolve({a, mod("Test.B", "1.0")}).hasBlockingIssues());
        QVERIFY(!DependencyResolver::resolve({a, mod("Test.B", "2.0", false)}).hasBlockingIssues());
        a.enabled = false;
        a.manifest.dependencies = {{"Missing.Required", {}, true}};
        QVERIFY(!DependencyResolver::resolve({a}).hasBlockingIssues());
    }
    void contentPackTransitiveAndCycles() {
        auto a = mod("Test.Pack");
        a.manifest.contentPackFor = ModDependency{"Test.Framework", "2", true};
        auto framework = mod("Test.Framework", "2");
        framework.manifest.dependencies = {{"Test.Next", {}, true}};
        auto result = DependencyResolver::resolve({a}, {framework.manifest});
        QCOMPARE(result.requests.size(), 2);
        framework.manifest.dependencies = {{"Test.Pack", {}, true}};
        result = DependencyResolver::resolve({a, framework});
        QVERIFY(result.hasBlockingIssues());
        QVERIFY(std::any_of(result.diagnostics.cbegin(), result.diagnostics.cend(),
                            [](const auto& d) { return d.status == DependencyStatus::Circular; }));
    }
    void deduplicatesStrongestMinimum() {
        auto a = mod("Test.A");
        a.manifest.dependencies = {{"Test.B", "1", true}, {"test.b", "2", true}};
        auto c = mod("Test.C");
        c.manifest.dependencies = {{"Test.B", "3", true}};
        const auto result = DependencyResolver::resolve({a, c});
        QCOMPARE(result.requests.size(), 1);
        QCOMPARE(result.requests.first().minimumVersion, QString("3"));
        QCOMPARE(result.requests.first().requiredBy.size(), 2);
    }
    void runtimeAndInvalidVersions() {
        auto a = mod("Test.A");
        a.manifest.minimumApiVersion = "4.0";
        a.manifest.minimumGameVersion = "1.6";
        auto result = DependencyResolver::resolve({a}, {}, "3.0", "1.5");
        QCOMPARE(result.diagnostics.size(), 2);
        QVERIFY(result.hasBlockingIssues());
        result = DependencyResolver::resolve({a});
        QVERIFY(!result.hasBlockingIssues());
        a.manifest.dependencies = {{"Test.B", "nonsense", true}};
        result = DependencyResolver::resolve({a, mod("Test.B")});
        QVERIFY(result.hasBlockingIssues());
    }
    void manifestSchemaValidation() {
        QTemporaryDir fixture(QDir::currentPath() + "/dependency-test-XXXXXX");
        QVERIFY(fixture.isValid());
        QFile file(fixture.path() + "/manifest.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(
            R"({"Name":"Pack","Author":"Tester","Version":"1.0","UniqueID":"Test.Pack","Description":"Description","MinimumApiVersion":"4.0","UpdateKeys":["Nexus:123"],"ContentPackFor":{"UniqueID":"Test.Framework","MinimumVersion":"2"},"Dependencies":[{"UniqueID":"Test.Optional","IsRequired":false}]})");
        file.close();
        QString error;
        auto manifest = ModManager::parseManifest(file.fileName(), &error);
        QVERIFY2(manifest.has_value(), qPrintable(error));
        QCOMPARE(manifest->description, QString("Description"));
        QVERIFY(manifest->contentPackFor.has_value());
        QVERIFY(!manifest->dependencies.first().isRequired);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(
            R"({"Name":"X","Author":"Y","Version":"1","UniqueID":"Test.X","Dependencies":[{"UniqueID":"Test.Y","IsRequired":"false"}]})");
        file.close();
        QVERIFY(!ModManager::parseManifest(file.fileName(), &error));
    }
};
QTEST_GUILESS_MAIN(DependencyResolverTests)
#include "DependencyResolverTests.moc"
