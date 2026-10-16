#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QtTest>
#include <cmath>

#include "ThemeManager.h"

namespace {
bool writeFixture(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFixture(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

double luminance(const QColor& color) {
    const auto channel = [](double value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) +
           0.0722 * channel(color.blueF());
}

double contrast(const QColor& first, const QColor& second) {
    return (std::max(luminance(first), luminance(second)) + 0.05) /
           (std::min(luminance(first), luminance(second)) + 0.05);
}
}  // namespace

class ThemeTests : public QObject {
    Q_OBJECT
   private slots:
    void missingPreferencesUseDefaultWithoutWriting() {
        QTemporaryDir temp(QDir::currentPath() + "/theme-test-XXXXXX");
        QVERIFY(temp.isValid());
        ThemeManager manager(temp.filePath("data"));
        QString error;
        QVERIFY2(manager.load(&error), qPrintable(error));
        QCOMPARE(manager.currentThemeId(), QString("prismatic-dark"));
        QVERIFY(!QFileInfo::exists(manager.preferencesPath()));
        QVERIFY(!qApp->styleSheet().isEmpty());
    }

    void loadsAndPersistsEveryTheme() {
        QTemporaryDir temp(QDir::currentPath() + "/theme-test-XXXXXX");
        QVERIFY(temp.isValid());
        ThemeManager manager(temp.path());
        QCOMPARE(ThemeManager::themes().size(), 3);
        QString previousStyle;
        for (const auto& theme : ThemeManager::themes()) {
            QString error;
            QVERIFY2(manager.setTheme(theme.id, &error), qPrintable(error));
            QCOMPARE(manager.currentThemeId(), theme.id);
            const auto saved = QJsonDocument::fromJson(readFixture(manager.preferencesPath()));
            QCOMPARE(saved.object().value("theme").toString(), theme.id);
            ThemeManager reopened(temp.path());
            QVERIFY2(reopened.load(&error), qPrintable(error));
            QCOMPARE(reopened.currentThemeId(), theme.id);
            QVERIFY(!qApp->styleSheet().isEmpty());
            QVERIFY(qApp->styleSheet() != previousStyle);
            previousStyle = qApp->styleSheet();
        }
    }

    void invalidPreferencesFallBackAndPreserveOriginal_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("malformed") << QByteArray("{broken");
        QTest::newRow("unknown") << QByteArray(R"({"schemaVersion":1,"theme":"unknown"})");
        QTest::newRow("wrong type") << QByteArray(R"({"schemaVersion":1,"theme":42})");
        QTest::newRow("unsupported schema")
            << QByteArray(R"({"schemaVersion":2,"theme":"retro-valley"})");
        QTest::newRow("oversized") << QByteArray(65537, 'x');
    }

    void invalidPreferencesFallBackAndPreserveOriginal() {
        QFETCH(QByteArray, bytes);
        QTemporaryDir temp(QDir::currentPath() + "/theme-test-XXXXXX");
        QVERIFY(temp.isValid());
        ThemeManager manager(temp.path());
        QVERIFY(writeFixture(manager.preferencesPath(), bytes));
        QString error;
        QVERIFY(!manager.load(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(manager.currentThemeId(), QString("prismatic-dark"));
        QCOMPARE(readFixture(manager.preferencesPath()), bytes);
    }

    void rejectedChangesKeepCurrentAppearanceAndPreferences() {
        QTemporaryDir temp(QDir::currentPath() + "/theme-test-XXXXXX");
        QVERIFY(temp.isValid());
        ThemeManager manager(temp.path());
        QVERIFY(manager.setTheme("retro-valley"));
        const auto saved = readFixture(manager.preferencesPath());
        const auto style = qApp->styleSheet();
        QString error;
        QVERIFY(!manager.setTheme("../../untrusted", &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(manager.currentThemeId(), QString("retro-valley"));
        QCOMPARE(qApp->styleSheet(), style);
        QCOMPARE(readFixture(manager.preferencesPath()), saved);
        ThemeManager blocked(temp.filePath("blocked"));
        QVERIFY(writeFixture(temp.filePath("blocked"), "regular-file"));
        QVERIFY(!blocked.setTheme("retro-terminal", &error));
        QCOMPARE(qApp->styleSheet(), style);
    }

    void rejectsSymlinkPreferenceFiles() {
#ifdef Q_OS_UNIX
        QTemporaryDir temp(QDir::currentPath() + "/theme-test-XXXXXX");
        QVERIFY(temp.isValid());
        const auto target = temp.filePath("untouched.json");
        const QByteArray bytes = R"({"schemaVersion":1,"theme":"retro-valley"})";
        QVERIFY(writeFixture(target, bytes));
        ThemeManager manager(temp.filePath("data"));
        QVERIFY(QDir().mkpath(temp.filePath("data")));
        QVERIFY(QFile::link(target, manager.preferencesPath()));
        QString error;
        QVERIFY(!manager.load(&error));
        QVERIFY(!manager.setTheme("retro-terminal", &error));
        QCOMPARE(readFixture(target), bytes);
#else
        QSKIP("Portable link fixture requires Unix symlinks.");
#endif
    }

    void themesReachExistingDialogsAndRetainReadableColors() {
        QTemporaryDir temp(QDir::currentPath() + "/theme-test-XXXXXX");
        QVERIFY(temp.isValid());
        ThemeManager manager(temp.path());
        QDialog dialog;
        auto* layout = new QVBoxLayout(&dialog);
        auto* edit = new QLineEdit(&dialog);
        auto* button = new QPushButton("Continue", &dialog);
        layout->addWidget(edit);
        layout->addWidget(button);
        dialog.show();
        for (const auto& theme : ThemeManager::themes()) {
            QVERIFY(manager.setTheme(theme.id));
            QApplication::processEvents();
            const auto palette = qApp->palette();
            QVERIFY(contrast(palette.color(QPalette::Window),
                             palette.color(QPalette::WindowText)) >= 4.5);
            QVERIFY(contrast(palette.color(QPalette::Base), palette.color(QPalette::Text)) >= 4.5);
            QVERIFY(contrast(palette.color(QPalette::Highlight),
                             palette.color(QPalette::HighlightedText)) >= 4.5);
            QCOMPARE(dialog.palette().color(QPalette::Window), palette.color(QPalette::Window));
            QVERIFY(edit->focusPolicy() != Qt::NoFocus);
            QVERIFY(button->focusPolicy() != Qt::NoFocus);
            if (theme.id == "retro-terminal") QVERIFY(qApp->font().families().size() > 1);
        }
    }
};

QTEST_MAIN(ThemeTests)
#include "ThemeTests.moc"
