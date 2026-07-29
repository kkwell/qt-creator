// Copyright (C) 2026 Kvell

#include "gatewaysettingspage.h"

#include "ethercatautomationgatewayconstants.h"
#include "ethercatautomationgatewaytr.h"
#include "gatewayruntime.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <ethercatcore/ethercatcoreconstants.h>

#include <utils/stylehelper.h>

#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSpinBox>
#include <QVBoxLayout>

namespace EtherCAT::AutomationGateway::Internal {

using namespace EtherCAT::AutomationGateway::Constants;

static QString stateText(GatewayRuntimeState state)
{
    switch (state) {
    case GatewayRuntimeState::Unavailable:
        return Tr::tr("Unavailable");
    case GatewayRuntimeState::Stopped:
        return Tr::tr("Stopped");
    case GatewayRuntimeState::Starting:
        return Tr::tr("Starting");
    case GatewayRuntimeState::Running:
        return Tr::tr("Running");
    case GatewayRuntimeState::Stopping:
        return Tr::tr("Stopping");
    case GatewayRuntimeState::Failed:
        return Tr::tr("Failed");
    }
    return Tr::tr("Unavailable");
}

class GatewaySettingsPageWidget final : public ::Core::IOptionsPageWidget
{
public:
    explicit GatewaySettingsPageWidget(GatewayRuntimeController *runtime)
        : m_runtime(runtime)
    {
        setObjectName(SETTINGS_PAGE_OBJECT_NAME);
        setAccessibleName(Tr::tr("EtherCAT Automation Gateway settings"));

        m_enabled = new QCheckBox(Tr::tr("Enable loopback automation service"), this);
        m_enabled->setObjectName(SETTINGS_ENABLED_OBJECT_NAME);
        m_enabled->setAccessibleName(Tr::tr("Enable EtherCAT Automation Gateway"));

        m_address = new QLineEdit(QString::fromLatin1(LOOPBACK_ADDRESS), this);
        m_address->setObjectName(SETTINGS_ADDRESS_OBJECT_NAME);
        m_address->setAccessibleName(Tr::tr("Gateway loopback address"));
        m_address->setReadOnly(true);

        m_mcpPort = new QSpinBox(this);
        m_mcpPort->setObjectName(SETTINGS_MCP_PORT_OBJECT_NAME);
        m_mcpPort->setAccessibleName(Tr::tr("MCP loopback port"));
        m_mcpPort->setRange(0, 65535);
        m_mcpPort->setSpecialValueText(Tr::tr("Automatic (0)"));

        m_restPort = new QSpinBox(this);
        m_restPort->setObjectName(SETTINGS_REST_PORT_OBJECT_NAME);
        m_restPort->setAccessibleName(Tr::tr("REST loopback port"));
        m_restPort->setRange(0, 65535);
        m_restPort->setSpecialValueText(Tr::tr("Automatic (0)"));

        m_state = new QLineEdit(this);
        m_state->setObjectName(SETTINGS_STATE_OBJECT_NAME);
        m_state->setAccessibleName(Tr::tr("Gateway runtime state"));
        m_state->setReadOnly(true);

        m_mcpEndpoint = new QLineEdit(this);
        m_mcpEndpoint->setObjectName(SETTINGS_MCP_ENDPOINT_OBJECT_NAME);
        m_mcpEndpoint->setAccessibleName(Tr::tr("Actual MCP endpoint"));
        m_mcpEndpoint->setReadOnly(true);

        m_restEndpoint = new QLineEdit(this);
        m_restEndpoint->setObjectName(SETTINGS_REST_ENDPOINT_OBJECT_NAME);
        m_restEndpoint->setAccessibleName(Tr::tr("Actual REST endpoint"));
        m_restEndpoint->setReadOnly(true);

        m_error = new QLabel(this);
        m_error->setObjectName(SETTINGS_ERROR_OBJECT_NAME);
        m_error->setAccessibleName(Tr::tr("Gateway error information"));
        m_error->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_error->setWordWrap(true);

        m_safety = new QLabel(
            Tr::tr("Safety boundary: controller views are loopback-only, Mock-only, and read-only. "
                   "Semantic operation tools submit approval-required intents to the IDE service; "
                   "they never call a Provider or controller directly."),
            this);
        m_safety->setObjectName(SETTINGS_SAFETY_OBJECT_NAME);
        m_safety->setAccessibleName(Tr::tr("Gateway safety boundary"));
        m_safety->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_safety->setWordWrap(true);

        auto form = new QFormLayout;
        form->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
        form->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
        form->addRow(Tr::tr("Loopback address:"), m_address);
        form->addRow(Tr::tr("MCP port:"), m_mcpPort);
        form->addRow(Tr::tr("REST port:"), m_restPort);
        form->addRow(Tr::tr("Runtime state:"), m_state);
        form->addRow(Tr::tr("MCP endpoint:"), m_mcpEndpoint);
        form->addRow(Tr::tr("REST endpoint:"), m_restEndpoint);
        form->addRow(Tr::tr("Last error:"), m_error);

        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(
            Utils::StyleHelper::SpacingTokens::PaddingHM,
            Utils::StyleHelper::SpacingTokens::PaddingVM,
            Utils::StyleHelper::SpacingTokens::PaddingHM,
            Utils::StyleHelper::SpacingTokens::PaddingVM);
        layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
        layout->addWidget(m_enabled);
        layout->addLayout(form);
        layout->addWidget(m_safety);
        layout->addStretch();

        connect(m_enabled, &QCheckBox::toggled, this, [this] { updatePortEditability(); });
        if (m_runtime) {
            connect(
                m_runtime,
                &GatewayRuntimeController::snapshotChanged,
                this,
                [this](const GatewayRuntimeSnapshot &snapshot) { updateFromSnapshot(snapshot); });
            updateFromSnapshot(m_runtime->snapshot());
        } else {
            updateFromSnapshot({});
        }
    }

    void apply() final
    {
        if (!m_runtime)
            return;
        const GatewayConfiguration candidate{
            m_enabled->isChecked(),
            m_mcpPort->value(),
            m_restPort->value(),
        };
        m_runtime->applyConfiguration(candidate);
        updateFromSnapshot(m_runtime->snapshot());
    }

    void cancel() final
    {
        if (m_runtime)
            updateFromSnapshot(m_runtime->snapshot());
    }

    bool isDirty() const final
    {
        if (!m_runtime)
            return false;
        return GatewayConfiguration{
                   m_enabled->isChecked(),
                   m_mcpPort->value(),
                   m_restPort->value(),
               }
               != m_runtime->configuration();
    }

private:
    void updateFromSnapshot(const GatewayRuntimeSnapshot &snapshot)
    {
        m_refreshing = true;
        m_enabled->setChecked(snapshot.configuration.enabled);
        m_mcpPort->setValue(snapshot.configuration.mcpPort);
        m_restPort->setValue(snapshot.configuration.restPort);
        m_state->setText(stateText(snapshot.state));
        m_mcpEndpoint->setText(
            snapshot.mcpEndpoint.isEmpty() ? Tr::tr("Not listening")
                                           : snapshot.mcpEndpoint.toString());
        m_restEndpoint->setText(
            snapshot.restEndpoint.isEmpty() ? Tr::tr("Not listening")
                                            : snapshot.restEndpoint.toString());
        m_error->setText(snapshot.lastError.isEmpty() ? Tr::tr("None") : snapshot.lastError);
        m_enabled->setEnabled(snapshot.state != GatewayRuntimeState::Unavailable);
        m_refreshing = false;
        updatePortEditability();
    }

    void updatePortEditability()
    {
        if (m_refreshing)
            return;
        const GatewayRuntimeState state = m_runtime ? m_runtime->snapshot().state
                                                    : GatewayRuntimeState::Unavailable;
        const bool editable = state == GatewayRuntimeState::Stopped
                              || state == GatewayRuntimeState::Failed;
        m_mcpPort->setEnabled(editable);
        m_restPort->setEnabled(editable);
    }

    QPointer<GatewayRuntimeController> m_runtime;
    QCheckBox *m_enabled = nullptr;
    QLineEdit *m_address = nullptr;
    QSpinBox *m_mcpPort = nullptr;
    QSpinBox *m_restPort = nullptr;
    QLineEdit *m_state = nullptr;
    QLineEdit *m_mcpEndpoint = nullptr;
    QLineEdit *m_restEndpoint = nullptr;
    QLabel *m_error = nullptr;
    QLabel *m_safety = nullptr;
    bool m_refreshing = false;
};

class GatewaySettingsPage final : public ::Core::IOptionsPage
{
public:
    explicit GatewaySettingsPage(GatewayRuntimeController *runtime)
    {
        setId(SETTINGS_PAGE_ID);
        setDisplayName(Tr::tr("Automation Gateway"));
        setCategory(EtherCAT::Core::Constants::SETTINGS_CATEGORY);
        setWidgetCreator([runtime] { return new GatewaySettingsPageWidget(runtime); });
        setFixedKeywords({
            Tr::tr("MCP"),
            Tr::tr("REST"),
            Tr::tr("loopback"),
            Tr::tr("Mock-only"),
            Tr::tr("read-only"),
        });
    }
};

std::unique_ptr<::Core::IOptionsPage> createGatewaySettingsPage(GatewayRuntimeController *runtime)
{
    return std::make_unique<GatewaySettingsPage>(runtime);
}

} // namespace EtherCAT::AutomationGateway::Internal
