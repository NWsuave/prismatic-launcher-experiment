#pragma once

#include <QList>
#include <QString>

struct ThemeInfo {
    QString id;
    QString name;
    QString description;
};

// Application-wide appearance. Preferences stay in the explicitly selected data
// directory; constructing a manager never reads or writes global Qt settings.
class ThemeManager {
   public:
    explicit ThemeManager(QString dataDirectory);

    static QList<ThemeInfo> themes();
    static QString styleSheetForTheme(const QString& id, QString* error = nullptr);

    QString currentThemeId() const;
    QString preferencesPath() const;

    // Missing preferences use the default without creating a file. Invalid
    // preferences apply the default, return false, and preserve the original.
    bool load(QString* error = nullptr);
    // Save first, then apply immediately. Failed saves leave appearance intact.
    bool setTheme(const QString& id, QString* error = nullptr);

   private:
    bool apply(const QString& id, QString* error);
    QString dataDirectory_;
    QString currentThemeId_ = "prismatic-dark";
};
