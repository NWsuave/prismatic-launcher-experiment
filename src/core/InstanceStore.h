#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace prismatic {

struct Instance {
    QString id;
    QString name;
    QString gamePath;
    bool useSmapi = true;
    QStringList launchArguments;
};

// Stores mod profiles which share game files; it never writes into a game directory.
// One object belongs to one thread. Independent objects are protected against stale writes.
class InstanceStore {
   public:
    explicit InstanceStore(const QString& rootPath);
    bool load(QString* error = nullptr);
    QList<Instance> instances() const;
    std::optional<Instance> find(const QString& id) const;
    QString selectedId() const;
    bool create(const QString& name, const QString& gamePath, QString* createdId = nullptr,
                QString* error = nullptr);
    bool update(const Instance& instance, QString* error = nullptr);
    bool duplicate(const QString& id, const QString& name, QString* createdId = nullptr,
                   QString* error = nullptr);
    // Moves the profile into root/trash for recovery; call only after user confirmation.
    bool remove(const QString& id, QString* error = nullptr);
    bool select(const QString& id, QString* error = nullptr);
    QString profilePath(const QString& id) const;
    QString modsPath(const QString& id) const;

   private:
    bool prepare(QString* error) const;
    bool commit(const QList<Instance>& instances, const QString& selected, QString* error);
    static bool validate(const Instance& instance, QString* error);
    QString m_root;
    QList<Instance> m_instances;
    QString m_selected;
    QByteArray m_diskSnapshot;
    bool m_diskExists = false;
    bool m_loaded = false;
};

}  // namespace prismatic
