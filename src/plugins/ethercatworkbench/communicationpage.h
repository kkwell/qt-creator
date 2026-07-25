// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>
#include <QWidget>

#include <optional>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;
class QTreeWidget;
QT_END_NAMESPACE

namespace EtherCAT::Workbench::Internal {

class WorkbenchController;

class CommunicationPage final : public QWidget
{
    Q_OBJECT

public:
    explicit CommunicationPage(WorkbenchController *controller, QWidget *parent = nullptr);

    void setContext(const Core::PropertyPageContext &context);

private:
    void refresh();
    void selectProvider(int index);
    void selectProfile(int index);
    void saveEndpoint();
    void updateSummary(
        const Data::ControllerConnectionSnapshot &snapshot,
        Core::ControllerConnectionProvider *provider);
    void updateChannels(const Data::ControllerConnectionSnapshot &snapshot);
    void updateControllerControl(const Data::ControllerConnectionSnapshot &snapshot);
    void updateTopology(const std::optional<Data::ControllerTopologySnapshot> &topology);
    void executeControllerControl(Data::ControllerControlCommand command);

    QPointer<WorkbenchController> m_controller;
    Core::PropertyPageContext m_context;
    QLabel *m_providerLabel = nullptr;
    QLabel *m_profileLabel = nullptr;
    QLabel *m_endpointLabel = nullptr;
    QComboBox *m_provider = nullptr;
    QComboBox *m_profile = nullptr;
    QLineEdit *m_endpoint = nullptr;
    QToolButton *m_saveEndpoint = nullptr;
    QToolButton *m_connect = nullptr;
    QToolButton *m_refresh = nullptr;
    QToolButton *m_disconnect = nullptr;
    QToolButton *m_acquireControl = nullptr;
    QToolButton *m_enterConfiguration = nullptr;
    QToolButton *m_scanBus = nullptr;
    QToolButton *m_restorePackage = nullptr;
    QToolButton *m_releaseControl = nullptr;
    QTreeWidget *m_summary = nullptr;
    QTreeWidget *m_channels = nullptr;
    QLabel *m_topologySummary = nullptr;
    QTreeWidget *m_actualBus = nullptr;
    bool m_updating = false;
    bool m_endpointDirty = false;
};

} // namespace EtherCAT::Workbench::Internal
