// Copyright (C) 2026 Kvell

#include "automationdispatcher.h"

#include "ethercatautomationgatewayconstants.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QUuid>

#include <algorithm>

namespace EtherCAT::AutomationGateway::Internal {

using namespace EtherCAT::AutomationGateway::Constants;

static constexpr qsizetype maximumJournalEntries = 1024;

static QString connectionStateName(Data::ControllerConnectionState state)
{
    using State = Data::ControllerConnectionState;
    switch (state) {
    case State::Disconnected:
        return "disconnected";
    case State::Connecting:
        return "connecting";
    case State::Handshaking:
        return "handshaking";
    case State::Connected:
        return "connected";
    case State::Degraded:
        return "degraded";
    case State::Disconnecting:
        return "disconnecting";
    case State::Failed:
        return "failed";
    }
    return "unknown";
}

static QString serviceStateName(Data::ControllerServiceState state)
{
    using State = Data::ControllerServiceState;
    switch (state) {
    case State::Unknown:
        return "unknown";
    case State::Boot:
        return "boot";
    case State::Configuring:
        return "configuring";
    case State::SafeOperational:
        return "safe-operational";
    case State::OperationalSafe:
        return "operational-safe";
    case State::Running:
        return "running";
    case State::Stopping:
        return "stopping";
    case State::Fault:
        return "fault";
    case State::Recovering:
        return "recovering";
    case State::Shutdown:
        return "shutdown";
    case State::Paused:
        return "paused";
    }
    return "unknown";
}

static QString ethercatStateName(Data::EtherCATState state)
{
    using State = Data::EtherCATState;
    switch (state) {
    case State::Unknown:
        return "unknown";
    case State::Init:
        return "init";
    case State::PreOperational:
        return "pre-operational";
    case State::SafeOperational:
        return "safe-operational";
    case State::Operational:
        return "operational";
    case State::Bootstrap:
        return "bootstrap";
    }
    return "unknown";
}

static QString workingCounterStateName(Data::WorkingCounterState state)
{
    using State = Data::WorkingCounterState;
    switch (state) {
    case State::Unknown:
        return "unknown";
    case State::Valid:
        return "valid";
    case State::Incomplete:
        return "incomplete";
    case State::Zero:
        return "zero";
    case State::Error:
        return "error";
    }
    return "unknown";
}

static QString hex32(quint32 value)
{
    return QString("0x%1").arg(value, 8, 16, QLatin1Char('0'));
}

static QJsonObject identityObject(const Data::DeviceIdentity &identity)
{
    return {
        {"vendorId", hex32(identity.vendorId)},
        {"productCode", hex32(identity.productCode)},
        {"revision", hex32(identity.revisionNumber)},
    };
}

static QJsonObject capabilityObject(
    const std::optional<Data::ControllerCapabilitySummary> &capability)
{
    if (!capability)
        return {{"available", false}};
    return {
        {"available", true},
        {"maximumSlaves", capability->maximumSlaves},
        {"minimumCycleTimeNs", QString::number(capability->minimumCycleTimeNs)},
        {"maximumCyclicFrames", capability->maximumCyclicFrames},
        {"maximumProcessInputBytes", capability->maximumProcessInputBytes},
        {"maximumProcessOutputBytes", capability->maximumProcessOutputBytes},
        {"coe", capability->coe},
        {"distributedClocks", capability->distributedClocks},
        {"multiSlaveDistributedClocks", capability->multiSlaveDistributedClocks},
        {"multiFrame", capability->multiFrame},
        {"runtimeProgram", capability->runtimeProgram},
    };
}

static QJsonObject stateObject(const Core::AutomationContextSnapshot &context)
{
    QJsonObject result{
        {"connection", connectionStateName(context.connection.state)},
        {"endpoint", context.connection.endpointSummary},
        {"readOnly", true},
        {"mock", context.mock},
    };
    if (context.connection.controllerState) {
        const Data::ControllerStateSummary &state = *context.connection.controllerState;
        result.insert("service", serviceStateName(state.serviceState));
        result.insert("ready", state.ready);
        result.insert("busOperational", state.busOperational);
        result.insert("applicationActive", state.applicationActive);
        result.insert("safeOutput", state.safeOutput);
        result.insert("fault", state.fault);
        result.insert("recovering", state.recovering);
        result.insert("paused", state.paused);
        result.insert("distributedClocksLocked", state.distributedClocksLocked);
        result.insert("expectedWorkingCounter", int(state.expectedWorkingCounter));
        result.insert("actualWorkingCounter", int(state.actualWorkingCounter));
        result.insert("cycleCount", QString::number(state.cycleCount));
    }
    if (context.connection.session) {
        const Data::ControllerSessionSummary &session = *context.connection.session;
        result.insert("bootId", QString::number(session.bootId));
        result.insert(
            "leaseState",
            session.ownsControlLease             ? "owned-by-ide"
            : session.controlLeaseOwnerSessionId ? "owned-by-other"
                                                 : "unowned");
    } else {
        result.insert("leaseState", "unavailable");
    }
    return result;
}

static QJsonObject topologyObject(const Core::AutomationContextSnapshot &context)
{
    QJsonArray slaves;
    QString source = "ide-project";
    bool complete = true;
    QString capturedAt;

    if (context.scan) {
        source = "ide-workbench-scan-snapshot";
        complete = context.scan->snapshot.complete;
        capturedAt = context.scan->snapshot.capturedAt.toUTC().toString(Qt::ISODateWithMs);
        for (const Data::ScannedSlave &slave : context.scan->snapshot.slaves) {
            slaves.append(QJsonObject{
                {"position", slave.position},
                {"name", slave.name},
                {"identity", identityObject(slave.identity)},
                {"serial", QString::number(slave.serialNumber)},
                {"alias", int(slave.alias)},
            });
        }
    } else if (context.connection.topology) {
        source = "ide-controller-topology-snapshot";
        capturedAt = context.connection.topology->discoveredAt.toUTC().toString(Qt::ISODateWithMs);
        for (const Data::ControllerTopologySlave &slave : context.connection.topology->slaves) {
            slaves.append(QJsonObject{
                {"position", int(slave.position)},
                {"identity",
                 QJsonObject{
                     {"vendorId", hex32(slave.vendorId)},
                     {"productCode", hex32(slave.productCode)},
                     {"revision", hex32(slave.revision)},
                 }},
                {"serial", QString::number(slave.serial)},
                {"stationAddress", int(slave.stationAddress)},
                {"alState", int(slave.alState)},
            });
        }
    } else {
        for (const Data::OfflineSlaveConfiguration &slave : context.project.slaves) {
            if (slave.masterId != context.scope.masterId)
                continue;
            slaves.append(QJsonObject{
                {"position", slave.position},
                {"name", slave.name},
                {"identity", identityObject(slave.identity)},
                {"serial", QString::number(slave.serialNumber)},
                {"alias", int(slave.alias)},
            });
        }
    }

    return {
        {"source", source},
        {"capturedAt", capturedAt},
        {"complete", complete},
        {"scanTriggeredByRequest", false},
        {"slaves", slaves},
    };
}

static const Data::DeviceDescription *descriptionFor(
    const Core::AutomationContextSnapshot &context,
    const Data::NodeId &descriptionId,
    const Data::DeviceIdentity &identity)
{
    for (const Data::DeviceDescription &description : context.deviceDescriptions) {
        if ((!descriptionId.isNull() && description.summary.id == descriptionId)
            || description.summary.identity == identity) {
            return &description;
        }
    }
    return nullptr;
}

static QJsonObject deviceDescriptionSummary(const Data::DeviceDescription *description)
{
    if (!description)
        return {{"available", false}};

    int rxEntryCount = 0;
    int txEntryCount = 0;
    int rxBits = 0;
    int txBits = 0;
    for (const Data::PdoDescription &pdo : description->rxPdos) {
        rxEntryCount += pdo.entries.size();
        for (const Data::PdoEntryDescription &entry : pdo.entries)
            rxBits += entry.bitLength;
    }
    for (const Data::PdoDescription &pdo : description->txPdos) {
        txEntryCount += pdo.entries.size();
        for (const Data::PdoEntryDescription &entry : pdo.entries)
            txBits += entry.bitLength;
    }
    return {
        {"available", true},
        {"name", description->summary.name},
        {"typeName", description->summary.typeName},
        {"group", description->summary.group},
        {"supported", description->summary.supported},
        {"esiSha256", QString::fromLatin1(description->sourceSha256.toHex())},
        {"syncManagerCount", description->syncManagers.size()},
        {"rxPdoCount", description->rxPdos.size()},
        {"txPdoCount", description->txPdos.size()},
        {"rxEntryCount", rxEntryCount},
        {"txEntryCount", txEntryCount},
        {"rxBits", rxBits},
        {"txBits", txBits},
        {"coe", description->coe.supported},
        {"distributedClockModeCount", description->dcModes.size()},
        {"startupParameterCount", description->startupParameters.size()},
        {"unsupportedFeatureCount", description->unsupportedFeatures.size()},
    };
}

static QJsonObject deviceObject(const Core::AutomationContextSnapshot &context, int position)
{
    if (context.scan) {
        for (const Data::ScannedSlave &slave : context.scan->snapshot.slaves) {
            if (slave.position != position)
                continue;
            return {
                {"position", position},
                {"name", slave.name},
                {"identity", identityObject(slave.identity)},
                {"serial", QString::number(slave.serialNumber)},
                {"alias", int(slave.alias)},
                {"model",
                 deviceDescriptionSummary(
                     descriptionFor(context, slave.deviceDescriptionId, slave.identity))},
            };
        }
    }
    for (const Data::OfflineSlaveConfiguration &slave : context.project.slaves) {
        if (slave.masterId != context.scope.masterId || slave.position != position)
            continue;
        return {
            {"position", position},
            {"name", slave.name},
            {"identity", identityObject(slave.identity)},
            {"serial", QString::number(slave.serialNumber)},
            {"alias", int(slave.alias)},
            {"model",
             deviceDescriptionSummary(
                 descriptionFor(context, slave.deviceDescriptionId, slave.identity))},
        };
    }
    return {};
}

static QJsonObject diagnosticsObject(const Core::AutomationContextSnapshot &context)
{
    if (!context.diagnostics)
        return {{"available", false}, {"mock", context.mock}};
    const Data::DiagnosticsSnapshot &snapshot = *context.diagnostics;
    return {
        {"available", true},
        {"mock", snapshot.mock},
        {"generation", QString::number(snapshot.generation)},
        {"capturedAt", snapshot.capturedAt.toUTC().toString(Qt::ISODateWithMs)},
        {"masterState", ethercatStateName(snapshot.masterState)},
        {"masterHasError", snapshot.masterHasError},
        {"workingCounter",
         QJsonObject{
             {"expected", int(snapshot.workingCounter.expected)},
             {"actual", int(snapshot.workingCounter.actual)},
             {"state", workingCounterStateName(snapshot.workingCounter.state)},
             {"mismatchCount", QString::number(snapshot.workingCounter.mismatchCount)},
         }},
        {"cycle",
         QJsonObject{
             {"nominalNs", QString::number(snapshot.cycle.nominalCycleNs)},
             {"lastNs", QString::number(snapshot.cycle.lastCycleNs)},
             {"jitterNs", QString::number(snapshot.cycle.jitterNs)},
             {"deadlineMarginNs", QString::number(snapshot.cycle.deadlineMarginNs)},
             {"missedDeadlineCount", QString::number(snapshot.cycle.missedDeadlineCount)},
         }},
        {"activeAlarmCount", snapshot.activeAlarmCount},
        {"unacknowledgedAlarmCount", snapshot.unacknowledgedAlarmCount},
        {"slaveCount", snapshot.slaves.size()},
    };
}

static QJsonValue canonicalized(const QJsonValue &value)
{
    if (value.isArray()) {
        QJsonArray result;
        for (const QJsonValue &item : value.toArray())
            result.append(canonicalized(item));
        return result;
    }
    if (!value.isObject())
        return value;
    const QJsonObject source = value.toObject();
    QStringList keys = source.keys();
    keys.sort(Qt::CaseSensitive);
    QJsonObject result;
    for (const QString &key : keys)
        result.insert(key, canonicalized(source.value(key)));
    return result;
}

AutomationDispatcher::AutomationDispatcher(Core::AutomationService *service)
    : m_service(service)
{}

QStringList AutomationDispatcher::toolNames()
{
    return {
        "adapter.list",
        "artifact.validate",
        "controller.get-capabilities",
        "controller.get-device",
        "controller.get-diagnostics",
        "controller.get-state",
        "controller.get-topology",
        "controller.list",
        "gateway.get-protocol",
    };
}

QByteArray AutomationDispatcher::canonicalJson(const QJsonValue &value)
{
    const QJsonValue normalized = canonicalized(value);
    if (normalized.isObject()) {
        return QJsonDocument(normalized.toObject()).toJson(QJsonDocument::Compact);
    }
    if (normalized.isArray()) {
        return QJsonDocument(normalized.toArray()).toJson(QJsonDocument::Compact);
    }
    QJsonArray wrapper{normalized};
    QByteArray encoded = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    return encoded.mid(1, encoded.size() - 2);
}

QJsonObject AutomationDispatcher::safeContextSnapshot(const Core::AutomationContextSnapshot &context)
{
    QString masterName;
    for (const Data::ProjectNodeSnapshot &node : context.project.nodes) {
        if (node.id == context.scope.masterId) {
            masterName = node.name;
            break;
        }
    }
    QList<QJsonObject> sortedDeviceModels;
    for (const Data::DeviceDescription &description : context.deviceDescriptions) {
        sortedDeviceModels.append(QJsonObject{
            {"deviceDescriptionId", description.summary.id.toString()},
            {"identity", identityObject(description.summary.identity)},
            {"model", deviceDescriptionSummary(&description)},
        });
    }
    std::sort(
        sortedDeviceModels.begin(),
        sortedDeviceModels.end(),
        [](const QJsonObject &left, const QJsonObject &right) {
            return left.value("deviceDescriptionId").toString()
                   < right.value("deviceDescriptionId").toString();
        });
    QJsonArray deviceModels;
    for (const QJsonObject &deviceModel : sortedDeviceModels) {
        deviceModels.append(deviceModel);
    }

    return {
        {"controllerId", context.controllerId},
        {"identitySource", context.identitySource},
        {"projectId", context.scope.projectId.toString()},
        {"projectName", context.project.name},
        {"projectFormatVersion", context.project.formatVersion},
        {"projectModified", context.project.modified},
        {"masterId", context.scope.masterId.toString()},
        {"masterName", masterName},
        {"mock", context.mock},
        {"capabilities", capabilityObject(context.connection.capability)},
        {"state", stateObject(context)},
        {"topology", topologyObject(context)},
        {"diagnostics", diagnosticsObject(context)},
        {"deviceModels", deviceModels},
    };
}

QString AutomationDispatcher::snapshotHash(const Core::AutomationContextSnapshot &context)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(
            canonicalJson(safeContextSnapshot(context)), QCryptographicHash::Sha256)
            .toHex());
}

QList<Core::AutomationContextSnapshot> AutomationDispatcher::mockContexts() const
{
    if (!m_service)
        return {};
    QList<Core::AutomationContextSnapshot> result;
    for (const Core::AutomationContextSnapshot &context : m_service->contexts()) {
        if (context.mock)
            result.append(context);
    }
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.controllerId < right.controllerId;
    });
    return result;
}

std::optional<Core::AutomationContextSnapshot> AutomationDispatcher::currentContext(
    const QString &controllerId) const
{
    if (!m_service)
        return std::nullopt;
    return m_service->context(controllerId);
}

static QString operationIdFrom(const QJsonObject &arguments)
{
    const QString supplied = arguments.value("operationId").toString();
    return supplied.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : supplied;
}

static bool operationIdIsInvalid(const QJsonObject &arguments)
{
    if (!arguments.contains("operationId"))
        return false;
    const QJsonValue supplied = arguments.value("operationId");
    return !supplied.isString() || supplied.toString().isEmpty() || supplied.toString().size() > 128;
}

static QString requestHashFor(const QString &tool, const QJsonObject &arguments)
{
    QJsonObject normalizedArguments = arguments;
    normalizedArguments.remove("operationId");
    const QJsonObject request{
        {"tool", tool},
        {"arguments", normalizedArguments},
    };
    return QString::fromLatin1(
        QCryptographicHash::hash(
            AutomationDispatcher::canonicalJson(request), QCryptographicHash::Sha256)
            .toHex());
}

QJsonObject AutomationDispatcher::dispatch(
    const QString &tool, const QJsonObject &arguments, const AutomationActor &actor)
{
    const QString requestHash = requestHashFor(tool, arguments);
    if (operationIdIsInvalid(arguments)) {
        const QString rejectedId = arguments.value("operationId").toString();
        return makeError(
            rejectedId,
            "CT010_BAD_REQUEST",
            "operationId must contain between 1 and 128 characters.",
            "$.operationId",
            "Correct the request fields and retry with a new operation ID.",
            {},
            requestHash,
            actor);
    }
    const QString operationId = operationIdFrom(arguments);

    const auto existing = m_journal.constFind(operationId);
    if (existing != m_journal.cend()) {
        if (existing->requestHash == requestHash)
            return existing->response;
        return makeError(
            operationId,
            "CT010_BAD_REQUEST",
            "The operationId is already bound to different parameters.",
            "$.operationId",
            "Retry the different request with a new operation ID.",
            {{"reason", "operation-id-conflict"}},
            requestHash,
            actor);
    }

    const QJsonObject response
        = dispatchUnjournaled(tool, arguments, operationId, requestHash, actor);
    remember(operationId, requestHash, response);
    return response;
}

QJsonObject AutomationDispatcher::rejectMutation(
    const QString &operation, const QJsonObject &arguments, const AutomationActor &actor)
{
    const QString requestHash = requestHashFor(operation, arguments);
    if (operationIdIsInvalid(arguments)) {
        return makeError(
            arguments.value("operationId").toString(),
            "CT010_BAD_REQUEST",
            "operationId must contain between 1 and 128 characters.",
            "$.operationId",
            "Correct the request fields and retry with a new operation ID.",
            {},
            requestHash,
            actor);
    }
    const QString operationId = operationIdFrom(arguments);
    const auto existing = m_journal.constFind(operationId);
    if (existing != m_journal.cend()) {
        if (existing->requestHash == requestHash)
            return existing->response;
        return makeError(
            operationId,
            "CT010_BAD_REQUEST",
            "The operationId is already bound to different parameters.",
            "$.operationId",
            "Retry the different request with a new operation ID.",
            {{"reason", "operation-id-conflict"}},
            requestHash,
            actor);
    }
    const QJsonObject response = makeError(
        operationId,
        "CT008_READ_ONLY",
        "This initial IDE Gateway exposes no controller mutation path.",
        "$",
        "Request approval through a future policy-bound mutation API.",
        {
            {"reason", "approval-required"},
            {"operation", operation},
            {"providerCalls", 0},
        },
        requestHash,
        actor);
    remember(operationId, requestHash, response);
    return response;
}

static QStringList allowedArguments(const QString &tool)
{
    if (tool == "controller.list" || tool == "adapter.list" || tool == "gateway.get-protocol") {
        return {"operationId"};
    }
    if (tool == "controller.get-device")
        return {"controllerId", "operationId", "position"};
    if (tool == "artifact.validate")
        return {"artifact", "operationId"};
    return {"controllerId", "operationId"};
}

static QString unexpectedArgument(const QString &tool, const QJsonObject &arguments)
{
    const QStringList allowed = allowedArguments(tool);
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        if (!allowed.contains(it.key()))
            return it.key();
    }
    return {};
}

QJsonObject AutomationDispatcher::dispatchUnjournaled(
    const QString &tool,
    const QJsonObject &arguments,
    const QString &operationId,
    const QString &requestHash,
    const AutomationActor &actor)
{
    if (!toolNames().contains(tool)) {
        return makeError(
            operationId,
            "CT011_PROTOCOL_UNSUPPORTED",
            "The requested tool is not in controller-tools-v1.",
            "$.tool",
            "Use gateway.get-protocol to negotiate the closed read-only tool catalog.",
            {{"tool", tool}},
            requestHash,
            actor);
    }
    const QString unexpected = unexpectedArgument(tool, arguments);
    if (!unexpected.isEmpty()) {
        return makeError(
            operationId,
            "CT010_BAD_REQUEST",
            "The request contains an undeclared argument.",
            QString("$.%1").arg(unexpected),
            "Remove undeclared fields and retry with a new operation ID.",
            {{"argument", unexpected}},
            requestHash,
            actor);
    }

    if (tool == "gateway.get-protocol") {
        QJsonArray mutations;
        const QStringList mutationNames{
            "controller.ip.configure",
            "controller.connect",
            "controller.lease.acquire",
            "controller.scan",
            "controller.configuration.apply",
            "controller.deploy",
            "controller.motion",
        };
        for (const QString &name : mutationNames) {
            mutations.append(QJsonObject{
                {"name", name},
                {"available", false},
                {"reason", "approval-required"},
            });
        }
        QJsonArray tools;
        for (const QString &name : toolNames())
            tools.append(name);
        return makeSuccess(
            operationId,
            {
                {"apiVersion", API_VERSION},
                {"mcpProtocolVersion", MCP_PROTOCOL_VERSION},
                {"mcpTransport", "streamable-http"},
                {"restOpenApi", "3.1.1"},
                {"loopbackOnly", true},
                {"defaultEnabled", false},
                {"mockOnly", true},
                {"readOnly", true},
                {"toolCatalog", tools},
                {"mutations", mutations},
                {"realTimeBoundary",
                 "Gateway produces read-only intent views and never enters the 125 us cycle."},
            },
            {},
            requestHash,
            {},
            {},
            actor);
    }

    if (tool == "adapter.list") {
        return makeSuccess(
            operationId,
            {
                {"adapters", QJsonArray{}},
                {"registryAvailable", false},
                {"reason", "No IDE Adapter Registry semantic service exists in this issue."},
            },
            {"The checked-in mock adapters are contract inputs, not IDE runtime state."},
            requestHash,
            {},
            {},
            actor);
    }

    if (tool == "artifact.validate") {
        if (!arguments.value("artifact").isObject()) {
            return makeError(
                operationId,
                "CT000_FORMAT_INVALID",
                "artifact must be a JSON object.",
                "$.artifact",
                "Submit one immutable controller artifact object.",
                {},
                requestHash,
                actor);
        }
        const QJsonObject artifact = arguments.value("artifact").toObject();
        QJsonArray errors;
        const QStringList requiredFields{
            "apiVersion",
            "kind",
            "metadata",
            "spec",
        };
        for (const QString &field : requiredFields) {
            if (!artifact.contains(field))
                errors.append(QString("$.artifact.%1 is required").arg(field));
        }
        const QString kind = artifact.value("kind").toString();
        const QStringList supportedKinds{
            "AdapterManifest",
            "ControlIntent",
            "ControllerProject",
            "NormalizedDeviceModel",
        };
        if (!kind.isEmpty() && !supportedKinds.contains(kind))
            errors.append("$.artifact.kind is not a controller-tools-v1 artifact kind");
        if (artifact.contains("apiVersion")
            && artifact.value("apiVersion").toString() != "controller.embed-labs.dev/v1") {
            errors.append("$.artifact.apiVersion is unsupported");
        }
        if (artifact.contains("metadata") && !artifact.value("metadata").isObject()) {
            errors.append("$.artifact.metadata must be an object");
        }
        if (artifact.contains("spec") && !artifact.value("spec").isObject())
            errors.append("$.artifact.spec must be an object");

        return makeSuccess(
            operationId,
            {
                {"valid", errors.isEmpty()},
                {"kind", kind},
                {"validationLevel", "gateway-structural-v1"},
                {"errors", errors},
                {"stored", false},
                {"deployed", false},
            },
            {"Nested JSON Schema and deterministic resource validation remain offline-only."},
            requestHash,
            {},
            {},
            actor);
    }

    if (tool == "controller.list") {
        const QList<Core::AutomationContextSnapshot> contexts = mockContexts();
        QJsonArray controllers;
        QJsonArray stateView;
        for (const Core::AutomationContextSnapshot &context : contexts) {
            const QJsonObject safe = safeContextSnapshot(context);
            stateView.append(safe);
            controllers.append(QJsonObject{
                {"controllerId", context.controllerId},
                {"identitySource", context.identitySource},
                {"projectId", context.scope.projectId.toString()},
                {"projectName", context.project.name},
                {"masterId", context.scope.masterId.toString()},
                {"mock", true},
                {"contextHash", snapshotHash(context)},
            });
        }
        const QString hash = QString::fromLatin1(
            QCryptographicHash::hash(canonicalJson(stateView), QCryptographicHash::Sha256).toHex());
        return makeSuccess(
            operationId,
            {{"controllers", controllers}, {"source", "ide-automation-service"}},
            {},
            requestHash,
            hash,
            hash,
            actor);
    }

    const QJsonValue controllerIdValue = arguments.value("controllerId");
    const QString controllerId = controllerIdValue.toString();
    if (!controllerIdValue.isString() || controllerId.size() < 3 || controllerId.size() > 128) {
        return makeError(
            operationId,
            "CT010_BAD_REQUEST",
            "controllerId must contain between 3 and 128 characters.",
            "$.controllerId",
            "Use controller.list and supply one returned controllerId.",
            {},
            requestHash,
            actor);
    }
    const std::optional<Core::AutomationContextSnapshot> context = currentContext(controllerId);
    if (!context || !context->mock) {
        const bool liveContextExists = context && !context->mock;
        return makeError(
            operationId,
            liveContextExists ? "CT008_READ_ONLY" : "CT009_NOT_FOUND",
            liveContextExists
                ? QStringLiteral("The initial Gateway refuses non-mock controller state.")
                : QStringLiteral("The controllerId is not present in the current IDE snapshot."),
            "$.controllerId",
            liveContextExists
                ? QStringLiteral(
                      "Use the IDE directly; production Gateway authorization is not implemented.")
                : QStringLiteral("Refresh controller.list and use an existing mock controllerId."),
            {{"reason", liveContextExists ? "approval-required" : "context-unavailable"}},
            requestHash,
            actor);
    }
    const QString stateHash = snapshotHash(*context);

    if (tool == "controller.get-capabilities") {
        return makeSuccess(
            operationId,
            {
                {"controllerId", controllerId},
                {"controller", capabilityObject(context->connection.capability)},
                {"gateway",
                 QJsonObject{
                     {"mockOnly", true},
                     {"readOnly", true},
                     {"mutationTools", false},
                     {"controlLeaseCalls", false},
                 }},
                {"contextHash", stateHash},
            },
            {},
            requestHash,
            stateHash,
            stateHash,
            actor);
    }
    if (tool == "controller.get-state") {
        return makeSuccess(
            operationId,
            {
                {"controllerId", controllerId},
                {"state", stateObject(*context)},
                {"contextHash", stateHash},
            },
            {},
            requestHash,
            stateHash,
            stateHash,
            actor);
    }
    if (tool == "controller.get-topology") {
        return makeSuccess(
            operationId,
            {
                {"controllerId", controllerId},
                {"topology", topologyObject(*context)},
                {"contextHash", stateHash},
            },
            {},
            requestHash,
            stateHash,
            stateHash,
            actor);
    }
    if (tool == "controller.get-diagnostics") {
        return makeSuccess(
            operationId,
            {
                {"controllerId", controllerId},
                {"diagnostics", diagnosticsObject(*context)},
                {"contextHash", stateHash},
            },
            {},
            requestHash,
            stateHash,
            stateHash,
            actor);
    }
    if (tool == "controller.get-device") {
        const QJsonValue positionValue = arguments.value("position");
        if (!positionValue.isDouble() || positionValue.toDouble() < 0
            || positionValue.toDouble() > 65535
            || positionValue.toInt(-1) != positionValue.toDouble()) {
            return makeError(
                operationId,
                "CT010_BAD_REQUEST",
                "position must be an integer from 0 through 65535.",
                "$.position",
                "Use a position returned by controller.get-topology.",
                {},
                requestHash,
                actor);
        }
        const int position = positionValue.toInt();
        const QJsonObject device = deviceObject(*context, position);
        if (device.isEmpty()) {
            return makeError(
                operationId,
                "CT009_NOT_FOUND",
                "No device exists at the requested position.",
                "$.position",
                "Refresh controller.get-topology and use an existing position.",
                {},
                requestHash,
                actor);
        }
        return makeSuccess(
            operationId,
            {
                {"controllerId", controllerId},
                {"device", device},
                {"contextHash", stateHash},
            },
            {},
            requestHash,
            stateHash,
            stateHash,
            actor);
    }

    return makeError(
        operationId,
        "CT011_PROTOCOL_UNSUPPORTED",
        "The requested tool is unavailable.",
        "$.tool",
        "Use gateway.get-protocol to inspect the closed tool catalog.",
        {},
        requestHash,
        actor);
}

QJsonObject AutomationDispatcher::makeSuccess(
    const QString &operationId,
    const QJsonObject &data,
    const QStringList &warnings,
    const QString &requestHash,
    const QString &beforeStateHash,
    const QString &afterStateHash,
    const AutomationActor &actor) const
{
    QJsonArray warningArray;
    for (const QString &warning : warnings)
        warningArray.append(warning);
    return {
        {"apiVersion", API_VERSION},
        {"operationId", operationId},
        {"ok", true},
        {"data", data},
        {"warnings", warningArray},
        {"audit",
         QJsonObject{
             {"transport", actor.transport},
             {"sessionId", actor.sessionId},
             {"client",
              QJsonObject{
                  {"name", actor.clientName},
                  {"version", actor.clientVersion},
              }},
             {"requestHash", requestHash},
             {"beforeStateHash", beforeStateHash},
             {"afterStateHash", afterStateHash},
             {"decision", "allow-read-only"},
             {"recordedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
         }},
    };
}

QJsonObject AutomationDispatcher::makeError(
    const QString &operationId,
    const QString &code,
    const QString &message,
    const QString &path,
    const QString &recovery,
    const QJsonObject &details,
    const QString &requestHash,
    const AutomationActor &actor) const
{
    QJsonObject error{
        {"code", code},
        {"message", message},
        {"path", path},
        {"recovery", recovery},
    };
    if (!details.isEmpty())
        error.insert("details", details);
    return {
        {"apiVersion", API_VERSION},
        {"operationId", operationId},
        {"ok", false},
        {"data", QJsonObject{}},
        {"warnings", QJsonArray{}},
        {"audit",
         QJsonObject{
             {"transport", actor.transport},
             {"sessionId", actor.sessionId},
             {"client",
              QJsonObject{
                  {"name", actor.clientName},
                  {"version", actor.clientVersion},
              }},
             {"requestHash", requestHash},
             {"beforeStateHash", ""},
             {"afterStateHash", ""},
             {"decision", "deny"},
             {"recordedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
         }},
        {"error", error},
    };
}

void AutomationDispatcher::remember(
    const QString &operationId, const QString &requestHash, const QJsonObject &response)
{
    if (m_journal.size() >= maximumJournalEntries && !m_journalOrder.isEmpty()) {
        m_journal.remove(m_journalOrder.takeFirst());
    }
    m_journal.insert(operationId, {requestHash, response});
    m_journalOrder.append(operationId);
}

} // namespace EtherCAT::AutomationGateway::Internal
