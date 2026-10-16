#include <QAbstractButton>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include "InstanceStore.h"
#include "ModExplorerPage.h"
#include "ModManager.h"

namespace {
bool makeMod(const QString& root, const QString& id, const QString& version) {
    if (!QDir().mkpath(root)) return false;
    QFile file(root + "/manifest.json");
    const auto bytes = QJsonDocument(QJsonObject{{"Name", "Fixture Mod"},
                                                 {"Author", "Fixture Author"},
                                                 {"Version", version},
                                                 {"UniqueID", id},
                                                 {"Description", "Fixture description"}})
                           .toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
void answerQuestion(QObject* context, QMessageBox::StandardButton answer) {
    QTimer::singleShot(0, context, [answer] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            if (auto* button = box->button(answer)) button->click();
        }
    });
}
}  // namespace

class ModExplorerTests : public QObject {
    Q_OBJECT
   private slots:
    void showsMetadataVersionsAndStateFilters() {
        QTemporaryDir fixture(QDir::currentPath() + "/explorer-ui-XXXXXX");
        QVERIFY(fixture.isValid());
        prismatic::InstanceStore store(fixture.path());
        QString first, second, error;
        QVERIFY(store.load(&error));
        QVERIFY(store.create("First", "", &first, &error));
        QVERIFY(store.create("Second", "", &second, &error));
        QVERIFY(makeMod(store.profilePath(first) + "/Mods/Test", "Test.Mod", "1.0"));
        QVERIFY(makeMod(store.profilePath(second) + "/DisabledMods/Test", "test.mod", "2.0"));
        ModExplorerPage page(fixture.path());
        page.show();
        auto* mods = page.findChild<QTableWidget*>("inventoryMods");
        auto* installations = page.findChild<QTableWidget*>("inventoryInstallations");
        auto* details = page.findChild<QPlainTextEdit*>("inventoryDetails");
        QTRY_VERIFY(!page.isBusy());
        QCOMPARE(mods->rowCount(), 1);
        QCOMPARE(mods->item(0, 2)->text(), QString("1.0, 2.0"));
        QCOMPARE(installations->rowCount(), 2);
        QVERIFY(details->toPlainText().contains("Fixture description"));
        page.findChild<QComboBox*>("inventoryFilter")->setCurrentIndex(2);
        QCOMPARE(installations->rowCount(), 1);
        QCOMPARE(installations->item(0, 0)->text(), QString("Second"));
        page.findChild<QLineEdit*>("inventorySearch")->setText("missing");
        QCOMPARE(mods->rowCount(), 0);
        QVERIFY(!page.findChild<QPushButton*>("inventoryRemove")->isEnabled());
        QVERIFY(details->toPlainText().contains("No installed mods"));
    }

    void mutationsOnlyAffectSelectedProfileAndRemovalNeedsConfirmation() {
        QTemporaryDir fixture(QDir::currentPath() + "/explorer-ui-XXXXXX");
        QVERIFY(fixture.isValid());
        prismatic::InstanceStore store(fixture.path());
        QString first, second, error;
        QVERIFY(store.load(&error));
        QVERIFY(store.create("First", "", &first, &error));
        QVERIFY(store.create("Second", "", &second, &error));
        QVERIFY(makeMod(store.profilePath(first) + "/Mods/Test", "Test.Mod", "1.0"));
        QVERIFY(makeMod(store.profilePath(second) + "/Mods/Test", "Test.Mod", "1.0"));
        ModExplorerPage page(fixture.path());
        page.show();
        QSignalSpy changed(&page, &ModExplorerPage::installationsChanged);
        QTRY_VERIFY(!page.isBusy());
        auto* installations = page.findChild<QTableWidget*>("inventoryInstallations");
        int firstRow = installations->item(0, 0)->text() == "First" ? 0 : 1;
        installations->selectRow(firstRow);
        page.findChild<QPushButton*>("inventoryToggle")->click();
        QTRY_COMPARE(changed.size(), 1);
        QTRY_VERIFY(!page.isBusy());
        QVERIFY(!prismatic::ModManager(store.profilePath(first)).mods().first().enabled);
        QVERIFY(prismatic::ModManager(store.profilePath(second)).mods().first().enabled);
        // Filtering also proves that actions remain anchored to the displayed profile.
        page.findChild<QComboBox*>("inventoryFilter")->setCurrentIndex(2);
        QCOMPARE(installations->rowCount(), 1);
        page.setMutationAllowed(false);
        QVERIFY(!page.findChild<QPushButton*>("inventoryToggle")->isEnabled());
        page.setMutationAllowed(true);
        int checks = 0;
        page.setEnableCheck([&checks](const QString&, const QString&) {
            ++checks;
            return false;
        });
        page.findChild<QPushButton*>("inventoryToggle")->click();
        QCOMPARE(checks, 1);
        QVERIFY(!prismatic::ModManager(store.profilePath(first)).mods().first().enabled);
        answerQuestion(&page, QMessageBox::No);
        page.findChild<QPushButton*>("inventoryRemove")->click();
        QCOMPARE(prismatic::ModManager(store.profilePath(first)).mods().size(), 1);
        answerQuestion(&page, QMessageBox::Yes);
        page.findChild<QPushButton*>("inventoryRemove")->click();
        QTRY_COMPARE(changed.size(), 2);
        QTRY_VERIFY(!page.isBusy());
        QVERIFY(prismatic::ModManager(store.profilePath(first)).mods().isEmpty());
        QCOMPARE(prismatic::ModManager(store.profilePath(second)).mods().size(), 1);
        QVERIFY(!QDir(store.profilePath(first) + "/RemovedMods").isEmpty());
    }

    void refreshesWhenLocalInstallationsChange() {
        QTemporaryDir fixture(QDir::currentPath() + "/explorer-ui-XXXXXX");
        QVERIFY(fixture.isValid());
        prismatic::InstanceStore store(fixture.path());
        QString id, error;
        QVERIFY(store.load(&error));
        QVERIFY(store.create("Profile", "", &id, &error));
        ModExplorerPage page(fixture.path());
        page.show();
        QTRY_VERIFY(!page.isBusy());
        auto* mods = page.findChild<QTableWidget*>("inventoryMods");
        QCOMPARE(mods->rowCount(), 0);
        QVERIFY(makeMod(fixture.path() + "/source", "Test.New", "1.0"));
        QVERIFY(prismatic::ModManager(store.profilePath(id))
                    .installFolder(fixture.path() + "/source", &error));
        QTRY_COMPARE(mods->rowCount(), 1);
        QTRY_VERIFY(!page.isBusy());
    }
};

QTEST_MAIN(ModExplorerTests)
#include "ModExplorerTests.moc"
