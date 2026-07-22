// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>
#include <QWidget>

#include <optional>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QTableView;
QT_END_NAMESPACE

namespace Utils {
class InfoLabel;
}

namespace EtherCAT::Workbench::Internal {

class StartupTableModel;
class WorkbenchController;

class StartupPage final : public QWidget
{
    Q_OBJECT

public:
    explicit StartupPage(WorkbenchController *controller, QWidget *parent = nullptr);
    ~StartupPage() final;

    void setContext(const Core::PropertyPageContext &context);

private:
    friend class StartupTableModel;

    std::optional<Data::StartupConfiguration> currentConfiguration(
        quint64 contextGeneration, const Data::NodeId &projectId, const Data::NodeId &slaveId) const;
    bool submitConfiguration(
        const Data::StartupConfiguration &configuration,
        const Data::NodeId &inlineParameterId = {},
        int inlineColumn = -1);
    void rebuildModel(
        bool preserveInlineEditor = false, bool notifyPreservedColumn = false);
    void trackInlineEditor(
        QWidget *editor,
        const Data::StartupParameterConfiguration &parameter,
        int column);
    bool canPreserveInlineEditor(bool stableContext) const;
    void updateButtonState();
    void updateTablePresentation();
    void addParameter();
    void editParameter();
    void deleteParameter();
    void moveParameter(int distance);
    void showValidation(const QList<Data::ConfigurationIssue> &issues, const QString &prefix = {});

    WorkbenchController *m_controller = nullptr;
    Core::PropertyPageContext m_context;
    Data::StartupConfiguration m_configuration;
    Data::StartupConfiguration m_esiDefaults;
    Data::NodeId m_selectedParameterId;
    quint64 m_contextGeneration = 0;
    bool m_editable = false;
    bool m_showingEsiDefaults = false;
    bool m_rebuilding = false;
    bool m_repositoryDeviceAvailable = false;
    bool m_repositoryDeviceSupported = false;
    bool m_repositoryStartupAvailable = false;
    bool m_repositoryStartupHasErrors = false;
    QPointer<QWidget> m_inlineEditor;
    std::optional<Data::StartupParameterConfiguration> m_inlineEditorAuthority;
    int m_inlineEditorColumn = -1;
    quint64 m_inlineEditorGeneration = 0;
    bool m_inlineEditorShowingEsiDefaults = false;
    bool m_inlineEditorCommitInProgress = false;
    std::optional<Data::StartupParameterConfiguration> m_inlineEditorSubmittedAuthority;
    bool m_inlineEditorMetadataDirty = false;

    QLabel *m_summary = nullptr;
    Utils::InfoLabel *m_validation = nullptr;
    QPushButton *m_restoreDefaults = nullptr;
    QTableView *m_table = nullptr;
    QPushButton *m_moveUp = nullptr;
    QPushButton *m_moveDown = nullptr;
    QPushButton *m_new = nullptr;
    QPushButton *m_delete = nullptr;
    QPushButton *m_edit = nullptr;
    StartupTableModel *m_model = nullptr;
};

} // namespace EtherCAT::Workbench::Internal
