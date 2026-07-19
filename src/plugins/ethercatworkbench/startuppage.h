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

class StartupTableModel;
class WorkbenchController;

class StartupPage final : public QWidget
{
    Q_OBJECT

public:
    explicit StartupPage(WorkbenchController *controller, QWidget *parent = nullptr);

    void setContext(const Core::PropertyPageContext &context);

private:
    friend class StartupTableModel;

    bool submitConfiguration(const Data::StartupConfiguration &configuration);
    void rebuildModel();
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
    bool m_editable = false;
    bool m_showingEsiDefaults = false;
    bool m_rebuilding = false;
    bool m_repositoryDeviceAvailable = false;
    bool m_repositoryDeviceSupported = false;
    bool m_repositoryStartupAvailable = false;
    bool m_repositoryStartupHasErrors = false;

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
