#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include "DependencyDialog.h"
#include "InstanceStore.h"
#include "LauncherWindow.h"
#include "ModExplorerPage.h"
#include "ModManager.h"
#include "ThemeManager.h"

namespace {
bool writeMod(const QString& path, const QString& version) {
    if (!QDir().mkpath(path)) return false;
    QFile file(path + "/manifest.json");
    const auto bytes = QJsonDocument(QJsonObject{{"Name", "Integration framework"},
                                                 {"Author", "Test fixtures"},
                                                 {"Description", "A local integration fixture"},
                                                 {"Version", version},
                                                 {"UniqueID", "Fixture.Integration"}})
                           .toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}  // namespace

class Phase2IntegrationTests : public QObject {
    Q_OBJECT
   private slots:
    void initTestCase() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); }
    void settingsApplyThemesImmediatelyAndPersistOnReopen() {
        QTemporaryDir fixture(QDir::currentPath() + "/phase2-ui-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto data = fixture.filePath("data");
        {
            LauncherWindow window(fixture.filePath("launcher.ini"), nullptr, data);
            window.show();
            auto* selector = window.findChild<QComboBox*>("themeSelector");
            QVERIFY(selector);
            QCOMPARE(selector->count(), 3);
            auto* pages = window.findChild<QStackedWidget*>("pages");
            auto* details = window.findChild<QStackedWidget*>("detailPages");
            QVERIFY(pages);
            QVERIFY(details);
            pages->setCurrentIndex(1);
            details->setCurrentIndex(2);
            if (auto* navigation = window.findChild<QListWidget*>("navigation"))
                navigation->setCurrentRow(2);
            window.resize(1000, 760);
            const QString previews = QDir::currentPath() + "/theme-previews";
            QVERIFY(QDir().mkpath(previews));
            for (const auto& theme : ThemeManager::themes()) {
                const auto index = selector->findData(theme.id);
                QVERIFY2(index >= 0, qPrintable(theme.id));
                selector->setCurrentIndex(index);
                QTRY_COMPARE(qApp->styleSheet(), ThemeManager::styleSheetForTheme(theme.id));
                QApplication::processEvents();
                QVERIFY(window.grab().save(previews + '/' + theme.id + ".png"));
            }
            QCOMPARE(selector->currentData().toString(), QString("retro-terminal"));
            QVERIFY(QFileInfo::exists(data + "/theme-preferences.json"));
        }
        {
            LauncherWindow reopened(fixture.filePath("launcher.ini"), nullptr, data);
            const auto* selector = reopened.findChild<QComboBox*>("themeSelector");
            QVERIFY(selector);
            QCOMPARE(selector->currentData().toString(), QString("retro-terminal"));
            QCOMPARE(qApp->styleSheet(), ThemeManager::styleSheetForTheme("retro-terminal"));
        }
    }

    void dependencyDialogDoesNotUseFrameworkFromAnotherProfile() {
        QTemporaryDir fixture(QDir::currentPath() + "/phase2-ui-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto data = fixture.filePath("data");
        prismatic::InstanceStore store(data);
        QString firstId, secondId, error;
        QVERIFY2(store.load(&error), qPrintable(error));
        QVERIFY2(store.create("Framework profile", {}, &firstId, &error), qPrintable(error));
        QVERIFY2(store.create("Content profile", {}, &secondId, &error), qPrintable(error));
        QVERIFY(writeMod(fixture.filePath("framework"), "1.0.0"));
        prismatic::ModManager first(store.profilePath(firstId)),
            second(store.profilePath(secondId));
        QVERIFY2(first.installFolder(fixture.filePath("framework"), &error), qPrintable(error));
        const auto pack = fixture.filePath("pack");
        QVERIFY(QDir().mkpath(pack));
        {
            QFile file(pack + "/manifest.json");
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto bytes =
                QJsonDocument(
                    QJsonObject{{"Name", "Content pack"},
                                {"Author", "Fixture"},
                                {"Version", "1.0.0"},
                                {"UniqueID", "Fixture.Pack"},
                                {"ContentPackFor", QJsonObject{{"UniqueID", "Fixture.Integration"},
                                                               {"MinimumVersion", "1.0.0"}}}})
                    .toJson();
            QCOMPARE(file.write(bytes), bytes.size());
        }
        QVERIFY2(second.installFolder(pack, &error), qPrintable(error));
        DependencyDialog dialog(data, store.profilePath(secondId));
        dialog.show();
        auto* summary = dialog.findChild<QPlainTextEdit*>("dependencySummary");
        auto* requests = dialog.findChild<QListWidget*>("dependencyRequests");
        auto* close = dialog.findChild<QPushButton*>("closeDependencies");
        QVERIFY(summary);
        QVERIFY(requests);
        QVERIFY(close);
        QTRY_VERIFY(close->isEnabled());
        QTRY_COMPARE(requests->count(), 1);
        QVERIFY(requests->item(0)->text().contains("Fixture.Integration"));
        QVERIFY(summary->toPlainText().contains("missing in this profile"));
        QVERIFY(summary->toPlainText().contains("Fixture.Pack"));
        QVERIFY(!(requests->item(0)->flags() & Qt::ItemIsUserCheckable));
        QCOMPARE(first.mods().size(), 1);
        QCOMPARE(second.mods().size(), 1);
        QCOMPARE(second.mods().first().manifest.uniqueId, QString("Fixture.Pack"));
        close->click();
    }

    void cancellingDependencyWarningKeepsModDisabled() {
        QTemporaryDir fixture(QDir::currentPath() + "/phase2-ui-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto data = fixture.filePath("data");
        prismatic::InstanceStore store(data);
        QString id, error;
        QVERIFY2(store.load(&error), qPrintable(error));
        QVERIFY2(store.create("Dependency warning", {}, &id, &error), qPrintable(error));
        const auto source = fixture.filePath("source");
        QVERIFY(QDir().mkpath(source));
        {
            QFile file(source + "/manifest.json");
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto bytes =
                QJsonDocument(QJsonObject{{"Name", "Needs missing framework"},
                                          {"Author", "Fixture"},
                                          {"Version", "1.0.0"},
                                          {"UniqueID", "Fixture.NeedsFramework"},
                                          {"Dependencies",
                                           QJsonArray{QJsonObject{{"UniqueID", "Fixture.Missing"},
                                                                  {"MinimumVersion", "2.0.0"}}}}})
                    .toJson();
            QCOMPARE(file.write(bytes), bytes.size());
        }
        prismatic::ModManager manager(store.profilePath(id));
        QVERIFY2(manager.installFolder(source, &error), qPrintable(error));
        QVERIFY2(manager.setEnabled("Fixture.NeedsFramework", false, &error), qPrintable(error));
        LauncherWindow window(fixture.filePath("launcher.ini"), nullptr, data);
        window.show();
        window.findChild<QPushButton*>("editInstance")->click();
        auto* mods = window.findChild<QTableWidget*>("modsTable");
        auto* toggle = window.findChild<QPushButton*>("toggleMod");
        QVERIFY(mods);
        QVERIFY(toggle);
        QTRY_COMPARE(mods->rowCount(), 1);
        mods->selectRow(0);
        QTRY_VERIFY(toggle->isEnabled());
        bool warningShown = false;
        QString warningText;
        QTimer::singleShot(0, &window, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                warningText = box->text();
                const bool isDependencyWarning =
                    warningText.contains("unresolved required dependencies", Qt::CaseInsensitive) &&
                    warningText.contains("Fixture.Missing") && warningText.contains("2.0.0") &&
                    (box->standardButtons() & QMessageBox::Cancel);
                if (isDependencyWarning) {
                    warningShown = true;
                    box->button(QMessageBox::Cancel)->click();
                }
            }
        });
        toggle->click();
        QVERIFY(warningShown);
        QVERIFY(warningText.contains("Fixture.Missing"));
        QVERIFY(warningText.contains("2.0.0"));
        const auto after = manager.mods();
        QCOMPARE(after.size(), 1);
        QVERIFY(!after.first().enabled);
    }

    void installingModOpensOfflineMissingDependencyList() {
        QTemporaryDir fixture(QDir::currentPath() + "/phase2-ui-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto source = fixture.filePath("source");
        QVERIFY(QDir().mkpath(source));
        {
            QFile file(source + "/manifest.json");
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto bytes =
                QJsonDocument(
                    QJsonObject{
                        {"Name", "Needs framework"},
                        {"Author", "Fixture"},
                        {"Version", "1.0.0"},
                        {"UniqueID", "Fixture.NeedsFramework"},
                        {"Dependencies", QJsonArray{QJsonObject{{"UniqueID", "Fixture.Missing"}}}}})
                    .toJson();
            QCOMPARE(file.write(bytes), bytes.size());
        }
        const auto data = fixture.filePath("data");
        LauncherWindow window(fixture.filePath("launcher.ini"), nullptr, data);
        window.show();
        window.findChild<QPushButton*>("editInstance")->click();
        auto* install = window.findChild<QPushButton*>("installFolder");
        QVERIFY(install);
        QTRY_VERIFY(install->isEnabled());
        bool selectedFolder = false, inspected = false;
        QString dependencyText;
        QTimer inspection;
        inspection.setInterval(10);
        connect(&inspection, &QTimer::timeout, &window, [&] {
            auto* dialog = qobject_cast<DependencyDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* close = dialog->findChild<QPushButton*>("closeDependencies");
            auto* requests = dialog->findChild<QListWidget*>("dependencyRequests");
            if (!close || !close->isEnabled() || !requests || requests->count() != 1) return;
            dependencyText = dialog->findChild<QPlainTextEdit*>("dependencySummary")->toPlainText();
            inspected = true;
            close->click();
        });
        inspection.start();
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                dialog->setDirectory(QFileInfo(source).absolutePath());
                dialog->selectFile(source);
                selectedFolder = true;
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            }
        });
        install->click();
        QVERIFY(selectedFolder);
        QTRY_VERIFY_WITH_TIMEOUT(inspected, 5000);
        inspection.stop();
        QVERIFY(dependencyText.contains("Fixture.Missing"));
        QVERIFY(dependencyText.contains("missing in this profile"));
        auto* mods = window.findChild<QTableWidget*>("modsTable");
        QVERIFY(mods);
        QTRY_COMPARE(mods->rowCount(), 1);
    }

    void outdatedKnownDependencyShowsManualOfficialPage() {
        QTemporaryDir fixture(QDir::currentPath() + "/phase2-ui-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto profile = fixture.filePath("profile");
        const auto framework = fixture.filePath("framework");
        const auto pack = fixture.filePath("pack");
        for (const auto& directory : {framework, pack}) QVERIFY(QDir().mkpath(directory));
        const QList<QPair<QString, QJsonObject>> manifests{
            {framework,
             {{"Name", "Framework"},
              {"Author", "Fixture"},
              {"Version", "1.0.0"},
              {"UniqueID", "Pathoschild.ContentPatcher"}}},
            {pack,
             {{"Name", "Content pack"},
              {"Author", "Fixture"},
              {"Version", "1.0.0"},
              {"UniqueID", "Fixture.Pack"},
              {"ContentPackFor", QJsonObject{{"UniqueID", "Pathoschild.ContentPatcher"},
                                             {"MinimumVersion", "2.0.0"}}}}}};
        for (const auto& entry : manifests) {
            QFile file(entry.first + "/manifest.json");
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto bytes = QJsonDocument(entry.second).toJson();
            QCOMPARE(file.write(bytes), bytes.size());
        }
        prismatic::ModManager manager(profile);
        QString error;
        QVERIFY2(manager.installFolder(framework, &error), qPrintable(error));
        QVERIFY2(manager.installFolder(pack, &error), qPrintable(error));
        DependencyDialog dialog(fixture.filePath("data"), profile);
        auto* summary = dialog.findChild<QPlainTextEdit*>("dependencySummary");
        auto* requests = dialog.findChild<QListWidget*>("dependencyRequests");
        auto* close = dialog.findChild<QPushButton*>("closeDependencies");
        auto* manual = dialog.findChild<QPushButton*>("manualDependencyPage");
        QVERIFY(summary);
        QVERIFY(requests);
        QVERIFY(close);
        QVERIFY(manual);
        QTRY_VERIFY(close->isEnabled());
        QTRY_COMPARE(requests->count(), 1);
        QVERIFY(summary->toPlainText().contains("too old"));
        QVERIFY(
            summary->toPlainText().contains("https://www.nexusmods.com/stardewvalley/mods/1915"));
        QVERIFY(!(requests->item(0)->flags() & Qt::ItemIsUserCheckable));
        requests->setCurrentRow(0);
        QVERIFY(manual->isEnabled());
    }

    void headerExplorerMutationRefreshesSelectedProfile() {
        QTemporaryDir fixture(QDir::currentPath() + "/phase2-ui-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto data = fixture.filePath("data");
        prismatic::InstanceStore store(data);
        QString firstId, secondId, error;
        QVERIFY2(store.load(&error), qPrintable(error));
        QVERIFY2(store.create("Profile Alpha", {}, &firstId, &error), qPrintable(error));
        QVERIFY2(store.create("Profile Beta", {}, &secondId, &error), qPrintable(error));
        QVERIFY2(store.select(firstId, &error), qPrintable(error));
        QVERIFY(writeMod(fixture.filePath("one"), "1.0.0"));
        QVERIFY(writeMod(fixture.filePath("two"), "2.0.0"));
        prismatic::ModManager first(store.profilePath(firstId)),
            second(store.profilePath(secondId));
        QVERIFY2(first.installFolder(fixture.filePath("one"), &error), qPrintable(error));
        QVERIFY2(second.installFolder(fixture.filePath("two"), &error), qPrintable(error));
        LauncherWindow window(fixture.filePath("launcher.ini"), nullptr, data);
        window.show();
        auto* button = window.findChild<QPushButton*>("openModExplorer");
        auto* explorer = window.findChild<ModExplorerPage*>();
        QVERIFY(button);
        QVERIFY(explorer);
        QTRY_VERIFY(!explorer->isBusy());
        button->click();
        auto* groups = explorer->findChild<QTableWidget*>("inventoryMods");
        auto* installations = explorer->findChild<QTableWidget*>("inventoryInstallations");
        auto* toggle = explorer->findChild<QPushButton*>("inventoryToggle");
        auto* selectedProfileMods = window.findChild<QTableWidget*>("modsTable");
        QVERIFY(groups);
        QVERIFY(installations);
        QVERIFY(toggle);
        QVERIFY(selectedProfileMods);
        QTRY_COMPARE(groups->rowCount(), 1);
        groups->selectRow(0);
        QTRY_COMPARE(installations->rowCount(), 2);
        int firstRow = -1;
        for (int row = 0; row < installations->rowCount(); ++row)
            if (installations->item(row, 0)->text() == "Profile Alpha") firstRow = row;
        QVERIFY(firstRow >= 0);
        installations->selectRow(firstRow);
        QTRY_VERIFY(toggle->isEnabled());
        toggle->click();
        QTRY_VERIFY(!explorer->isBusy());
        QTRY_VERIFY([&] {
            const auto mods = first.mods();
            return mods.size() == 1 && !mods.first().enabled;
        }());
        const auto secondInstalled = second.mods();
        QCOMPARE(secondInstalled.size(), 1);
        QVERIFY(secondInstalled.first().enabled);
        QCOMPARE(secondInstalled.first().manifest.version, QString("2.0.0"));
        QTRY_COMPARE(selectedProfileMods->rowCount(), 1);
        QTRY_COMPARE(selectedProfileMods->item(0, 0)->text(), QString("Disabled"));
    }
};

QTEST_MAIN(Phase2IntegrationTests)
#include "Phase2IntegrationTests.moc"
