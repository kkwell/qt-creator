// Copyright (C) 2026 Kvell

#include "automationdispatcher.h"

#include "ethercatautomationgatewayconstants.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

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

static QString semanticSignalAvailabilityName(Data::SemanticSignalAvailability availability)
{
    using Availability = Data::SemanticSignalAvailability;
    switch (availability) {
    case Availability::Unavailable:
        return "unavailable";
    case Availability::Unverified:
        return "unverified";
    case Availability::Stale:
        return "stale";
    case Availability::Ready:
        return "ready";
    case Availability::Rejected:
        return "rejected";
    }
    return "unavailable";
}

static QString semanticActionAvailabilityName(Data::SemanticActionAvailability availability)
{
    using Availability = Data::SemanticActionAvailability;
    switch (availability) {
    case Availability::Unavailable:
        return "unavailable";
    case Availability::Unverified:
        return "unverified";
    case Availability::Stale:
        return "stale";
    case Availability::Ready:
        return "ready";
    case Availability::AwaitingApproval:
        return "awaiting-approval";
    case Availability::Rejected:
        return "rejected";
    }
    return "unavailable";
}

static QString semanticOperationStateName(Data::SemanticOperationState state)
{
    using State = Data::SemanticOperationState;
    switch (state) {
    case State::Rejected:
        return "rejected";
    case State::Submitted:
        return "submitted";
    case State::ApprovalRequired:
        return "approval-required";
    case State::Approved:
        return "approved";
    case State::Executing:
        return "executing";
    case State::Succeeded:
        return "succeeded";
    case State::Failed:
        return "failed";
    case State::TimedOut:
        return "timed-out";
    case State::OutcomeUnknown:
        return "outcome-unknown";
    case State::Canceled:
        return "canceled";
    case State::Expired:
        return "expired";
    }
    return "rejected";
}

static QString runtimeQualityName(Data::RuntimeResourceQualityState state)
{
    using State = Data::RuntimeResourceQualityState;
    switch (state) {
    case State::Unknown:
        return "unknown";
    case State::Good:
        return "good";
    case State::Uncertain:
        return "uncertain";
    case State::Bad:
        return "bad";
    case State::Stale:
        return "stale";
    case State::Unavailable:
        return "unavailable";
    }
    return "unknown";
}

static QString runtimeValueTypeName(Data::RuntimeResourcePrimitiveType type)
{
    using Type = Data::RuntimeResourcePrimitiveType;
    switch (type) {
    case Type::Opaque:
        return "unavailable";
    case Type::Boolean:
        return "boolean";
    case Type::SignedInteger:
        return "signed-integer";
    case Type::UnsignedInteger:
        return "unsigned-integer";
    case Type::FloatingPoint:
        return "floating-point";
    case Type::Text:
        return "text";
    case Type::ByteArray:
        return "bytes";
    }
    return "unavailable";
}

static QJsonObject semanticValueObject(const Data::RuntimeResourceTypedValue &typed)
{
    QJsonObject result{{"type", runtimeValueTypeName(typed.primitiveType)}};
    if (!typed.value.isValid())
        return result;

    using Type = Data::RuntimeResourcePrimitiveType;
    switch (typed.primitiveType) {
    case Type::Boolean:
        result.insert("value", typed.value.toBool());
        break;
    case Type::SignedInteger:
        result.insert("value", QString::number(typed.value.toLongLong()));
        break;
    case Type::UnsignedInteger:
        result.insert("value", QString::number(typed.value.toULongLong()));
        break;
    case Type::FloatingPoint: {
        const double value = typed.value.toDouble();
        if (std::isfinite(value))
            result.insert("value", value);
        break;
    }
    case Type::Text:
        result.insert("value", typed.value.toString());
        break;
    case Type::ByteArray:
        result.insert(
            "value",
            QString::fromLatin1(typed.value.toByteArray().toBase64(
                QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
        result.insert("encoding", "base64url");
        break;
    case Type::Opaque:
        break;
    }
    return result;
}

static bool semanticDigestsMatch(
    const Data::SemanticRuntimeDigest &left, const Data::SemanticRuntimeDigest &right)
{
    return Core::isCanonicalSha256Digest(left) && Core::isCanonicalSha256Digest(right)
           && left == right;
}

static bool semanticContextIsVerified(const Data::SemanticRuntimeContext &context)
{
    return context.complete && !context.controllerId.isEmpty() && context.sessionGeneration != 0
           && context.contextHash.size() == 32
           && context.bindingVerification.state == Data::SemanticBindingVerificationState::Verified
           && Core::isCanonicalSha256Digest(context.bindingVerification.signedManifestDigest)
           && semanticDigestsMatch(context.mappingDigest, context.controllerMappingDigest)
           && Core::isCompleteRuntimeResourceCatalogEpoch(context.epoch);
}

static QString semanticContextHash(const Data::SemanticRuntimeContext &context)
{
    return QString::fromLatin1(context.contextHash.toHex());
}

static std::optional<QByteArray> semanticContextHashFrom(const QJsonValue &value)
{
    static const QRegularExpression expression(QStringLiteral("^[0-9a-fA-F]{64}$"));
    if (!value.isString() || !expression.match(value.toString()).hasMatch())
        return std::nullopt;
    return QByteArray::fromHex(value.toString().toLatin1());
}

static QJsonObject semanticSignalObject(const Data::SemanticSignalRuntimeState &signal)
{
    QJsonObject result{
        {"deviceId", signal.target.deviceId.toString()},
        {"signalId", signal.target.signalId.value},
        {"availability", semanticSignalAvailabilityName(signal.availability)},
        {"quality", runtimeQualityName(signal.quality.state)},
        {"snapshotComplete", signal.snapshotComplete},
        {"captureCycle", QString::number(signal.captureCycle)},
        {"disabledReason",
         signal.availability == Data::SemanticSignalAvailability::Ready
             ? QString{}
             : QStringLiteral("semantic-signal-")
                   + semanticSignalAvailabilityName(signal.availability)},
    };
    if (signal.value)
        result.insert("value", semanticValueObject(*signal.value));
    return result;
}

static QJsonObject semanticActionObject(const Data::SemanticActionRuntimeState &action)
{
    return {
        {"deviceId", action.target.deviceId.toString()},
        {"actionId", action.target.actionId.value},
        {"availability", semanticActionAvailabilityName(action.availability)},
        {"approvalRequired", action.requiresApproval},
        {"holdToRun", action.holdToRun},
        {"maximumTtlMs", int(action.maximumTtlMs)},
        {"disabledReason",
         action.availability == Data::SemanticActionAvailability::Ready
             ? QString{}
             : QStringLiteral("semantic-action-")
                   + semanticActionAvailabilityName(action.availability)},
    };
}

static QJsonObject semanticContextObject(const Data::SemanticRuntimeContext &context)
{
    QList<QJsonObject> signalObjects;
    signalObjects.reserve(context.signalStates.size());
    for (const Data::SemanticSignalRuntimeState &signal : context.signalStates)
        signalObjects.append(semanticSignalObject(signal));
    std::sort(
        signalObjects.begin(),
        signalObjects.end(),
        [](const QJsonObject &left, const QJsonObject &right) {
            const QString leftKey = left.value("deviceId").toString() + QLatin1Char('\n')
                                    + left.value("signalId").toString();
            const QString rightKey = right.value("deviceId").toString() + QLatin1Char('\n')
                                     + right.value("signalId").toString();
            return leftKey < rightKey;
        });
    QJsonArray signalArray;
    for (const QJsonObject &signal : std::as_const(signalObjects))
        signalArray.append(signal);

    QList<QJsonObject> actionObjects;
    actionObjects.reserve(context.actionStates.size());
    for (const Data::SemanticActionRuntimeState &action : context.actionStates)
        actionObjects.append(semanticActionObject(action));
    std::sort(
        actionObjects.begin(),
        actionObjects.end(),
        [](const QJsonObject &left, const QJsonObject &right) {
            const QString leftKey = left.value("deviceId").toString() + QLatin1Char('\n')
                                    + left.value("actionId").toString();
            const QString rightKey = right.value("deviceId").toString() + QLatin1Char('\n')
                                     + right.value("actionId").toString();
            return leftKey < rightKey;
        });
    QJsonArray actions;
    for (const QJsonObject &action : std::as_const(actionObjects))
        actions.append(action);

    return {
        {"controllerId", context.controllerId},
        {"contextHash", semanticContextHash(context)},
        {"complete", context.complete},
        {"bindingVerified", semanticContextIsVerified(context)},
        {"mock", context.mock},
        {"signals", signalArray},
        {"actions", actions},
    };
}

static QJsonObject semanticOperationObject(const Data::SemanticOperationRecord &record)
{
    QJsonObject result{
        {"operationId", record.request.operationId.value},
        {"state", semanticOperationStateName(record.state)},
    };
    if (!record.approvalChallenge.isEmpty())
        result.insert("approvalChallenge", QString::fromLatin1(record.approvalChallenge.toHex()));
    return result;
}

static Data::SemanticRuntimeActor semanticAutomationActor(const AutomationActor &actor)
{
    const QJsonObject identity{
        {"transport", actor.transport},
        {"sessionId", actor.sessionId},
        {"clientName", actor.clientName},
        {"clientVersion", actor.clientVersion},
    };
    const QByteArray digest = QCryptographicHash::hash(
        AutomationDispatcher::canonicalJson(identity), QCryptographicHash::Sha256);
    Data::SemanticRuntimeActor result;
    result.id = "gateway-automation:" + QString::fromLatin1(digest.toHex().left(24));
    result.displayName = actor.clientName.isEmpty() ? QStringLiteral("Loopback automation client")
                                                    : actor.clientName;
    result.kind = Data::SemanticRuntimeActorKind::Automation;
    result.origin = "ethercat-automation-gateway/" + actor.transport;
    result.authenticationDigest = digest;
    return result;
}

static QVariant scalarVariant(const QJsonValue &value)
{
    if (value.isBool())
        return value.toBool();
    if (value.isString())
        return value.toString();
    if (value.isDouble() && std::isfinite(value.toDouble()))
        return value.toDouble();
    return {};
}

static std::optional<QVariant> semanticSignalValue(
    const QJsonValue &value, const Data::SemanticRuntimeBinding &binding)
{
    static constexpr double maximumExactJsonInteger = 9007199254740991.0;
    using Type = Data::RuntimeResourcePrimitiveType;
    switch (binding.primitiveType) {
    case Type::Boolean:
        if (value.isBool())
            return QVariant(value.toBool());
        break;
    case Type::SignedInteger: {
        bool ok = false;
        qlonglong converted = 0;
        if (value.isString())
            converted = value.toString().toLongLong(&ok);
        else if (
            value.isDouble() && std::isfinite(value.toDouble())
            && std::trunc(value.toDouble()) == value.toDouble()
            && value.toDouble() >= -maximumExactJsonInteger
            && value.toDouble() <= maximumExactJsonInteger) {
            converted = qlonglong(value.toDouble());
            ok = true;
        }
        if (ok)
            return QVariant::fromValue(converted);
        break;
    }
    case Type::UnsignedInteger: {
        bool ok = false;
        qulonglong converted = 0;
        if (value.isString())
            converted = value.toString().toULongLong(&ok);
        else if (
            value.isDouble() && std::isfinite(value.toDouble())
            && std::trunc(value.toDouble()) == value.toDouble() && value.toDouble() >= 0
            && value.toDouble() <= maximumExactJsonInteger) {
            converted = qulonglong(value.toDouble());
            ok = true;
        }
        if (ok)
            return QVariant::fromValue(converted);
        break;
    }
    case Type::FloatingPoint:
        if (value.isDouble() && std::isfinite(value.toDouble()))
            return QVariant(value.toDouble());
        break;
    case Type::Text:
        if (value.isString())
            return QVariant(value.toString());
        break;
    case Type::ByteArray:
        if (value.isString()) {
            const QByteArray encoded = value.toString().toLatin1();
            const QByteArray decoded = QByteArray::fromBase64(
                encoded, QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded.isNull())
                return QVariant(decoded);
        }
        break;
    case Type::Opaque:
        break;
    }
    return std::nullopt;
}

AutomationDispatcher::AutomationDispatcher(
    Core::AutomationService *service, Core::SemanticRuntimeService *semanticRuntimeService)
    : m_service(service)
    , m_semanticRuntimeService(semanticRuntimeService)
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
        "runtime.get-context",
        "runtime.operation.get",
        "runtime.operation.request",
        "runtime.read",
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

std::optional<Data::SemanticRuntimeContext> AutomationDispatcher::semanticContext(
    const QString &controllerId) const
{
    if (!m_semanticRuntimeService)
        return std::nullopt;
    return m_semanticRuntimeService->context(controllerId);
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
    if (!supplied.isString())
        return true;
    const QString operationId = supplied.toString();
    if (operationId.isEmpty() || operationId.size() > 128)
        return true;
    return std::any_of(operationId.cbegin(), operationId.cend(), [](QChar character) {
        return character.category() == QChar::Other_Control;
    });
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
    if (tool == "runtime.get-context")
        return {"controllerId", "operationId"};
    if (tool == "runtime.read") {
        return {"contextHash", "controllerId", "deviceId", "operationId", "signalId"};
    }
    if (tool == "runtime.operation.request") {
        return {
            "actionId",
            "contextHash",
            "controllerId",
            "deviceId",
            "operationId",
            "parameters",
            "signalId",
            "ttlMs",
            "value",
        };
    }
    if (tool == "runtime.operation.get")
        return {"operationId", "targetOperationId"};
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
            "Use gateway.get-protocol to negotiate the closed tool catalog.",
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
                {"controllerViews",
                 QJsonObject{
                     {"mockOnly", true},
                     {"readOnly", true},
                     {"directProviderCalls", false},
                 }},
                {"semanticRuntime",
                 QJsonObject{
                     {"available", bool(m_semanticRuntimeService)},
                     {"verifiedContextRead", bool(m_semanticRuntimeService)},
                     {"operationIntentSubmission", bool(m_semanticRuntimeService)},
                     {"approvalRequired", true},
                     {"automationCanApprove", false},
                     {"directProviderCalls", false},
                 }},
                {"toolCatalog", tools},
                {"mutations", mutations},
                {"realTimeBoundary",
                 "Gateway submits semantic intents to the IDE service and never enters the "
                 "125 us cycle."},
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

    if (tool.startsWith("runtime.")) {
        if (!m_semanticRuntimeService) {
            return makeError(
                operationId,
                "CT020_SEMANTIC_RUNTIME_UNAVAILABLE",
                "The IDE semantic runtime service is unavailable.",
                "$.tool",
                "Open a project with a registered SemanticRuntimeService and retry.",
                {
                    {"reason", "semantic-runtime-service-unavailable"},
                    {"providerCalls", 0},
                },
                requestHash,
                actor);
        }

        if (tool == "runtime.operation.get") {
            const QJsonValue targetIdValue = arguments.value("targetOperationId");
            const Data::SemanticOperationId targetOperationId{targetIdValue.toString()};
            if (!targetIdValue.isString()
                || !Core::isCanonicalSemanticOperationId(targetOperationId)) {
                return makeError(
                    operationId,
                    "CT010_BAD_REQUEST",
                    "targetOperationId must contain 1 to 128 characters and no controls.",
                    "$.targetOperationId",
                    "Use the semantic OperationId returned by runtime.operation.request.",
                    {{"providerCalls", 0}},
                    requestHash,
                    actor);
            }

            const std::optional<Data::SemanticOperationRecord> record
                = m_semanticRuntimeService->operation(targetOperationId);
            if (!record) {
                return makeError(
                    operationId,
                    "CT009_NOT_FOUND",
                    "The semantic operation is not present in the IDE operation journal.",
                    "$.targetOperationId",
                    "Refresh the operation using a current semantic OperationId.",
                    {
                        {"reason", "semantic-operation-unavailable"},
                        {"providerCalls", 0},
                    },
                    requestHash,
                    actor);
            }
            if (record->request.operationId != targetOperationId) {
                return makeError(
                    operationId,
                    "CT020_SEMANTIC_RUNTIME_UNAVAILABLE",
                    "The semantic runtime service returned an invalid operation record.",
                    "$.targetOperationId",
                    "Retry after the IDE semantic runtime service is repaired.",
                    {
                        {"reason", "semantic-operation-invalid"},
                        {"providerCalls", 0},
                    },
                    requestHash,
                    actor);
            }
            const QString contextHash = QString::fromLatin1(
                record->request.expectedContextHash.toHex());
            return makeSuccess(
                operationId,
                {{"operation", semanticOperationObject(*record)}},
                {},
                requestHash,
                contextHash,
                contextHash,
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
                "Use a controllerId returned by the IDE semantic runtime context.",
                {{"providerCalls", 0}},
                requestHash,
                actor);
        }

        const std::optional<Data::SemanticRuntimeContext> context = semanticContext(controllerId);
        if (!context) {
            return makeError(
                operationId,
                "CT009_NOT_FOUND",
                "The semantic runtime context is not present in the IDE.",
                "$.controllerId",
                "Open the matching project and refresh its verified runtime binding.",
                {
                    {"reason", "semantic-context-unavailable"},
                    {"providerCalls", 0},
                },
                requestHash,
                actor);
        }
        if (!semanticContextIsVerified(*context)) {
            return makeError(
                operationId,
                "CT021_SEMANTIC_BINDING_UNVERIFIED",
                "The semantic runtime context is not verified and complete.",
                "$.controllerId",
                "Verify the signed semantic binding and refresh the runtime context.",
                {
                    {"reason", "semantic-binding-unverified"},
                    {"providerCalls", 0},
                },
                requestHash,
                actor);
        }
        const QString contextHash = semanticContextHash(*context);

        if (tool == "runtime.get-context") {
            return makeSuccess(
                operationId,
                {{"context", semanticContextObject(*context)}},
                {},
                requestHash,
                contextHash,
                contextHash,
                actor);
        }

        const std::optional<QByteArray> requestedContextHash = semanticContextHashFrom(
            arguments.value("contextHash"));
        if (!requestedContextHash) {
            return makeError(
                operationId,
                "CT010_BAD_REQUEST",
                "contextHash must be a 64-character SHA-256 hexadecimal string.",
                "$.contextHash",
                "Use contextHash from runtime.get-context.",
                {{"providerCalls", 0}},
                requestHash,
                actor);
        }
        if (*requestedContextHash != context->contextHash) {
            return makeError(
                operationId,
                "CT022_CONTEXT_STALE",
                "The semantic runtime context changed.",
                "$.contextHash",
                "Refresh runtime.get-context and require a new approval decision.",
                {
                    {"reason", "context-stale"},
                    {"currentContextHash", contextHash},
                    {"providerCalls", 0},
                },
                requestHash,
                actor);
        }

        const QJsonValue deviceIdValue = arguments.value("deviceId");
        const Data::NodeId deviceId = Data::NodeId::fromString(deviceIdValue.toString());
        if (!deviceIdValue.isString() || deviceId.isNull()) {
            return makeError(
                operationId,
                "CT010_BAD_REQUEST",
                "deviceId must be one semantic device identifier from the current context.",
                "$.deviceId",
                "Use a deviceId returned by runtime.get-context.",
                {{"providerCalls", 0}},
                requestHash,
                actor);
        }

        if (tool == "runtime.read") {
            const QJsonValue signalIdValue = arguments.value("signalId");
            const QString signalId = signalIdValue.toString();
            if (!signalIdValue.isString() || signalId.isEmpty() || signalId.size() > 256) {
                return makeError(
                    operationId,
                    "CT010_BAD_REQUEST",
                    "signalId must contain between 1 and 256 characters.",
                    "$.signalId",
                    "Use a signalId returned by runtime.get-context.",
                    {{"providerCalls", 0}},
                    requestHash,
                    actor);
            }
            QList<Data::SemanticSignalRuntimeState> matches;
            std::copy_if(
                context->signalStates.cbegin(),
                context->signalStates.cend(),
                std::back_inserter(matches),
                [&deviceId, &signalId](const Data::SemanticSignalRuntimeState &candidate) {
                    return candidate.target.deviceId == deviceId
                           && candidate.target.signalId.value == signalId;
                });
            if (matches.size() != 1) {
                return makeError(
                    operationId,
                    "CT023_SEMANTIC_TARGET_UNAVAILABLE",
                    "The semantic signal is not uniquely present in the runtime context.",
                    "$.signalId",
                    "Refresh runtime.get-context and choose one available semantic signal.",
                    {
                        {"reason", "semantic-target-unavailable"},
                        {"providerCalls", 0},
                    },
                    requestHash,
                    actor);
            }

            const Data::SemanticSignalRuntimeState &signal = matches.constFirst();
            const bool bindingValid
                = signal.binding && signal.binding->target == signal.target
                  && signal.binding->sessionGeneration == context->sessionGeneration
                  && signal.binding->epoch == context->epoch
                  && semanticDigestsMatch(signal.binding->mappingDigest, context->mappingDigest)
                  && semanticDigestsMatch(
                      signal.binding->controllerMappingDigest, context->controllerMappingDigest)
                  && Core::validateSemanticRuntimeBinding(*signal.binding).accepted();
            if (!bindingValid) {
                return makeError(
                    operationId,
                    "CT021_SEMANTIC_BINDING_UNVERIFIED",
                    "The semantic signal binding is not verified for this runtime context.",
                    "$.signalId",
                    "Refresh the signed semantic binding before reading the signal.",
                    {
                        {"reason", "semantic-binding-unverified"},
                        {"providerCalls", 0},
                    },
                    requestHash,
                    actor);
            }
            if (signal.availability != Data::SemanticSignalAvailability::Ready
                || !signal.snapshotComplete || !signal.value) {
                return makeError(
                    operationId,
                    "CT023_SEMANTIC_TARGET_UNAVAILABLE",
                    "The semantic signal has no complete runtime value.",
                    "$.signalId",
                    "Refresh the runtime context and inspect the signal disabled reason.",
                    {
                        {"reason", "semantic-target-unavailable"},
                        {"disabledReason",
                         QString(
                             QStringLiteral("semantic-signal-")
                             + semanticSignalAvailabilityName(signal.availability))},
                        {"providerCalls", 0},
                    },
                    requestHash,
                    actor);
            }
            return makeSuccess(
                operationId,
                {
                    {"controllerId", controllerId},
                    {"contextHash", contextHash},
                    {"signal", semanticSignalObject(signal)},
                },
                {},
                requestHash,
                contextHash,
                contextHash,
                actor);
        }

        const QJsonValue signalIdValue = arguments.value("signalId");
        const QJsonValue actionIdValue = arguments.value("actionId");
        const bool hasSignal = signalIdValue.isString() && !signalIdValue.toString().isEmpty();
        const bool hasAction = actionIdValue.isString() && !actionIdValue.toString().isEmpty();
        if (hasSignal == hasAction || (hasSignal && signalIdValue.toString().size() > 256)
            || (hasAction && actionIdValue.toString().size() > 256)) {
            return makeError(
                operationId,
                "CT010_BAD_REQUEST",
                "Exactly one signalId or actionId semantic target is required.",
                "$",
                "Choose one target returned by runtime.get-context.",
                {{"providerCalls", 0}},
                requestHash,
                actor);
        }
        const QJsonValue ttlValue = arguments.value("ttlMs");
        if (!ttlValue.isDouble() || !std::isfinite(ttlValue.toDouble())
            || std::trunc(ttlValue.toDouble()) != ttlValue.toDouble() || ttlValue.toDouble() < 1
            || ttlValue.toDouble() > double(std::numeric_limits<quint32>::max())) {
            return makeError(
                operationId,
                "CT010_BAD_REQUEST",
                "ttlMs must be an integer from 1 through 4294967295.",
                "$.ttlMs",
                "Use a bounded TTL allowed by the selected semantic target.",
                {{"providerCalls", 0}},
                requestHash,
                actor);
        }
        QMap<QString, QVariant> parameters;
        if (arguments.contains("parameters")) {
            if (!arguments.value("parameters").isObject()) {
                return makeError(
                    operationId,
                    "CT010_BAD_REQUEST",
                    "parameters must be an object of scalar semantic values.",
                    "$.parameters",
                    "Remove structured, null, or opaque parameter values.",
                    {{"providerCalls", 0}},
                    requestHash,
                    actor);
            }
            const QJsonObject parameterObject = arguments.value("parameters").toObject();
            for (auto it = parameterObject.begin(); it != parameterObject.end(); ++it) {
                const QVariant converted = scalarVariant(it.value());
                if (it.key().isEmpty() || !converted.isValid()) {
                    return makeError(
                        operationId,
                        "CT010_BAD_REQUEST",
                        "parameters must contain named scalar semantic values.",
                        "$.parameters",
                        "Remove structured, null, non-finite, or unnamed parameters.",
                        {{"providerCalls", 0}},
                        requestHash,
                        actor);
                }
                parameters.insert(it.key(), converted);
            }
        }

        Data::SemanticOperationRequest semanticRequest;
        semanticRequest.operationId = {operationId};
        semanticRequest.expectedEpoch = context->epoch;
        semanticRequest.expectedMappingDigest = context->mappingDigest;
        semanticRequest.expectedControllerMappingDigest = context->controllerMappingDigest;
        semanticRequest.expectedContextHash = context->contextHash;
        semanticRequest.parameters = parameters;
        semanticRequest.ttlMs = quint32(ttlValue.toDouble());

        if (hasSignal) {
            QList<Data::SemanticSignalRuntimeState> matches;
            std::copy_if(
                context->signalStates.cbegin(),
                context->signalStates.cend(),
                std::back_inserter(matches),
                [&deviceId, &signalIdValue](const Data::SemanticSignalRuntimeState &candidate) {
                    return candidate.target.deviceId == deviceId
                           && candidate.target.signalId.value == signalIdValue.toString();
                });
            if (matches.size() != 1 || !matches.constFirst().binding) {
                return makeError(
                    operationId,
                    "CT023_SEMANTIC_TARGET_UNAVAILABLE",
                    "The semantic signal is not uniquely bound for operation submission.",
                    "$.signalId",
                    "Refresh runtime.get-context and choose one ready semantic signal.",
                    {
                        {"reason", "semantic-target-unavailable"},
                        {"providerCalls", 0},
                    },
                    requestHash,
                    actor);
            }
            if (!arguments.contains("value")) {
                return makeError(
                    operationId,
                    "CT010_BAD_REQUEST",
                    "value is required for a semantic signal operation.",
                    "$.value",
                    "Supply a scalar value compatible with the semantic signal.",
                    {{"providerCalls", 0}},
                    requestHash,
                    actor);
            }
            const std::optional<QVariant> value
                = semanticSignalValue(arguments.value("value"), *matches.constFirst().binding);
            if (!value) {
                return makeError(
                    operationId,
                    "CT010_BAD_REQUEST",
                    "value is incompatible with the semantic signal type.",
                    "$.value",
                    "Use the semantic value type returned by runtime.read.",
                    {{"providerCalls", 0}},
                    requestHash,
                    actor);
            }
            semanticRequest.kind = Data::SemanticOperationKind::SetSignalValue;
            semanticRequest.target = matches.constFirst().target;
            semanticRequest.value = *value;
        } else {
            if (arguments.contains("value")) {
                return makeError(
                    operationId,
                    "CT010_BAD_REQUEST",
                    "value is not accepted for a semantic action operation.",
                    "$.value",
                    "Pass named action parameters instead.",
                    {{"providerCalls", 0}},
                    requestHash,
                    actor);
            }
            QList<Data::SemanticActionRuntimeState> matches;
            std::copy_if(
                context->actionStates.cbegin(),
                context->actionStates.cend(),
                std::back_inserter(matches),
                [&deviceId, &actionIdValue](const Data::SemanticActionRuntimeState &candidate) {
                    return candidate.target.deviceId == deviceId
                           && candidate.target.actionId.value == actionIdValue.toString();
                });
            if (matches.size() != 1) {
                return makeError(
                    operationId,
                    "CT023_SEMANTIC_TARGET_UNAVAILABLE",
                    "The semantic action is not uniquely bound for operation submission.",
                    "$.actionId",
                    "Refresh runtime.get-context and choose one ready semantic action.",
                    {
                        {"reason", "semantic-target-unavailable"},
                        {"providerCalls", 0},
                    },
                    requestHash,
                    actor);
            }
            semanticRequest.kind = Data::SemanticOperationKind::InvokeAction;
            semanticRequest.target = matches.constFirst().target;
        }

        const Core::SemanticRuntimeValidation validation
            = Core::validateSemanticOperationRequest(semanticRequest, *context);
        if (!validation.accepted()) {
            const bool bindingFailure
                = validation.error == Core::SemanticRuntimeValidationError::BindingUnverified
                  || validation.error == Core::SemanticRuntimeValidationError::MappingDigestMissing
                  || validation.error == Core::SemanticRuntimeValidationError::MappingDigestMismatch
                  || validation.error == Core::SemanticRuntimeValidationError::InvalidBinding;
            return makeError(
                operationId,
                bindingFailure ? "CT021_SEMANTIC_BINDING_UNVERIFIED"
                               : "CT023_SEMANTIC_TARGET_UNAVAILABLE",
                bindingFailure
                    ? QStringLiteral(
                          "The semantic target binding is not verified for this operation.")
                    : QStringLiteral("The semantic target is not available for this operation."),
                hasSignal ? "$.signalId" : "$.actionId",
                "Refresh runtime.get-context and satisfy the target's approval policy.",
                {
                    {"reason",
                     bindingFailure ? "semantic-binding-unverified" : "semantic-target-unavailable"},
                    {"disabledReason",
                     bindingFailure ? "semantic-binding-unverified" : "semantic-target-unavailable"},
                    {"providerCalls", 0},
                },
                requestHash,
                actor);
        }

        const Data::SemanticRuntimeActor semanticActor = semanticAutomationActor(actor);
        const Data::SemanticOperationRecord record
            = m_semanticRuntimeService->submit(semanticRequest, semanticActor);
        if (record.request.operationId != semanticRequest.operationId
            || record.request.target != semanticRequest.target || record.actor != semanticActor) {
            return makeError(
                operationId,
                "CT020_SEMANTIC_RUNTIME_UNAVAILABLE",
                "The semantic runtime service returned an invalid operation record.",
                "$",
                "Retry after the IDE semantic runtime service is repaired.",
                {
                    {"reason", "semantic-operation-invalid"},
                    {"providerCalls", 0},
                },
                requestHash,
                actor);
        }
        return makeSuccess(
            operationId,
            {{"operation", semanticOperationObject(record)}},
            {},
            requestHash,
            contextHash,
            contextHash,
            actor,
            record.state == Data::SemanticOperationState::Rejected
                ? QStringLiteral("semantic-intent-rejected")
                : QStringLiteral("semantic-intent-submitted"));
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
    const AutomationActor &actor,
    const QString &auditDecision) const
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
             {"decision", auditDecision},
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
