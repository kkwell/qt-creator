// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QToolButton;
class QTreeWidget;
QT_END_NAMESPACE

namespace Utils {
class InfoLabel;
}

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
    void updateSummary(
        const Data::ControllerConnectionSnapshot &snapshot,
        Core::ControllerConnectionProvider *provider);
    void updateChannels(const Data::ControllerConnectionSnapshot &snapshot);

    QPointer<WorkbenchController> m_controller;
    Core::PropertyPageContext m_context;
    Utils::InfoLabel *m_banner = nullptr;
    QLabel *m_safety = nullptr;
    QComboBox *m_provider = nullptr;
    QComboBox *m_profile = nullptr;
    QLabel *m_endpoint = nullptr;
    QToolButton *m_connect = nullptr;
    QToolButton *m_refresh = nullptr;
    QToolButton *m_disconnect = nullptr;
    QTreeWidget *m_summary = nullptr;
    QTreeWidget *m_channels = nullptr;
    QLabel *m_error = nullptr;
    bool m_updating = false;
};

} // namespace EtherCAT::Workbench::Internal
