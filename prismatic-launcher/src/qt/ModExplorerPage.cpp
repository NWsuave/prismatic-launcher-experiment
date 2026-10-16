#include "ModExplorerPage.h"

#include <QComboBox>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <utility>

namespace {
QTableWidget* table(const QStringList& labels, const QString& name, QWidget* parent) {
    auto* result = new QTableWidget(0, static_cast<int>(labels.size()), parent);
    result->setObjectName(name);
    result->setHorizontalHeaderLabels(labels);
    result->setSelectionBehavior(QAbstractItemView::SelectRows);
    result->setSelectionMode(QAbstractItemView::SingleSelection);
    result->setEditTriggers(QAbstractItemView::NoEditTriggers);
    result->verticalHeader()->hide();
    result->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    result->horizontalHeader()->setStretchLastSection(true);
    return result;
}
void fillRow(QTableWidget* target, int row, const QStringList& values) {
    for (int column = 0; column < values.size(); ++column)
        target->setItem(row, column, new QTableWidgetItem(values.at(column)));
}
}  // namespace

ModExplorerPage::ModExplorerPage(QString dataRoot, QWidget* parent, bool autoRefresh)
    : QWidget(parent), dataRoot_(std::move(dataRoot)), initialRefreshPending_(autoRefresh) {
    setObjectName("modExplorerPage");
    auto* layout = new QVBoxLayout(this);
    auto* title = new QLabel(tr("Installed Mods"), this);
    title->setObjectName("pageTitle");
    layout->addWidget(title);
    auto* explanation = new QLabel(
        tr("All local profile installations. Each profile keeps its own mod files."), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* toolbar = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setObjectName("inventorySearch");
    search_->setPlaceholderText(tr("Search name, author, ID, version, or profile"));
    search_->setAccessibleName(tr("Search installed mods"));
    filter_ = new QComboBox(this);
    filter_->setObjectName("inventoryFilter");
    filter_->setAccessibleName(tr("Filter installation state"));
    filter_->addItems({tr("All installations"), tr("Enabled"), tr("Disabled")});
    refresh_ = new QPushButton(tr("Refresh"), this);
    refresh_->setObjectName("inventoryRefresh");
    toolbar->addWidget(search_, 1);
    toolbar->addWidget(filter_);
    toolbar->addWidget(refresh_);
    layout->addLayout(toolbar);
    auto* splitter = new QSplitter(Qt::Vertical, this);
    mods_ = table({tr("Mod"), tr("Author"), tr("Versions"), tr("Profiles")}, "inventoryMods", this);
    mods_->setAccessibleName(tr("Mods across all profiles"));
    splitter->addWidget(mods_);
    auto* detailPane = new QWidget(this);
    auto* detailLayout = new QVBoxLayout(detailPane);
    details_ = new QPlainTextEdit(detailPane);
    details_->setObjectName("inventoryDetails");
    details_->setAccessibleName(tr("Mod metadata and dependencies"));
    details_->setReadOnly(true);
    details_->setMaximumHeight(160);
    detailLayout->addWidget(details_);
    installations_ =
        table({tr("Profile"), tr("Version"), tr("State")}, "inventoryInstallations", detailPane);
    installations_->setAccessibleName(tr("Selected mod profile installations"));
    detailLayout->addWidget(installations_);
    auto* actions = new QHBoxLayout;
    toggle_ = new QPushButton(tr("Enable / Disable"), this);
    toggle_->setObjectName("inventoryToggle");
    remove_ = new QPushButton(tr("Remove from profile"), this);
    remove_->setObjectName("inventoryRemove");
    dependencies_ = new QPushButton(tr("Check dependencies"), this);
    dependencies_->setObjectName("inventoryDependencies");
    actions->addWidget(toggle_);
    actions->addWidget(remove_);
    actions->addWidget(dependencies_);
    actions->addStretch();
    detailLayout->addLayout(actions);
    splitter->addWidget(detailPane);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter, 1);
    status_ = new QLabel(this);
    status_->setObjectName("inventoryStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    watcher_ = new QFileSystemWatcher(this);
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(150);
    const auto schedule = [this] { refreshTimer_->start(); };
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, schedule);
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, schedule);
    connect(refreshTimer_, &QTimer::timeout, this, &ModExplorerPage::refresh);
    connect(search_, &QLineEdit::textChanged, this, &ModExplorerPage::renderMods);
    connect(filter_, &QComboBox::currentIndexChanged, this, &ModExplorerPage::renderMods);
    connect(refresh_, &QPushButton::clicked, this, &ModExplorerPage::refresh);
    connect(mods_, &QTableWidget::itemSelectionChanged, this,
            &ModExplorerPage::renderInstallations);
    connect(installations_, &QTableWidget::itemSelectionChanged, this,
            &ModExplorerPage::renderDetails);
    connect(toggle_, &QPushButton::clicked, this, [this] { changeSelected(false); });
    connect(remove_, &QPushButton::clicked, this, [this] { changeSelected(true); });
    connect(dependencies_, &QPushButton::clicked, this, [this] {
        const auto selected = selectedInstallation();
        if (selected)
            emit dependencyInspectionRequested(selected->profileId,
                                               selected->mod.manifest.uniqueId);
    });
    updateActions();
    // Let the containing window finish bootstrapping its profile registry before
    // a worker takes the same registry lock.
    if (autoRefresh) {
        QTimer::singleShot(0, this, [this] {
            if (initialRefreshPending_) refresh();
        });
    }
}

ModExplorerPage::~ModExplorerPage() {
    // Workers only capture values. Await them before allowing parent data teardown.
    for (auto* watcher : findChildren<QFutureWatcher<prismatic::ModInventorySnapshot>*>())
        watcher->waitForFinished();
    for (auto* watcher : findChildren<QFutureWatcher<QString>*>()) watcher->waitForFinished();
}

void ModExplorerPage::setMutationAllowed(bool allowed) {
    mutationAllowed_ = allowed;
    updateActions();
    if (allowed && refreshPending_ && !busy_) refreshTimer_->start();
}
void ModExplorerPage::setEnableCheck(std::function<bool(const QString&, const QString&)> check) {
    enableCheck_ = std::move(check);
}
bool ModExplorerPage::isBusy() const { return busy_ || initialRefreshPending_; }
void ModExplorerPage::setBusy(bool busy) {
    busy_ = busy;
    updateActions();
    emit busyChanged(busy);
}

void ModExplorerPage::refresh() {
    initialRefreshPending_ = false;
    if (busy_ || !mutationAllowed_) {
        refreshPending_ = true;
        return;
    }
    refreshPending_ = false;
    setBusy(true);
    status_->setText(tr("Reading installed mods across profiles…"));
    auto* watcher = new QFutureWatcher<prismatic::ModInventorySnapshot>(this);
    connect(watcher, &QFutureWatcher<prismatic::ModInventorySnapshot>::finished, this,
            [this, watcher] {
                snapshot_ = watcher->result();
                watcher->deleteLater();
                renderMods();
                watchDirectories();
                status_->setText(
                    tr("%1 unique mods across local profiles.").arg(snapshot_.mods.size()) +
                    (snapshot_.warnings.isEmpty() ? QString()
                                                  : tr("\nSome profiles could not be read:\n%1")
                                                        .arg(snapshot_.warnings.join('\n'))));
                setBusy(false);
                if (refreshPending_) refreshTimer_->start();
            });
    const QString root = dataRoot_;
    watcher->setFuture(QtConcurrent::run([root] { return prismatic::ModInventory::scan(root); }));
}

void ModExplorerPage::renderMods() {
    QString selectedId;
    if (mods_->currentRow() >= 0 && mods_->currentRow() < visible_.size())
        selectedId = visible_.at(mods_->currentRow()).uniqueId;
    visible_ = prismatic::ModInventory::filtered(
        snapshot_.mods, search_->text(),
        static_cast<prismatic::InstallationFilter>(filter_->currentIndex()));
    const QSignalBlocker blocker(mods_);
    mods_->setRowCount(static_cast<int>(visible_.size()));
    int selectedRow = 0;
    for (int row = 0; row < visible_.size(); ++row) {
        const auto& mod = visible_.at(row);
        const auto& manifest = mod.installations.first().mod.manifest;
        QStringList profiles;
        for (const auto& installation : mod.installations)
            profiles.append(installation.profileName);
        profiles.removeDuplicates();
        fillRow(mods_, row,
                {manifest.name, manifest.author, mod.versions().join(", "), profiles.join(", ")});
        mods_->item(row, 0)->setToolTip(mod.uniqueId);
        if (mod.uniqueId == selectedId) selectedRow = row;
    }
    if (!visible_.isEmpty()) mods_->selectRow(selectedRow);
    renderInstallations();
}

void ModExplorerPage::renderInstallations() {
    const auto previous = selectedInstallation();
    const QString profileId = previous ? previous->profileId : QString();
    const QSignalBlocker blocker(installations_);
    installations_->setRowCount(0);
    const int row = mods_->currentRow();
    if (row >= 0 && row < visible_.size()) {
        const auto& entries = visible_.at(row).installations;
        installations_->setRowCount(static_cast<int>(entries.size()));
        int selectedRow = 0;
        for (int index = 0; index < entries.size(); ++index) {
            const auto& entry = entries.at(index);
            fillRow(installations_, index,
                    {entry.profileName, entry.mod.manifest.version,
                     entry.mod.enabled ? tr("Enabled") : tr("Disabled")});
            if (entry.profileId == profileId) selectedRow = index;
        }
        if (!entries.isEmpty()) installations_->selectRow(selectedRow);
    }
    renderDetails();
}

std::optional<prismatic::ModInstallation> ModExplorerPage::selectedInstallation() const {
    const int modRow = mods_->currentRow();
    const int row = installations_->currentRow();
    if (modRow < 0 || modRow >= visible_.size() || row < 0 ||
        row >= visible_.at(modRow).installations.size())
        return std::nullopt;
    return visible_.at(modRow).installations.at(row);
}

void ModExplorerPage::renderDetails() {
    const auto selected = selectedInstallation();
    if (!selected) {
        details_->setPlainText(visible_.isEmpty()
                                   ? tr("No installed mods match this view.")
                                   : tr("Select a profile installation to inspect its metadata."));
        updateActions();
        return;
    }
    const auto& manifest = selected->mod.manifest;
    QStringList lines{
        manifest.name + " — " + manifest.version, tr("Unique ID: %1").arg(manifest.uniqueId),
        tr("Author: %1").arg(manifest.author), manifest.description, tr("Dependencies:")};
    for (const auto& dependency : manifest.dependencies)
        lines.append(tr("%1%2 (%3)")
                         .arg(dependency.uniqueId,
                              dependency.minimumVersion.isEmpty()
                                  ? QString()
                                  : tr(" >= %1").arg(dependency.minimumVersion),
                              dependency.isRequired ? tr("required") : tr("optional")));
    if (manifest.contentPackFor)
        lines.append(tr("Content pack framework: %1%2")
                         .arg(manifest.contentPackFor->uniqueId,
                              manifest.contentPackFor->minimumVersion.isEmpty()
                                  ? QString()
                                  : tr(" >= %1").arg(manifest.contentPackFor->minimumVersion)));
    if (manifest.dependencies.isEmpty() && !manifest.contentPackFor)
        lines.append(tr("None declared."));
    if (!manifest.updateKeys.isEmpty())
        lines.append(tr("Update keys: %1").arg(manifest.updateKeys.join(", ")));
    details_->setPlainText(lines.join('\n'));
    updateActions();
}

void ModExplorerPage::updateActions() {
    const auto selected = selectedInstallation();
    const bool allowed = selected.has_value() && !busy_ && mutationAllowed_;
    toggle_->setEnabled(allowed);
    remove_->setEnabled(allowed);
    dependencies_->setEnabled(selected.has_value() && !busy_ && mutationAllowed_);
    toggle_->setText(selected && selected->mod.enabled ? tr("Disable in profile")
                                                       : tr("Enable in profile"));
    refresh_->setEnabled(!busy_);
    mods_->setEnabled(!busy_);
    installations_->setEnabled(!busy_);
}

void ModExplorerPage::changeSelected(bool remove) {
    const auto selected = selectedInstallation();
    if (!selected || busy_ || !mutationAllowed_) return;
    if (remove) {
        QMessageBox confirmation(QMessageBox::Question, tr("Remove profile installation"),
                                 tr("Remove %1 from %2? Its files will be retained in that "
                                    "profile's RemovedMods directory.")
                                     .arg(selected->mod.manifest.name, selected->profileName),
                                 QMessageBox::Yes | QMessageBox::No, this);
        confirmation.setTextFormat(Qt::PlainText);
        confirmation.setDefaultButton(QMessageBox::No);
        if (confirmation.exec() != QMessageBox::Yes) return;
    }
    if (!remove && !selected->mod.enabled && enableCheck_ &&
        !enableCheck_(selected->profileId, selected->mod.manifest.uniqueId))
        return;
    // A modal callback may have changed launcher state.
    if (busy_ || !mutationAllowed_) return;
    setBusy(true);
    status_->setText(remove ? tr("Removing selected profile installation…")
                            : tr("Updating selected profile installation…"));
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const QString error = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (!error.isEmpty())
            emit errorOccurred(error);
        else
            emit installationsChanged();
        refresh();
    });
    const auto installation = *selected;
    watcher->setFuture(QtConcurrent::run([installation, remove] {
        QString error;
        prismatic::ModManager manager(installation.profileRoot);
        if (remove)
            manager.remove(installation.mod.manifest.uniqueId, &error);
        else
            manager.setEnabled(installation.mod.manifest.uniqueId, !installation.mod.enabled,
                               &error);
        return error;
    }));
}

void ModExplorerPage::watchDirectories() {
    const auto oldDirectories = watcher_->directories();
    if (!oldDirectories.isEmpty()) watcher_->removePaths(oldDirectories);
    const auto oldFiles = watcher_->files();
    if (!oldFiles.isEmpty()) watcher_->removePaths(oldFiles);
    if (!snapshot_.watchedPaths.isEmpty()) watcher_->addPaths(snapshot_.watchedPaths);
}
