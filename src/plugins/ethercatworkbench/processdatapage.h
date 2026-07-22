// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>
#include <QWidget>

#include <optional>

QT_BEGIN_NAMESPACE
class QHideEvent;
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
    ~ProcessDataPage() final;

    void setContext(const Core::PropertyPageContext &context);

protected:
    void hideEvent(QHideEvent *event) final;

private:
    friend class PdoAssignmentTableModel;
    friend class PdoContentTableModel;

    bool submitConfiguration(
        const Data::ProcessDataConfiguration &configuration,
        const Data::NodeId &inlineEntryId = {},
        int inlineColumn = -1);
    void rebuildModels(
        bool preserveInlineEditor = false, bool notifyPreservedColumn = false);
    void rebuildPdoModels(
        bool preserveInlineEditor = false, bool notifyPreservedColumn = false);
    void selectPdo(const Data::NodeId &pdoId);
    void trackInlineEditor(
        QWidget *editor, const Data::PdoEntryConfiguration &entry, int column);
    bool canPreserveInlineEditor(bool stableContext) const;
    void updateTablePresentation();
    void showValidation(const Data::ConfigurationValidation &validation, const QString &prefix = {});

    WorkbenchController *m_controller = nullptr;
    Core::PropertyPageContext m_context;
    Data::ProcessDataConfiguration m_configuration;
    Data::ProcessDataConfiguration m_esiDefaults;
    Data::NodeId m_ownerSlaveId;
    Data::NodeId m_selectedSyncManagerId;
    Data::NodeId m_selectedPdoId;
    bool m_editable = false;
    bool m_showingEsiDefaults = false;
    bool m_rebuilding = false;
    bool m_repositoryDeviceAvailable = false;
    bool m_repositoryDeviceSupported = false;
    bool m_repositoryProcessDataAvailable = false;
    bool m_repositoryProcessDataHasErrors = false;
    QPointer<QWidget> m_inlineEditor;
    std::optional<Data::PdoEntryConfiguration> m_inlineEditorAuthority;
    Data::NodeId m_inlineEditorPdoId;
    int m_inlineEditorColumn = -1;
    quint64 m_inlineEditorGeneration = 0;
    bool m_inlineEditorShowingEsiDefaults = false;
    bool m_inlineEditorCommitInProgress = false;
    std::optional<Data::PdoEntryConfiguration> m_inlineEditorSubmittedAuthority;
    bool m_inlineEditorMetadataDirty = false;

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
