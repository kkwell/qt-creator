// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

namespace Utils {
class InfoLabel;
}

namespace EtherCAT::Workbench::Internal {

class WorkbenchController;

class DcPage final : public QWidget
{
    Q_OBJECT

public:
    explicit DcPage(WorkbenchController *controller, QWidget *parent = nullptr);

    void setContext(const Core::PropertyPageContext &context);

private:
    enum class SignalField { CycleTime, ShiftTime };

    bool submitConfiguration(const Data::DcConfiguration &configuration);
    void rebuildControls();
    void updateControlState();
    void selectEsiMode(int index);
    void commitModeName();
    void commitAssignActivate();
    void commitSignalValue(bool sync1, SignalField field, QLineEdit *editor);
    void showValidation(const QList<Data::ConfigurationIssue> &issues, const QString &prefix = {});
    void rejectInput(const QString &message);

    WorkbenchController *m_controller = nullptr;
    Core::PropertyPageContext m_context;
    Data::DcConfiguration m_configuration;
    Data::DcConfiguration m_esiDefaults;
    QList<Data::DcModeDescription> m_esiModes;
    bool m_editable = false;
    bool m_repositoryDeviceAvailable = false;
    bool m_repositoryDeviceSupported = false;
    bool m_showingEsiDefaults = false;
    bool m_rebuilding = false;

    QLabel *m_summary = nullptr;
    Utils::InfoLabel *m_validation = nullptr;
    QLabel *m_units = nullptr;
    QPushButton *m_restoreDefaults = nullptr;
    QComboBox *m_operationMode = nullptr;
    QCheckBox *m_enabled = nullptr;
    QLineEdit *m_assignActivate = nullptr;
    QCheckBox *m_sync0Enabled = nullptr;
    QLineEdit *m_sync0Cycle = nullptr;
    QLineEdit *m_sync0Shift = nullptr;
    QCheckBox *m_sync1Enabled = nullptr;
    QLineEdit *m_sync1Cycle = nullptr;
    QLineEdit *m_sync1Shift = nullptr;
    QCheckBox *m_potentialReferenceClock = nullptr;
};

} // namespace EtherCAT::Workbench::Internal
