// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <utils/filepath.h>

#include <QPointer>
#include <QWidget>

#include <functional>

QT_BEGIN_NAMESPACE
class QDragEnterEvent;
class QDropEvent;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
QT_END_NAMESPACE

namespace EtherCAT::Workbench::Internal {

class EsiRepositoryPage final : public QWidget
{
public:
    using FilePicker = std::function<Utils::FilePaths()>;

    explicit EsiRepositoryPage(
        Core::DeviceRepositoryProvider *repository,
        QWidget *parent = nullptr,
        FilePicker filePicker = {});

    void refresh();

protected:
    void dragEnterEvent(QDragEnterEvent *event) final;
    void dropEvent(QDropEvent *event) final;

private:
    void chooseFiles();
    void importFiles(const Utils::FilePaths &filePaths);
    void reloadDescriptions();
    void startJob(Core::DeviceImportJob *job, const QString &operationName);
    void cancelOperation();
    void updateProgress(int value, int maximum);
    void finishOperation(const Data::DeviceImportResult &result);
    void showStartFailure(const QString &message);
    bool operationActive() const;

    QPointer<Core::DeviceRepositoryProvider> m_repository;
    QPointer<Core::DeviceImportJob> m_job;
    FilePicker m_filePicker;
    QString m_operationName;
    bool m_operationActive = false;
    QLabel *m_title;
    QLabel *m_description;
    QGroupBox *m_summary;
    QLineEdit *m_status;
    QLineEdit *m_deviceCount;
    QLineEdit *m_supportedCount;
    QLineEdit *m_limitedCount;
    QLineEdit *m_vendorCount;
    QLineEdit *m_sourceCount;
    QPushButton *m_importFiles;
    QPushButton *m_reload;
    QPushButton *m_cancel;
    QProgressBar *m_progress;
    QLabel *m_operation;
    QPlainTextEdit *m_result;
};

} // namespace EtherCAT::Workbench::Internal
