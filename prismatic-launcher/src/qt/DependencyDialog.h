#pragma once

#include <QDialog>
#include <QString>

#include "DependencyResolver.h"
#include "ModSource.h"

class QCloseEvent;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

// Inspection is offline. Network work begins only after a checked selection and
// explicit confirmation naming the publisher and repository.
class DependencyDialog : public QDialog {
    Q_OBJECT
   public:
    DependencyDialog(QString dataRoot, QString profileRoot, QWidget* parent = nullptr);
    ~DependencyDialog() override;
   signals:
    void installationsChanged();
    void busyChanged(bool busy);

   protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

   private:
    void refresh();
    void resolveSelected();
    void setBusy(bool busy);
    QString dataRoot_;
    QString profileRoot_;
    QString lastResult_;
    bool busy_ = false;
    prismatic::DependencyResolution resolution_;
    prismatic::ModSourceCatalog catalog_;
    QPlainTextEdit* summary_ = nullptr;
    QListWidget* requests_ = nullptr;
    QPushButton* resolve_ = nullptr;
    QPushButton* manual_ = nullptr;
    QPushButton* close_ = nullptr;
};
