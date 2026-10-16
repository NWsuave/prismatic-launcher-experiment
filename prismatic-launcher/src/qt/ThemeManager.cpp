#include "ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QResource>
#include <algorithm>
#include <utility>

#include "AtomicFile.h"

static void initializeThemeResources() { Q_INIT_RESOURCE(theme_resources); }

namespace {
bool knownTheme(const QString& id) {
    const auto themes = ThemeManager::themes();
    return std::any_of(themes.cbegin(), themes.cend(),
                       [&id](const auto& theme) { return theme.id == id; });
}

QPalette paletteForTheme(const QString& id) {
    QPalette palette;
    const bool valley = id == "retro-valley";
    const bool terminal = id == "retro-terminal";
    const QColor window(valley ? "#f3ecd6" : terminal ? "#101810" : "#151b20");
    const QColor surface(valley ? "#fff9e8" : terminal ? "#142014" : "#1d272d");
    const QColor text(valley ? "#342b1c" : terminal ? "#b8e6a8" : "#e5ebe8");
    const QColor accent(valley ? "#526127" : terminal ? "#a3d98b" : "#8bd6b8");
    const QColor selectedText(valley ? "#fff9e8" : terminal ? "#101810" : "#132c22");
    const QColor border(valley ? "#93845a" : terminal ? "#587b48" : "#3b4a52");
    const QColor muted(valley ? "#655b3e" : terminal ? "#86a77a" : "#adbbb9");
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, surface);
    palette.setColor(QPalette::AlternateBase, QColor(valley     ? "#e9e0c4"
                                                     : terminal ? "#1b2a19"
                                                                : "#202c31"));
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::PlaceholderText, muted);
    palette.setColor(QPalette::Button, QColor(valley     ? "#e1d5b2"
                                              : terminal ? "#20321b"
                                                         : "#273239"));
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, selectedText);
    palette.setColor(QPalette::Link, QColor(valley ? "#526127" : terminal ? "#e2be73" : "#8bd6b8"));
    palette.setColor(QPalette::LinkVisited, muted);
    palette.setColor(QPalette::Mid, border);
    palette.setColor(QPalette::Dark, border);
    palette.setColor(QPalette::Light, surface);
    palette.setColor(QPalette::ToolTipBase, surface);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::BrightText, muted);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, muted);
    return palette;
}

QFont fontForTheme(const QString& id) {
    QFont font;
    if (id == "retro-terminal") {
        font.setFamilies({"Cascadia Mono", "Consolas", "Menlo", "DejaVu Sans Mono", "monospace"});
        font.setStyleHint(QFont::Monospace);
    } else if (id == "retro-valley") {
        font.setFamilies({"Verdana", "Trebuchet MS", "DejaVu Sans", "sans-serif"});
    } else {
        font.setFamilies({"Segoe UI", "Helvetica Neue", "DejaVu Sans", "sans-serif"});
    }
    return font;
}
}  // namespace

ThemeManager::ThemeManager(QString dataDirectory) : dataDirectory_(std::move(dataDirectory)) {}

QList<ThemeInfo> ThemeManager::themes() {
    return {{"prismatic-dark", "Prismatic Dark", "Charcoal surfaces and mint highlights."},
            {"retro-valley", "Retro Valley", "Warm cream, olive, brown and gold."},
            {"retro-terminal", "Retro Terminal", "Vintage green terminal with amber accents."}};
}

QString ThemeManager::styleSheetForTheme(const QString& id, QString* error) {
    if (error) error->clear();
    if (!knownTheme(id)) {
        if (error) *error = "Unknown theme: " + id;
        return {};
    }
    static const bool initialized = [] {
        initializeThemeResources();
        return true;
    }();
    Q_UNUSED(initialized);
    QString result;
    for (const auto& resource : {QString(":/themes/common.qss"), ":/themes/" + id + ".qss"}) {
        QFile file(resource);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = "Could not load the bundled theme resource: " + resource;
            return {};
        }
        result += QString::fromUtf8(file.readAll()) + '\n';
    }
    return result;
}

QString ThemeManager::currentThemeId() const { return currentThemeId_; }

QString ThemeManager::preferencesPath() const { return dataDirectory_ + "/theme-preferences.json"; }

bool ThemeManager::apply(const QString& id, QString* error) {
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) {
        if (error) *error = "Themes require a running QApplication.";
        return false;
    }
    const auto stylesheet = styleSheetForTheme(id, error);
    if (stylesheet.isEmpty()) return false;
    app->setPalette(paletteForTheme(id));
    app->setFont(fontForTheme(id));
    app->setStyleSheet(stylesheet);
    currentThemeId_ = id;
    return true;
}

bool ThemeManager::load(QString* error) {
    if (error) error->clear();
    const auto fallback = [this, error](const QString& reason) {
        QString themeError;
        const bool applied = apply("prismatic-dark", &themeError);
        if (error) *error = applied ? reason : reason + " " + themeError;
        return false;
    };
    QString pathError;
    if (dataDirectory_.isEmpty() || !prismatic::validateStoragePath(preferencesPath(), &pathError))
        return fallback("Unsafe theme preference path. " + pathError);
    const QFileInfo info(preferencesPath());
    if (!info.exists()) return apply("prismatic-dark", error);
    if (!info.isFile() || info.size() > 65536)
        return fallback("Theme preferences must be a regular JSON file of at most 64 KiB.");
    QFile file(preferencesPath());
    if (!file.open(QIODevice::ReadOnly))
        return fallback("Could not read theme preferences: " + file.errorString());
    const auto bytes = file.read(65537);
    if (bytes.size() > 65536) return fallback("Theme preferences exceed 64 KiB.");
    const auto document = QJsonDocument::fromJson(bytes);
    const auto object = document.object();
    const auto schema = object.value("schemaVersion");
    const auto theme = object.value("theme");
    if (!document.isObject() || !schema.isDouble() || schema.toDouble() != 1 || !theme.isString() ||
        !knownTheme(theme.toString()))
        return fallback("Invalid or unsupported theme preferences; using Prismatic Dark.");
    return apply(theme.toString(), error);
}

bool ThemeManager::setTheme(const QString& id, QString* error) {
    if (error) error->clear();
    if (styleSheetForTheme(id, error).isEmpty()) return false;
    if (!qobject_cast<QApplication*>(QCoreApplication::instance())) {
        if (error) *error = "Themes require a running QApplication.";
        return false;
    }
    if (dataDirectory_.isEmpty() || !prismatic::validateStoragePath(preferencesPath(), error)) {
        if (error && error->isEmpty()) *error = "A theme data directory is required.";
        return false;
    }
    if (!QDir().mkpath(dataDirectory_)) {
        if (error) *error = "Could not create the theme preference directory.";
        return false;
    }
    const auto bytes = QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"theme", id}}).toJson();
    if (!prismatic::writeFileAtomically(preferencesPath(), bytes, error)) return false;
    return apply(id, error);
}
