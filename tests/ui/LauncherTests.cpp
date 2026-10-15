#include <archive.h>
#include <archive_entry.h>

#include <QAbstractButton>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <memory>

#include "GameLauncher.h"
#include "LauncherConfig.h"
#include "LauncherWindow.h"

namespace {
bool idle(const LauncherWindow& window) {
    const auto* status = window.findChild<QLabel*>("modStatus");
    const auto* progress = window.findChild<QProgressBar*>();
    return status && !status->text().startsWith("Reading profile mods") && progress &&
           progress->isHidden();
}

void enterDialogText(LauncherWindow& window, const QString& text) {
    QTimer::singleShot(0, &window, [text] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            if (auto* input = dialog->findChild<QLineEdit*>()) input->setText(text);
            dialog->accept();
        }
    });
}

void answerQuestion(LauncherWindow& window, QMessageBox::StandardButton answer) {
    QTimer::singleShot(0, &window, [answer] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            if (auto* button = box->button(answer)) button->click();
        }
    });
}

bool writeFixture(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFixture(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeZipFixture(const QString& path, const QList<QPair<QByteArray, QByteArray>>& entries) {
    std::unique_ptr<archive, decltype(&archive_write_free)> writer(archive_write_new(),
                                                                   archive_write_free);
    if (!writer || archive_write_set_format_zip(writer.get()) != ARCHIVE_OK) return false;
    QByteArray buffer(16384, '\0');
    size_t used = 0;
    if (archive_write_open_memory(writer.get(), buffer.data(), buffer.size(), &used) != ARCHIVE_OK)
        return false;
    for (const auto& item : entries) {
        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(),
                                                                            archive_entry_free);
        if (!entry) return false;
        archive_entry_set_pathname_utf8(entry.get(), item.first.constData());
        archive_entry_set_size(entry.get(), item.second.size());
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_perm(entry.get(), 0644);
        if (archive_write_header(writer.get(), entry.get()) != ARCHIVE_OK ||
            archive_write_data(writer.get(), item.second.constData(), item.second.size()) !=
                item.second.size() ||
            archive_write_finish_entry(writer.get()) != ARCHIVE_OK)
            return false;
    }
    if (archive_write_close(writer.get()) != ARCHIVE_OK) return false;
    return writeFixture(path, buffer.left(static_cast<qsizetype>(used)));
}
}  // namespace

class LauncherTests : public QObject {
    Q_OBJECT
   private slots:
    void initTestCase() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QFile style(":/ui/style.qss");
        QVERIFY(style.open(QIODevice::ReadOnly));
        qApp->setStyle("Fusion");
        qApp->setStyleSheet(style.readAll());
    }
    void defaultPath() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        LauncherConfig config(temp.filePath("launcher.ini"));
        QCOMPARE(config.gameDirectory(),
                 QDir::homePath() + "/.local/share/Steam/steamapps/common/Stardew Valley");
        QVERIFY(!QFileInfo::exists(config.filePath()));
    }

    void persistPath() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        const auto file = temp.filePath("nested/launcher.ini");
        LauncherConfig config(file);
        QVERIFY(!config.saveGameDirectory("   "));
        QVERIFY(!config.saveGameDirectory("relative/game"));
        QVERIFY(config.saveGameDirectory(temp.filePath("Game with spaces")));
        QCOMPARE(LauncherConfig(file).gameDirectory(), temp.filePath("Game with spaces"));
        QCOMPARE(LauncherConfig::expandHome("~/Games"), QDir::homePath() + "/Games");
    }

    void invalidConfigDoesNotUseWorkingDirectory() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        const auto file = temp.filePath("launcher.ini");
        QVERIFY(writeFixture(file, "[game]\ndirectory=relative/game\n"));
        QCOMPARE(LauncherConfig(file).gameDirectory(),
                 QDir::homePath() + "/.local/share/Steam/steamapps/common/Stardew Valley");
    }

    void bootstrapPreservesTrickyPathsAndUnrelatedSettings() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        QVERIFY(temp.isValid());
        const auto file = temp.filePath("launcher.ini");
        QVERIFY(writeFixture(file,
                             QString("; retain this comment\n[custom]\n"
                                     "retained=\"semi;comma,unicode-農場\"\n[window]\nwidth=1234\n")
                                 .toUtf8()));
        QStringList names{QStringLiteral("GameA"),
                          QStringLiteral("GameB"),
                          QStringLiteral("Game with spaces"),
                          QStringLiteral("comma,semicolon;equals=game"),
                          QStringLiteral("農場-é-ゲーム"),
                          QStringLiteral("hash#at@brackets[game]")};
#ifndef Q_OS_WIN
        names.append(QStringLiteral("quote\"backslash\\game"));
#endif
        LauncherConfig config(file);
        for (const auto& name : names) {
            const auto path = temp.filePath(name);
            QVERIFY(QDir().mkpath(path));
            QVERIFY(config.saveGameDirectory(path));
            QCOMPARE(LauncherConfig(file).gameDirectory(), path);
            const auto firstSave = readFixture(file);
            QVERIFY(config.saveGameDirectory(path));
            QCOMPARE(LauncherConfig(file).gameDirectory(), path);
            QCOMPARE(readFixture(file), firstSave);
            QVERIFY(firstSave.contains("; retain this comment"));
            QSettings preserved(file, QSettings::IniFormat);
            QCOMPARE(preserved.value("custom/retained").toString(),
                     QString("semi;comma,unicode-農場"));
            QCOMPARE(preserved.value("window/width").toInt(), 1234);
        }
    }

    void bootstrapRejectsRawParentTraversalBeforeCreatingDirectories() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        QVERIFY(temp.isValid());
        const auto path = temp.path() + "/not-created/../config/launcher.ini";
        LauncherConfig config(path);
        QVERIFY(!config.saveGameDirectory(temp.path()));
        QVERIFY(!QFileInfo::exists(temp.filePath("not-created")));
        QVERIFY(!QFileInfo::exists(temp.filePath("config")));
    }

    void bootstrapReadsExternalSameSizeRewriteWithUnchangedTimestamp() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        QVERIFY(temp.isValid());
        const auto file = temp.filePath("launcher.ini");
        const auto first = temp.filePath("GameA");
        const auto second = temp.filePath("GameB");
        const auto ini = [](const QString& path) {
            return ("[game]\ndirectory=\"" + path + "\"\n").toUtf8();
        };
        QVERIFY(writeFixture(file, ini(first)));
        const auto modified = QFileInfo(file).lastModified();
        QCOMPARE(LauncherConfig(file).gameDirectory(), first);
        QCOMPARE(ini(first).size(), ini(second).size());
        QVERIFY(writeFixture(file, ini(second)));
        QFile rewritten(file);
        QVERIFY(rewritten.open(QIODevice::ReadWrite));
        QVERIFY(rewritten.setFileTime(modified, QFileDevice::FileModificationTime));
        rewritten.close();
        QCOMPARE(QFileInfo(file).lastModified(), modified);
        QCOMPARE(LauncherConfig(file).gameDirectory(), second);
    }

    void bootstrapRejectsSymlinkParentBeforeCreatingDirectories() {
#ifdef Q_OS_UNIX
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        QVERIFY(temp.isValid());
        const auto target = temp.filePath("real-directory");
        const auto link = temp.filePath("linked-directory");
        QVERIFY(QDir().mkpath(target));
        QVERIFY(writeFixture(target + "/keep.txt", "original-data"));
        QVERIFY(QFile::link(target, link));
        LauncherConfig config(link + "/not-created/launcher.ini");
        QVERIFY(!config.saveGameDirectory(temp.path()));
        QVERIFY(!QFileInfo::exists(target + "/not-created"));
        QCOMPARE(readFixture(target + "/keep.txt"), QByteArray("original-data"));
#else
        QSKIP("Creating filesystem symlinks requires native privileges on Windows.");
#endif
    }

    void createSessionInstance() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        LauncherWindow window(temp.filePath("launcher.ini"));
        QTimer::singleShot(0, &window, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                dialog->findChild<QLineEdit*>()->setText("Test farm");
                dialog->accept();
            }
        });
        for (auto* button : window.findChildren<QPushButton*>()) {
            if (button->text() == "+  Add instance") button->click();
        }
        auto* instances = window.findChild<QListWidget*>("instances");
        QCOMPARE(instances->count(), 2);
        QCOMPARE(instances->currentItem()->data(Qt::UserRole).toString(), QString("Test farm"));
        QCOMPARE(window.findChild<QStackedWidget*>("pages")->currentIndex(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
    }

    void navigateAndFilter() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        LauncherWindow window(temp.filePath("launcher.ini"));
        window.show();
        auto* instances = window.findChild<QListWidget*>("instances");
        auto* search = window.findChild<QLineEdit*>("instanceSearch");
        auto* pages = window.findChild<QStackedWidget*>("pages");
        QVERIFY(instances && search && pages);
        QCOMPARE(instances->count(), 1);
        const auto screenshots = qEnvironmentVariable("PRISMATIC_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QVERIFY(QDir().mkpath(screenshots));
            QTest::qWait(40);
            QApplication::processEvents();
            QVERIFY(window.grab().save(screenshots + "/library.png"));
        }
        search->setText("does not exist");
        QVERIFY(instances->item(0)->isHidden());
        search->clear();
        QVERIFY(!instances->item(0)->isHidden());
        window.findChild<QPushButton*>("editInstance")->click();
        QCOMPARE(pages->currentIndex(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
        if (!screenshots.isEmpty()) {
            QTest::qWait(40);
            QApplication::processEvents();
            QVERIFY(window.grab().save(screenshots + "/mods.png"));
        }
        window.findChild<QPushButton*>("backToLibrary")->click();
        QCOMPARE(pages->currentIndex(), 0);
    }

    void missingExecutable() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        LauncherConfig config(temp.filePath("launcher.ini"));
        QVERIFY(config.saveGameDirectory(temp.path()));
        LauncherWindow window(config.filePath());
        bool warningShown = false;
        QTimer::singleShot(0, &window, [&warningShown] {
            if (auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                warningShown = warning->text().contains("Executable not found");
                warning->accept();
            }
        });
        for (auto* button : window.findChildren<QPushButton*>()) {
            if (button->text() == "▶  Play") button->click();
        }
        QVERIFY(warningShown);
    }

    void saveSettings() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        const auto file = temp.filePath("launcher.ini");
        LauncherWindow window(file);
        auto* path = window.findChild<QLineEdit*>("gamePath");
        QVERIFY(path);
        path->setText(temp.path());
        window.findChild<QPushButton*>("savePath")->click();
        QCOMPARE(LauncherConfig(file).gameDirectory(), temp.path());
        window.findChild<QPushButton*>("editInstance")->click();
        QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
        window.findChild<QListWidget*>("navigation")->setCurrentRow(2);
        window.resize(820, 600);
        window.show();
        const auto screenshots = qEnvironmentVariable("PRISMATIC_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QTest::qWait(40);
            QVERIFY(window.grab().save(screenshots + "/settings-minimum.png"));
        }
    }

    void persistentRenameDuplicateAndConfirmedDeletion() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        QVERIFY(temp.isValid());
        const auto config = temp.filePath("launcher.ini");
        const auto data = temp.filePath("data");
        QString originalId;
        {
            LauncherWindow window(config, nullptr, data);
            auto* instances = window.findChild<QListWidget*>("instances");
            QCOMPARE(instances->count(), 1);
            originalId = instances->currentItem()->data(Qt::UserRole + 2).toString();
            QVERIFY(!originalId.isEmpty());
            enterDialogText(window, "Renamed farm");
            window.findChild<QPushButton*>("renameInstance")->click();
            QCOMPARE(instances->currentItem()->data(Qt::UserRole).toString(),
                     QString("Renamed farm"));
            const auto configPath = data + "/profiles/" + originalId + "/Mods/custom.json";
            QVERIFY(writeFixture(configPath, "keep-profile-settings"));
            enterDialogText(window, "Copied farm");
            window.findChild<QPushButton*>("duplicateInstance")->click();
            QTRY_COMPARE_WITH_TIMEOUT(instances->count(), 2, 5000);
            QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
            QCOMPARE(instances->currentItem()->data(Qt::UserRole).toString(),
                     QString("Copied farm"));
            const auto duplicateId = instances->currentItem()->data(Qt::UserRole + 2).toString();
            QVERIFY(duplicateId != originalId);
            QCOMPARE(readFixture(data + "/profiles/" + duplicateId + "/Mods/custom.json"),
                     QByteArray("keep-profile-settings"));
            answerQuestion(window, QMessageBox::Cancel);
            window.findChild<QPushButton*>("deleteInstance")->click();
            QCOMPARE(instances->count(), 2);
            QVERIFY(QFileInfo::exists(configPath));
            answerQuestion(window, QMessageBox::Yes);
            window.findChild<QPushButton*>("deleteInstance")->click();
            QTRY_COMPARE_WITH_TIMEOUT(instances->count(), 1, 5000);
            QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
            QCOMPARE(readFixture(configPath), QByteArray("keep-profile-settings"));
            QVERIFY(!QDir(data + "/profiles/" + duplicateId).exists());
            const auto trash = QDir(data + "/trash").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            QCOMPARE(trash.size(), 1);
            QCOMPARE(readFixture(data + "/trash/" + trash.front() + "/Mods/custom.json"),
                     QByteArray("keep-profile-settings"));
        }
        LauncherWindow reopened(config, nullptr, data);
        auto* instances = reopened.findChild<QListWidget*>("instances");
        QCOMPARE(instances->count(), 1);
        QCOMPARE(instances->currentItem()->data(Qt::UserRole).toString(), QString("Renamed farm"));
        QCOMPARE(instances->currentItem()->data(Qt::UserRole + 2).toString(), originalId);
    }

    void deletingLastProfileStaysEmptyAfterReopening() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        QVERIFY(temp.isValid());
        const auto config = temp.filePath("launcher.ini");
        const auto data = temp.filePath("data");
        {
            LauncherWindow window(config, nullptr, data);
            auto* instances = window.findChild<QListWidget*>("instances");
            QCOMPARE(instances->count(), 1);
            answerQuestion(window, QMessageBox::Yes);
            window.findChild<QPushButton*>("deleteInstance")->click();
            QTRY_COMPARE_WITH_TIMEOUT(instances->count(), 0, 5000);
            QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
        }
        LauncherWindow reopened(config, nullptr, data);
        QCOMPARE(reopened.findChild<QListWidget*>("instances")->count(), 0);
        QCOMPARE(reopened.findChild<QStackedWidget*>("pages")->currentIndex(), 0);
    }

    void settingsRemainAssociatedWithSelectedProfile() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        QVERIFY(temp.isValid());
        const auto firstGame = temp.filePath("Game one");
        const auto secondGame = temp.filePath("Game two");
        QVERIFY(QDir().mkpath(firstGame));
        QVERIFY(QDir().mkpath(secondGame));
        const auto config = temp.filePath("launcher.ini");
        const auto data = temp.filePath("data");
        QString firstId, secondId;
        {
            LauncherWindow window(config, nullptr, data);
            auto* instances = window.findChild<QListWidget*>("instances");
            auto* path = window.findChild<QLineEdit*>("gamePath");
            auto* mode = window.findChild<QComboBox*>("launchMode");
            auto* arguments = window.findChild<QPlainTextEdit*>("launchArguments");
            firstId = instances->currentItem()->data(Qt::UserRole + 2).toString();
            path->setText(firstGame);
            mode->setCurrentIndex(1);
            arguments->setPlainText("--first\ntwo words");
            window.findChild<QPushButton*>("savePath")->click();
            enterDialogText(window, "Second farm");
            for (auto* button : window.findChildren<QPushButton*>()) {
                if (button->text() == "+  Add instance") button->click();
            }
            QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
            QCOMPARE(instances->count(), 2);
            secondId = instances->currentItem()->data(Qt::UserRole + 2).toString();
            path->setText(secondGame);
            mode->setCurrentIndex(0);
            arguments->setPlainText("--second");
            window.findChild<QPushButton*>("savePath")->click();
            instances->setCurrentRow(0);
            QCOMPARE(path->text(), firstGame);
            QCOMPARE(mode->currentIndex(), 1);
            QCOMPARE(arguments->toPlainText(), QString("--first\ntwo words"));
            instances->setCurrentRow(1);
            QCOMPARE(path->text(), secondGame);
            QCOMPARE(mode->currentIndex(), 0);
            QCOMPARE(arguments->toPlainText(), QString("--second"));
        }
        LauncherWindow reopened(config, nullptr, data);
        auto* instances = reopened.findChild<QListWidget*>("instances");
        QCOMPARE(instances->currentItem()->data(Qt::UserRole + 2).toString(), secondId);
        QCOMPARE(reopened.findChild<QLineEdit*>("gamePath")->text(), secondGame);
        instances->setCurrentRow(0);
        QCOMPARE(instances->currentItem()->data(Qt::UserRole + 2).toString(), firstId);
        QCOMPARE(reopened.findChild<QLineEdit*>("gamePath")->text(), firstGame);
    }

    void folderInstallToggleAndConfirmedRemoval() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        QVERIFY(temp.isValid());
        const auto source = temp.filePath("download/My Mod");
        QVERIFY(writeFixture(
            source + "/manifest.json",
            R"({"Name":"UI Fixture","Author":"Tests","Version":"1.2.3","UniqueID":"Fixture.UI"})"));
        QVERIFY(writeFixture(source + "/config.json", "preserve-me"));
        const auto data = temp.filePath("data");
        LauncherWindow window(temp.filePath("launcher.ini"), nullptr, data);
        window.show();
        window.findChild<QPushButton*>("editInstance")->click();
        QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
        bool dialogHandled = false;
        QTimer::singleShot(0, &window, [source, &dialogHandled] {
            if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                dialog->setDirectory(QFileInfo(source).absolutePath());
                dialog->selectFile(source);
                dialogHandled = true;
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            }
        });
        window.findChild<QPushButton*>("installFolder")->click();
        QVERIFY(dialogHandled);
        auto* table = window.findChild<QTableWidget*>("modsTable");
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
        QCOMPARE(table->item(0, 0)->text(), QString("Enabled"));
        QCOMPARE(table->item(0, 1)->text(), QString("UI Fixture"));
        QCOMPARE(table->item(0, 2)->text(), QString("1.2.3"));
        QCOMPARE(table->item(0, 3)->text(), QString("Tests"));
        QCOMPARE(table->item(0, 4)->text(), QString("Fixture.UI"));
        table->selectRow(0);
        window.findChild<QPushButton*>("toggleMod")->click();
        QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(), 1, 5000);
        QCOMPARE(table->item(0, 0)->text(), QString("Disabled"));
        auto* instances = window.findChild<QListWidget*>("instances");
        const auto id = instances->currentItem()->data(Qt::UserRole + 2).toString();
        prismatic::ModManager manager(data + "/profiles/" + id);
        auto installed = manager.mods();
        QCOMPARE(installed.size(), 1);
        QCOMPARE(readFixture(installed.front().directory + "/config.json"),
                 QByteArray("preserve-me"));
        table->selectRow(0);
        answerQuestion(window, QMessageBox::Cancel);
        window.findChild<QPushButton*>("removeMod")->click();
        QCOMPARE(table->rowCount(), 1);
        table->selectRow(0);
        answerQuestion(window, QMessageBox::Yes);
        window.findChild<QPushButton*>("removeMod")->click();
        QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(), 0, 5000);
        QVERIFY(manager.mods().isEmpty());
        QCOMPARE(readFixture(source + "/config.json"), QByteArray("preserve-me"));
        auto* logs = window.findChild<QPlainTextEdit*>("sessionLogs");
        QVERIFY(logs->toPlainText().contains("Installing local mods"));
        auto* filter = window.findChild<QLineEdit*>("logFilter");
        filter->setText("not-a-real-log-entry");
        QVERIFY(logs->toPlainText().isEmpty());
        filter->clear();
        QVERIFY(!logs->toPlainText().isEmpty());
    }

    void importsZipCollectionIntoOnlySelectedProfile() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        QVERIFY(temp.isValid());
        const auto source = temp.filePath("collection.zip");
        QVERIFY(writeZipFixture(
            source,
            {{"collection/A/manifest.json",
              R"({"Name":"Collection A","Author":"Tests","Version":"1.0","UniqueID":"Fixture.CollectionA"})"},
             {"collection/A/config.json", "collection-options"},
             {"collection/B/manifest.json",
              R"({"Name":"Collection B","Author":"Tests","Version":"2.0","UniqueID":"Fixture.CollectionB"})"}}));
        const auto originalArchive = readFixture(source);
        const auto data = temp.filePath("data");
        LauncherWindow window(temp.filePath("launcher.ini"), nullptr, data);
        auto* instances = window.findChild<QListWidget*>("instances");
        const auto firstId = instances->currentItem()->data(Qt::UserRole + 2).toString();
        enterDialogText(window, "Collection profile");
        for (auto* button : window.findChildren<QPushButton*>()) {
            if (button->text() == "+  Add instance") button->click();
        }
        QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
        QCOMPARE(instances->count(), 2);
        const auto collectionId = instances->currentItem()->data(Qt::UserRole + 2).toString();
        QVERIFY(collectionId != firstId);
        auto* navigation = window.findChild<QListWidget*>("navigation");
        navigation->setCurrentRow(3);
        bool dialogHandled = false;
        QTimer::singleShot(0, &window, [source, &dialogHandled] {
            if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                dialog->setDirectory(QFileInfo(source).absolutePath());
                dialog->selectFile(source);
                dialogHandled = true;
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            }
        });
        window.findChild<QPushButton*>("importModpack")->click();
        QVERIFY(dialogHandled);
        auto* table = window.findChild<QTableWidget*>("modsTable");
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
        navigation->setCurrentRow(0);
        QSet<QString> ids;
        for (int row = 0; row < table->rowCount(); ++row) {
            QCOMPARE(table->item(row, 0)->text(), QString("Enabled"));
            ids.insert(table->item(row, 4)->text());
        }
        QCOMPARE(ids, QSet<QString>({"Fixture.CollectionA", "Fixture.CollectionB"}));
        prismatic::ModManager collection(data + "/profiles/" + collectionId);
        const auto installed = collection.mods();
        QCOMPARE(installed.size(), 2);
        for (const auto& mod : installed) {
            if (mod.manifest.uniqueId == "Fixture.CollectionA") {
                QCOMPARE(readFixture(mod.directory + "/config.json"),
                         QByteArray("collection-options"));
            }
        }
        QVERIFY(prismatic::ModManager(data + "/profiles/" + firstId).mods().isEmpty());
        QCOMPARE(readFixture(source), originalArchive);
        window.findChild<QPushButton*>("backToLibrary")->click();
        instances->setCurrentRow(0);
        window.findChild<QPushButton*>("editInstance")->click();
        QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
        QCOMPARE(table->rowCount(), 0);
        window.findChild<QPushButton*>("backToLibrary")->click();
        instances->setCurrentRow(1);
        window.findChild<QPushButton*>("editInstance")->click();
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(idle(window), 5000);
    }

    void diagnosticExportIsBoundedRedactedAndIndependentOfFilter() {
        QTemporaryDir temp(QDir::currentPath() + "/ui-test-XXXXXX");
        QVERIFY(temp.isValid());
        const auto data = temp.filePath("data");
        const auto game = temp.filePath("private-game");
        QVERIFY(QDir().mkpath(game));
        LauncherWindow window(temp.filePath("launcher.ini"), nullptr, data);
        window.findChild<QLineEdit*>("gamePath")->setText(game);
        window.findChild<QPushButton*>("savePath")->click();
        QStringList lines;
        for (int i = 0; i < 2105; ++i) lines.append(QString("entry-%1").arg(i));
        lines.append("Private home " + QDir::homePath() + " game " + game + " data " + data);
        auto* launcher = window.findChild<prismatic::GameLauncher*>();
        QVERIFY(launcher);
        launcher->output(lines.join('\n'));
        auto* logs = window.findChild<QPlainTextEdit*>("sessionLogs");
        QVERIFY(logs->document()->blockCount() <= 2000);
        const auto visible = logs->toPlainText();
        QVERIFY(!visible.contains("entry-0\n"));
        QVERIFY(visible.contains("entry-2104"));
        QVERIFY(!visible.contains(game));
        QVERIFY(!visible.contains(data));
        QVERIFY(!visible.contains(QDir::homePath()));
        QVERIFY(visible.contains("<game>"));
        QVERIFY(visible.contains("<launcher-data>"));
        window.findChild<QLineEdit*>("logFilter")->setText("entry-2104");
        QVERIFY(logs->toPlainText().contains("entry-2104"));
        QVERIFY(!logs->toPlainText().contains("entry-2103"));
        const auto destination = temp.filePath("diagnostics.txt");
        bool dialogHandled = false;
        QTimer::singleShot(0, &window, [destination, &dialogHandled] {
            if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                dialog->selectFile(destination);
                dialogHandled = true;
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            }
        });
        window.findChild<QPushButton*>("exportLogs")->click();
        QVERIFY(dialogHandled);
        const auto exported = readFixture(destination);
        QVERIFY(!exported.isEmpty());
        QVERIFY(exported.contains("entry-2103"));
        QVERIFY(exported.contains("entry-2104"));
        QVERIFY(exported.contains("<game>"));
        QVERIFY(exported.contains("<launcher-data>"));
        QVERIFY(!exported.contains(game.toUtf8()));
        QVERIFY(!exported.contains(data.toUtf8()));
        QVERIFY(!exported.contains(QDir::homePath().toUtf8()));
        QVERIFY(exported.count('\n') < 2000);
    }
};

QTEST_MAIN(LauncherTests)
#include "LauncherTests.moc"
