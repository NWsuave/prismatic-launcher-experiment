#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include "AtomicFile.h"
#include "DependencyResolver.h"
#include "GameLauncher.h"
#include "LauncherWindow.h"
#include "ThemeManager.h"

namespace {
QLabel* label(const QString& text, const QString& role) {
    auto* result = new QLabel(text);
    result->setObjectName(role);
    result->setWordWrap(true);
    result->setTextFormat(Qt::PlainText);
    return result;
}
}  // namespace

QWidget* LauncherWindow::createDetails() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    auto* heading = new QHBoxLayout;
    auto* back = new QPushButton("←  Library");
    back->setObjectName("backToLibrary");
    heading->addWidget(back);
    detailTitle_ = label("Stardew Valley", "sectionTitle");
    heading->addWidget(detailTitle_, 1);
    auto* play = new QPushButton("▶  Play instance");
    play->setObjectName("primary");
    play->setProperty("requiresIdle", true);
    heading->addWidget(play);
    layout->addLayout(heading);
    auto* body = new QHBoxLayout;
    auto* sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(152);
    auto* side = new QVBoxLayout(sidebar);
    side->addWidget(label("INSTANCE", "eyebrow"));
    navigation_ = new QListWidget;
    navigation_->setObjectName("navigation");
    navigation_->setAccessibleName("Instance sections");
    navigation_->addItems({"Mods", "Logs", "Settings", "Modpacks"});
    side->addWidget(navigation_, 1);
    side->addWidget(label("Shared game files\nIsolated mod profiles", "muted"));
    body->addWidget(sidebar);
    detailPages_ = new QStackedWidget;
    detailPages_->setObjectName("detailPages");
    detailPages_->addWidget(createModsPage());
    detailPages_->addWidget(createLogsPage());
    detailPages_->addWidget(createSettingsPage());
    detailPages_->addWidget(createModpacksPage());
    body->addWidget(detailPages_, 1);
    layout->addLayout(body, 1);
    connect(navigation_, &QListWidget::currentRowChanged, detailPages_,
            &QStackedWidget::setCurrentIndex);
    navigation_->setCurrentRow(0);
    connect(back, &QPushButton::clicked, this, [this] { pages_->setCurrentIndex(0); });
    connect(play, &QPushButton::clicked, this, &LauncherWindow::launchGame);
    return page;
}

QWidget* LauncherWindow::createModsPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->addWidget(label("Your mods", "sectionTitle"));
    layout->addWidget(
        label("Install local mods into this profile. Existing game Mods are untouched. "
              "Install mod dependencies into each profile too.",
              "muted"));
    auto* tools = new QHBoxLayout;
    auto* zip = new QPushButton("Install ZIP…");
    zip->setProperty("requiresIdle", true);
    zip->setObjectName("installZip");
    auto* folder = new QPushButton("Install folder…");
    folder->setProperty("requiresIdle", true);
    folder->setObjectName("installFolder");
    auto* refresh = new QPushButton("Refresh");
    refresh->setProperty("requiresIdle", true);
    refresh->setObjectName("refreshMods");
    tools->addWidget(zip);
    tools->addWidget(folder);
    tools->addWidget(refresh);
    auto* dependencies = new QPushButton("Check dependencies…");
    dependencies->setObjectName("checkDependencies");
    dependencies->setProperty("requiresIdle", true);
    tools->addWidget(dependencies);
    connect(dependencies, &QPushButton::clicked, this, [this] {
        if (const auto profile = selectedInstance()) inspectDependencies(profile->id);
    });
    tools->addStretch();
    layout->addLayout(tools);
    mods_ = new QTableWidget(0, 5);
    mods_->setObjectName("modsTable");
    mods_->setAccessibleName("Installed mods in selected profile");
    mods_->setHorizontalHeaderLabels({"Status", "Name", "Version", "Author", "Unique ID"});
    mods_->setSelectionBehavior(QAbstractItemView::SelectRows);
    mods_->setSelectionMode(QAbstractItemView::SingleSelection);
    mods_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mods_->verticalHeader()->hide();
    mods_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    mods_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(mods_, 1);
    auto* actions = new QHBoxLayout;
    auto* toggle = new QPushButton("Enable / Disable");
    toggle->setProperty("requiresIdle", true);
    toggle->setObjectName("toggleMod");
    auto* remove = new QPushButton("Remove…");
    remove->setProperty("requiresIdle", true);
    remove->setObjectName("removeMod");
    actions->addWidget(toggle);
    actions->addWidget(remove);
    actions->addStretch();
    layout->addLayout(actions);
    modStatus_ = label("No mods installed. Choose a ZIP or folder to get started.", "muted");
    modStatus_->setObjectName("modStatus");
    layout->addWidget(modStatus_);
    connect(zip, &QPushButton::clicked, this, [this] { installMods(true); });
    connect(folder, &QPushButton::clicked, this, [this] { installMods(false); });
    connect(refresh, &QPushButton::clicked, this, &LauncherWindow::refreshMods);
    connect(toggle, &QPushButton::clicked, this, &LauncherWindow::toggleMod);
    connect(remove, &QPushButton::clicked, this, &LauncherWindow::removeMod);
    return page;
}

void LauncherWindow::refreshMods() {
    if (busy_) return;
    const auto instance = selectedInstance();
    mods_->setRowCount(0);
    installedMods_.clear();
    if (!instance) return;
    const auto profile = store_->profilePath(instance->id);
    const auto id = instance->id;
    busy_ = true;
    updateControls();
    modStatus_->setText("Reading profile mods…");
    using Scan = QPair<QList<prismatic::InstalledMod>, QString>;
    auto* watcher = new QFutureWatcher<Scan>(this);
    connect(watcher, &QFutureWatcher<Scan>::finished, this, [this, watcher, id] {
        const auto result = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        updateControls();
        const auto current = selectedInstance();
        if (!current || current->id != id) {
            refreshMods();
            return;
        }
        // Bound widget creation even if an externally modified profile contains many mods.
        installedMods_ = result.first.mid(0, 10000);
        mods_->setRowCount(static_cast<int>(installedMods_.size()));
        int row = 0;
        for (const auto& mod : installedMods_) {
            const QStringList values{mod.enabled ? "Enabled" : "Disabled", mod.manifest.name,
                                     mod.manifest.version, mod.manifest.author,
                                     mod.manifest.uniqueId};
            for (int column = 0; column < values.size(); ++column) {
                auto* item = new QTableWidgetItem(values.at(column));
                item->setToolTip("<qt>" + values.at(column).toHtmlEscaped() + "</qt>");
                mods_->setItem(row, column, item);
            }
            ++row;
        }
        modStatus_->setText(
            result.second.isEmpty()
                ? (installedMods_.isEmpty()
                       ? "No mods installed. Choose a ZIP or folder to get started."
                       : QString("%1 mod(s). Disabled mods keep all their files and configuration.")
                             .arg(installedMods_.size()))
                : "Some mods could not be read: " + result.second);
        if (result.first.size() > installedMods_.size()) {
            modStatus_->setText(
                "Showing the first 10000 mods. This profile exceeds the display limit.");
        }
        if (!result.second.isEmpty()) appendLog("Mod scan: " + result.second);
        const auto dependencies = prismatic::DependencyResolver::resolve(installedMods_);
        if (dependencies.hasBlockingIssues()) {
            modStatus_->setText(modStatus_->text() +
                                "\nRequired dependencies need attention. Use Check dependencies.");
            modStatus_->setToolTip(dependencies.summary());
            appendLog("Dependency check: " + dependencies.summary());
        } else {
            modStatus_->setToolTip({});
        }
        if (dependencyPromptProfileId_ == id) {
            dependencyPromptProfileId_.clear();
            if (dependencies.hasBlockingIssues()) inspectDependencies(id);
        }
    });
    watcher->setFuture(QtConcurrent::run([profile] {
        QString error;
        const auto mods = prismatic::ModManager(profile).mods(&error);
        return Scan(mods, error);
    }));
}

void LauncherWindow::installMods(bool archive) {
    if (!canChange()) return;
    const auto instance = selectedInstance();
    if (!instance) return;
    const auto source = archive
                            ? QFileDialog::getOpenFileName(this, "Install local mod archive", {},
                                                           "ZIP archives (*.zip)")
                            : QFileDialog::getExistingDirectory(this, "Install local mod folder");
    if (source.isEmpty()) return;
    const auto profile = store_->profilePath(instance->id);
    const auto installed = std::make_shared<bool>(false);
    runTask(
        "Installing local mods…",
        [profile, source, archive, installed] {
            prismatic::ModManager manager(profile);
            QString error;
            *installed = archive ? manager.installZip(source, &error)
                                 : manager.installFolder(source, &error);
            return *installed ? QString() : error;
        },
        [this, installed, id = instance->id] {
            if (*installed) dependencyPromptProfileId_ = id;
        });
}

void LauncherWindow::toggleMod() {
    if (!canChange()) return;
    const auto instance = selectedInstance();
    const int row = mods_->currentRow();
    if (!instance || row < 0 || row >= installedMods_.size()) return;
    const auto mod = installedMods_.at(row);
    if (!mod.enabled && !confirmDependencies(instance->id, mod.manifest.uniqueId)) return;
    const auto profile = store_->profilePath(instance->id);
    runTask(mod.enabled ? "Disabling mod…" : "Enabling mod…", [profile, mod] {
        QString error;
        if (!prismatic::ModManager(profile).setEnabled(mod.manifest.uniqueId, !mod.enabled, &error))
            return error;
        return QString();
    });
}

void LauncherWindow::removeMod() {
    if (!canChange()) return;
    const auto instance = selectedInstance();
    const int row = mods_->currentRow();
    if (!instance || row < 0 || row >= installedMods_.size()) return;
    const auto mod = installedMods_.at(row);
    QMessageBox confirmation(QMessageBox::Question, "Remove mod?",
                             "Remove “" + mod.manifest.name +
                                 "” and its configuration from this profile?\nChoose Disable to "
                                 "preserve its files instead.",
                             QMessageBox::Yes | QMessageBox::Cancel, this);
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setDefaultButton(QMessageBox::Cancel);
    if (confirmation.exec() != QMessageBox::Yes) return;
    const auto profile = store_->profilePath(instance->id);
    runTask("Removing mod…", [profile, mod] {
        QString error;
        if (!prismatic::ModManager(profile).remove(mod.manifest.uniqueId, &error)) return error;
        return QString();
    });
}

QWidget* LauncherWindow::createLogsPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->addWidget(label("Launcher activity", "sectionTitle"));
    layout->addWidget(
        label("Current session, including captured game and SMAPI output. "
              "Home and configured game paths are redacted; review exports before sharing.",
              "muted"));
    logFilter_ = new QLineEdit;
    logFilter_->setObjectName("logFilter");
    logFilter_->setPlaceholderText("Filter activity…");
    logFilter_->setAccessibleName("Filter launcher activity");
    layout->addWidget(logFilter_);
    logs_ = new QPlainTextEdit;
    logs_->setObjectName("sessionLogs");
    logs_->setReadOnly(true);
    logs_->setMaximumBlockCount(2000);
    logs_->setAccessibleName("Launcher activity log");
    layout->addWidget(logs_, 1);
    auto* exportLog = new QPushButton("Export diagnostic log…");
    exportLog->setObjectName("exportLogs");
    layout->addWidget(exportLog, 0, Qt::AlignLeft);
    connect(logFilter_, &QLineEdit::textChanged, this, &LauncherWindow::renderLogs);
    connect(exportLog, &QPushButton::clicked, this, [this] {
        const auto destination = QFileDialog::getSaveFileName(
            this, "Export redacted diagnostic log", {}, "Text files (*.txt)");
        if (destination.isEmpty()) return;
        const auto bytes = redacted(logLines_.join('\n')).toUtf8();
        QString error;
        if (!prismatic::writeFileAtomically(destination, bytes, &error)) {
            reportError("Could not write diagnostic log: " + error);
            return;
        }
        statusBar()->showMessage("Redacted diagnostic log exported. Review before sharing.");
    });
    return page;
}

QWidget* LauncherWindow::createSettingsPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    layout->addWidget(label("Instance settings", "sectionTitle"));
    auto* form = new QFormLayout;
    auto* theme = new QComboBox;
    theme->setObjectName("themeSelector");
    theme->setAccessibleName("Application theme");
    for (const auto& entry : ThemeManager::themes()) theme->addItem(entry.name, entry.id);
    theme->setCurrentIndex(theme->findData(themes_->currentThemeId()));
    form->addRow("Application theme", theme);
    connect(theme, &QComboBox::currentIndexChanged, this, [this, theme] {
        QString error;
        if (!themes_->setTheme(theme->currentData().toString(), &error)) {
            const QSignalBlocker blocker(theme);
            theme->setCurrentIndex(theme->findData(themes_->currentThemeId()));
            reportError(error);
        }
    });
    launchMode_ = new QComboBox;
    launchMode_->setObjectName("launchMode");
    launchMode_->addItems({"Vanilla", "Modded (SMAPI)"});
    form->addRow("Launch mode", launchMode_);
    gamePath_ = new QLineEdit;
    gamePath_->setObjectName("gamePath");
    gamePath_->setAccessibleName("Stardew Valley installation directory");
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(gamePath_, 1);
    auto* browse = new QPushButton("Browse…");
    browse->setProperty("requiresIdle", true);
    pathRow->addWidget(browse);
    form->addRow("Game directory", pathRow);
    auto* discover = new QPushButton("Find Steam installations");
    discover->setProperty("requiresIdle", true);
    discover->setObjectName("discoverGames");
    form->addRow(discover);
    arguments_ = new QPlainTextEdit;
    arguments_->setObjectName("launchArguments");
    arguments_->setAccessibleName("Extra launch arguments, one argument per line");
    arguments_->setPlaceholderText("One argument per line; spaces belong to the same argument.");
    arguments_->setMaximumHeight(96);
    form->addRow("Extra arguments", arguments_);
    layout->addLayout(form);
    pathStatus_ = label("", "muted");
    pathStatus_->setObjectName("pathStatus");
    layout->addWidget(pathStatus_);
    auto* save = new QPushButton("Save settings");
    save->setProperty("requiresIdle", true);
    save->setObjectName("savePath");
    layout->addWidget(save, 0, Qt::AlignLeft);
    layout->addWidget(label(
        "Each profile has its own enabled and disabled mods. Modded launches "
        "pass this profile’s Mods directory to SMAPI with --mods-path. Game files and save files "
        "are shared. SMAPI must already be installed; this launcher does not download or install "
        "it.",
        "muted"));
    layout->addStretch();
    auto* location = label("Profile storage: " + dataDirectory_, "muted");
    location->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(location);
    connect(browse, &QPushButton::clicked, this, [this] {
        if (!canChange()) return;
        const auto directory = QFileDialog::getExistingDirectory(
            this, "Select Stardew Valley folder", gamePath_->text());
        if (!directory.isEmpty()) gamePath_->setText(directory);
    });
    connect(save, &QPushButton::clicked, this, &LauncherWindow::saveSettings);
    connect(discover, &QPushButton::clicked, this, [this] {
        if (!canChange()) return;
        busy_ = true;
        updateControls();
        pathStatus_->setText("Looking for Steam installations…");
        auto* watcher = new QFutureWatcher<QList<prismatic::GameInstallation>>(this);
        connect(watcher, &QFutureWatcher<QList<prismatic::GameInstallation>>::finished, this,
                [this, watcher] {
                    const auto found = watcher->result();
                    watcher->deleteLater();
                    busy_ = false;
                    updateControls();
                    if (found.isEmpty()) {
                        pathStatus_->setText(
                            "No conventional Steam installation found. Select your game directory "
                            "manually.");
                        return;
                    }
                    QStringList paths;
                    for (const auto& installation : found) paths.append(installation.gameDirectory);
                    if (paths.size() == 1)
                        gamePath_->setText(paths.first());
                    else {
                        bool accepted = false;
                        const auto selected =
                            QInputDialog::getItem(this, "Select game installation", "Installation",
                                                  paths, 0, false, &accepted);
                        if (accepted) gamePath_->setText(selected);
                    }
                    pathStatus_->setText(
                        "Installation found. Save settings to associate it with this profile.");
                });
        watcher->setFuture(QtConcurrent::run([] { return prismatic::GameLauncher::discover(); }));
    });
    auto* scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    return scroll;
}

void LauncherWindow::saveSettings() {
    if (!canChange()) return;
    auto instance = selectedInstance();
    if (!instance) return;
    const auto path = LauncherConfig::expandHome(gamePath_->text().trimmed());
    if (!QDir::isAbsolutePath(path) || !QFileInfo(path).isDir()) {
        pathStatus_->setText("Choose an existing absolute game directory before saving.");
        return;
    }
    instance->gamePath = QDir::cleanPath(path);
    instance->useSmapi = launchMode_->currentIndex() == 1;
    instance->launchArguments = arguments_->toPlainText().split('\n', Qt::SkipEmptyParts);
    QString error;
    if (!store_->update(*instance, &error)) return reportError(error);
    if (!config_.saveGameDirectory(path))
        appendLog("Profile saved, but bootstrap INI could not be updated.");
    reloadInstances();
    appendLog("Saved settings for profile: " + instance->name);
    statusBar()->showMessage("Profile settings saved.");
}

QWidget* LauncherWindow::createModpacksPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->addWidget(label("Local mod collections", "sectionTitle"));
    layout->addWidget(
        label("Install a ZIP containing several mods into this profile in one transaction. "
              "Every mod must contain a valid manifest.json. Duplicate IDs are rejected to protect "
              "existing "
              "mods and configuration. No online catalog or automatic updates are provided.",
              "muted"));
    auto* import = new QPushButton("Import collection ZIP…");
    import->setProperty("requiresIdle", true);
    import->setObjectName("importModpack");
    layout->addWidget(import, 0, Qt::AlignLeft);
    layout->addWidget(label(
        "Create a separate collection by duplicating this profile, including "
        "enabled mods, disabled mods, and configuration files. Game files and saves stay shared.",
        "muted"));
    auto* duplicate = new QPushButton("Duplicate as new profile…");
    duplicate->setProperty("requiresIdle", true);
    layout->addWidget(duplicate, 0, Qt::AlignLeft);
    layout->addStretch();
    connect(import, &QPushButton::clicked, this, [this] { installMods(true); });
    connect(duplicate, &QPushButton::clicked, this, &LauncherWindow::duplicateInstance);
    return page;
}
