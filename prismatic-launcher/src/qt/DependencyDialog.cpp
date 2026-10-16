#include "DependencyDialog.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <exception>

namespace {
struct Inspection {
    prismatic::DependencyResolution resolution;
    prismatic::ModSourceCatalog catalog;
    QString error;
};
}  // namespace

DependencyDialog::DependencyDialog(QString dataRoot, QString profileRoot, QWidget* parent)
    : QDialog(parent), dataRoot_(std::move(dataRoot)), profileRoot_(std::move(profileRoot)) {
    setWindowTitle("Profile dependencies");
    setWindowModality(Qt::ApplicationModal);
    resize(760, 580);
    auto* layout = new QVBoxLayout(this);
    auto* note = new QLabel(
        "Dependencies are checked only in this profile. Select eligible missing "
        "dependencies to download. Existing installations are never replaced.");
    note->setWordWrap(true);
    layout->addWidget(note);
    summary_ = new QPlainTextEdit;
    summary_->setObjectName("dependencySummary");
    summary_->setAccessibleName("Dependency diagnostics");
    summary_->setReadOnly(true);
    layout->addWidget(summary_, 2);
    requests_ = new QListWidget;
    requests_->setObjectName("dependencyRequests");
    requests_->setAccessibleName("Missing dependencies eligible for installation");
    layout->addWidget(requests_, 1);
    auto* actions = new QHBoxLayout;
    resolve_ = new QPushButton("Download && install selected…");
    resolve_->setObjectName("resolveDependencies");
    manual_ = new QPushButton("Open official page");
    manual_->setObjectName("manualDependencyPage");
    close_ = new QPushButton("Close");
    close_->setObjectName("closeDependencies");
    actions->addWidget(resolve_);
    actions->addWidget(manual_);
    actions->addStretch();
    actions->addWidget(close_);
    layout->addLayout(actions);
    connect(close_, &QPushButton::clicked, this, &QDialog::reject);
    connect(resolve_, &QPushButton::clicked, this, &DependencyDialog::resolveSelected);
    connect(requests_, &QListWidget::currentRowChanged, this, [this] {
        const auto* item = requests_->currentItem();
        const auto source =
            item ? catalog_.find(item->data(Qt::UserRole).toString()) : std::nullopt;
        manual_->setEnabled(!busy_ && source && source->officialPage.isValid());
    });
    connect(manual_, &QPushButton::clicked, this, [this] {
        const auto* item = requests_->currentItem();
        if (busy_ || !item) return;
        const auto source = catalog_.find(item->data(Qt::UserRole).toString());
        if (source && source->officialPage.scheme() == "https")
            QDesktopServices::openUrl(source->officialPage);
    });
    refresh();
}

DependencyDialog::~DependencyDialog() {
    for (auto* watcher : findChildren<QFutureWatcherBase*>()) watcher->waitForFinished();
}
void DependencyDialog::closeEvent(QCloseEvent* event) {
    if (busy_)
        event->ignore();
    else
        QDialog::closeEvent(event);
}
void DependencyDialog::reject() {
    if (!busy_) QDialog::reject();
}
void DependencyDialog::setBusy(bool busy) {
    busy_ = busy;
    resolve_->setEnabled(!busy && !resolution_.requests.isEmpty());
    requests_->setEnabled(!busy);
    close_->setEnabled(!busy);
    const auto* item = requests_->currentItem();
    const auto source = item ? catalog_.find(item->data(Qt::UserRole).toString()) : std::nullopt;
    manual_->setEnabled(!busy && source && source->officialPage.isValid());
    emit busyChanged(busy);
}
void DependencyDialog::refresh() {
    setBusy(true);
    summary_->setPlainText(lastResult_ + "\nReading profile manifests…");
    const auto profile = profileRoot_;
    const auto data = dataRoot_;
    auto* watcher = new QFutureWatcher<Inspection>(this);
    connect(watcher, &QFutureWatcher<Inspection>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        resolution_ = result.resolution;
        catalog_ = result.catalog;
        QStringList text;
        if (!lastResult_.isEmpty()) text << lastResult_;
        if (!result.error.isEmpty()) text << result.error;
        text << resolution_.summary();
        if (resolution_.diagnostics.isEmpty() && result.error.isEmpty())
            text << "No dependency issues detected.";
        if (!result.error.isEmpty())
            text << "Dependency inspection is incomplete; review the errors above.";
        requests_->clear();
        auto displayed = resolution_.requests;
        QSet<QString> missingIds;
        QSet<QString> displayedIds;
        for (const auto& request : displayed) {
            missingIds.insert(request.uniqueId.toCaseFolded());
            displayedIds.insert(request.uniqueId.toCaseFolded());
        }
        for (const auto& diagnostic : resolution_.diagnostics) {
            const auto id = diagnostic.dependencyId.toCaseFolded();
            if (id.isEmpty() || displayedIds.contains(id) || !catalog_.find(id) ||
                diagnostic.status == prismatic::DependencyStatus::Satisfied)
                continue;
            displayed.append(
                {diagnostic.dependencyId, diagnostic.minimumVersion, {diagnostic.modId}});
            displayedIds.insert(id);
        }
        for (const auto& request : displayed) {
            const auto source = catalog_.find(request.uniqueId);
            const bool eligible =
                source && source->automatic && missingIds.contains(request.uniqueId.toCaseFolded());
            auto* item = new QListWidgetItem(
                request.uniqueId +
                    (request.minimumVersion.isEmpty() ? QString()
                                                      : " >= " + request.minimumVersion) +
                    (eligible ? " — verified GitHub source" : " — manage manually"),
                requests_);
            item->setData(Qt::UserRole, request.uniqueId);
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            item->setToolTip("Required by: " + request.requiredBy.join(", "));
            if (eligible) {
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Unchecked);
                text << request.uniqueId + ": " + source->publisher + "\n" +
                            source->officialPage.toString();
            } else if (source) {
                text << request.uniqueId + ": manual download\n" + source->officialPage.toString();
            } else {
                text << request.uniqueId +
                            ": no verified source in the local catalog. "
                            "Obtain this dependency from its publisher and install the ZIP into "
                            "this profile.";
            }
        }
        summary_->setPlainText(text.join("\n\n"));
        setBusy(false);
    });
    watcher->setFuture(QtConcurrent::run([profile, data] {
        Inspection result;
        QString scanError;
        const auto mods = prismatic::ModManager(profile).mods(&scanError);
        result.error = scanError;
        result.resolution = prismatic::DependencyResolver::resolve(mods);
        result.catalog = prismatic::ModSourceCatalog::bundled();
        if (QFileInfo::exists(data + "/sources.json")) {
            QString catalogError;
            const auto local =
                prismatic::ModSourceCatalog::load(data + "/sources.json", &catalogError);
            if (local)
                result.catalog = *local;
            else
                result.error += "\nLocal source catalog rejected: " + catalogError;
        }
        return result;
    }));
}
void DependencyDialog::resolveSelected() {
    if (busy_) return;
    QList<QPair<prismatic::DependencyRequest, prismatic::ModSource>> selected;
    QStringList confirmation;
    for (int row = 0; row < requests_->count(); ++row) {
        const auto* item = requests_->item(row);
        if (item->checkState() != Qt::Checked) continue;
        const auto id = item->data(Qt::UserRole).toString();
        const auto source = catalog_.find(id);
        if (!source || !source->automatic) continue;
        for (const auto& request : resolution_.requests) {
            if (request.uniqueId.compare(id, Qt::CaseInsensitive) != 0) continue;
            selected.append({request, *source});
            confirmation << id + " — " + source->publisher + "\nRepository: " + source->repository +
                                "\nPublisher evidence: " + source->identityEvidence.toString();
        }
    }
    if (selected.isEmpty()) return;
    QMessageBox approval(QMessageBox::Question, "Authorize dependency downloads?",
                         "Download and install these dependencies into this profile?\n"
                         "Mod assemblies can execute code when you run SMAPI.\n\n" +
                             confirmation.join("\n\n"),
                         QMessageBox::Yes | QMessageBox::Cancel, this);
    approval.setTextFormat(Qt::PlainText);
    approval.setDefaultButton(QMessageBox::Cancel);
    if (approval.exec() != QMessageBox::Yes) return;
    setBusy(true);
    summary_->appendPlainText("\nDownloading approved dependencies…");
    const auto profile = profileRoot_;
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        lastResult_ = watcher->result();
        watcher->deleteLater();
        emit installationsChanged();
        refresh();
    });
    watcher->setFuture(QtConcurrent::run([selected, profile] {
        QStringList results;
        for (const auto& selection : selected) {
            QString error;
            try {
                prismatic::GitHubReleaseProvider provider;
                const auto asset =
                    provider.resolve(selection.second, selection.first.minimumVersion, &error);
                const bool installed =
                    asset && provider.install(selection.second, *asset, profile, true, &error);
                results << selection.first.uniqueId + (installed ? ": installed." : ": " + error);
            } catch (const std::exception& exception) {
                results << selection.first.uniqueId + ": " + QString::fromUtf8(exception.what());
            }
        }
        return results.join('\n');
    }));
}
