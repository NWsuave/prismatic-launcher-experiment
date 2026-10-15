#include "LauncherConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <algorithm>
#include <optional>
#include <utility>

#include "AtomicFile.h"

namespace {
#ifdef Q_OS_WIN
constexpr auto iniCase = Qt::CaseInsensitive;
#else
constexpr auto iniCase = Qt::CaseSensitive;
#endif

bool containsControl(const QString& text) {
    return std::any_of(text.cbegin(), text.cend(), [](QChar character) {
        return character.category() == QChar::Other_Control;
    });
}

// The bootstrap has one string setting. Parse that setting from fresh bytes instead of
// QSettings' shared file cache, which can miss rapid equal-size atomic replacements.
// Support the quoted/backslash-escaped scalar syntax emitted by this writer and legacy
// unquoted paths. Structured INI values and malformed quoting are not valid game paths.
std::optional<QString> pathValue(const QString& input) {
    const auto value = input.trimmed();
    const bool quoted = value.startsWith('"');
    QString result;
    bool escaped = false;
    bool closed = !quoted;
    for (qsizetype index = quoted ? 1 : 0; index < value.size(); ++index) {
        const auto character = value.at(index);
        if (escaped) {
            if (character != '\\' && character != '"') return std::nullopt;
            result.append(character);
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (quoted && character == '"') {
            const auto remainder = value.mid(index + 1).trimmed();
            if (!remainder.isEmpty() && !remainder.startsWith(';') && !remainder.startsWith('#'))
                return std::nullopt;
            closed = true;
            break;
        } else if (!quoted && (character == ';' || character == '#')) {
            break;
        } else if (!quoted && (character == ',' || character == '"')) {
            return std::nullopt;
        } else {
            result.append(character);
        }
    }
    if (escaped || !closed || containsControl(result)) return std::nullopt;
    return quoted ? result : result.trimmed();
}

std::optional<QString> bootstrapPath(const QByteArray& contents) {
    auto text = QString::fromUtf8(contents);
    if (text.startsWith(QChar::ByteOrderMark)) text.remove(0, 1);
    QString section;
    std::optional<QString> result;
    for (const auto& line : text.split('\n')) {
        const auto trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(';') || trimmed.startsWith('#')) continue;
        const auto closing = trimmed.indexOf(']');
        if (trimmed.startsWith('[') && closing > 0) {
            section = trimmed.mid(1, closing - 1);
            continue;
        }
        const auto equal = trimmed.indexOf('=');
        if (section.compare("game", iniCase) == 0 && equal >= 0 &&
            trimmed.left(equal).trimmed().compare("directory", iniCase) == 0) {
            result = pathValue(trimmed.mid(equal + 1));
        }
    }
    return result;
}
}  // namespace

LauncherConfig::LauncherConfig(QString filePath) : filePath_(std::move(filePath)) {}

QString LauncherConfig::expandHome(const QString& path) {
    return path.startsWith("~/") ? QDir::homePath() + path.mid(1) : path;
}

QString LauncherConfig::gameDirectory() const {
    QSettings defaults(":/ui/config/defaults.ini", QSettings::IniFormat);
    const auto fallback = expandHome(defaults.value("game/directory").toString());
    if (!prismatic::validateStoragePath(filePath_)) return fallback;
    QFile file(filePath_);
    if (file.size() > 1024 * 1024 || !file.open(QIODevice::ReadOnly)) return fallback;
    const auto configured = bootstrapPath(file.readAll());
    if (file.error() != QFileDevice::NoError || !configured) return fallback;
    const auto path = expandHome(configured->trimmed());
    return QDir::cleanPath(QDir::isAbsolutePath(path) ? path : fallback);
}

QString LauncherConfig::filePath() const { return filePath_; }

bool LauncherConfig::saveGameDirectory(const QString& directory) {
    const auto path = expandHome(directory.trimmed());
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) return false;
    if (containsControl(path)) return false;
    if (!prismatic::validateStoragePath(filePath_)) return false;
    const auto target = QFileInfo(filePath_).absoluteFilePath();
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) return false;
    QString original;
    if (QFileInfo::exists(target)) {
        QFile file(target);
        if (file.size() > 1024 * 1024 || !file.open(QIODevice::ReadOnly)) return false;
        original = QString::fromUtf8(file.readAll());
        if (file.error() != QFileDevice::NoError) return false;
    }
    auto escaped = QDir::cleanPath(path);
    escaped.replace("\\", "\\\\");
    escaped.replace("\"", "\\\"");
    const QString entry = "directory=\"" + escaped + "\"";
    // Keep unrelated INI settings and comments. Replace every duplicate key, emitting only
    // one value into the first [game] section, so repeated saves never grow the file.
    QStringList lines;
    QString section;
    bool written = false;
    for (const auto& line : original.split('\n')) {
        const auto trimmed = line.trimmed();
        const auto closing = trimmed.indexOf(']');
        if (trimmed.startsWith('[') && closing > 0) {
            section = trimmed.mid(1, closing - 1);
            lines.append(line);
            if (section.compare("game", iniCase) == 0 && !written) {
                lines.append(entry);
                written = true;
            }
            continue;
        }
        if (section.compare("game", iniCase) == 0 && !trimmed.startsWith(';') &&
            !trimmed.startsWith('#') &&
            trimmed.section('=', 0, 0).trimmed().compare("directory", iniCase) == 0 &&
            trimmed.contains('=')) {
            continue;
        }
        lines.append(line);
    }
    if (!written) {
        lines.append("[game]");
        lines.append(entry);
    }
    auto output = lines.join('\n');
    if (!output.endsWith('\n')) output.append('\n');
    return prismatic::writeFileAtomically(target, output.toUtf8());
}
