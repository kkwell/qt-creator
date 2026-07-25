// Copyright (C) 2026 Kvell

#include "workbenchautomationservice.h"

#include "workbenchcontroller.h"

#include <ethercatcore/providers.h>

#include <QSet>

namespace EtherCAT::Workbench::Internal {

WorkbenchAutomationService::WorkbenchAutomationService(
    WorkbenchController *controller, QObject *parent)
    : AutomationService(parent)
    , m_controller(controller)
{
    if (!m_controller)
        return;
    connect(
        m_controller,
        &WorkbenchController::controllerConnectionChanged,
        this,
        &AutomationService::contextsChanged);
    connect(
        m_controller,
        &WorkbenchController::diagnosticsStatusChanged,
        this,
        &AutomationService::contextsChanged);
    if (Core::ProjectService *projects = m_controller->projectService()) {
        connect(projects, &Core::ProjectService::projectAdded, this, [this] {
            emit contextsChanged();
        });
        connect(projects, &Core::ProjectService::projectAboutToBeRemoved, this, [this] {
            emit contextsChanged();
        });
        connect(projects, &Core::ProjectService::projectChanged, this, [this] {
            emit contextsChanged();
        });
    }
}

QList<Core::AutomationContextSnapshot> WorkbenchAutomationService::contexts() const
{
    QList<Core::AutomationContextSnapshot> result;
    if (!m_controller)
        return result;
    Core::ProjectService *projects = m_controller->projectService();
    if (!projects)
        return result;

    Core::DeviceRepositoryProvider *repository = m_controller->deviceRepository();
    for (const Data::ProjectSnapshot &project : projects->projects()) {
        if (!project.valid || project.id.isNull())
            continue;
        for (const Data::ProjectNodeSnapshot &node : project.nodes) {
            if (node.kind != Data::ProjectNodeKind::Master || node.id.isNull())
                continue;

            Core::AutomationContextSnapshot context;
            context.scope = {project.id, node.id};
            context.controllerId = Core::automationControllerId(context.scope);
            context.identitySource = "ide-project-master";
            context.project = project;
            context.connection = m_controller->controllerConnectionSnapshot(context.scope);

            const ControllerConnectionSelection selection
                = m_controller->controllerConnectionSelection(context.scope);
            if (!selection.profileId.isNull()) {
                context.connectionProfile = m_controller->controllerConnectionProfileConfiguration(
                    context.scope, selection.profileId);
            }

            context.scan = m_controller->automationScanResult(context.scope);
            context.diagnostics = m_controller->automationDiagnosticsSnapshot(context.scope);
            const bool hasMockSource = context.connection.mock
                                       || (context.scan && context.scan->snapshot.mock)
                                       || (context.diagnostics && context.diagnostics->mock);
            const bool connectionIsMockSafe
                = context.connection.mock
                  || context.connection.state == Data::ControllerConnectionState::Disconnected;
            const bool scanIsMockSafe = !context.scan || context.scan->snapshot.mock;
            const bool diagnosticsAreMockSafe = !context.diagnostics || context.diagnostics->mock;
            context.mock = hasMockSource && connectionIsMockSafe && scanIsMockSafe
                           && diagnosticsAreMockSafe;

            if (repository) {
                QSet<QString> addedDescriptions;
                const auto addDescription = [&context, &addedDescriptions, repository](
                                                const Data::NodeId &deviceDescriptionId) {
                    if (deviceDescriptionId.isNull())
                        return;
                    const QString id = deviceDescriptionId.toString();
                    if (addedDescriptions.contains(id))
                        return;
                    const std::optional<Data::DeviceDescription> description = repository->device(
                        deviceDescriptionId);
                    if (!description)
                        return;
                    addedDescriptions.insert(id);
                    context.deviceDescriptions.append(*description);
                };
                for (const Data::OfflineSlaveConfiguration &slave : project.slaves) {
                    if (slave.masterId == context.scope.masterId)
                        addDescription(slave.deviceDescriptionId);
                }
                if (context.scan) {
                    for (const Data::ScannedSlave &slave : context.scan->snapshot.slaves) {
                        addDescription(slave.deviceDescriptionId);
                    }
                }
            }
            result.append(context);
        }
    }
    return result;
}

} // namespace EtherCAT::Workbench::Internal
