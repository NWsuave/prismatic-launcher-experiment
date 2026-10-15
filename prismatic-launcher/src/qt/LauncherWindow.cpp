#include "LauncherWindow.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <exception>

#include "GameLauncher.h"

namespace {
QIcon instanceIcon() {
    QPixmap art(160, 160);
    art.fill(QColor("#253e39"));
    QPainter painter(&art);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#72c7ab"));
    painter.drawEllipse(75, 32, 40, 40);
    painter.setBrush(QColor("#3c6655"));
    painter.drawPolygon(QPolygon{{0, 115}, {55, 60}, {110, 115}});
    painter.setBrush(QColor("#527b5f"));
    painter.drawPolygon(QPolygon{{55, 140}, {120, 65}, {180, 140}});
    painter.setBrush(QColor("#d8c495"));
    painter.drawRoundedRect(48, 101, 40, 32, 2, 2);
    painter.setBrush(QColor("#b97656"));
    painter.drawPolygon(QPolygon{{40, 102}, {68, 78}, {96, 102}});
    painter.setBrush(QColor("#253e39"));
    painter.drawRect(64, 113, 10, 20);
    return QIcon(art);
}
}  // namespace

LauncherWindow::LauncherWindow(const QString& configFile, QWidget* parent,
                               const QString& dataDirectory)
    : QMainWindow(parent),
      config_(configFile),
      dataDirectory_(dataDirectory.isEmpty() ? QFileInfo(configFile).absolutePath() + "/profiles"
                                             : QFileInfo(dataDirectory).absoluteFilePath()),
      store_(std::make_unique<prismatic::InstanceStore>(dataDirectory_)),
      launcher_(new prismatic::GameLauncher(this)) {
    setWindowTitle("Prismatic Launcher");
    resize(1120, 760);
    setMinimumSize(820, 600);
    setWindowIcon(instanceIcon());
    auto* root = new QWidget(this);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* header = new QWidget;
    header->setObjectName("header");
    auto* bar = new QHBoxLayout(header);
    bar->setContentsMargins(24, 16, 24, 16);
    auto* mark = new QLabel("◈");
    mark->setObjectName("brandMark");
    auto* brand = new QLabel("PRISMATIC");
    brand->setObjectName("brand");
    bar->addWidget(mark);
    bar->addWidget(brand);
    bar->addStretch();
    auto* activity = new QPushButton("Logs");
    activity->setObjectName("openLogs");
    bar->addWidget(activity);
    auto* settings = new QPushButton("Settings");
    settings->setAccessibleName("Launcher settings");
    bar->addWidget(settings);
    layout->addWidget(header);
    pages_ = new QStackedWidget;
    pages_->setObjectName("pages");
    pages_->addWidget(createLibrary());
    pages_->addWidget(createDetails());
    layout->addWidget(pages_, 1);
    setCentralWidget(root);
    progress_ = new QProgressBar;
    progress_->setRange(0, 0);
    progress_->setMaximumWidth(160);
    progress_->hide();
    statusBar()->addPermanentWidget(progress_);
    connect(settings, &QPushButton::clicked, this, [this] { openInstance(2); });
    connect(activity, &QPushButton::clicked, this, [this] { openInstance(1); });
    connect(launcher_, &prismatic::GameLauncher::output, this, &LauncherWindow::appendLog);
    connect(launcher_, &prismatic::GameLauncher::error, this, &LauncherWindow::reportError);
    connect(launcher_, &prismatic::GameLauncher::started, this, [this] {
        updateControls();
        appendLog("Game started. Profile changes are locked until the game exits.");
        statusBar()->showMessage("Game running — profile changes locked.");
    });
    connect(launcher_, &prismatic::GameLauncher::finished, this, [this](int code) {
        updateControls();
        appendLog(QString("Game exited with code %1.").arg(code));
        statusBar()->showMessage("Game finished. Profiles can be changed.");
    });
    const bool bootstrap = !QFileInfo::exists(dataDirectory_ + "/instances.json") &&
                           !QFileInfo::exists(dataDirectory_ + "/instances.json.bak");
    QString warning;
    if (!store_->load(&warning)) {
        appendLog("Cannot load profiles: " + warning);
        statusBar()->showMessage("Profiles could not be loaded. See Logs.");
    } else {
        if (!warning.isEmpty()) appendLog("Recovery warning: " + warning);
        if (bootstrap && store_->instances().isEmpty()) {
            QString id;
            const auto bootstrapPath = config_.gameDirectory();
            if (store_->create("Stardew Valley",
                               QFileInfo(bootstrapPath).isDir() ? bootstrapPath : QString(), &id,
                               &warning)) {
                if (auto instance = store_->find(id)) {
                    instance->useSmapi = false;
                    if (!store_->update(*instance, &warning)) appendLog(warning);
                } else {
                    appendLog("Created profile could not be found in storage.");
                }
            } else {
                appendLog("Cannot create initial profile: " + warning);
            }
        }
        statusBar()->showMessage("Local launcher — isolated mod profiles, shared game files.");
    }
    reloadInstances();
    appendLog("Prismatic Launcher ready. Profiles share game files and saves.");
}

LauncherWindow::~LauncherWindow() {
    // Explicit destruction (including tests) must not outlive disposable profile fixtures.
    for (auto* watcher : findChildren<QFutureWatcherBase*>()) watcher->waitForFinished();
}

void LauncherWindow::closeEvent(QCloseEvent* event) {
    if (busy_ || launcher_->running()) {
        statusBar()->showMessage("Wait for the operation or game to finish before closing.");
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

QWidget* LauncherWindow::createLibrary() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 24);
    layout->setSpacing(16);
    auto* eyebrow = new QLabel("YOUR COLLECTION");
    eyebrow->setObjectName("eyebrow");
    layout->addWidget(eyebrow);
    auto* heading = new QLabel("Make the valley yours.");
    heading->setObjectName("heading");
    layout->addWidget(heading);
    auto* description =
        new QLabel("Independent mod profiles. Your game files and saves stay in place.");
    description->setObjectName("muted");
    layout->addWidget(description);
    auto* toolbar = new QHBoxLayout;
    auto* add = new QPushButton("+  Add instance");
    add->setObjectName("primary");
    auto* play = new QPushButton("▶  Play");
    auto* edit = new QPushButton("Edit instance");
    edit->setObjectName("editInstance");
    auto* rename = new QPushButton("Rename");
    rename->setObjectName("renameInstance");
    auto* duplicate = new QPushButton("Duplicate");
    duplicate->setObjectName("duplicateInstance");
    auto* remove = new QPushButton("Delete");
    remove->setObjectName("deleteInstance");
    for (auto* button : {add, play, edit, rename, duplicate, remove}) {
        button->setProperty("requiresIdle", true);
        toolbar->addWidget(button);
    }
    layout->addLayout(toolbar);
    auto* searchRow = new QHBoxLayout;
    instanceCount_ = new QLabel;
    instanceCount_->setObjectName("eyebrow");
    searchRow->addWidget(instanceCount_);
    searchRow->addStretch();
    search_ = new QLineEdit;
    search_->setObjectName("instanceSearch");
    search_->setPlaceholderText("Search instances…");
    search_->setAccessibleName("Search instances");
    search_->setMaximumWidth(280);
    searchRow->addWidget(search_);
    layout->addLayout(searchRow);
    instances_ = new QListWidget;
    instances_->setObjectName("instances");
    instances_->setAccessibleName("Game profiles. Double click to manage.");
    instances_->setViewMode(QListView::IconMode);
    instances_->setMovement(QListView::Static);
    instances_->setResizeMode(QListView::Adjust);
    instances_->setWordWrap(true);
    instances_->setTextElideMode(Qt::ElideNone);
    instances_->setIconSize(QSize(144, 144));
    instances_->setGridSize(QSize(208, 224));
    instances_->setSpacing(12);
    layout->addWidget(instances_, 1);
    connect(search_, &QLineEdit::textChanged, this, &LauncherWindow::filterInstances);
    connect(add, &QPushButton::clicked, this, &LauncherWindow::addInstance);
    connect(edit, &QPushButton::clicked, this, [this] { openInstance(); });
    connect(play, &QPushButton::clicked, this, &LauncherWindow::launchGame);
    connect(rename, &QPushButton::clicked, this, &LauncherWindow::renameInstance);
    connect(duplicate, &QPushButton::clicked, this, &LauncherWindow::duplicateInstance);
    connect(remove, &QPushButton::clicked, this, &LauncherWindow::removeInstance);
    connect(instances_, &QListWidget::itemActivated, this, [this] { openInstance(); });
    connect(instances_, &QListWidget::itemDoubleClicked, this, [this] { openInstance(); });
    connect(instances_, &QListWidget::currentItemChanged, this, [this] {
        if (refreshing_) return;
        if (const auto instance = selectedInstance()) {
            QString error;
            if (!store_->select(instance->id, &error)) appendLog(error);
            refreshPathStatus();
        }
    });
    return page;
}

std::optional<prismatic::Instance> LauncherWindow::selectedInstance() const {
    const auto* item = instances_->currentItem();
    return item && !item->isHidden() ? store_->find(item->data(Qt::UserRole + 2).toString())
                                     : std::nullopt;
}

void LauncherWindow::reloadInstances() {
    refreshing_ = true;
    instances_->clear();
    for (const auto& instance : store_->instances()) {
        auto* item = new QListWidgetItem(
            instanceIcon(), instance.name + (instance.useSmapi ? "\nModded" : "\nVanilla"),
            instances_);
        item->setSizeHint(QSize(192, 224));
        item->setData(Qt::UserRole, instance.name);
        item->setData(Qt::UserRole + 1, instance.useSmapi);
        item->setData(Qt::UserRole + 2, instance.id);
        if (instance.id == store_->selectedId()) instances_->setCurrentItem(item);
    }
    if (!instances_->currentItem() && instances_->count()) instances_->setCurrentRow(0);
    refreshing_ = false;
    filterInstances(search_->text());
    refreshPathStatus();
}

void LauncherWindow::filterInstances(const QString& query) {
    int visible = 0;
    for (int index = 0; index < instances_->count(); ++index) {
        auto* item = instances_->item(index);
        item->setHidden(!item->text().contains(query, Qt::CaseInsensitive));
        if (!item->isHidden()) ++visible;
    }
    instanceCount_->setText(visible ? QString("%1 INSTANCE(S)").arg(visible)
                                    : "NO MATCHING INSTANCES — ADD A PROFILE TO GET STARTED");
}

bool LauncherWindow::canChange() {
    if (busy_ || launcher_->running()) {
        statusBar()->showMessage(busy_ ? "Wait for the current operation to finish."
                                       : "Close the game before changing profiles or mods.");
        return false;
    }
    return true;
}

void LauncherWindow::updateControls() {
    const bool idle = !busy_ && !launcher_->running();
    instances_->setEnabled(idle);
    for (auto* button : findChildren<QPushButton*>()) {
        if (button->property("requiresIdle").toBool()) button->setEnabled(idle);
    }
    gamePath_->setEnabled(idle);
    launchMode_->setEnabled(idle);
    arguments_->setEnabled(idle);
}

void LauncherWindow::addInstance() {
    if (!canChange()) return;
    QDialog dialog(this);
    dialog.setWindowTitle("Add instance");
    dialog.setMinimumWidth(420);
    QFormLayout form(&dialog);
    QLineEdit name("My Stardew Valley");
    name.setMaxLength(64);
    QComboBox mode;
    mode.addItems({"Vanilla", "Modded (requires installed SMAPI)"});
    QLabel note(
        "Creates a persistent mod profile, sharing game files and saves.\n"
        "Each modded profile uses its own Mods folder; SMAPI must already be installed.");
    note.setWordWrap(true);
    form.addRow("Name", &name);
    form.addRow("Launch mode", &mode);
    form.addRow(&note);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons.button(QDialogButtonBox::Ok)->setText("Create instance");
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&name, &QLineEdit::textChanged, &dialog, [&buttons](const QString& value) {
        buttons.button(QDialogButtonBox::Ok)->setEnabled(!value.trimmed().isEmpty());
    });
    if (dialog.exec() != QDialog::Accepted) return;
    const auto selected = selectedInstance();
    QString id, error;
    if (!store_->create(name.text().trimmed(), selected ? selected->gamePath : QString(), &id,
                        &error)) {
        reportError(error);
        return;
    }
    auto instance = store_->find(id);
    if (!instance) return reportError("Created profile could not be found in storage.");
    instance->useSmapi = mode.currentIndex() == 1;
    if (!store_->update(*instance, &error)) reportError(error);
    if (!store_->select(id, &error)) reportError(error);
    search_->clear();
    reloadInstances();
    appendLog("Created profile: " + instance->name);
    openInstance();
}

void LauncherWindow::renameInstance() {
    if (!canChange()) return;
    auto instance = selectedInstance();
    if (!instance) return;
    bool accepted = false;
    const auto name = QInputDialog::getText(this, "Rename instance", "Profile name",
                                            QLineEdit::Normal, instance->name, &accepted)
                          .trimmed();
    if (!accepted) return;
    instance->name = name;
    QString error;
    if (!store_->update(*instance, &error)) return reportError(error);
    reloadInstances();
    appendLog("Renamed profile: " + name);
}

void LauncherWindow::duplicateInstance() {
    if (!canChange()) return;
    const auto instance = selectedInstance();
    if (!instance) return;
    bool accepted = false;
    const auto name = QInputDialog::getText(this, "Duplicate instance", "New profile name",
                                            QLineEdit::Normal, instance->name + " copy", &accepted)
                          .trimmed();
    if (!accepted || name.isEmpty()) return;
    const auto root = dataDirectory_;
    const auto id = instance->id;
    runTask(
        "Duplicating profile…",
        [root, id, name] {
            prismatic::InstanceStore worker(root);
            QString error, createdId;
            if (!worker.load(&error)) return error;
            if (!worker.duplicate(id, name, &createdId, &error)) return error;
            if (!worker.select(createdId, &error)) return error;
            return QString();
        },
        [this] {
            QString error;
            if (!store_->load(&error)) return reportError(error);
            if (!error.isEmpty()) appendLog(error);
            reloadInstances();
        });
}

void LauncherWindow::removeInstance() {
    if (!canChange()) return;
    const auto instance = selectedInstance();
    if (!instance) return;
    QMessageBox confirmation(
        QMessageBox::Question, "Delete instance?",
        "Delete profile “" + instance->name +
            "”?\nIts mods and configuration move to the launcher recovery trash. "
            "Game files and saves are preserved.",
        QMessageBox::Yes | QMessageBox::Cancel, this);
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setDefaultButton(QMessageBox::Cancel);
    if (confirmation.exec() != QMessageBox::Yes) return;
    const auto root = dataDirectory_;
    const auto id = instance->id;
    runTask(
        "Removing profile…",
        [root, id] {
            prismatic::InstanceStore worker(root);
            QString error;
            if (!worker.load(&error) || !worker.remove(id, &error)) return error;
            return QString();
        },
        [this] {
            QString error;
            if (!store_->load(&error)) return reportError(error);
            reloadInstances();
            if (store_->instances().isEmpty()) pages_->setCurrentIndex(0);
        });
}

void LauncherWindow::openInstance(int tab) {
    const auto instance = selectedInstance();
    if (!instance) {
        if (tab == 1) {
            detailTitle_->setText("Launcher activity");
            navigation_->setCurrentRow(1);
            pages_->setCurrentIndex(1);
        } else {
            statusBar()->showMessage("Create or select an instance first.");
        }
        return;
    }
    detailTitle_->setText(instance->name);
    refreshPathStatus();
    navigation_->setCurrentRow(tab);
    pages_->setCurrentIndex(1);
    refreshMods();
}

void LauncherWindow::refreshPathStatus() {
    const auto instance = selectedInstance();
    if (!instance) {
        gamePath_->clear();
        pathStatus_->setText("Create or select a profile to configure the game.");
        return;
    }
    detailTitle_->setText(instance->name);
    gamePath_->setText(instance->gamePath);
    launchMode_->setCurrentIndex(instance->useSmapi ? 1 : 0);
    // One argument per line preserves spaces and never invokes a shell.
    arguments_->setPlainText(instance->launchArguments.join("\n"));
    const auto installation = prismatic::GameLauncher::inspect(instance->gamePath);
    pathStatus_->setText(
        installation.diagnostics.join("\n") +
        (installation.hasSmapi() ? "\nSMAPI detected." : "\nSMAPI is not installed here."));
}

void LauncherWindow::launchGame() {
    if (!canChange()) return;
    const auto instance = selectedInstance();
    if (!instance || instances_->currentItem()->isHidden()) {
        statusBar()->showMessage("Select an instance first.");
        return;
    }
    prismatic::LaunchRequest request;
    request.installation = prismatic::GameLauncher::inspect(instance->gamePath);
    request.useSmapi = instance->useSmapi;
    request.modsDirectory = store_->modsPath(instance->id);
    request.arguments = instance->launchArguments;
    QString error;
    if (!prismatic::GameLauncher::prepareLaunch(request, &error)) {
        reportError("Executable not found or not runnable.\n" + error);
        return;
    }
    appendLog("Launching profile: " + instance->name);
    launcher_->start(request);
}

void LauncherWindow::runTask(const QString& description, std::function<QString()> operation,
                             std::function<void()> complete) {
    if (!canChange()) return;
    busy_ = true;
    progress_->show();
    statusBar()->showMessage(description);
    appendLog(description);
    // Disable all profile controls, retaining navigation and logs on the main thread.
    updateControls();
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
            [this, watcher, description, complete] {
                const auto error = watcher->result();
                watcher->deleteLater();
                busy_ = false;
                progress_->hide();
                updateControls();
                if (!error.isEmpty())
                    reportError(error);
                else {
                    appendLog(description + " Done.");
                    statusBar()->showMessage("Operation complete.");
                }
                if (complete) complete();
                refreshMods();
            });
    watcher->setFuture(QtConcurrent::run([operation = std::move(operation)] {
        try {
            return operation();
        } catch (const std::exception& error) {
            return QString::fromUtf8(error.what());
        } catch (...) {
            return QString("Operation failed unexpectedly.");
        }
    }));
}

void LauncherWindow::reportError(const QString& message) {
    appendLog("Error: " + message);
    QMessageBox box(QMessageBox::Warning, "Prismatic Launcher", message, QMessageBox::Ok, this);
    box.setTextFormat(Qt::PlainText);
    box.exec();
}

QString LauncherWindow::redacted(const QString& text) const {
    auto result = text;
    for (const auto& instance : store_->instances()) {
        if (!instance.gamePath.isEmpty()) result.replace(instance.gamePath, "<game>");
    }
    result.replace(dataDirectory_, "<launcher-data>");
    const auto home = QDir::homePath();
    if (home.size() > 1) result.replace(home, "<home>");
    return result;
}

void LauncherWindow::appendLog(const QString& message) {
    const auto prefix = QDateTime::currentDateTime().toString("HH:mm:ss ");
    for (const auto& line : message.left(65536).split('\n')) {
        logLines_.append(prefix + redacted(line));
    }
    while (logLines_.size() > 2000) logLines_.removeFirst();
    renderLogs();
}

void LauncherWindow::renderLogs() {
    QStringList visible;
    const auto query = logFilter_->text();
    for (const auto& line : logLines_) {
        if (line.contains(query, Qt::CaseInsensitive)) visible.append(line);
    }
    logs_->setPlainText(visible.join('\n'));
    auto cursor = logs_->textCursor();
    cursor.movePosition(QTextCursor::End);
    logs_->setTextCursor(cursor);
}
