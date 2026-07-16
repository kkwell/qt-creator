// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QTableView;
QT_END_NAMESPACE

namespace Utils {
class InfoLabel;
}

namespace EtherCAT::Workbench::Internal {

class PdoAssignmentTableModel;
class PdoContentTableModel;
class PdoListTableModel;
class ProcessImageTableModel;
class SyncManagerTableModel;
class WorkbenchController;

class ProcessDataPage final : public QWidget
{
    Q_OBJECT

public:
    explicit ProcessDataPage(WorkbenchController *controller, QWidget *parent = nullptr);

    void setContext(const Core::PropertyPageContext &context);

private:
    friend class PdoAssignmentTableModel;
    friend class PdoContentTableModel;

    bool submitConfiguration(const Data::ProcessDataConfiguration &configuration);
    void rebuildModels();
    void rebuildPdoModels();
    void selectPdo(const Data::NodeId &pdoId);
    void showValidation(const Data::ConfigurationValidation &validation, const QString &prefix = {});

    WorkbenchController *m_controller = nullptr;
    Core::PropertyPageContext m_context;
    Data::ProcessDataConfiguration m_configuration;
    Data::ProcessDataConfiguration m_esiDefaults;
    Data::NodeId m_selectedSyncManagerId;
    Data::NodeId m_selectedPdoId;
    bool m_editable = false;
    bool m_showingEsiDefaults = false;
    bool m_rebuilding = false;

    QLabel *m_summary = nullptr;
    Utils::InfoLabel *m_validation = nullptr;
    QPushButton *m_restoreDefaults = nullptr;
    QTableView *m_syncManagers = nullptr;
    QTableView *m_assignments = nullptr;
    QTableView *m_pdoList = nullptr;
    QTableView *m_pdoContent = nullptr;
    QTableView *m_processImage = nullptr;
    SyncManagerTableModel *m_syncManagerModel = nullptr;
    PdoAssignmentTableModel *m_assignmentModel = nullptr;
    PdoListTableModel *m_pdoListModel = nullptr;
    PdoContentTableModel *m_pdoContentModel = nullptr;
    ProcessImageTableModel *m_processImageModel = nullptr;
};

} // namespace EtherCAT::Workbench::Internal
