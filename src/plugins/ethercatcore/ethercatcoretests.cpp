// Copyright (C) 2026 Kvell

#include "ethercatcoretests.h"

#include "automationservice.h"
#include "ethercatcoreconstants.h"
#include "ethercatcoresettings.h"
#include "ethercatcoretr.h"
#include "manualcontrolcontract.h"
#include "providerregistry.h"
#include "providers.h"
#include "selectionservice.h"
#include "semanticruntimeservice.h"
#include "stateservice.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <ethercatdata/controllerconnection.h>
#include <ethercatdata/deviceadapter.h>
#include <ethercatdata/engineeringvalue.h>
#include <ethercatdata/manualcontrol.h>
#include <ethercatdata/nodeid.h>
#include <ethercatdata/offlineconfiguration.h>
#include <ethercatdata/projectsnapshot.h>
#include <ethercatdata/runtimeoutputtransaction.h>
#include <ethercatdata/runtimeresource.h>
#include <ethercatdata/semanticmappingattestation.h>
#include <ethercatdata/semanticruntime.h>

#include <QScopeGuard>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QWidget>

#include <algorithm>
#include <array>
#include <limits>
#include <type_traits>

namespace EtherCAT::Core::Internal {

class TestAutomationService final : public AutomationService
{
public:
    using AutomationService::AutomationService;

    QList<AutomationContextSnapshot> contexts() const final
    {
        ++readCount;
        return snapshots;
    }

    mutable int readCount = 0;
    QList<AutomationContextSnapshot> snapshots;
};

class TestSemanticRuntimeService final : public SemanticRuntimeService
{
public:
    using SemanticRuntimeService::SemanticRuntimeService;

    QList<Data::SemanticRuntimeContext> contexts() const final
    {
        ++readCount;
        return snapshots;
    }

    mutable int readCount = 0;
    QList<Data::SemanticRuntimeContext> snapshots;
};

struct SemanticRuntimeFixture
{
    Data::ControllerConnectionScope scope{Data::NodeId::create(), Data::NodeId::create()};
    Data::NodeId deviceId = Data::NodeId::create();
    Data::RuntimeResourceCatalogEpoch epoch;
    Data::SemanticRuntimeDigest digest;
    Data::SemanticRuntimeTarget target;
    Data::SemanticRuntimeBinding binding;
    Data::RuntimeResourceCatalog catalog;
    Data::RuntimeResourceSnapshot snapshot;
    Data::SemanticRuntimeContext context;
    Data::SemanticRuntimeActor actor;
    Data::SemanticOperationRequest request;

    SemanticRuntimeFixture()
    {
        epoch.controllerBootId = 11;
        epoch.activePackageSlot = Data::ControllerSlot::B;
        epoch.activePackageGeneration = 12;
        epoch.configurationId = 13;
        epoch.topologyGeneration = 14;
        epoch.runtimeGeneration = 15;
        epoch.catalogRevision = 16;
        epoch.topologyIdentity = QByteArray::fromHex("0102030405060708");

        digest.algorithm = "sha256";
        digest.value = QByteArray(32, '\x5a');

        target.controllerId = "ide:test-controller";
        target.scope = scope;
        target.deviceId = deviceId;
        target.kind = Data::SemanticRuntimeTargetKind::Signal;
        target.signalId = {"urn:example.test:signal/input.1"};

        binding.target = target;
        binding.semanticBindingId = "binding:test:input.1";
        binding.componentBindingId = "component:test:device.1";
        binding.adapterId = {"org.example.test.adapter/runtime"};
        binding.adapterVersion = "1.0.0";
        binding.adapterContentSha256 = QByteArray(32, '\x21');
        binding.esiSha256 = QByteArray(32, '\x32');
        binding.bindingArtifactSha256 = QByteArray(32, '\x43');
        binding.sessionGeneration = 7;
        binding.epoch = epoch;
        binding.mappingDigest = digest;
        binding.controllerMappingDigest = digest;
        binding.verification.state = Data::SemanticBindingVerificationState::Verified;
        binding.verification.verifierId = "test-verifier";
        binding.verification.signedManifestDigest = {"sha256", QByteArray(32, '\x6b')};
        binding.verification.verifiedAt = QDateTime::currentDateTimeUtc();
        binding.resourceId = {QByteArray::fromHex("1000000000000001")};
        binding.componentInstanceId = {QByteArray::fromHex("2000000000000001")};
        binding.consistencyGroupId = {QByteArray::fromHex("3000000000000001")};
        binding.primitiveType = Data::RuntimeResourcePrimitiveType::Boolean;
        binding.valueTypeIdentity = "ethercat.runtime.value/primitive-1/bits-1";
        binding.bitWidth = 1;
        binding.direction = Data::RuntimeResourceDirection::Bidirectional;
        binding.access = Data::RuntimeResourceAccess::ReadWrite;

        Data::RuntimeResourceDescriptor descriptor;
        descriptor.id = binding.resourceId;
        descriptor.componentInstanceId = binding.componentInstanceId;
        descriptor.consistencyGroupId = binding.consistencyGroupId;
        descriptor.displayName = "Input 1";
        descriptor.primitiveType = binding.primitiveType;
        descriptor.valueTypeIdentity = binding.valueTypeIdentity;
        descriptor.bitWidth = binding.bitWidth;
        descriptor.direction = binding.direction;
        descriptor.access = binding.access;
        descriptor.processImageBitOffset = 23;
        descriptor.processImageBitLength = 1;

        catalog.scope = scope;
        catalog.sessionGeneration = binding.sessionGeneration;
        catalog.epoch = epoch;
        catalog.receivedAt = QDateTime::currentDateTimeUtc();
        catalog.resources = {descriptor};

        Data::RuntimeResourceSample sample;
        sample.resourceId = binding.resourceId;
        sample.consistencyGroupId = binding.consistencyGroupId;
        sample.value.primitiveType = binding.primitiveType;
        sample.value.typeIdentity = binding.valueTypeIdentity;
        sample.value.value = true;
        sample.quality.state = Data::RuntimeResourceQualityState::Good;
        sample.valueSequence = 9;
        sample.controllerTimestampNs = 123456;

        snapshot.scope = scope;
        snapshot.sessionGeneration = binding.sessionGeneration;
        snapshot.epoch = epoch;
        snapshot.snapshotSequence = 10;
        snapshot.captureCycle = 101;
        snapshot.controllerTimestampNs = sample.controllerTimestampNs;
        snapshot.receivedAt = catalog.receivedAt;
        snapshot.complete = true;
        snapshot.samples = {sample};

        Data::SemanticSignalRuntimeState signal;
        signal.target = target;
        signal.definition.id = target.signalId;
        signal.definition.displayName = "Input 1";
        signal.definition.direction = Data::SemanticSignalDirection::Bidirectional;
        signal.definition.access = Data::SemanticSignalAccess::ReadWrite;
        signal.definition.manualControl.allowed = true;
        signal.definition.manualControl.holdToRun = true;
        signal.definition.manualControl.commandTimeoutMs = 250;
        signal.availability = Data::SemanticSignalAvailability::Ready;
        signal.binding = binding;
        signal.value = sample.value;
        signal.quality = sample.quality;
        signal.snapshotComplete = true;
        signal.captureCycle = snapshot.captureCycle;
        signal.controllerTimestampNs = snapshot.controllerTimestampNs;

        context.controllerId = target.controllerId;
        context.scope = scope;
        context.sessionGeneration = binding.sessionGeneration;
        context.epoch = epoch;
        context.mappingDigest = digest;
        context.controllerMappingDigest = digest;
        context.bindingVerification = binding.verification;
        context.contextHash = QByteArray(32, '\x7d');
        context.signalStates = {signal};
        context.complete = true;
        context.mock = false;

        actor.id = "user:test";
        actor.displayName = "Test User";
        actor.kind = Data::SemanticRuntimeActorKind::User;
        actor.origin = "qt-test";
        actor.authenticationDigest = QByteArray(32, '\x19');

        request.operationId = {"gateway-operation-001"};
        request.kind = Data::SemanticOperationKind::SetSignalValue;
        request.target = target;
        request.expectedEpoch = epoch;
        request.expectedMappingDigest = digest;
        request.expectedControllerMappingDigest = digest;
        request.expectedContextHash = context.contextHash;
        request.value = true;
        request.parameters.insert("mode", "manual");
        request.ttlMs = 250;
        request.reason = "test";
    }
};

struct RuntimeOutputFixture
{
    Data::ControllerConnectionScope scope{Data::NodeId::create(), Data::NodeId::create()};
    Data::RuntimeResourceCatalogEpoch epoch;
    Data::RuntimeConsistencyGroupId groupId{QByteArray::fromHex("00000055")};
    QByteArray mappingDigest = QByteArray(32, '\x33');
    QByteArray groupRecordDigest = QByteArray(32, '\x44');
    Data::RuntimeOutputOperationId operationId{
        QByteArray::fromHex("00112233445566778899aabbccddeeff")};
    Data::RuntimeOutputGroupPolicyRequest policyRequest;
    Data::RuntimeOutputGroupPolicy policy;
    Data::RuntimeOutputTransactionStateRequest stateRequest;
    Data::RuntimeOutputTransactionState idleState;
    Data::RuntimeOutputTransactionRequest transactionRequest;
    Data::RuntimeOutputTransactionState appliedState;

    RuntimeOutputFixture()
    {
        epoch.controllerBootId = 0x2122232425262728;
        epoch.activePackageSlot = Data::ControllerSlot::A;
        epoch.activePackageGeneration = 7;
        epoch.configurationId = 0x100;
        epoch.topologyGeneration = 9;
        epoch.runtimeGeneration = 11;
        epoch.catalogRevision = 13;
        epoch.topologyIdentity = QByteArray::fromHex("6000000000000001");

        policyRequest.correlationId = "output-policy-1";
        policyRequest.scope = scope;
        policyRequest.sessionGeneration = 3;
        policyRequest.expectedEpoch = epoch;
        policyRequest.consistencyGroupId = groupId;
        policyRequest.expectedMappingDigest = mappingDigest;

        policy.scope = scope;
        policy.sessionGeneration = policyRequest.sessionGeneration;
        policy.epoch = epoch;
        policy.consistencyGroupId = groupId;
        policy.mappingDigest = mappingDigest;
        policy.completeGroupRecordDigest = groupRecordDigest;
        policy.recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::ReturnTask;
        policy.maximumTtlCycles = 1000;
        policy.completeResourceCount = 2;
        policy.currentOutputGeneration = 9;
        policy.manualWriteAllowed = true;
        policy.receivedAt = QDateTime::currentDateTimeUtc();

        stateRequest.correlationId = "output-state-1";
        stateRequest.scope = scope;
        stateRequest.sessionGeneration = policyRequest.sessionGeneration;
        stateRequest.expectedEpoch = epoch;
        stateRequest.expectedMappingDigest = mappingDigest;

        idleState.scope = scope;
        idleState.sessionGeneration = stateRequest.sessionGeneration;
        idleState.epoch = epoch;
        idleState.mappingDigest = mappingDigest;
        idleState.state = Data::RuntimeOutputState::Idle;
        idleState.outputGeneration = policy.currentOutputGeneration;
        idleState.controllerTimestampNs = 1000;
        idleState.receivedAt = policy.receivedAt;

        Data::RuntimeOutputValueWrite first;
        first.resourceId.value = QByteArray::fromHex("1000000000000001");
        first.bitWidth = 8;
        first.value.primitiveType = Data::RuntimeResourcePrimitiveType::UnsignedInteger;
        first.value.value = QVariant::fromValue<qulonglong>(1);
        first.value.typeIdentity = "u8";

        Data::RuntimeOutputValueWrite second = first;
        second.resourceId.value = QByteArray::fromHex("1000000000000002");
        second.value.value = QVariant::fromValue<qulonglong>(2);

        transactionRequest.operationId = operationId;
        transactionRequest.scope = scope;
        transactionRequest.sessionGeneration = policyRequest.sessionGeneration;
        transactionRequest.expectedEpoch = epoch;
        transactionRequest.expectedMappingDigest = mappingDigest;
        transactionRequest.expectedCompleteGroupRecordDigest = groupRecordDigest;
        transactionRequest.expectedCompleteResourceCount = 2;
        transactionRequest.expectedRecoveryPolicy = policy.recoveryPolicy;
        transactionRequest.expectedMaximumTtlCycles = policy.maximumTtlCycles;
        transactionRequest.expectedOutputGeneration = policy.currentOutputGeneration;
        transactionRequest.ttlCycles = 5;
        transactionRequest.consistencyGroupId = groupId;
        transactionRequest.completeGroupWrites = {first, second};

        appliedState = idleState;
        appliedState.state = Data::RuntimeOutputState::OverrideActive;
        appliedState.resultFlags = Data::RuntimeOutputTransactionResultFlag::OverrideActive;
        appliedState.operationId = operationId;
        appliedState.appliedCycle = 100;
        appliedState.expiryCycle = 105;
        appliedState.outputGeneration = 10;
        appliedState.consistencyGroupId = groupId;
        appliedState.ttlCycles = transactionRequest.ttlCycles;
        appliedState.recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::ReturnTask;
        appliedState.valueCount = 2;
        appliedState.controllerTimestampNs = 2000;
    }
};

class TestDeviceImportJob final : public DeviceImportJob
{
public:
    using DeviceImportJob::DeviceImportJob;

    void start()
    {
        setState(DeviceImportState::Running);
        setProgress(1, 2);
    }

    void cancel() final
    {
        setState(DeviceImportState::Canceling);
        Data::DeviceImportResult result;
        result.requestedFiles = 2;
        result.canceled = true;
        finish(result);
    }
};

static Data::DeviceAdapterManifest testDeviceAdapterManifest(
    Data::DeviceAdapterQualification qualification = Data::DeviceAdapterQualification::Qualified,
    const QString &version = "2.1.0")
{
    Data::DeviceSignalBinding binding;
    binding.kind = Data::DeviceSignalBindingKind::ProcessDataObject;
    binding.pdoDirection = Data::PdoDirection::Rx;
    binding.pdoIndex = 0x1600;
    binding.objectIndex = 0x7001;
    binding.objectSubIndex = 0;
    binding.physicalType = Data::EtherCATDataType::Integer32;
    binding.bitWidth = 32;
    binding.byteOrder = Data::DeviceByteOrder::LittleEndian;

    Data::SemanticSignalDefinition signal;
    signal.id = {"urn:example.test:signal/custom.axis.target-velocity"};
    signal.displayName = "Target velocity";
    signal.description = "Adapter-owned velocity command";
    signal.capabilities
        = {{"example.test.capability/axis.velocity"},
           {"example.test.capability/custom-diagnostics"}};
    signal.direction = Data::SemanticSignalDirection::Output;
    signal.access = Data::SemanticSignalAccess::WriteOnly;
    signal.bindings = {binding};
    signal.valueMetadata.unit = "rpm";
    signal.valueMetadata.scale = 0.1;
    signal.valueMetadata.offset = -1.0;
    signal.valueMetadata.hasMinimum = true;
    signal.valueMetadata.minimum = -3000.0;
    signal.valueMetadata.hasMaximum = true;
    signal.valueMetadata.maximum = 3000.0;
    signal.valueMetadata.hasStep = true;
    signal.valueMetadata.step = 0.1;
    signal.valueMetadata.enumValues = {{0, "stopped", "Stopped"}};
    signal.hasSafeValue = true;
    signal.safeValue = 0;
    signal.manualControl.policyId = "manual.axis.hold-to-run";
    signal.manualControl.allowed = true;
    signal.manualControl.requiresExclusiveControl = true;
    signal.manualControl.holdToRun = true;
    signal.manualControl.commandTimeoutMs = 250;
    signal.manualControl.timeoutAction = Data::ManualControlTimeoutAction::ControlledStop;

    Data::DeviceAdapterManifest manifest;
    manifest.id = {"org.example.test.adapter/custom-drive"};
    manifest.version = version;
    manifest.displayName = "Vendor Example Drive";
    manifest.description = "Test-only semantic adapter";
    manifest.qualification = qualification;
    manifest.matchPriority = 120;
    manifest.match.vendorId = 0x00a1b2c3;
    manifest.match.productCode = 0x01020304;
    manifest.match.minimumRevision = 0x00020003;
    manifest.match.maximumRevision = 0x00020003;
    manifest.match.exactEsiSha256 = QByteArray::fromHex(
        "1111111111111111111111111111111111111111111111111111111111111111");
    manifest.capabilities = signal.capabilities;
    manifest.semanticSignals = {signal};
    manifest.provenance.sourceId = "test.adapter.catalog";
    manifest.provenance.sourceVersion = "2026.07";
    manifest.provenance.sourceLocation = "tests/custom-drive.adapter.json";
    manifest.provenance.sourceSha256 = QByteArray::fromHex(
        "2222222222222222222222222222222222222222222222222222222222222222");
    manifest.contentSha256 = QByteArray::fromHex(
        "3333333333333333333333333333333333333333333333333333333333333333");
    manifest.evidenceSha256 = QByteArray::fromHex(
        "4444444444444444444444444444444444444444444444444444444444444444");
    manifest.signatureVerified = qualification == Data::DeviceAdapterQualification::Qualified;
    manifest.realHardwareAllowed = qualification == Data::DeviceAdapterQualification::Qualified;
    return manifest;
}

class TestDeviceAdapterProvider final : public DeviceAdapterProvider
{
public:
    explicit TestDeviceAdapterProvider(const QList<Data::DeviceAdapterManifest> &manifests)
        : DeviceAdapterProvider("EtherCAT.DeviceAdapter.Test", "Test device adapters")
        , m_manifests(manifests)
    {}

    QList<Data::DeviceAdapterManifest> adapterManifests() const final { return m_manifests; }

    std::optional<Data::DeviceAdapterManifest> adapterManifest(
        const Data::DeviceAdapterId &adapterId, const QString &version) const final
    {
        const auto found = std::find_if(
            m_manifests.cbegin(),
            m_manifests.cend(),
            [&adapterId, &version](const Data::DeviceAdapterManifest &manifest) {
                return manifest.id == adapterId && manifest.version == version;
            });
        if (found == m_manifests.cend())
            return std::nullopt;
        return *found;
    }

    Data::DeviceAdapterResolutionResult resolveDevice(
        const Data::DeviceAdapterResolutionRequest &request) const final
    {
        lastResolutionRequest = request;
        ++resolutionCount;
        return resolutionResult;
    }

    void replaceManifests(const QList<Data::DeviceAdapterManifest> &manifests)
    {
        m_manifests = manifests;
        emit adapterManifestsChanged();
    }

    mutable Data::DeviceAdapterResolutionRequest lastResolutionRequest;
    mutable int resolutionCount = 0;
    Data::DeviceAdapterResolutionResult resolutionResult;

private:
    QList<Data::DeviceAdapterManifest> m_manifests;
};

void EtherCATCoreTests::testAutomationServiceValueLookup()
{
    TestAutomationService service;
    AutomationContextSnapshot snapshot;
    snapshot.scope = {Data::NodeId::create(), Data::NodeId::create()};
    snapshot.controllerId = automationControllerId(snapshot.scope);
    snapshot.identitySource = "ide-project-master";
    snapshot.project.id = snapshot.scope.projectId;
    snapshot.project.valid = true;
    snapshot.connection.scope = snapshot.scope;
    snapshot.mock = true;
    service.snapshots = {snapshot};

    const std::optional<AutomationContextSnapshot> first = service.context(snapshot.controllerId);
    QVERIFY(first);
    QCOMPARE(*first, snapshot);
    QCOMPARE(service.readCount, 1);

    service.snapshots.clear();
    QVERIFY(!service.context(snapshot.controllerId));
    QCOMPARE(service.readCount, 2);
    QCOMPARE(snapshot.identitySource, "ide-project-master");
}

class TestPropertyPageProvider final : public PropertyPageProvider
{
public:
    TestPropertyPageProvider()
        : PropertyPageProvider("EtherCAT.Test.Pages", "Test pages")
    {}

    QList<PropertyPageDescriptor> pages(const PropertyPageContext &context) const final
    {
        if (context.nodeKind != WorkbenchNodeKind::Device)
            return {};
        return {{Utils::Id("EtherCAT.Test.General"), "General", 10}};
    }

    QWidget *createPage(Utils::Id pageId, QWidget *parent) final
    {
        if (pageId != Utils::Id("EtherCAT.Test.General"))
            return nullptr;
        return new QWidget(parent);
    }

    void updatePage(Utils::Id pageId, QWidget *page, const PropertyPageContext &context) final
    {
        if (pageId == Utils::Id("EtherCAT.Test.General") && page)
            page->setObjectName(context.nodeId.toString());
    }
};

class TestControllerConnectionProvider final : public ControllerConnectionProvider
{
public:
    TestControllerConnectionProvider()
        : TestControllerConnectionProvider(
              "EtherCAT.Connection.Test",
              "Test controller",
              "192.168.3.101:15200",
              {"control", "events", "bulk"})
    {}

    TestControllerConnectionProvider(
        Utils::Id id,
        const QString &displayName,
        const QString &endpointSummary,
        const QStringList &channelIds,
        Data::NodeId profileId = {})
        : ControllerConnectionProvider(id, displayName)
        , m_channelIds(channelIds)
    {
        m_profile.id = profileId.isNull() ? Data::NodeId::create() : profileId;
        m_profile.displayName = displayName + " default";
        m_profile.endpointSummary = endpointSummary;
        m_profile.configured = true;
        m_profile.supported = true;
        m_profile.defaultProfile = true;
    }

    QList<Data::ControllerConnectionProfile> connectionProfiles(
        const Data::ControllerConnectionScope &scope) const final
    {
        if (scope.projectId.isNull() || scope.masterId.isNull())
            return {};
        return {m_profile};
    }

    Data::ControllerConnectionSnapshot connectionSnapshot() const final { return m_snapshot; }

    Utils::Result<> connectToController(const Data::ControllerConnectionRequest &request) final
    {
        if (!isAvailable())
            return Utils::ResultError("Connection provider is unavailable");
        if (request.scope.projectId.isNull() || request.scope.masterId.isNull()
            || request.profileId != m_profile.id || !m_profile.configured || !m_profile.supported)
            return Utils::ResultError("Invalid controller profile");
        if (m_snapshot.state != Data::ControllerConnectionState::Disconnected)
            return Utils::ResultError("Connection provider is busy");

        m_snapshot = {};
        m_snapshot.scope = request.scope;
        m_snapshot.profileId = request.profileId;
        m_snapshot.endpointSummary = m_profile.endpointSummary;
        m_snapshot.state = Data::ControllerConnectionState::Connecting;
        m_snapshot.readOnly = true;
        m_snapshot.sessionGeneration = ++m_generation;
        for (qsizetype index = 0; index < m_channelIds.size(); ++index) {
            const QString &channelId = m_channelIds.at(index);
            m_snapshot.channels.append(
                {channelId,
                 channelId.toUpper(),
                 index == 0 ? Data::ControllerChannelState::Connecting
                            : Data::ControllerChannelState::Disconnected,
                 channelId == "control" ? 4096 : 65536,
                 {},
                 index == 0 ? QString("Connecting") : QString()});
        }
        emit connectionSnapshotChanged();
        return Utils::ResultOk;
    }

    Utils::Result<> disconnectFromController() final
    {
        if (m_snapshot.state == Data::ControllerConnectionState::Disconnected)
            return Utils::ResultOk;
        m_snapshot.state = Data::ControllerConnectionState::Disconnected;
        m_snapshot.sessionGeneration = ++m_generation;
        m_snapshot.session.reset();
        m_snapshot.lastHeartbeatAt = {};
        for (Data::ControllerChannelStatus &channel : m_snapshot.channels)
            channel.state = Data::ControllerChannelState::Disconnected;
        emit connectionSnapshotChanged();
        return Utils::ResultOk;
    }

    Utils::Result<> refreshController() final
    {
        if (!isAvailable())
            return Utils::ResultError("Connection provider is unavailable");
        if (m_snapshot.state != Data::ControllerConnectionState::Connected
            && m_snapshot.state != Data::ControllerConnectionState::Degraded) {
            return Utils::ResultError("Controller is not connected");
        }
        m_snapshot.updatedAt = QDateTime::currentDateTimeUtc();
        emit connectionSnapshotChanged();
        return Utils::ResultOk;
    }

    void setProfileConfigured(bool configured)
    {
        if (m_profile.configured == configured)
            return;
        m_profile.configured = configured;
        m_profile.configurationIssue = configured ? QString()
                                                  : QString("Controller profile is incomplete");
        emit connectionProfilesChanged();
    }

    void setProfileSupported(bool supported)
    {
        if (m_profile.supported == supported)
            return;
        m_profile.supported = supported;
        m_profile.configurationIssue = supported ? QString()
                                                 : QString("Controller profile is not supported");
        emit connectionProfilesChanged();
    }

    void completeHandshake()
    {
        m_snapshot.state = Data::ControllerConnectionState::Connected;
        m_snapshot.protocolVersion = Data::ControllerProtocolVersion{1, 9};
        m_snapshot.connectedAt = QDateTime::currentDateTimeUtc();
        m_snapshot.updatedAt = m_snapshot.connectedAt;
        m_snapshot.lastHeartbeatAt = m_snapshot.connectedAt;
        m_snapshot.session = Data::ControllerSessionSummary{17, 42, 0, 5000, false};
        for (Data::ControllerChannelStatus &channel : m_snapshot.channels) {
            channel.state = Data::ControllerChannelState::Connected;
            channel.lastActivityAt = m_snapshot.connectedAt;
            channel.detail.clear();
        }

        Data::ControllerStateSummary state;
        state.serviceState = Data::ControllerServiceState::Shutdown;
        state.severity = Data::ControllerSeverity::None;
        state.ready = true;
        state.controllerBootId = 42;
        m_snapshot.controllerState = state;

        Data::ControllerCapabilitySummary capability;
        capability.maximumSlaves = 64;
        capability.minimumCycleTimeNs = 125000;
        capability.maximumCyclicFrames = 8;
        capability.maximumProcessInputBytes = 4096;
        capability.maximumProcessOutputBytes = 4096;
        capability.descriptorSha256 = QByteArray::fromHex(
            "74ea5e67b3e1d7ba575339b636abb5432ecf6790d3504d32ce1756bcfec49568");
        capability.controlLease = true;
        capability.resumablePush = true;
        capability.transactionalBulk = true;
        capability.capabilityQuery = true;
        capability.exactAlarmReplay = true;
        capability.linkDiagnostics = true;
        capability.timeCorrelation = true;
        capability.firmwareUpdate = true;
        capability.coe = true;
        capability.distributedClocks = true;
        m_snapshot.capability = capability;

        Data::ControllerPackageSummary package;
        package.stagedSlot = Data::ControllerSlot::A;
        package.stagedGeneration = 11;
        package.stagedConfigurationId = 810;
        package.activeSlot = Data::ControllerSlot::A;
        package.activeGeneration = 11;
        package.activeConfigurationId = 810;
        package.controllerState = Data::ControllerPackageState::Empty;
        package.controllerBootId = 42;
        m_snapshot.package = package;

        Data::ControllerFirmwareSummary firmware;
        firmware.state = Data::ControllerFirmwareState::Confirmed;
        firmware.activeSlot = Data::ControllerSlot::A;
        firmware.previousSlot = Data::ControllerSlot::B;
        firmware.targetSlot = Data::ControllerSlot::A;
        firmware.packageVerified = true;
        firmware.confirmed = true;
        firmware.progressPerMille = 1000;
        firmware.generation = 2517;
        firmware.updatedAt = m_snapshot.connectedAt;
        m_snapshot.firmware = firmware;
        emit connectionSnapshotChanged();
    }

    void degradePush()
    {
        m_snapshot.state = Data::ControllerConnectionState::Degraded;
        m_snapshot.channels[1].state = Data::ControllerChannelState::Failed;
        m_snapshot.channels[1].detail = "Push channel unavailable";
        emit connectionSnapshotChanged();
    }

    void failProtocol()
    {
        Data::ControllerOperationError error;
        error.source = Data::ControllerErrorSource::Protocol;
        error.channelId = "events";
        error.operation = Data::ControllerOperation::SubscribeEvents;
        error.codeName = "UNEXPECTED_PUSH_FRAME";
        error.requestId = 73;
        error.occurredAt = QDateTime::currentDateTimeUtc();
        error.retryDisposition = Data::ControllerRetryDisposition::Reconnect;
        error.summary = "Unexpected push frame";
        m_snapshot.state = Data::ControllerConnectionState::Failed;
        m_snapshot.lastError = error;
        emit connectionSnapshotChanged();
    }

private:
    Data::ControllerConnectionProfile m_profile;
    QStringList m_channelIds;
    Data::ControllerConnectionSnapshot m_snapshot;
    quint64 m_generation = 0;
};

class TestScanProvider final : public ScanProvider
{
public:
    TestScanProvider()
        : ScanProvider("EtherCAT.Scan.Test", "Test scanner")
    {}

    Data::ScanState scanState() const final { return m_progress.state; }
    Data::ScanProgress scanProgress() const final { return m_progress; }
    std::optional<Data::ScanResult> lastScanResult() const final { return m_result; }
    QString lastScanError() const final { return m_error; }

    Utils::Result<> startScan(const Data::ScanRequest &request) final
    {
        if (request.projectId.isNull() || request.masterId.isNull())
            return Utils::ResultError("Invalid scan request");
        if (m_progress.state != Data::ScanState::Idle)
            return Utils::ResultError("Scanner is busy");
        m_request = request;
        m_progress = {Data::ScanState::Preparing, 1, 4, 0, "Preparing"};
        emit scanStateChanged(m_progress.state);
        emit scanProgressChanged(m_progress);
        return Utils::ResultOk;
    }

    void complete()
    {
        Data::ScannedSlave slave;
        slave.id = Data::NodeId::create();
        slave.position = 0;
        slave.identity = {2, 0x1234, 1};
        slave.serialNumber = 17;
        slave.name = "Mock Slave";

        Data::ScanResult result;
        result.snapshot.id = Data::NodeId::create();
        result.snapshot.projectId = m_request.projectId;
        result.snapshot.masterId = m_request.masterId;
        result.snapshot.slaves = {slave};
        result.snapshot.complete = true;
        result.snapshot.mock = true;
        result.comparison.projectId = m_request.projectId;
        result.comparison.masterId = m_request.masterId;
        result.comparison.differences = {
            {Data::TopologyDifferenceKind::Added,
             Data::DifferenceSeverity::Information,
             {},
             slave.id,
             -1,
             0,
             "Added slave",
             "Mock Slave"}};
        result.comparison.acceptAllowed = true;
        m_result = result;
        m_progress = {Data::ScanState::Completed, 4, 4, 1, "Completed"};
        emit scanProgressChanged(m_progress);
        emit scanResultChanged();
        emit scanStateChanged(m_progress.state);
        emit scanFinished(m_progress.state);
    }

    void cancelScan() final
    {
        if (m_progress.state == Data::ScanState::Idle)
            return;
        m_progress.state = Data::ScanState::Cancelled;
        emit scanStateChanged(m_progress.state);
        emit scanFinished(m_progress.state);
    }

    void clearScanResult() final
    {
        m_result.reset();
        m_error.clear();
        m_progress = {};
        emit scanResultChanged();
        emit scanStateChanged(m_progress.state);
    }

private:
    Data::ScanProgress m_progress;
    Data::ScanRequest m_request;
    std::optional<Data::ScanResult> m_result;
    QString m_error;
};

class TestDiagnosticsProvider final : public DiagnosticsProvider
{
public:
    TestDiagnosticsProvider()
        : DiagnosticsProvider("EtherCAT.Diagnostics.Test", "Test diagnostics")
    {
        m_limits = {4, 5, 10, 100};
    }

    Data::DiagnosticsStreamState streamState() const final { return m_state; }
    Data::DiagnosticsRequest activeRequest() const final { return m_request; }
    std::optional<Data::DiagnosticsSnapshot> latestSnapshot() const final { return m_snapshot; }
    QList<Data::DiagnosticEvent> events() const final { return m_events; }
    QList<Data::DiagnosticTrendSample> trendSamples() const final { return m_trend; }
    Data::DiagnosticsLimits limits() const final { return m_limits; }
    QString lastDiagnosticsError() const final { return m_error; }

    Utils::Result<> startMonitoring(const Data::DiagnosticsRequest &request) final
    {
        if (request.projectId.isNull() || request.masterId.isNull())
            return Utils::ResultError("Invalid diagnostics request");
        if (m_state != Data::DiagnosticsStreamState::Stopped)
            return Utils::ResultError("Diagnostics provider is busy");
        m_request = request;
        m_state = Data::DiagnosticsStreamState::Starting;
        emit streamStateChanged(m_state);
        m_state = Data::DiagnosticsStreamState::Running;
        emit streamStateChanged(m_state);
        return Utils::ResultOk;
    }

    void stopMonitoring() final
    {
        if (m_state == Data::DiagnosticsStreamState::Stopped)
            return;
        const bool alreadyFinished = m_state == Data::DiagnosticsStreamState::Failed;
        m_state = Data::DiagnosticsStreamState::Stopping;
        emit streamStateChanged(m_state);
        m_state = Data::DiagnosticsStreamState::Stopped;
        m_request = {};
        emit streamStateChanged(m_state);
        if (!alreadyFinished)
            emit monitoringStopped();
    }

    Utils::Result<> requestRunMode(Data::DiagnosticsRunMode mode) final
    {
        if (m_state != Data::DiagnosticsStreamState::Running || !m_snapshot)
            return Utils::ResultError("Diagnostics provider is not running");
        m_snapshot->runMode = mode;
        emit diagnosticsSnapshotChanged();
        return Utils::ResultOk;
    }

    Utils::Result<> acknowledgeAlarm(const Data::NodeId &eventId) final
    {
        const auto alarm = std::find_if(
            m_events.begin(), m_events.end(), [&eventId](const Data::DiagnosticEvent &event) {
                return event.id == eventId && event.lifecycle == Data::AlarmLifecycle::Active;
            });
        if (alarm == m_events.end())
            return Utils::ResultError("Active alarm not found");
        alarm->lifecycle = Data::AlarmLifecycle::Acknowledged;
        alarm->acknowledgedAt = QDateTime::currentDateTimeUtc();
        emit diagnosticEventsChanged();
        return Utils::ResultOk;
    }

    Utils::Result<> clearRecoveredEvents() final
    {
        m_events.erase(
            std::remove_if(
                m_events.begin(),
                m_events.end(),
                [](const Data::DiagnosticEvent &event) {
                    return event.lifecycle == Data::AlarmLifecycle::Recovered;
                }),
            m_events.end());
        emit diagnosticEventsChanged();
        return Utils::ResultOk;
    }

    void publish()
    {
        const Data::NodeId slaveId = Data::NodeId::create();
        Data::DiagnosticsSnapshot snapshot;
        snapshot.projectId = m_request.projectId;
        snapshot.masterId = m_request.masterId;
        snapshot.generation = 7;
        snapshot.capturedAt = QDateTime::currentDateTimeUtc();
        snapshot.mock = true;
        snapshot.runMode = Data::DiagnosticsRunMode::Run;
        snapshot.masterState = Data::EtherCATState::Operational;
        snapshot.masterAlStatusText = "No error";
        snapshot.workingCounter = {4, 3, Data::WorkingCounterState::Incomplete, 2};
        snapshot.masterPorts = {{0, Data::LinkState::Up, true, 1, 0, 0}};
        snapshot.frameErrors = {1, 2, 3, 4, 5, 6};
        snapshot.distributedClock = {Data::DcSyncState::Synchronized, 17, 30, 4, 1};
        snapshot.cycle = {125000, 125010, 124990, 125020, 10, 20000, 8, 0};
        snapshot.slaves = {
            {slaveId,
             0,
             "Mock Slave",
             Data::EtherCATState::Operational,
             false,
             0,
             "No error",
             Data::WorkingCounterState::Valid,
             {{0, Data::LinkState::Up, true, 0, 0, 0}},
             {},
             {Data::DcSyncState::Synchronized, 5, 12, 2, 0},
             snapshot.capturedAt}};
        snapshot.activeAlarmCount = 1;
        snapshot.unacknowledgedAlarmCount = 1;
        snapshot.sourceSampleCount = 10;
        snapshot.coalescedSampleCount = 9;
        m_snapshot = snapshot;

        Data::DiagnosticEvent alarm;
        alarm.id = Data::NodeId::create();
        alarm.sequence = 11;
        alarm.occurredAt = snapshot.capturedAt;
        alarm.nodeId = slaveId;
        alarm.kind = Data::DiagnosticEventKind::Alarm;
        alarm.severity = Data::DiagnosticSeverity::Error;
        alarm.lifecycle = Data::AlarmLifecycle::Active;
        alarm.code = "MOCK-WKC";
        alarm.summary = "Working Counter mismatch";
        alarm.repeatCount = 2;
        m_events = {alarm};
        m_trend = {{12, snapshot.capturedAt, 3, 125010, 10, 20000, 17}};
        emit diagnosticsSnapshotChanged();
        emit diagnosticEventsChanged();
        emit diagnosticTrendChanged();
    }

    void recoverAlarm()
    {
        m_events[0].lifecycle = Data::AlarmLifecycle::Recovered;
        m_events[0].recoveredAt = QDateTime::currentDateTimeUtc();
        emit diagnosticEventsChanged();
    }

    void fail(const QString &error)
    {
        m_error = error;
        m_state = Data::DiagnosticsStreamState::Failed;
        emit streamStateChanged(m_state);
        emit monitoringStopped();
    }

private:
    Data::DiagnosticsStreamState m_state = Data::DiagnosticsStreamState::Stopped;
    Data::DiagnosticsRequest m_request;
    std::optional<Data::DiagnosticsSnapshot> m_snapshot;
    QList<Data::DiagnosticEvent> m_events;
    QList<Data::DiagnosticTrendSample> m_trend;
    Data::DiagnosticsLimits m_limits;
    QString m_error;
};

void EtherCATCoreTests::testMetadataAndServices()
{
    const ExtensionSystem::PluginSpec *spec = ExtensionSystem::PluginManager::specById(
        "ethercatcore");
    QVERIFY(spec);
    QCOMPARE(spec->name(), QString("EtherCATCore"));
    QVERIFY(!spec->version().isEmpty());
    QVERIFY(!spec->compatVersion().isEmpty());
    QVERIFY(!spec->hasError());
    const QList<ExtensionSystem::PluginDependency> dependencies = spec->dependencies();
    const bool hasCoreDependency = std::any_of(
        dependencies.cbegin(),
        dependencies.cend(),
        [](const ExtensionSystem::PluginDependency &dependency) {
            return dependency.id == "core"
                   && dependency.type == ExtensionSystem::PluginDependency::Required;
        });
    QVERIFY(hasCoreDependency);

    QVERIFY(ExtensionSystem::PluginManager::getObject<SelectionService>());
    QVERIFY(ExtensionSystem::PluginManager::getObject<StateService>());
    QVERIFY(ExtensionSystem::PluginManager::getObject<ProviderRegistry>());
}

void EtherCATCoreTests::testMockUiVisibility()
{
    static constexpr char environmentVariable[] = "QTC_ETHER_CAT_ENABLE_MOCK_UI";
    const bool wasSet = qEnvironmentVariableIsSet(environmentVariable);
    const QByteArray previousValue = qgetenv(environmentVariable);
    const QScopeGuard restoreEnvironment([wasSet, previousValue] {
        if (wasSet)
            qputenv(environmentVariable, previousValue);
        else
            qunsetenv(environmentVariable);
    });

    qunsetenv(environmentVariable);
    QVERIFY(isMockUiEnabled());

    qputenv(environmentVariable, "0");
    QVERIFY(!isMockUiEnabled());

    qputenv(environmentVariable, "1");
    QVERIFY(isMockUiEnabled());

    qputenv(environmentVariable, "true");
    QVERIFY(!isMockUiEnabled());
}

void EtherCATCoreTests::testNodeIdRoundTrip()
{
    const Data::NodeId created = Data::NodeId::create();
    QVERIFY(!created.isNull());
    QCOMPARE(Data::NodeId::fromString(created.toString()), created);
    QCOMPARE(qHash(Data::NodeId::fromString(created.toString())), qHash(created));
    QVERIFY(Data::NodeId::fromString("not-a-node-id").isNull());
}

void EtherCATCoreTests::testProjectSnapshotValueSemantics()
{
    using RenameStructuralNodeMethod = Utils::Result<> (
        ProjectService::*)(const Data::NodeId &, const Data::NodeId &, const QString &);
    static_assert(
        std::is_same_v<decltype(&ProjectService::renameStructuralNode), RenameStructuralNodeMethod>);
    using SetMasterConfigurationMethod = Utils::Result<> (ProjectService::*)(
        const Data::NodeId &, const Data::NodeId &, const Data::MasterConfiguration &);
    static_assert(std::is_same_v<
                  decltype(&ProjectService::setMasterConfiguration),
                  SetMasterConfigurationMethod>);
    using SetDeviceAdapterSelectionMethod = Utils::Result<> (ProjectService::*)(
        const Data::NodeId &,
        const Data::NodeId &,
        const QByteArray &,
        const Data::DeviceAdapterProjectSelection &);
    static_assert(std::is_same_v<
                  decltype(&ProjectService::setDeviceAdapterSelection),
                  SetDeviceAdapterSelectionMethod>);
    using SetMasterBindingArtifactMethod = Utils::Result<> (
        ProjectService::*)(const Data::NodeId &, const Data::SemanticBindingArtifactReference &);
    static_assert(std::is_same_v<
                  decltype(&ProjectService::setMasterBindingArtifact),
                  SetMasterBindingArtifactMethod>);

    const Data::NodeId projectId = Data::NodeId::create();
    const Data::NodeId targetId = Data::NodeId::create();
    const Data::NodeId slaveId = Data::NodeId::create();
    Data::ProjectSnapshot snapshot{
        projectId,
        "Line 1",
        1,
        "Embed Labs 20.0.1",
        {{projectId, {}, Data::ProjectNodeKind::Project, "Line 1"},
         {targetId, projectId, Data::ProjectNodeKind::Target, "Target Controller"}},
        true,
        true,
        false,
        {},
        {},
        {},
        {"binding/test",
         QByteArray(32, '\x31'),
         QByteArray(32, '\x32'),
         {{slaveId, "embedlabs:project:device:test"}}},
    };

    const Data::ProjectSnapshot copy = snapshot;
    QCOMPARE(copy, snapshot);
    snapshot.nodes[1].name = "Offline Target";
    QVERIFY(copy != snapshot);
    QCOMPARE(copy.nodes[1].parentId, projectId);
    QCOMPARE(
        copy.masterBindingArtifact.projectDeviceBindings.first().slaveId,
        slaveId);
    QCOMPARE(
        copy.masterBindingArtifact.projectDeviceBindings.first().projectDeviceId,
        QString("embedlabs:project:device:test"));

    Data::OfflineSlaveConfiguration slave;
    slave.id = slaveId;
    slave.masterId = targetId;
    slave.position = 0;
    slave.identity = {2, 0x1234, 1};
    slave.name = "Offline Slave";
    slave.esiSha256 = QByteArray(32, '\x41');
    slave.adapterSelection = {
        Data::DeviceAdapterId{"com.embedlabs.test.adapter"},
        "1.0.0",
        QByteArray(32, '\x42'),
        "default",
        {{0, 0x00010001, 0, 0}},
    };
    snapshot.slaves.append(slave);
    QVERIFY(copy != snapshot);
    QCOMPARE(copy.masterBindingArtifact.artifactId, QString("binding/test"));
}

void EtherCATCoreTests::testRuntimeResourceValueSemantics()
{
    const Data::ControllerConnectionScope scope{
        Data::NodeId::create(),
        Data::NodeId::create(),
    };
    const Data::RuntimeResourceId resourceId{QByteArray::fromHex("1020304050607080")};
    const Data::RuntimeComponentInstanceId componentId{QByteArray::fromHex("90a0b0c0d0e0f000")};
    const Data::RuntimeComponentInstanceId parentId{QByteArray::fromHex("1011121314151617")};
    const Data::RuntimeConsistencyGroupId groupId{QByteArray::fromHex("0102030405060708")};
    QVERIFY(resourceId.isValid());
    QVERIFY(componentId.isValid());
    QVERIFY(groupId.isValid());
    QCOMPARE(qHash(Data::RuntimeResourceId(resourceId)), qHash(resourceId));

    Data::RuntimeResourceDescriptor descriptor;
    descriptor.id = resourceId;
    descriptor.componentInstanceId = componentId;
    descriptor.parentInstanceId = parentId;
    descriptor.instanceOrdinal = 3;
    descriptor.consistencyGroupId = groupId;
    descriptor.displayName = "Primary sample";
    descriptor.description = "Provider-neutral runtime value";
    descriptor.primitiveType = Data::RuntimeResourcePrimitiveType::Opaque;
    descriptor.valueTypeIdentity = "org.example.runtime/custom-u24";
    descriptor.bitWidth = 24;
    descriptor.direction = Data::RuntimeResourceDirection::Input;
    descriptor.access = Data::RuntimeResourceAccess::ReadOnly;
    descriptor.processImageBitOffset = 72;
    descriptor.processImageBitLength = 24;
    descriptor.qualityMask = 0x000f;
    descriptor.unit = "unit";

    Data::RuntimeResourceTypedValue safeValue;
    safeValue.primitiveType = Data::RuntimeResourcePrimitiveType::UnsignedInteger;
    safeValue.value = QVariant::fromValue<qulonglong>(0);
    descriptor.safeValue = safeValue;

    Data::RuntimeResourceCatalogEpoch epoch;
    epoch.controllerBootId = 41;
    epoch.activePackageSlot = Data::ControllerSlot::B;
    epoch.activePackageGeneration = 79;
    epoch.configurationId = 83;
    epoch.topologyGeneration = 2;
    epoch.runtimeGeneration = 4;
    epoch.catalogRevision = 5;
    epoch.topologyIdentity = QByteArray::fromHex("abcdef0123456789");

    Data::RuntimeResourceCatalog catalog;
    catalog.scope = scope;
    catalog.sessionGeneration = 7;
    catalog.epoch = epoch;
    catalog.receivedAt = QDateTime::currentDateTimeUtc();
    catalog.resources = {descriptor};
    const Data::RuntimeResourceCatalog catalogCopy = catalog;
    QCOMPARE(catalogCopy, catalog);

    Data::RuntimeResourceTypedValue opaqueValue;
    opaqueValue.primitiveType = Data::RuntimeResourcePrimitiveType::Opaque;
    opaqueValue.typeIdentity = descriptor.valueTypeIdentity;
    opaqueValue.opaqueRepresentation = QByteArray::fromHex("123456");

    Data::RuntimeResourceQuality quality;
    quality.state = Data::RuntimeResourceQualityState::Uncertain;
    quality.flags = 0x0005;
    quality.opaqueCode = QByteArray::fromHex("8001");
    quality.detail = "Provider quality retained";

    Data::RuntimeResourceSample sample;
    sample.resourceId = resourceId;
    sample.consistencyGroupId = groupId;
    sample.value = opaqueValue;
    sample.quality = quality;
    sample.valueSequence = 9;
    sample.controllerTimestampNs = 123456789;

    Data::RuntimeResourceSnapshot snapshot;
    snapshot.scope = scope;
    snapshot.sessionGeneration = catalog.sessionGeneration;
    snapshot.epoch = catalog.epoch;
    snapshot.snapshotSequence = 11;
    snapshot.captureCycle = 101;
    snapshot.controllerTimestampNs = sample.controllerTimestampNs;
    snapshot.receivedAt = catalog.receivedAt;
    snapshot.complete = true;
    snapshot.samples = {sample};

    const Data::RuntimeResourceSnapshot snapshotCopy = snapshot;
    QCOMPARE(snapshotCopy, snapshot);
    QCOMPARE(
        snapshotCopy.samples.constFirst().value.opaqueRepresentation, QByteArray::fromHex("123456"));
    QCOMPARE(snapshotCopy.samples.constFirst().quality.opaqueCode, QByteArray::fromHex("8001"));

    snapshot.samples.first().value.primitiveType
        = Data::RuntimeResourcePrimitiveType::UnsignedInteger;
    snapshot.samples.first().value.value = QVariant::fromValue<qulonglong>(0x123456);
    snapshot.samples.first().value.opaqueRepresentation.clear();
    QVERIFY(snapshot != snapshotCopy);

    QVERIFY(QMetaType::fromType<Data::RuntimeResourceCatalog>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeResourceSnapshot>().isValid());
}

void EtherCATCoreTests::testRuntimeResourceSnapshotRequestContract()
{
    Data::RuntimeResourceSnapshotRequest request;
    request.correlationId = "targeted-read-1";
    request.scope = {Data::NodeId::create(), Data::NodeId::create()};
    request.sessionGeneration = 7;
    request.expectedEpoch.controllerBootId = 11;
    request.expectedEpoch.activePackageSlot = Data::ControllerSlot::B;
    request.expectedEpoch.activePackageGeneration = 12;
    request.expectedEpoch.configurationId = 13;
    request.expectedEpoch.topologyGeneration = 14;
    request.expectedEpoch.runtimeGeneration = 15;
    request.expectedEpoch.catalogRevision = 16;
    request.expectedEpoch.topologyIdentity = QByteArray::fromHex("0102030405060708");
    request.resourceIds = {
        {QByteArray::fromHex("1000000000000001")},
        {QByteArray::fromHex("1000000000000041")},
    };
    QVERIFY(request.isValid());
    QCOMPARE(Data::RuntimeResourceSnapshotRequest(request), request);

    Data::RuntimeResourceSnapshotResult success;
    success.request = request;
    Data::RuntimeResourceSnapshot snapshot;
    snapshot.scope = request.scope;
    snapshot.sessionGeneration = request.sessionGeneration;
    snapshot.epoch = request.expectedEpoch;
    snapshot.complete = true;
    Data::RuntimeResourceSample firstSample;
    firstSample.resourceId = request.resourceIds.at(0);
    Data::RuntimeResourceSample secondSample;
    secondSample.resourceId = request.resourceIds.at(1);
    snapshot.samples = {firstSample, secondSample};
    success.snapshot = snapshot;
    QVERIFY(success.isValid());
    QCOMPARE(Data::RuntimeResourceSnapshotResult(success), success);
    Data::RuntimeResourceSnapshotResult invalidSuccess = success;
    invalidSuccess.snapshot->complete = false;
    QVERIFY(!invalidSuccess.isValid());
    invalidSuccess = success;
    invalidSuccess.snapshot->scope.masterId = Data::NodeId::create();
    QVERIFY(!invalidSuccess.isValid());
    invalidSuccess = success;
    std::swap(invalidSuccess.snapshot->samples[0], invalidSuccess.snapshot->samples[1]);
    QVERIFY(!invalidSuccess.isValid());

    Data::RuntimeResourceSnapshotResult failure;
    failure.request = request;
    Data::ControllerOperationError error;
    error.operation = Data::ControllerOperation::QueryRuntimeResourceSnapshot;
    failure.error = error;
    QVERIFY(failure.isValid());
    Data::RuntimeResourceSnapshotResult invalidFailure = failure;
    invalidFailure.error->operation = Data::ControllerOperation::Refresh;
    QVERIFY(!invalidFailure.isValid());
    failure.snapshot = Data::RuntimeResourceSnapshot{};
    QVERIFY(!failure.isValid());

    Data::RuntimeResourceSnapshotRequest invalid = request;
    invalid.correlationId.clear();
    QVERIFY(!invalid.isValid());
    invalid = request;
    invalid.resourceIds.clear();
    QVERIFY(!invalid.isValid());
    invalid = request;
    invalid.resourceIds.append({QByteArray::fromHex("1000000000000042")});
    for (int index = invalid.resourceIds.size(); index < 65; ++index) {
        invalid.resourceIds.append({QByteArray::number(index).rightJustified(8, '\0')});
    }
    QVERIFY(!invalid.isValid());
    invalid = request;
    std::swap(invalid.resourceIds[0], invalid.resourceIds[1]);
    QVERIFY(!invalid.isValid());
    invalid = request;
    invalid.resourceIds[1] = invalid.resourceIds[0];
    QVERIFY(!invalid.isValid());
    invalid = request;
    invalid.correlationId = QStringLiteral("targeted\nread");
    QVERIFY(!invalid.isValid());
    invalid = request;
    invalid.correlationId = QString(129, QLatin1Char('a'));
    QVERIFY(!invalid.isValid());

    QVERIFY(QMetaType::fromType<Data::RuntimeResourceSnapshotRequest>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeResourceSnapshotResult>().isValid());
}

void EtherCATCoreTests::testRuntimeSemanticMappingAttestationContract()
{
    using SupportsMethod = bool (ControllerConnectionProvider::*)() const;
    using CachedMethod = std::optional<Data::RuntimeSemanticMappingAttestation> (
        ControllerConnectionProvider::*)() const;
    using RequestMethod = Utils::Result<> (ControllerConnectionProvider::*)(
        const Data::RuntimeSemanticMappingAttestationRequest &);
    static_assert(std::is_same_v<
                  decltype(&ControllerConnectionProvider::
                               supportsRuntimeSemanticMappingAttestation),
                  SupportsMethod>);
    static_assert(std::is_same_v<
                  decltype(&ControllerConnectionProvider::runtimeSemanticMappingAttestation),
                  CachedMethod>);
    static_assert(std::is_same_v<
                  decltype(&ControllerConnectionProvider::
                               requestRuntimeSemanticMappingAttestation),
                  RequestMethod>);

    Data::RuntimeSemanticMappingProof proof;
    proof.formatVersion = 1;
    proof.bindingCount = 56;
    proof.packageSigned = true;
    proof.signatureVerified = true;
    proof.semanticBindingVerified = true;
    proof.trust = Data::RuntimeSemanticMappingTrust::Production;
    proof.packageSha256 = QByteArray(32, '\x11');
    proof.manifestSha256 = QByteArray(32, '\x22');
    proof.mappingSha256 = QByteArray(32, '\x33');
    proof.resourceRecordsSha256 = QByteArray(32, '\x44');
    proof.resourceSectionSha256 = QByteArray(32, '\x55');
    proof.topologySha256 = QByteArray(32, '\x66');
    proof.signingKeyIdSha256 = QByteArray(32, '\x77');
    QVERIFY(proof.isValid());
    QCOMPARE(Data::RuntimeSemanticMappingProof(proof), proof);
    QVERIFY(Data::isValidRuntimeSemanticMappingDigest(proof.packageSha256));
    QVERIFY(Data::runtimeSemanticMappingDigestsEqual(
        proof.packageSha256, QByteArray(32, '\x11')));
    QVERIFY(!Data::runtimeSemanticMappingDigestsEqual(
        proof.packageSha256, QByteArray(32, '\x12')));
    QVERIFY(!Data::runtimeSemanticMappingDigestsEqual(
        proof.packageSha256, QByteArray(31, '\x11')));

    Data::RuntimeSemanticMappingAttestationRequest request;
    request.correlationId = "semantic-attestation-1";
    request.scope = {Data::NodeId::create(), Data::NodeId::create()};
    request.sessionGeneration = 7;
    request.expectedEpoch.controllerBootId = 11;
    request.expectedEpoch.activePackageSlot = Data::ControllerSlot::B;
    request.expectedEpoch.activePackageGeneration = 12;
    request.expectedEpoch.configurationId = 13;
    request.expectedEpoch.topologyGeneration = 14;
    request.expectedEpoch.runtimeGeneration = 15;
    request.expectedEpoch.catalogRevision = 16;
    request.expectedEpoch.topologyIdentity = QByteArray::fromHex("0102030405060708");
    request.expectedProof = proof;
    QVERIFY(request.isValid());
    QVERIFY(Data::isCompleteRuntimeSemanticMappingEpoch(request.expectedEpoch));
    QVERIFY(Data::isValidRuntimeSemanticMappingCorrelationId(request.correlationId));
    QCOMPARE(Data::RuntimeSemanticMappingAttestationRequest(request), request);

    Data::RuntimeSemanticMappingAttestation attestation;
    attestation.scope = request.scope;
    attestation.sessionGeneration = request.sessionGeneration;
    attestation.epoch = request.expectedEpoch;
    attestation.proof = proof;
    attestation.receivedAt = QDateTime::currentDateTimeUtc();
    QVERIFY(attestation.isValid());
    QCOMPARE(Data::RuntimeSemanticMappingAttestation(attestation), attestation);

    Data::RuntimeSemanticMappingAttestationResult success;
    success.request = request;
    success.attestation = attestation;
    QVERIFY(success.isValid());
    QCOMPARE(Data::RuntimeSemanticMappingAttestationResult(success), success);

    Data::RuntimeSemanticMappingAttestationResult failure;
    failure.request = request;
    Data::ControllerOperationError error;
    error.operation = Data::ControllerOperation::QueryRuntimeSemanticMappingAttestation;
    failure.error = error;
    QVERIFY(failure.isValid());
    failure.error->operation = Data::ControllerOperation::QueryRuntimeResourceCatalog;
    QVERIFY(!failure.isValid());
    failure = success;
    failure.error = error;
    QVERIFY(!failure.isValid());

    Data::RuntimeSemanticMappingProof invalidProof = proof;
    invalidProof.formatVersion = 3;
    QVERIFY(!invalidProof.isValid());
    invalidProof = proof;
    invalidProof.bindingCount = 0;
    QVERIFY(!invalidProof.isValid());
    invalidProof = proof;
    invalidProof.packageSigned = false;
    QVERIFY(!invalidProof.isValid());
    invalidProof = proof;
    invalidProof.signatureVerified = false;
    QVERIFY(!invalidProof.isValid());
    invalidProof = proof;
    invalidProof.semanticBindingVerified = false;
    QVERIFY(!invalidProof.isValid());
    invalidProof = proof;
    invalidProof.trust = Data::RuntimeSemanticMappingTrust::Unknown;
    QVERIFY(!invalidProof.isValid());
    invalidProof = proof;
    invalidProof.trust = Data::RuntimeSemanticMappingTrust::Engineering;
    QVERIFY(invalidProof.isValid());

    using DigestMember = QByteArray Data::RuntimeSemanticMappingProof::*;
    constexpr std::array<DigestMember, 7> digestMembers{
        &Data::RuntimeSemanticMappingProof::packageSha256,
        &Data::RuntimeSemanticMappingProof::manifestSha256,
        &Data::RuntimeSemanticMappingProof::mappingSha256,
        &Data::RuntimeSemanticMappingProof::resourceRecordsSha256,
        &Data::RuntimeSemanticMappingProof::resourceSectionSha256,
        &Data::RuntimeSemanticMappingProof::topologySha256,
        &Data::RuntimeSemanticMappingProof::signingKeyIdSha256,
    };
    for (DigestMember member : digestMembers) {
        invalidProof = proof;
        (invalidProof.*member).resize(31);
        QVERIFY(!invalidProof.isValid());
        invalidProof = proof;
        (invalidProof.*member).append('\x01');
        QVERIFY(!invalidProof.isValid());
        invalidProof = proof;
        (invalidProof.*member).fill('\0');
        QVERIFY(!invalidProof.isValid());

        Data::RuntimeSemanticMappingAttestationResult mismatch = success;
        (mismatch.attestation->proof.*member)[31] ^= '\x01';
        QVERIFY(!mismatch.isValid());
    }

    Data::RuntimeSemanticMappingAttestationRequest invalidRequest = request;
    invalidRequest.correlationId.clear();
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = request;
    invalidRequest.correlationId.prepend(' ');
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = request;
    invalidRequest.correlationId = QStringLiteral("semantic\nattestation");
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = request;
    invalidRequest.correlationId = QString(129, QLatin1Char('a'));
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = request;
    invalidRequest.scope.projectId = {};
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = request;
    invalidRequest.scope.masterId = {};
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = request;
    invalidRequest.sessionGeneration = 0;
    QVERIFY(!invalidRequest.isValid());

    QList<Data::RuntimeResourceCatalogEpoch> invalidEpochs;
    Data::RuntimeResourceCatalogEpoch epoch = request.expectedEpoch;
    epoch.controllerBootId = 0;
    invalidEpochs.append(epoch);
    epoch = request.expectedEpoch;
    epoch.activePackageSlot = Data::ControllerSlot::None;
    invalidEpochs.append(epoch);
    epoch = request.expectedEpoch;
    epoch.activePackageGeneration = 0;
    invalidEpochs.append(epoch);
    epoch = request.expectedEpoch;
    epoch.configurationId = 0;
    invalidEpochs.append(epoch);
    epoch = request.expectedEpoch;
    epoch.topologyGeneration = 0;
    invalidEpochs.append(epoch);
    epoch = request.expectedEpoch;
    epoch.runtimeGeneration = 0;
    invalidEpochs.append(epoch);
    epoch = request.expectedEpoch;
    epoch.catalogRevision = 0;
    invalidEpochs.append(epoch);
    epoch = request.expectedEpoch;
    epoch.topologyIdentity.clear();
    invalidEpochs.append(epoch);
    for (const Data::RuntimeResourceCatalogEpoch &invalidEpoch : std::as_const(invalidEpochs)) {
        invalidRequest = request;
        invalidRequest.expectedEpoch = invalidEpoch;
        QVERIFY(!invalidRequest.isValid());
    }

    Data::RuntimeSemanticMappingAttestationResult mismatch = success;
    mismatch.attestation->scope.projectId = Data::NodeId::create();
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    mismatch.attestation->scope.masterId = Data::NodeId::create();
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->sessionGeneration;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->epoch.controllerBootId;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    mismatch.attestation->epoch.activePackageSlot = Data::ControllerSlot::A;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->epoch.activePackageGeneration;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->epoch.configurationId;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->epoch.topologyGeneration;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->epoch.runtimeGeneration;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->epoch.catalogRevision;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    mismatch.attestation->epoch.topologyIdentity[0] ^= '\x01';
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    ++mismatch.attestation->proof.bindingCount;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    mismatch.attestation->proof.trust = Data::RuntimeSemanticMappingTrust::Engineering;
    QVERIFY(!mismatch.isValid());
    mismatch = success;
    mismatch.attestation->receivedAt = {};
    QVERIFY(!mismatch.isValid());

    Data::ControllerCapabilitySummary capability;
    QVERIFY(!capability.semanticMappingAttestation);
    capability.semanticMappingAttestation = true;
    QCOMPARE(Data::ControllerCapabilitySummary(capability), capability);

    QVERIFY(QMetaType::fromType<Data::RuntimeSemanticMappingTrust>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeSemanticMappingProof>().isValid());
    QVERIFY(
        QMetaType::fromType<Data::RuntimeSemanticMappingAttestationRequest>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeSemanticMappingAttestation>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeSemanticMappingAttestationResult>().isValid());
}

void EtherCATCoreTests::testRuntimeOutputTransactionContract()
{
    RuntimeOutputFixture fixture;

    QVERIFY(fixture.operationId.isValid());
    QCOMPARE(qHash(Data::RuntimeOutputOperationId(fixture.operationId)), qHash(fixture.operationId));
    QVERIFY(Data::isCompleteRuntimeOutputEpoch(fixture.epoch));
    QVERIFY(Data::isValidRuntimeOutputDigest(fixture.mappingDigest));
    QVERIFY(Data::isValidRuntimeOutputCorrelationId(fixture.policyRequest.correlationId));
    QVERIFY(fixture.policyRequest.isValid());
    QVERIFY(fixture.policy.isValid());
    QVERIFY(fixture.stateRequest.isValid());
    QVERIFY(fixture.idleState.isValid());
    QVERIFY(fixture.transactionRequest.isValid());
    QVERIFY(fixture.appliedState.isValid());

    Data::RuntimeOutputGroupPolicyResult policySuccess;
    policySuccess.request = fixture.policyRequest;
    policySuccess.policy = fixture.policy;
    QVERIFY(policySuccess.isValid());
    QCOMPARE(Data::RuntimeOutputGroupPolicyResult(policySuccess), policySuccess);

    Data::RuntimeOutputGroupPolicyResult policyFailure;
    policyFailure.request = fixture.policyRequest;
    Data::ControllerOperationError policyError;
    policyError.operation = Data::ControllerOperation::QueryRuntimeOutputGroupPolicy;
    policyFailure.error = policyError;
    QVERIFY(policyFailure.isValid());
    policyFailure.error->operation = Data::ControllerOperation::QueryRuntimeResourceCatalog;
    QVERIFY(!policyFailure.isValid());
    policyFailure = policySuccess;
    policyFailure.policy->mappingDigest = QByteArray(32, '\x45');
    QVERIFY(!policyFailure.isValid());
    policyFailure = policySuccess;
    policyFailure.error = policyError;
    QVERIFY(!policyFailure.isValid());

    Data::RuntimeOutputGroupPolicy invalidPolicy = fixture.policy;
    invalidPolicy.manualWriteAllowed = false;
    QVERIFY(!invalidPolicy.isValid());
    invalidPolicy = fixture.policy;
    invalidPolicy.recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::Unknown;
    QVERIFY(!invalidPolicy.isValid());
    invalidPolicy = fixture.policy;
    invalidPolicy.completeResourceCount = 65;
    QVERIFY(!invalidPolicy.isValid());
    invalidPolicy = fixture.policy;
    invalidPolicy.maximumTtlCycles = 65536;
    QVERIFY(!invalidPolicy.isValid());
    invalidPolicy = fixture.policy;
    invalidPolicy.completeGroupRecordDigest.fill('\0');
    QVERIFY(!invalidPolicy.isValid());

    Data::RuntimeOutputGroupPolicyRequest invalidPolicyRequest = fixture.policyRequest;
    invalidPolicyRequest.correlationId = " output-policy-1";
    QVERIFY(!invalidPolicyRequest.isValid());
    invalidPolicyRequest = fixture.policyRequest;
    invalidPolicyRequest.correlationId = "output\npolicy";
    QVERIFY(!invalidPolicyRequest.isValid());
    invalidPolicyRequest = fixture.policyRequest;
    invalidPolicyRequest.consistencyGroupId = {};
    QVERIFY(!invalidPolicyRequest.isValid());
    invalidPolicyRequest = fixture.policyRequest;
    invalidPolicyRequest.expectedEpoch.runtimeGeneration = 0;
    QVERIFY(!invalidPolicyRequest.isValid());

    Data::RuntimeOutputTransactionStateResult stateSuccess;
    stateSuccess.request = fixture.stateRequest;
    stateSuccess.state = fixture.idleState;
    QVERIFY(stateSuccess.isValid());
    QCOMPARE(Data::RuntimeOutputTransactionStateResult(stateSuccess), stateSuccess);

    Data::RuntimeOutputTransactionStateResult stateFailure;
    stateFailure.request = fixture.stateRequest;
    Data::ControllerOperationError stateError;
    stateError.operation = Data::ControllerOperation::QueryRuntimeOutputTransactionState;
    stateFailure.error = stateError;
    QVERIFY(stateFailure.isValid());
    stateFailure.error->operation = Data::ControllerOperation::QueryRuntimeResourceSnapshot;
    QVERIFY(!stateFailure.isValid());
    stateFailure = stateSuccess;
    stateFailure.state->sessionGeneration++;
    QVERIFY(!stateFailure.isValid());

    Data::RuntimeOutputTransactionState invalidState = fixture.idleState;
    invalidState.operationId = fixture.operationId;
    QVERIFY(!invalidState.isValid());
    invalidState = fixture.appliedState;
    invalidState.resultFlags = Data::RuntimeOutputTransactionResultFlag::SafeHold;
    QVERIFY(!invalidState.isValid());
    invalidState = fixture.appliedState;
    invalidState.resultFlags = static_cast<Data::RuntimeOutputTransactionResultFlag>(
        quint32(1) << 4);
    QVERIFY(!invalidState.isValid());
    invalidState = fixture.appliedState;
    invalidState.expiryCycle++;
    QVERIFY(!invalidState.isValid());
    invalidState = fixture.appliedState;
    invalidState.ttlCycles = 65536;
    invalidState.expiryCycle = invalidState.appliedCycle + invalidState.ttlCycles;
    QVERIFY(!invalidState.isValid());

    Data::RuntimeOutputTransactionState returnedTask = fixture.appliedState;
    returnedTask.state = Data::RuntimeOutputState::Idle;
    returnedTask.resultFlags = Data::RuntimeOutputTransactionResultFlag::ReturnedTask;
    returnedTask.outputGeneration++;
    QVERIFY(returnedTask.isValid());

    Data::RuntimeOutputTransactionState safeHold = fixture.appliedState;
    safeHold.state = Data::RuntimeOutputState::SafeHold;
    safeHold.resultFlags = Data::RuntimeOutputTransactionResultFlag::SafeHold;
    safeHold.recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::HoldSafe;
    safeHold.outputGeneration++;
    QVERIFY(safeHold.isValid());
    safeHold.resultFlags |= Data::RuntimeOutputTransactionResultFlag::OverrideActive;
    QVERIFY(!safeHold.isValid());
    safeHold.resultFlags = Data::RuntimeOutputTransactionResultFlag::SafeHold;
    QVERIFY(safeHold.isValid());

    Data::RuntimeOutputValueWrite booleanWrite;
    booleanWrite.resourceId.value = QByteArray::fromHex("01");
    booleanWrite.bitWidth = 1;
    booleanWrite.value.primitiveType = Data::RuntimeResourcePrimitiveType::Boolean;
    booleanWrite.value.value = true;
    QVERIFY(booleanWrite.isValid());
    booleanWrite.bitWidth = 2;
    QVERIFY(!booleanWrite.isValid());

    Data::RuntimeOutputValueWrite signedWrite;
    signedWrite.resourceId.value = QByteArray::fromHex("02");
    signedWrite.bitWidth = 4;
    signedWrite.value.primitiveType = Data::RuntimeResourcePrimitiveType::SignedInteger;
    signedWrite.value.value = QVariant::fromValue<qlonglong>(-8);
    QVERIFY(signedWrite.isValid());
    signedWrite.value.value = QVariant::fromValue<qlonglong>(-9);
    QVERIFY(!signedWrite.isValid());
    signedWrite.value.value = QVariant::fromValue<qint8>(-8);
    QVERIFY(!signedWrite.isValid());

    Data::RuntimeOutputValueWrite unsignedWrite;
    unsignedWrite.resourceId.value = QByteArray::fromHex("03");
    unsignedWrite.bitWidth = 4;
    unsignedWrite.value.primitiveType = Data::RuntimeResourcePrimitiveType::UnsignedInteger;
    unsignedWrite.value.value = QVariant::fromValue<qulonglong>(15);
    QVERIFY(unsignedWrite.isValid());
    unsignedWrite.value.value = QVariant::fromValue<qulonglong>(16);
    QVERIFY(!unsignedWrite.isValid());
    unsignedWrite.value.value = QVariant::fromValue<quint8>(15);
    QVERIFY(!unsignedWrite.isValid());

    Data::RuntimeOutputValueWrite floatingWrite;
    floatingWrite.resourceId.value = QByteArray::fromHex("0350");
    floatingWrite.bitWidth = 64;
    floatingWrite.value.primitiveType = Data::RuntimeResourcePrimitiveType::FloatingPoint;
    floatingWrite.value.value = 1.0;
    QVERIFY(!floatingWrite.isValid());

    Data::RuntimeOutputValueWrite bytesWrite;
    bytesWrite.resourceId.value = QByteArray::fromHex("04");
    bytesWrite.bitWidth = 12;
    bytesWrite.value.primitiveType = Data::RuntimeResourcePrimitiveType::ByteArray;
    bytesWrite.value.value = QByteArray::fromHex("0fff");
    QVERIFY(bytesWrite.isValid());
    bytesWrite.value.value = QByteArray::fromHex("1fff");
    QVERIFY(!bytesWrite.isValid());
    bytesWrite.value.value = QByteArray::fromHex("0f");
    QVERIFY(!bytesWrite.isValid());

    Data::RuntimeOutputValueWrite opaqueWrite = bytesWrite;
    opaqueWrite.bitWidth = 8;
    opaqueWrite.value.primitiveType = Data::RuntimeResourcePrimitiveType::Opaque;
    opaqueWrite.value.value = QByteArray::fromHex("01");
    QVERIFY(!opaqueWrite.isValid());

    Data::RuntimeOutputTransactionRequest invalidRequest = fixture.transactionRequest;
    invalidRequest.operationId.value.fill('\0');
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    invalidRequest.expectedMappingDigest.resize(31);
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    invalidRequest.expectedCompleteResourceCount = 1;
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    invalidRequest.expectedRecoveryPolicy = Data::RuntimeOutputRecoveryPolicy::Unknown;
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    invalidRequest.ttlCycles = invalidRequest.expectedMaximumTtlCycles + 1;
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    invalidRequest.completeGroupWrites.removeLast();
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    std::swap(invalidRequest.completeGroupWrites[0], invalidRequest.completeGroupWrites[1]);
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    invalidRequest.completeGroupWrites[1].resourceId
        = invalidRequest.completeGroupWrites[0].resourceId;
    QVERIFY(!invalidRequest.isValid());
    invalidRequest = fixture.transactionRequest;
    invalidRequest.completeGroupWrites[0].value.opaqueRepresentation = "wire-bytes";
    QVERIFY(!invalidRequest.isValid());

    Data::RuntimeOutputTransactionResult applied;
    applied.request = fixture.transactionRequest;
    applied.outcome = Data::RuntimeOutputTransactionOutcome::Applied;
    applied.finalResponseObserved = true;
    applied.state = fixture.appliedState;
    QVERIFY(applied.isValid());
    QCOMPARE(Data::RuntimeOutputTransactionResult(applied), applied);
    applied.finalResponseObserved = false;
    QVERIFY(applied.isValid());
    applied.finalResponseObserved = true;
    applied.state->outputGeneration++;
    QVERIFY(!applied.isValid());
    applied.state = fixture.appliedState;
    applied.state->recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::HoldSafe;
    QVERIFY(!applied.isValid());
    applied.state = fixture.appliedState;
    applied.state->state = Data::RuntimeOutputState::Idle;
    applied.state->resultFlags = Data::RuntimeOutputTransactionResultFlag::ReturnedTask;
    QVERIFY(!applied.isValid());

    Data::RuntimeOutputTransactionResult recovered;
    recovered.request = fixture.transactionRequest;
    recovered.outcome = Data::RuntimeOutputTransactionOutcome::AppliedThenRecovered;
    recovered.state = returnedTask;
    QVERIFY(recovered.isValid());
    recovered.finalResponseObserved = true;
    QVERIFY(!recovered.isValid());
    recovered.finalResponseObserved = false;
    recovered.state = safeHold;
    QVERIFY(!recovered.isValid());
    recovered.request.expectedRecoveryPolicy = Data::RuntimeOutputRecoveryPolicy::HoldSafe;
    recovered.state->recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::HoldSafe;
    QVERIFY(recovered.isValid());
    recovered.state->outputGeneration--;
    QVERIFY(!recovered.isValid());
    recovered.state = safeHold;
    recovered.state->operationId = Data::RuntimeOutputOperationId{QByteArray(16, '\x55')};
    QVERIFY(!recovered.isValid());
    recovered.state = safeHold;
    recovered.state->resultFlags |= Data::RuntimeOutputTransactionResultFlag::Replayed;
    QVERIFY(recovered.isValid());
    const auto rejectRecoveredMutation =
        [&recovered](const auto &mutation) {
            Data::RuntimeOutputTransactionResult invalid = recovered;
            mutation(invalid);
            QVERIFY(!invalid.isValid());
        };
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        invalid.state->scope.masterId = Data::NodeId::create();
    });
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        ++invalid.state->sessionGeneration;
    });
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        ++invalid.state->epoch.runtimeGeneration;
    });
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        invalid.state->mappingDigest[0] ^= 1;
    });
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        invalid.state->consistencyGroupId.value[0] ^= 1;
    });
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        ++invalid.state->ttlCycles;
        invalid.state->expiryCycle = invalid.state->appliedCycle + invalid.state->ttlCycles;
    });
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        --invalid.state->valueCount;
    });
    rejectRecoveredMutation([](Data::RuntimeOutputTransactionResult &invalid) {
        invalid.state->operationId.reset();
        invalid.state->state = Data::RuntimeOutputState::Idle;
        invalid.state->resultFlags = {};
        invalid.state->appliedCycle = 0;
        invalid.state->expiryCycle = 0;
        invalid.state->consistencyGroupId = {};
        invalid.state->ttlCycles = 0;
        invalid.state->recoveryPolicy = Data::RuntimeOutputRecoveryPolicy::Unknown;
        invalid.state->valueCount = 0;
    });

    Data::RuntimeOutputTransactionResult rejected;
    rejected.request = fixture.transactionRequest;
    rejected.outcome = Data::RuntimeOutputTransactionOutcome::Rejected;
    rejected.finalResponseObserved = true;
    Data::ControllerOperationError applyError;
    applyError.operation = Data::ControllerOperation::ApplyRuntimeOutputTransaction;
    rejected.error = applyError;
    QVERIFY(rejected.isValid());
    rejected.outcome = Data::RuntimeOutputTransactionOutcome::OutcomeUnknown;
    QVERIFY(!rejected.isValid());
    rejected.finalResponseObserved = false;
    QVERIFY(rejected.isValid());
    rejected.outcome = Data::RuntimeOutputTransactionOutcome::Unknown;
    QVERIFY(!rejected.isValid());
    rejected.outcome = Data::RuntimeOutputTransactionOutcome::Rejected;
    rejected.state = fixture.appliedState;
    QVERIFY(!rejected.isValid());

    QVERIFY(!Data::RuntimeOutputOperationId{QByteArray(16, '\0')}.isValid());
    QVERIFY(!Data::RuntimeOutputOperationId{QByteArray(15, '\x01')}.isValid());
    QVERIFY(!Data::isValidRuntimeOutputDigest(QByteArray(32, '\0')));
    Data::RuntimeOutputTransactionRequest saturatedGeneration = fixture.transactionRequest;
    saturatedGeneration.expectedOutputGeneration = std::numeric_limits<quint64>::max();
    QVERIFY(!saturatedGeneration.isValid());
    saturatedGeneration.expectedOutputGeneration
        = std::numeric_limits<quint64>::max() - quint64(1);
    QVERIFY(!saturatedGeneration.isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeOutputOperationId>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeOutputGroupPolicyResult>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeOutputTransactionStateResult>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeOutputTransactionRequest>().isValid());
    QVERIFY(QMetaType::fromType<Data::RuntimeOutputTransactionResult>().isValid());
}

void EtherCATCoreTests::testSemanticRuntimeValueSemantics()
{
    SemanticRuntimeFixture fixture;

    const Data::SemanticRuntimeContext contextCopy = fixture.context;
    QCOMPARE(contextCopy, fixture.context);
    QCOMPARE(contextCopy.signalStates.constFirst().binding, fixture.binding);
    QCOMPARE(
        contextCopy.signalStates.constFirst().binding->mappingDigest,
        contextCopy.signalStates.constFirst().binding->controllerMappingDigest);
    QCOMPARE(contextCopy.signalStates.constFirst().definition.id, fixture.target.signalId);
    QVERIFY(!contextCopy.mock);

    fixture.context.signalStates.first().value->value = false;
    QVERIFY(fixture.context != contextCopy);

    Data::SemanticActionRuntimeState action;
    action.target = fixture.target;
    action.target.kind = Data::SemanticRuntimeTargetKind::Action;
    action.target.signalId = {};
    action.target.actionId = {"urn:example.test:action/controlled-stop"};
    action.definition.id = action.target.actionId;
    action.definition.displayName = "Controlled stop";
    action.availability = Data::SemanticActionAvailability::AwaitingApproval;
    action.bindings = {fixture.binding};
    action.requiresApproval = true;
    action.requiresExclusiveControl = true;
    action.holdToRun = true;
    action.maximumTtlMs = 250;
    QCOMPARE(action.definition.id, action.target.actionId);

    Data::SemanticOperationApproval approval;
    approval.request.operationId = fixture.request.operationId;
    approval.request.decision = Data::SemanticApprovalDecision::Approved;
    approval.request.challenge = QByteArray(32, '\x4d');
    approval.request.expectedContextHash = fixture.context.contextHash;
    approval.actor = fixture.actor;
    approval.decidedAt = QDateTime::currentDateTimeUtc();

    Data::SemanticOperationRecord record;
    record.request = fixture.request;
    record.actor = fixture.actor;
    record.state = Data::SemanticOperationState::ApprovalRequired;
    record.approvals = {approval};
    record.canonicalRequestDigest = QByteArray(32, '\x7c');
    record.approvalChallenge = approval.request.challenge;
    record.createdAt = approval.decidedAt;
    record.updatedAt = approval.decidedAt;
    const Data::SemanticOperationRecord recordCopy = record;
    QCOMPARE(recordCopy, record);
    QVERIFY(Data::SemanticOperationState::TimedOut != Data::SemanticOperationState::OutcomeUnknown);

    Data::SemanticRuntimeAuditEvent auditEvent;
    auditEvent.sequence = 1;
    auditEvent.controllerId = fixture.target.controllerId;
    auditEvent.operationId = fixture.request.operationId;
    auditEvent.kind = Data::SemanticAuditEventKind::ApprovalRecorded;
    auditEvent.state = record.state;
    auditEvent.actor = approval.actor;
    auditEvent.canonicalRequestDigest = record.canonicalRequestDigest;
    auditEvent.occurredAt = approval.decidedAt;
    QVERIFY(auditEvent == Data::SemanticRuntimeAuditEvent(auditEvent));

    QVERIFY(QMetaType::fromType<Data::SemanticRuntimeTarget>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticRuntimeBinding>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticSignalRuntimeState>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticActionRuntimeState>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticRuntimeContext>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticOperationRequest>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticOperationApprovalRequest>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticOperationRecord>().isValid());
    QVERIFY(QMetaType::fromType<Data::SemanticRuntimeAuditEvent>().isValid());
}

void EtherCATCoreTests::testSemanticRuntimeEpochValidation()
{
    const SemanticRuntimeFixture fixture;
    QVERIFY(isCompleteRuntimeResourceCatalogEpoch(fixture.epoch));
    QVERIFY(validateSemanticRuntimeEpoch(fixture.epoch, fixture.epoch).accepted());

    Data::RuntimeResourceCatalogEpoch incomplete = fixture.epoch;
    incomplete.catalogRevision = 0;
    QCOMPARE(
        validateSemanticRuntimeEpoch(fixture.epoch, incomplete).error,
        SemanticRuntimeValidationError::InvalidEpoch);

    QList<Data::RuntimeResourceCatalogEpoch> changedEpochs;
    Data::RuntimeResourceCatalogEpoch changed = fixture.epoch;
    ++changed.controllerBootId;
    changedEpochs.append(changed);
    changed = fixture.epoch;
    changed.activePackageSlot = Data::ControllerSlot::A;
    changedEpochs.append(changed);
    changed = fixture.epoch;
    ++changed.activePackageGeneration;
    changedEpochs.append(changed);
    changed = fixture.epoch;
    ++changed.configurationId;
    changedEpochs.append(changed);
    changed = fixture.epoch;
    ++changed.topologyGeneration;
    changedEpochs.append(changed);
    changed = fixture.epoch;
    ++changed.runtimeGeneration;
    changedEpochs.append(changed);
    changed = fixture.epoch;
    ++changed.catalogRevision;
    changedEpochs.append(changed);
    changed = fixture.epoch;
    changed.topologyIdentity.append('\x09');
    changedEpochs.append(changed);

    QCOMPARE(changedEpochs.size(), 8);
    for (const Data::RuntimeResourceCatalogEpoch &candidate : changedEpochs) {
        QCOMPARE(
            validateSemanticRuntimeEpoch(fixture.epoch, candidate).error,
            SemanticRuntimeValidationError::EpochMismatch);
    }

    QVERIFY(validateSemanticRuntimeBinding(fixture.binding).accepted());
    QVERIFY(isCanonicalSha256Digest(fixture.binding.mappingDigest));
    Data::SemanticRuntimeBinding shortAdapterHash = fixture.binding;
    shortAdapterHash.adapterContentSha256.chop(1);
    QCOMPARE(
        validateSemanticRuntimeBinding(shortAdapterHash).error,
        SemanticRuntimeValidationError::InvalidBinding);

    Data::SemanticRuntimeBinding unverified = fixture.binding;
    unverified.verification.state = Data::SemanticBindingVerificationState::Unverified;
    QCOMPARE(
        validateSemanticRuntimeBinding(unverified).error,
        SemanticRuntimeValidationError::BindingUnverified);

    Data::SemanticRuntimeBinding mismatchedDigest = fixture.binding;
    mismatchedDigest.controllerMappingDigest.value[0] ^= '\x01';
    QCOMPARE(
        validateSemanticRuntimeBinding(mismatchedDigest).error,
        SemanticRuntimeValidationError::MappingDigestMismatch);
}

void EtherCATCoreTests::testSemanticRuntimeReadValidation()
{
    const SemanticRuntimeFixture fixture;

    const SemanticRuntimeReadValidation good
        = validateSemanticRuntimeRead(fixture.binding, fixture.catalog, fixture.snapshot);
    QVERIFY(good.validation.accepted());
    QVERIFY(good.sample);
    QCOMPARE(good.sample->value.value.toBool(), true);

    Data::SemanticRuntimeBinding outputBinding = fixture.binding;
    outputBinding.direction = Data::RuntimeResourceDirection::Output;
    outputBinding.access = Data::RuntimeResourceAccess::ReadWrite;
    Data::RuntimeResourceCatalog outputCatalog = fixture.catalog;
    outputCatalog.resources.first().direction = outputBinding.direction;
    outputCatalog.resources.first().access = outputBinding.access;
    const SemanticRuntimeReadValidation outputRead
        = validateSemanticRuntimeRead(outputBinding, outputCatalog, fixture.snapshot);
    QVERIFY(outputRead.validation.accepted());
    QVERIFY(outputRead.sample);

    Data::SemanticRuntimeBinding writeOnlyBinding = outputBinding;
    writeOnlyBinding.access = Data::RuntimeResourceAccess::WriteOnly;
    Data::RuntimeResourceCatalog writeOnlyCatalog = outputCatalog;
    writeOnlyCatalog.resources.first().access = writeOnlyBinding.access;
    QCOMPARE(
        validateSemanticRuntimeRead(
            writeOnlyBinding, writeOnlyCatalog, fixture.snapshot)
            .validation.error,
        SemanticRuntimeValidationError::InvalidBinding);

    Data::RuntimeResourceSnapshot incomplete = fixture.snapshot;
    incomplete.complete = false;
    QCOMPARE(
        validateSemanticRuntimeRead(fixture.binding, fixture.catalog, incomplete).validation.error,
        SemanticRuntimeValidationError::SnapshotIncomplete);

    Data::RuntimeResourceSnapshot stale = fixture.snapshot;
    stale.samples.first().quality.state = Data::RuntimeResourceQualityState::Stale;
    QCOMPARE(
        validateSemanticRuntimeRead(fixture.binding, fixture.catalog, stale).validation.error,
        SemanticRuntimeValidationError::SampleQualityNotGood);

    Data::RuntimeResourceSnapshot changedEpoch = fixture.snapshot;
    ++changedEpoch.epoch.runtimeGeneration;
    QCOMPARE(
        validateSemanticRuntimeRead(fixture.binding, fixture.catalog, changedEpoch).validation.error,
        SemanticRuntimeValidationError::EpochMismatch);

    // A descriptor with the same presentation and PI coordinates is never a fallback for the
    // exact verified ResourceId.
    Data::RuntimeResourceCatalog lookalikeCatalog = fixture.catalog;
    lookalikeCatalog.resources.first().id = {QByteArray::fromHex("1000000000000002")};
    QCOMPARE(
        validateSemanticRuntimeRead(fixture.binding, lookalikeCatalog, fixture.snapshot)
            .validation.error,
        SemanticRuntimeValidationError::ResourceNotFound);

    Data::RuntimeResourceSnapshot duplicate = fixture.snapshot;
    duplicate.samples.append(duplicate.samples.constFirst());
    QCOMPARE(
        validateSemanticRuntimeRead(fixture.binding, fixture.catalog, duplicate).validation.error,
        SemanticRuntimeValidationError::SampleAmbiguous);

    Data::RuntimeResourceCatalog mismatchedDescriptor = fixture.catalog;
    mismatchedDescriptor.resources.first().consistencyGroupId = {
        QByteArray::fromHex("3000000000000002")};
    QCOMPARE(
        validateSemanticRuntimeRead(fixture.binding, mismatchedDescriptor, fixture.snapshot)
            .validation.error,
        SemanticRuntimeValidationError::DescriptorMismatch);

    Data::RuntimeResourceSnapshot mismatchedValueType = fixture.snapshot;
    mismatchedValueType.samples.first().value.value = QVariant::fromValue<qulonglong>(1);
    QCOMPARE(
        validateSemanticRuntimeRead(
            fixture.binding, fixture.catalog, mismatchedValueType)
            .validation.error,
        SemanticRuntimeValidationError::SampleValueMismatch);

    Data::SemanticRuntimeBinding mismatchedBitWidth = fixture.binding;
    mismatchedBitWidth.bitWidth = 8;
    mismatchedBitWidth.valueTypeIdentity = "ethercat.runtime.value/primitive-1/bits-8";
    Data::RuntimeResourceCatalog mismatchedBitWidthCatalog = fixture.catalog;
    mismatchedBitWidthCatalog.resources.first().bitWidth = mismatchedBitWidth.bitWidth;
    mismatchedBitWidthCatalog.resources.first().valueTypeIdentity
        = mismatchedBitWidth.valueTypeIdentity;
    Data::RuntimeResourceSnapshot mismatchedBitWidthSnapshot = fixture.snapshot;
    mismatchedBitWidthSnapshot.samples.first().value.typeIdentity
        = mismatchedBitWidth.valueTypeIdentity;
    QCOMPARE(
        validateSemanticRuntimeRead(
            mismatchedBitWidth, mismatchedBitWidthCatalog, mismatchedBitWidthSnapshot)
            .validation.error,
        SemanticRuntimeValidationError::SampleValueMismatch);
}

void EtherCATCoreTests::testSemanticRuntimeOperationContract()
{
    using SubmitMethod = Data::SemanticOperationRecord (SemanticRuntimeService::*)(
        const Data::SemanticOperationRequest &, const Data::SemanticRuntimeActor &);
    using ApproveMethod = Data::SemanticOperationRecord (SemanticRuntimeService::*)(
        const Data::SemanticOperationApprovalRequest &, const Data::SemanticRuntimeActor &);
    static_assert(std::is_same_v<decltype(&SemanticRuntimeService::submit), SubmitMethod>);
    static_assert(std::is_same_v<decltype(&SemanticRuntimeService::approve), ApproveMethod>);

    const SemanticRuntimeFixture fixture;

    QVERIFY(isCanonicalSemanticOperationId(fixture.request.operationId));
    QVERIFY(!canonicalSemanticOperationRequest(fixture.request).isEmpty());
    QVERIFY(validateSemanticOperationRequest(fixture.request, fixture.context).accepted());

    const Data::SemanticOperationRequest requestCopy = fixture.request;
    QVERIFY(semanticOperationRequestsCanonicallyEqual(fixture.request, requestCopy));
    QCOMPARE(
        canonicalSemanticOperationRequest(fixture.request),
        canonicalSemanticOperationRequest(requestCopy));

    Data::SemanticOperationRequest differentId = fixture.request;
    differentId.operationId.value = "gateway-operation-002";
    QVERIFY(semanticOperationRequestsCanonicallyEqual(fixture.request, differentId));

    Data::SemanticOperationRequest changed = fixture.request;
    changed.value = false;
    QVERIFY(!semanticOperationRequestsCanonicallyEqual(fixture.request, changed));

    Data::SemanticOperationRequest gatewayStyleId = fixture.request;
    gatewayStyleId.operationId.value = "read:controller/device-17";
    QVERIFY(isCanonicalSemanticOperationId(gatewayStyleId.operationId));
    QVERIFY(!canonicalSemanticOperationRequest(gatewayStyleId).isEmpty());

    Data::SemanticOperationRequest invalidId = fixture.request;
    invalidId.operationId.value = "line-one\nline-two";
    QVERIFY(!isCanonicalSemanticOperationId(invalidId.operationId));
    QVERIFY(canonicalSemanticOperationRequest(invalidId).isEmpty());
    QCOMPARE(
        validateSemanticOperationRequest(invalidId, fixture.context).error,
        SemanticRuntimeValidationError::InvalidOperationId);

    invalidId.operationId.value = QString(129, 'x');
    QVERIFY(!isCanonicalSemanticOperationId(invalidId.operationId));

    Data::SemanticOperationRequest staleEpoch = fixture.request;
    ++staleEpoch.expectedEpoch.catalogRevision;
    QCOMPARE(
        validateSemanticOperationRequest(staleEpoch, fixture.context).error,
        SemanticRuntimeValidationError::EpochMismatch);

    Data::SemanticOperationRequest staleDigest = fixture.request;
    staleDigest.expectedMappingDigest.value[0] ^= '\x01';
    QCOMPARE(
        validateSemanticOperationRequest(staleDigest, fixture.context).error,
        SemanticRuntimeValidationError::MappingDigestMismatch);

    Data::SemanticOperationRequest staleContext = fixture.request;
    staleContext.expectedContextHash[0] ^= '\x01';
    QCOMPARE(
        validateSemanticOperationRequest(staleContext, fixture.context).error,
        SemanticRuntimeValidationError::InvalidRequest);

    Data::SemanticOperationRequest invalidKind = fixture.request;
    invalidKind.kind = Data::SemanticOperationKind::InvokeAction;
    QCOMPARE(
        validateSemanticOperationRequest(invalidKind, fixture.context).error,
        SemanticRuntimeValidationError::InvalidRequest);

    Data::SemanticOperationRequest releaseHold = fixture.request;
    releaseHold.kind = Data::SemanticOperationKind::ReleaseHold;
    releaseHold.value = {};
    releaseHold.parameters.clear();
    QVERIFY(validateSemanticOperationRequest(releaseHold, fixture.context).accepted());

    Data::SemanticOperationRequest resourceInjection = fixture.request;
    resourceInjection.value = QVariant::fromValue(fixture.binding.resourceId);
    QVERIFY(!isAllowedSemanticRuntimeValue(resourceInjection.value));
    QVERIFY(canonicalSemanticOperationRequest(resourceInjection).isEmpty());
    QCOMPARE(
        validateSemanticOperationRequest(resourceInjection, fixture.context).error,
        SemanticRuntimeValidationError::InvalidRequest);

    Data::SemanticOperationRequest wrongBooleanType = fixture.request;
    wrongBooleanType.value = QVariant::fromValue<qulonglong>(1);
    QVERIFY(!isSemanticRuntimeValueCompatible(wrongBooleanType.value, fixture.binding));
    QCOMPARE(
        validateSemanticOperationRequest(wrongBooleanType, fixture.context).error,
        SemanticRuntimeValidationError::InvalidRequest);

    Data::SemanticRuntimeBinding signedEight = fixture.binding;
    signedEight.primitiveType = Data::RuntimeResourcePrimitiveType::SignedInteger;
    signedEight.bitWidth = 8;
    QVERIFY(isSemanticRuntimeValueCompatible(QVariant::fromValue<qlonglong>(-128), signedEight));
    QVERIFY(isSemanticRuntimeValueCompatible(QVariant::fromValue<qlonglong>(127), signedEight));
    QVERIFY(!isSemanticRuntimeValueCompatible(QVariant::fromValue<qlonglong>(128), signedEight));

    Data::SemanticRuntimeBinding unsignedEight = fixture.binding;
    unsignedEight.primitiveType = Data::RuntimeResourcePrimitiveType::UnsignedInteger;
    unsignedEight.bitWidth = 8;
    QVERIFY(isSemanticRuntimeValueCompatible(QVariant::fromValue<qulonglong>(255), unsignedEight));
    QVERIFY(!isSemanticRuntimeValueCompatible(QVariant::fromValue<qulonglong>(256), unsignedEight));

    Data::SemanticRuntimeBinding fixedPoint = fixture.binding;
    fixedPoint.primitiveType = Data::RuntimeResourcePrimitiveType::FloatingPoint;
    fixedPoint.bitWidth = 64;
    QVERIFY(isSemanticRuntimeValueCompatible(QVariant(1.25), fixedPoint));
    QVERIFY(!isAllowedSemanticRuntimeValue(QVariant(std::numeric_limits<double>::quiet_NaN())));
    QVERIFY(!isSemanticRuntimeValueCompatible(
        QVariant(std::numeric_limits<double>::infinity()), fixedPoint));

    Data::SemanticRuntimeBinding rawBits = fixture.binding;
    rawBits.primitiveType = Data::RuntimeResourcePrimitiveType::ByteArray;
    rawBits.bitWidth = 9;
    QVERIFY(isSemanticRuntimeValueCompatible(QByteArray(2, '\0'), rawBits));
    QVERIFY(!isSemanticRuntimeValueCompatible(QByteArray(1, '\0'), rawBits));

    Data::SemanticRuntimeContext incomplete = fixture.context;
    incomplete.complete = false;
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, incomplete).error,
        SemanticRuntimeValidationError::BindingUnverified);

    Data::SemanticRuntimeContext verifierMissing = fixture.context;
    verifierMissing.bindingVerification.verifierId.clear();
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, verifierMissing).error,
        SemanticRuntimeValidationError::BindingUnverified);

    Data::SemanticRuntimeContext definitionMismatch = fixture.context;
    definitionMismatch.signalStates.first().definition.id
        = {"urn:example.test:signal/another"};
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, definitionMismatch).error,
        SemanticRuntimeValidationError::InvalidTarget);

    Data::SemanticRuntimeContext bindingTargetMismatch = fixture.context;
    bindingTargetMismatch.signalStates.first().binding->target.signalId
        = {"urn:example.test:signal/another"};
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, bindingTargetMismatch).error,
        SemanticRuntimeValidationError::InvalidBinding);

    Data::SemanticRuntimeContext duplicateSignal = fixture.context;
    duplicateSignal.signalStates.append(duplicateSignal.signalStates.constFirst());
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, duplicateSignal).error,
        SemanticRuntimeValidationError::InvalidTarget);

    Data::SemanticRuntimeContext verifierMismatch = fixture.context;
    verifierMismatch.signalStates.first().binding->verification.verifierId = "other-verifier";
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, verifierMismatch).error,
        SemanticRuntimeValidationError::BindingUnverified);

    Data::SemanticRuntimeContext manifestMismatch = fixture.context;
    manifestMismatch.signalStates.first()
        .binding->verification.signedManifestDigest.value[0]
        ^= '\x01';
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, manifestMismatch).error,
        SemanticRuntimeValidationError::BindingUnverified);

    Data::SemanticRuntimeContext proofRecordMismatch = fixture.context;
    proofRecordMismatch.signalStates.first().binding->verification.detail = "different proof";
    QCOMPARE(
        validateSemanticOperationRequest(fixture.request, proofRecordMismatch).error,
        SemanticRuntimeValidationError::BindingUnverified);

    Data::SemanticActionRuntimeState action;
    action.target = fixture.target;
    action.target.kind = Data::SemanticRuntimeTargetKind::Action;
    action.target.signalId = {};
    action.target.actionId = {"urn:example.test:action/manual"};
    action.definition.id = action.target.actionId;
    action.definition.enabled = true;
    action.availability = Data::SemanticActionAvailability::Ready;
    action.bindings = {fixture.binding};
    action.requiresApproval = true;
    action.requiresExclusiveControl = true;
    action.holdToRun = true;
    action.maximumTtlMs = 250;

    Data::SemanticOperationRequest actionRequest = fixture.request;
    actionRequest.kind = Data::SemanticOperationKind::InvokeAction;
    actionRequest.target = action.target;
    actionRequest.value = {};

    Data::SemanticRuntimeContext actionContext = fixture.context;
    actionContext.actionStates = {action};
    QVERIFY(validateSemanticOperationRequest(actionRequest, actionContext).accepted());

    Data::SemanticRuntimeBinding secondBinding = fixture.binding;
    secondBinding.target.signalId = {"urn:example.test:signal/velocity"};
    secondBinding.semanticBindingId = "binding:test:velocity";
    secondBinding.resourceId = {QByteArray::fromHex("1000000000000002")};
    secondBinding.consistencyGroupId = {QByteArray::fromHex("3000000000000002")};
    secondBinding.primitiveType = Data::RuntimeResourcePrimitiveType::SignedInteger;
    secondBinding.valueTypeIdentity = "ethercat.runtime.value/primitive-7/bits-32";
    secondBinding.bitWidth = 32;
    actionContext.actionStates.first().bindings.append(secondBinding);
    QVERIFY(validateSemanticOperationRequest(actionRequest, actionContext).accepted());

    Data::SemanticRuntimeContext actionDefinitionMismatch = actionContext;
    actionDefinitionMismatch.actionStates.first().definition.id
        = {"urn:example.test:action/another"};
    QCOMPARE(
        validateSemanticOperationRequest(actionRequest, actionDefinitionMismatch).error,
        SemanticRuntimeValidationError::InvalidTarget);

    Data::SemanticRuntimeContext actionProofMismatch = actionContext;
    actionProofMismatch.actionStates.first().bindings[1].verification.verifierId
        = "other-verifier";
    QCOMPARE(
        validateSemanticOperationRequest(actionRequest, actionProofMismatch).error,
        SemanticRuntimeValidationError::BindingUnverified);

    Data::SemanticRuntimeContext actionBindingTargetMismatch = actionContext;
    actionBindingTargetMismatch.actionStates.first().bindings[1].target.deviceId
        = Data::NodeId::create();
    QCOMPARE(
        validateSemanticOperationRequest(actionRequest, actionBindingTargetMismatch).error,
        SemanticRuntimeValidationError::InvalidBinding);

    Data::SemanticRuntimeContext duplicateActionBinding = fixture.context;
    duplicateActionBinding.actionStates = {action};
    duplicateActionBinding.actionStates.first().bindings.append(fixture.binding);
    QCOMPARE(
        validateSemanticOperationRequest(actionRequest, duplicateActionBinding).error,
        SemanticRuntimeValidationError::InvalidBinding);

    Data::SemanticOperationRecord approvalOperation;
    approvalOperation.request = fixture.request;
    approvalOperation.actor = fixture.actor;
    approvalOperation.state = Data::SemanticOperationState::ApprovalRequired;
    approvalOperation.approvalChallenge = QByteArray(32, '\x2a');

    Data::SemanticOperationApprovalRequest approval;
    approval.operationId = fixture.request.operationId;
    approval.decision = Data::SemanticApprovalDecision::Approved;
    approval.challenge = approvalOperation.approvalChallenge;
    approval.expectedContextHash = fixture.context.contextHash;
    QVERIFY(
        validateSemanticOperationApproval(approval, fixture.actor, approvalOperation, fixture.context)
            .accepted());

    Data::SemanticRuntimeActor automation = fixture.actor;
    automation.kind = Data::SemanticRuntimeActorKind::Automation;
    QCOMPARE(
        validateSemanticOperationApproval(approval, automation, approvalOperation, fixture.context)
            .error,
        SemanticRuntimeValidationError::ApprovalActorInvalid);

    Data::SemanticOperationApprovalRequest wrongChallenge = approval;
    wrongChallenge.challenge[0] ^= '\x01';
    QCOMPARE(
        validateSemanticOperationApproval(
            wrongChallenge, fixture.actor, approvalOperation, fixture.context)
            .error,
        SemanticRuntimeValidationError::ApprovalChallengeMismatch);

    Data::SemanticOperationApprovalRequest pending = approval;
    pending.decision = Data::SemanticApprovalDecision::Pending;
    QCOMPARE(
        validateSemanticOperationApproval(pending, fixture.actor, approvalOperation, fixture.context)
            .error,
        SemanticRuntimeValidationError::InvalidRequest);
}

void EtherCATCoreTests::testSemanticRuntimeServiceFailsClosed()
{
    SemanticRuntimeFixture fixture;
    TestSemanticRuntimeService service;
    service.snapshots = {fixture.context};

    const std::optional<Data::SemanticRuntimeContext> context = service.context(
        fixture.context.controllerId);
    QVERIFY(context);
    QCOMPARE(*context, fixture.context);
    QCOMPARE(service.readCount, 1);
    QVERIFY(!service.context("missing"));
    QCOMPARE(service.readCount, 2);

    QSignalSpy operationSpy(&service, &SemanticRuntimeService::operationChanged);
    QSignalSpy auditSpy(&service, &SemanticRuntimeService::auditChanged);

    const Data::SemanticOperationRecord submission = service.submit(fixture.request, fixture.actor);
    QCOMPARE(submission.state, Data::SemanticOperationState::Rejected);
    QCOMPARE(submission.resultCode, "semantic-runtime-unavailable");
    QCOMPARE(submission.actor, fixture.actor);
    QVERIFY(!submission.canonicalRequestDigest.isEmpty());
    QVERIFY(!submission.executionAttempted);
    QCOMPARE(submission.appliedCycle, quint64(0));
    QCOMPARE(submission.appliedRuntimeGeneration, quint64(0));
    QVERIFY(!service.operation(fixture.request.operationId));
    QVERIFY(service.audit(fixture.context.controllerId).isEmpty());

    Data::SemanticOperationApprovalRequest approval;
    approval.operationId = fixture.request.operationId;
    approval.decision = Data::SemanticApprovalDecision::Approved;
    approval.challenge = QByteArray(32, '\x2a');
    approval.expectedContextHash = fixture.context.contextHash;
    const Data::SemanticOperationRecord approvalResult = service.approve(approval, fixture.actor);
    QCOMPARE(approvalResult.state, Data::SemanticOperationState::Rejected);
    QCOMPARE(approvalResult.resultCode, "semantic-runtime-unavailable");
    QVERIFY(!approvalResult.executionAttempted);
    QCOMPARE(approvalResult.actor, fixture.actor);
    QCOMPARE(approvalResult.approvals.size(), 1);
    QCOMPARE(approvalResult.approvals.constFirst().request, approval);
    QCOMPARE(approvalResult.approvals.constFirst().actor, fixture.actor);

    QCOMPARE(operationSpy.count(), 0);
    QCOMPARE(auditSpy.count(), 0);
}

void EtherCATCoreTests::testExactEngineeringRationalContract()
{
    const ExactRationalResult half = normalizedExactRational(6, 12);
    QVERIFY(half.validation.accepted());
    QCOMPARE(half.value, std::optional<Data::ExactRational>({1, 2}));
    QCOMPARE(normalizedExactRational(0, 999).value, std::optional<Data::ExactRational>({0, 1}));

    const ExactRationalResult invalidDenominator = normalizedExactRational(1, 0);
    QCOMPARE(invalidDenominator.validation.error, EngineeringContractError::InvalidRational);
    QVERIFY(!invalidDenominator.value);
    QCOMPARE(validateExactRational({2, 4}).error, EngineeringContractError::InvalidRational);
    QCOMPARE(validateExactRational({0, 2}).error, EngineeringContractError::InvalidRational);
    QCOMPARE(validateExactRational({1, -2}).error, EngineeringContractError::InvalidRational);
    QVERIFY(validateExactRational({std::numeric_limits<qint64>::min(), 1}).accepted());

    QVERIFY(validateEngineeringValue(Data::EngineeringValue::fromBoolean(true)).accepted());
    QVERIFY(validateEngineeringValue(Data::EngineeringValue::fromSignedInteger(-17)).accepted());
    QVERIFY(validateEngineeringValue(Data::EngineeringValue::fromUnsignedInteger(17)).accepted());
    QVERIFY(validateEngineeringValue(Data::EngineeringValue::fromExactRational({-3, 7})).accepted());
    QVERIFY(
        validateEngineeringValue(Data::EngineeringValue::fromEnumeration("mode.ready")).accepted());

    Data::EngineeringValue polluted = Data::EngineeringValue::fromSignedInteger(7);
    polluted.unsignedInteger = 1;
    QCOMPARE(validateEngineeringValue(polluted).error, EngineeringContractError::InvalidValue);
}

void EtherCATCoreTests::testExactEngineeringConversionContract()
{
    Data::EngineeringTransform transform;
    transform.scale = {1, 10};
    transform.offset = {-1, 1};
    transform.rounding = Data::EngineeringRounding::RejectInexact;
    transform.constraint.minimum = {-100, 1};
    transform.constraint.maximum = {100, 1};

    const EngineeringConversionResult engineering
        = convertRawToEngineering(Data::EngineeringValue::fromSignedInteger(20), 16, transform);
    QVERIFY(engineering.validation.accepted());
    QCOMPARE(
        engineering.value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromExactRational({1, 1})));

    const EngineeringConversionResult raw = convertEngineeringToRaw(
        Data::EngineeringValue::fromExactRational({3, 2}),
        Data::EngineeringValueKind::SignedInteger,
        16,
        transform);
    QVERIFY(raw.validation.accepted());
    QCOMPARE(
        raw.value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromSignedInteger(25)));

    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromExactRational({1, 3}),
            Data::EngineeringValueKind::SignedInteger,
            16,
            transform)
            .validation.error,
        EngineeringContractError::InexactConversion);

    Data::EngineeringTransform rounded;
    rounded.scale = {1, 1};
    rounded.offset = {0, 1};
    rounded.rounding = Data::EngineeringRounding::NearestTiesToEven;
    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromExactRational({5, 2}),
            Data::EngineeringValueKind::SignedInteger,
            8,
            rounded)
            .value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromSignedInteger(2)));
    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromExactRational({7, 2}),
            Data::EngineeringValueKind::SignedInteger,
            8,
            rounded)
            .value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromSignedInteger(4)));
    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromExactRational({-5, 2}),
            Data::EngineeringValueKind::SignedInteger,
            8,
            rounded)
            .value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromSignedInteger(-2)));

    Data::EngineeringTransform towardZero = rounded;
    towardZero.rounding = Data::EngineeringRounding::TowardZero;
    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromExactRational({-7, 3}),
            Data::EngineeringValueKind::SignedInteger,
            8,
            towardZero)
            .value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromSignedInteger(-2)));
    Data::EngineeringTransform towardNegative = rounded;
    towardNegative.rounding = Data::EngineeringRounding::TowardNegativeInfinity;
    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromExactRational({-7, 3}),
            Data::EngineeringValueKind::SignedInteger,
            8,
            towardNegative)
            .value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromSignedInteger(-3)));
    Data::EngineeringTransform towardPositive = rounded;
    towardPositive.rounding = Data::EngineeringRounding::TowardPositiveInfinity;
    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromExactRational({7, 3}),
            Data::EngineeringValueKind::SignedInteger,
            8,
            towardPositive)
            .value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromSignedInteger(3)));

    QVERIFY(convertRawToEngineering(Data::EngineeringValue::fromSignedInteger(127), 8, rounded)
                .validation.accepted());
    QCOMPARE(
        convertRawToEngineering(Data::EngineeringValue::fromSignedInteger(128), 8, rounded)
            .validation.error,
        EngineeringContractError::RawValueOutOfRange);
    QVERIFY(convertRawToEngineering(Data::EngineeringValue::fromUnsignedInteger(255), 8, rounded)
                .validation.accepted());
    QCOMPARE(
        convertRawToEngineering(Data::EngineeringValue::fromUnsignedInteger(256), 8, rounded)
            .validation.error,
        EngineeringContractError::RawValueOutOfRange);
    QCOMPARE(
        convertRawToEngineering(
            Data::EngineeringValue::fromUnsignedInteger(std::numeric_limits<quint64>::max()),
            64,
            rounded)
            .validation.error,
        EngineeringContractError::ArithmeticOverflow);

    Data::EngineeringTransform booleanTransform = rounded;
    const EngineeringConversionResult booleanEngineering
        = convertRawToEngineering(Data::EngineeringValue::fromBoolean(true), 1, booleanTransform);
    QCOMPARE(
        booleanEngineering.value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromBoolean(true)));
    QCOMPARE(
        convertEngineeringToRaw(
            Data::EngineeringValue::fromBoolean(false),
            Data::EngineeringValueKind::Boolean,
            1,
            booleanTransform)
            .value,
        std::optional<Data::EngineeringValue>(Data::EngineeringValue::fromBoolean(false)));

    Data::EngineeringTransform missingRounding = rounded;
    missingRounding.rounding.reset();
    QCOMPARE(
        validateEngineeringTransform(missingRounding).error,
        EngineeringContractError::InvalidTransform);
    Data::EngineeringTransform zeroScale = rounded;
    zeroScale.scale = {0, 1};
    QCOMPARE(validateEngineeringTransform(zeroScale).error, EngineeringContractError::ScaleIsZero);
}

void EtherCATCoreTests::testExactEngineeringConstraintContract()
{
    Data::EngineeringConstraint stepped;
    stepped.minimum = {-2, 1};
    stepped.maximum = {2, 1};
    stepped.step = {1, 2};
    stepped.stepOrigin = {0, 1};
    QVERIFY(validateEngineeringConstraint(stepped).accepted());
    QVERIFY(validateEngineeringValueAgainstConstraint(
                Data::EngineeringValue::fromExactRational({3, 2}), stepped)
                .accepted());
    QCOMPARE(
        validateEngineeringValueAgainstConstraint(
            Data::EngineeringValue::fromExactRational({1, 4}), stepped)
            .error,
        EngineeringContractError::ConstraintStepViolation);
    QCOMPARE(
        validateEngineeringValueAgainstConstraint(Data::EngineeringValue::fromSignedInteger(3), stepped)
            .error,
        EngineeringContractError::ConstraintRangeViolation);

    Data::EngineeringConstraint enumeration;
    enumeration.minimum = {0, 1};
    enumeration.maximum = {1, 1};
    enumeration.enumeration = {
        {"mode.off", "Off", {0, 1}},
        {"mode.on", "On", {1, 1}},
    };
    QVERIFY(validateEngineeringConstraint(enumeration).accepted());
    QVERIFY(validateEngineeringValueAgainstConstraint(
                Data::EngineeringValue::fromEnumeration("mode.on"), enumeration)
                .accepted());
    QCOMPARE(
        validateEngineeringValueAgainstConstraint(
            Data::EngineeringValue::fromEnumeration("mode.unknown"), enumeration)
            .error,
        EngineeringContractError::ConstraintEnumerationViolation);

    Data::EngineeringConstraint unsorted = enumeration;
    std::reverse(unsorted.enumeration.begin(), unsorted.enumeration.end());
    QCOMPARE(
        validateEngineeringConstraint(unsorted).error, EngineeringContractError::InvalidConstraint);
    Data::EngineeringConstraint missingOrigin;
    missingOrigin.step = {1, 1};
    QCOMPARE(
        validateEngineeringConstraint(missingOrigin).error,
        EngineeringContractError::InvalidConstraint);
}

void EtherCATCoreTests::testManualControlEnvelopeContract()
{
    Data::EngineeringTransform signalTransform;
    signalTransform.scale = {1, 1};
    signalTransform.offset = {0, 1};
    signalTransform.rounding = Data::EngineeringRounding::RejectInexact;
    signalTransform.constraint.minimum = {0, 1};
    signalTransform.constraint.maximum = {1, 1};
    signalTransform.constraint.step = {1, 1};
    signalTransform.constraint.stepOrigin = {0, 1};

    Data::SemanticSignalDefinition signalDefinition;
    signalDefinition.id = {"urn:test:signal/output"};
    signalDefinition.direction = Data::SemanticSignalDirection::Output;
    signalDefinition.access = Data::SemanticSignalAccess::WriteOnly;
    signalDefinition.exposure = Data::SemanticSignalExposure::Public;
    signalDefinition.engineeringTransform = signalTransform;

    Data::EngineeringConstraint speedConstraint;
    speedConstraint.minimum = {-100, 1};
    speedConstraint.maximum = {100, 1};
    speedConstraint.step = {1, 1};
    speedConstraint.stepOrigin = {0, 1};

    Data::SemanticSignalDefinition speedSignalDefinition;
    speedSignalDefinition.id = {"urn:test:signal/target-speed"};
    speedSignalDefinition.direction = Data::SemanticSignalDirection::Output;
    speedSignalDefinition.access = Data::SemanticSignalAccess::WriteOnly;
    speedSignalDefinition.exposure = Data::SemanticSignalExposure::ActionOnly;
    Data::EngineeringTransform speedTransform;
    speedTransform.scale = {1, 1};
    speedTransform.offset = {0, 1};
    speedTransform.rounding = Data::EngineeringRounding::RejectInexact;
    speedTransform.constraint = speedConstraint;
    speedSignalDefinition.engineeringTransform = speedTransform;
    QList<Data::SemanticSignalDefinition> signalDefinitions{
        signalDefinition,
        speedSignalDefinition,
    };

    const auto terminalAction = [&signalDefinition](const QString &id) {
        Data::DeviceControlAction action;
        action.id = {id};
        action.enabled = true;
        action.holdToRun = false;
        Data::DeviceControlStep step;
        step.kind = Data::DeviceControlStepKind::WriteSignal;
        step.signalId = signalDefinition.id;
        step.value.source = Data::DeviceControlValueSource::Literal;
        step.value.engineeringLiteralValue = Data::EngineeringValue::fromSignedInteger(0);
        action.steps = {step};
        return action;
    };
    Data::DeviceControlAction mainAction;
    mainAction.id = {"urn:test:action/move"};
    mainAction.enabled = true;
    mainAction.holdToRun = true;
    mainAction.allowedReleaseActionIds = {{"urn:test:action/release"}};
    mainAction.allowedTimeoutActionIds = {{"urn:test:action/timeout"}};
    mainAction.allowedFailureActionIds = {{"urn:test:action/fail"}};
    Data::DeviceControlActionParameter speedParameter;
    speedParameter.id = "speed";
    speedParameter.required = true;
    speedParameter.engineeringConstraint = speedConstraint;
    mainAction.parameters = {speedParameter};
    Data::DeviceControlStep mainStep;
    mainStep.kind = Data::DeviceControlStepKind::WriteSignal;
    mainStep.signalId = speedSignalDefinition.id;
    mainStep.value.source = Data::DeviceControlValueSource::Parameter;
    mainStep.value.parameterId = speedParameter.id;
    mainAction.steps = {mainStep};

    QList<Data::DeviceControlAction> actionDefinitions{
        mainAction,
        terminalAction("urn:test:action/fail"),
        terminalAction("urn:test:action/release"),
        terminalAction("urn:test:action/timeout"),
    };

    Data::ManualSignalEnvelope signal;
    signal.signalId = signalDefinition.id;
    signal.enabled = true;
    signal.holdToRun = false;
    signal.timing.commandTtlMs = 100;
    signal.allowedRange = signalTransform.constraint;
    signal.safeValue = Data::EngineeringValue::fromSignedInteger(0);

    Data::ManualActionParameterEnvelope parameter;
    parameter.parameterId = "speed";
    parameter.allowedRange = speedConstraint;
    parameter.defaultValue = Data::EngineeringValue::fromSignedInteger(0);

    Data::ManualActionEnvelope action;
    action.actionId = mainAction.id;
    action.enabled = true;
    action.holdToRun = true;
    action.timing.commandTtlMs = 100;
    action.timing.refreshTimeoutMs = 250;
    action.timing.maxContinuousHoldMs = 5000;
    action.parameters = {parameter};
    action.releaseActionId = {"urn:test:action/release"};
    action.timeoutActionId = {"urn:test:action/timeout"};
    action.failureActionId = {"urn:test:action/fail"};

    Data::ManualControlEnvelope envelope;
    envelope.enabled = true;
    envelope.signalEnvelopes = {signal};
    envelope.actionEnvelopes = {action};
    QVERIFY(
        validateManualControlEnvelope(envelope, signalDefinitions, actionDefinitions).accepted());

    QVERIFY(QMetaType::fromType<Data::ExactRational>().isValid());
    QVERIFY(QMetaType::fromType<Data::EngineeringTransform>().isValid());
    QVERIFY(QMetaType::fromType<Data::ManualControlEnvelope>().isValid());
    QVERIFY(QMetaType::fromType<EngineeringConversionResult>().isValid());
    QVERIFY(QMetaType::fromType<ManualControlContractValidation>().isValid());

    Data::SemanticSignalDefinition legacySignal = signalDefinition;
    legacySignal.engineeringTransform.reset();
    QList<Data::SemanticSignalDefinition> legacySignals = signalDefinitions;
    legacySignals[0] = legacySignal;
    QCOMPARE(
        validateManualControlEnvelope(envelope, legacySignals, actionDefinitions).error,
        ManualControlContractError::ExactTransformMissing);

    Data::SemanticSignalDefinition actionOnly = signalDefinition;
    actionOnly.exposure = Data::SemanticSignalExposure::ActionOnly;
    QList<Data::SemanticSignalDefinition> actionOnlySignals = signalDefinitions;
    actionOnlySignals[0] = actionOnly;
    QCOMPARE(
        validateManualControlEnvelope(envelope, actionOnlySignals, actionDefinitions).error,
        ManualControlContractError::SignalNotPublic);

    Data::ManualControlEnvelope noTtl = envelope;
    noTtl.signalEnvelopes[0].timing.commandTtlMs = 0;
    QCOMPARE(
        validateManualControlEnvelope(noTtl, signalDefinitions, actionDefinitions).error,
        ManualControlContractError::InvalidTiming);

    Data::ManualControlEnvelope duplicateSignal = envelope;
    duplicateSignal.signalEnvelopes.append(signal);
    QCOMPARE(
        validateManualControlEnvelope(duplicateSignal, signalDefinitions, actionDefinitions).error,
        ManualControlContractError::DuplicateIdentifier);

    Data::ManualControlEnvelope broaderRange = envelope;
    broaderRange.signalEnvelopes[0].allowedRange.minimum.reset();
    QCOMPARE(
        validateManualControlEnvelope(broaderRange, signalDefinitions, actionDefinitions).error,
        ManualControlContractError::ConstraintNotSubset);

    Data::ManualControlEnvelope unsignedEnumeration = envelope;
    unsignedEnumeration.signalEnvelopes[0].allowedRange.enumeration = {
        {"project.custom", "Custom", {0, 1}},
    };
    QCOMPARE(
        validateManualControlEnvelope(unsignedEnumeration, signalDefinitions, actionDefinitions)
            .error,
        ManualControlContractError::ConstraintNotSubset);

    Data::SemanticSignalDefinition alphabeticSignal = signalDefinition;
    alphabeticSignal.id = {"urn:test:signal/alpha"};
    Data::ManualSignalEnvelope alphabeticEnvelope = signal;
    alphabeticEnvelope.signalId = alphabeticSignal.id;
    alphabeticEnvelope.enabled = false;
    Data::ManualControlEnvelope unsortedSignals = envelope;
    unsortedSignals.signalEnvelopes.append(alphabeticEnvelope);
    QList<Data::SemanticSignalDefinition> definitionsWithAlpha = signalDefinitions;
    definitionsWithAlpha.append(alphabeticSignal);
    QCOMPARE(
        validateManualControlEnvelope(unsortedSignals, definitionsWithAlpha, actionDefinitions).error,
        ManualControlContractError::NonCanonicalIdentifierOrder);

    Data::ManualControlEnvelope unsortedActions = envelope;
    Data::ManualActionEnvelope alphabeticAction;
    alphabeticAction.actionId = {"urn:test:action/alpha"};
    unsortedActions.actionEnvelopes.append(alphabeticAction);
    QCOMPARE(
        validateManualControlEnvelope(unsortedActions, signalDefinitions, actionDefinitions).error,
        ManualControlContractError::NonCanonicalIdentifierOrder);

    Data::SemanticSignalDefinition inputPeer = signalDefinition;
    inputPeer.id = {"urn:test:signal/input-peer"};
    inputPeer.direction = Data::SemanticSignalDirection::Input;
    inputPeer.access = Data::SemanticSignalAccess::ReadOnly;
    Data::ManualControlEnvelope unsafeGroup = envelope;
    unsafeGroup.signalEnvelopes[0].consistencyGroupSafeValues = {
        {inputPeer.id, Data::EngineeringValue::fromSignedInteger(0)},
    };
    QList<Data::SemanticSignalDefinition> definitionsWithInput = signalDefinitions;
    definitionsWithInput.append(inputPeer);
    QCOMPARE(
        validateManualControlEnvelope(unsafeGroup, definitionsWithInput, actionDefinitions).error,
        ManualControlContractError::InvalidSafeValue);

    Data::SemanticSignalDefinition zuluPeer = signalDefinition;
    zuluPeer.id = {"urn:test:signal/zulu-peer"};
    Data::ManualControlEnvelope unsortedGroup = envelope;
    unsortedGroup.signalEnvelopes[0].consistencyGroupSafeValues = {
        {zuluPeer.id, Data::EngineeringValue::fromSignedInteger(0)},
        {alphabeticSignal.id, Data::EngineeringValue::fromSignedInteger(0)},
    };
    QList<Data::SemanticSignalDefinition> definitionsForGroup = definitionsWithAlpha;
    definitionsForGroup.append(zuluPeer);
    QCOMPARE(
        validateManualControlEnvelope(unsortedGroup, definitionsForGroup, actionDefinitions).error,
        ManualControlContractError::InvalidSafeValue);

    Data::ManualControlEnvelope selfFallback = envelope;
    selfFallback.actionEnvelopes[0].failureActionId = mainAction.id;
    QCOMPARE(
        validateManualControlEnvelope(selfFallback, signalDefinitions, actionDefinitions).error,
        ManualControlContractError::SelfReferentialFallback);

    QList<Data::DeviceControlAction> disabledMain = actionDefinitions;
    disabledMain[0].enabled = false;
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, disabledMain).error,
        ManualControlContractError::InexactActionDefinition);
    QList<Data::DeviceControlAction> emptyMain = actionDefinitions;
    emptyMain[0].steps.clear();
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, emptyMain).error,
        ManualControlContractError::InexactActionDefinition);
    QList<Data::DeviceControlAction> inexactLiteral = actionDefinitions;
    inexactLiteral[0].steps[0].value.source = Data::DeviceControlValueSource::Literal;
    inexactLiteral[0].steps[0].value.literalValue = 0;
    inexactLiteral[0].steps[0].value.parameterId.clear();
    inexactLiteral[0].steps[0].value.engineeringLiteralValue.reset();
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, inexactLiteral).error,
        ManualControlContractError::InexactActionDefinition);
    QList<Data::DeviceControlAction> broadParameter = actionDefinitions;
    broadParameter[0].parameters[0].engineeringConstraint->minimum = {-200, 1};
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, broadParameter).error,
        ManualControlContractError::InexactActionDefinition);

    QList<Data::DeviceControlAction> nonterminalFallbacks = actionDefinitions;
    nonterminalFallbacks[1].allowedFailureActionIds = {{"urn:test:action/timeout"}};
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, nonterminalFallbacks).error,
        ManualControlContractError::FallbackNotTerminal);

    QList<Data::DeviceControlAction> emptyFallback = actionDefinitions;
    emptyFallback[1].steps.clear();
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, emptyFallback).error,
        ManualControlContractError::FallbackNotTerminal);

    QList<Data::DeviceControlAction> fallbackMissingDefault = actionDefinitions;
    Data::DeviceControlActionParameter fallbackParameter;
    fallbackParameter.id = "required";
    fallbackParameter.required = true;
    fallbackParameter.engineeringConstraint = speedConstraint;
    fallbackMissingDefault[1].parameters = {fallbackParameter};
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, fallbackMissingDefault).error,
        ManualControlContractError::FallbackNotTerminal);

    QList<Data::DeviceControlAction> unsortedAllowList = actionDefinitions;
    unsortedAllowList[0].allowedFailureActionIds = {
        {"urn:test:action/timeout"},
        {"urn:test:action/fail"},
    };
    QCOMPARE(
        validateManualControlEnvelope(envelope, signalDefinitions, unsortedAllowList).error,
        ManualControlContractError::NonCanonicalIdentifierOrder);

    QList<Data::DeviceControlAction> parameterDefinitions = actionDefinitions;
    Data::DeviceControlActionParameter alphaParameter;
    alphaParameter.id = "alpha";
    alphaParameter.required = false;
    alphaParameter.engineeringConstraint = speedConstraint;
    parameterDefinitions[0].parameters.append(alphaParameter);
    Data::ManualControlEnvelope unsortedParameters = envelope;
    Data::ManualActionParameterEnvelope alphaEnvelope;
    alphaEnvelope.parameterId = alphaParameter.id;
    alphaEnvelope.allowedRange = speedConstraint;
    unsortedParameters.actionEnvelopes[0].parameters.append(alphaEnvelope);
    QCOMPARE(
        validateManualControlEnvelope(unsortedParameters, signalDefinitions, parameterDefinitions)
            .error,
        ManualControlContractError::InvalidParameter);

    Data::ManualControlEnvelope disabled;
    QVERIFY(
        validateManualControlEnvelope(disabled, signalDefinitions, actionDefinitions).accepted());
}

void EtherCATCoreTests::testProcessDataConfigurationPreview()
{
    Data::ProcessDataConfiguration configuration;
    configuration.syncManagers = {
        {Data::NodeId::create(), 2, "Outputs", Data::SyncManagerDirection::MasterToSlave, true, 3},
        {Data::NodeId::create(), 3, "Inputs", Data::SyncManagerDirection::SlaveToMaster, true, 3},
    };

    Data::PdoConfiguration outputs;
    outputs.id = Data::NodeId::create();
    outputs.index = 0x1600;
    outputs.name = "Outputs";
    outputs.direction = Data::PdoDirection::Rx;
    outputs.syncManager = 2;
    outputs.selected = true;
    outputs.mandatory = true;
    outputs.defaultSelected = true;
    outputs.entries = {
        {Data::NodeId::create(), 0x7000, 1, "Enable", 1, Data::EtherCATDataType::Boolean, "BOOL"},
        {Data::NodeId::create(),
         0x7000,
         2,
         "Target",
         16,
         Data::EtherCATDataType::UnsignedInteger16,
         "UINT"},
    };

    Data::PdoConfiguration inputs;
    inputs.id = Data::NodeId::create();
    inputs.index = 0x1a00;
    inputs.name = "Inputs";
    inputs.direction = Data::PdoDirection::Tx;
    inputs.syncManager = 3;
    inputs.selected = true;
    inputs.entries = {
        {Data::NodeId::create(),
         0x6000,
         1,
         "Status",
         16,
         Data::EtherCATDataType::UnsignedInteger16,
         "UINT",
         8}};
    configuration.pdos = {outputs, inputs};

    const Data::ConfigurationValidation validation = Data::validateProcessDataConfiguration(
        configuration);
    QVERIFY(!validation.hasErrors());
    QVERIFY(validation.issues.isEmpty());
    QCOMPARE(validation.processImage.outputs.bitSize, 17);
    QCOMPARE(validation.processImage.outputs.byteSize, 3);
    QCOMPARE(validation.processImage.outputs.entries.size(), 2);
    QCOMPARE(validation.processImage.outputs.entries.at(0).bitOffset, 0);
    QCOMPARE(validation.processImage.outputs.entries.at(1).bitOffset, 1);
    QCOMPARE(validation.processImage.inputs.bitSize, 24);
    QCOMPARE(validation.processImage.inputs.byteSize, 3);
    QCOMPARE(validation.processImage.inputs.entries.first().bitOffset, 8);
}

void EtherCATCoreTests::testProcessDataConfigurationValidation()
{
    Data::ProcessDataConfiguration configuration;
    configuration.syncManagers = {
        {Data::NodeId::create(), 2, "Outputs", Data::SyncManagerDirection::SlaveToMaster, true, 1},
    };

    Data::PdoConfiguration first;
    first.id = Data::NodeId::create();
    first.index = 0x1600;
    first.name = "First";
    first.direction = Data::PdoDirection::Rx;
    first.syncManager = 2;
    first.selected = true;
    first.mappingSupported = false;
    first.entries = {
        {Data::NodeId::create(),
         0x7000,
         1,
         "Invalid width",
         12,
         Data::EtherCATDataType::UnsignedInteger16,
         "UINT",
         0}};

    Data::PdoConfiguration duplicate = first;
    duplicate.id = Data::NodeId::create();
    duplicate.name = "Duplicate";
    duplicate.mappingSupported = true;
    duplicate.entries.first().id = Data::NodeId::create();
    duplicate.entries.first().bitLength = 16;
    duplicate.entries.first().requestedBitOffset = 8;

    Data::PdoConfiguration mandatory;
    mandatory.id = Data::NodeId::create();
    mandatory.index = 0x1601;
    mandatory.direction = Data::PdoDirection::Rx;
    mandatory.syncManager = 4;
    mandatory.mandatory = true;
    mandatory.selected = false;
    configuration.pdos = {first, duplicate, mandatory};

    const Data::ConfigurationValidation validation = Data::validateProcessDataConfiguration(
        configuration);
    QVERIFY(validation.hasErrors());
    const auto hasIssue = [&validation](Data::ConfigurationIssueCode code) {
        return std::any_of(
            validation.issues.cbegin(),
            validation.issues.cend(),
            [code](const Data::ConfigurationIssue &issue) { return issue.code == code; });
    };
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::SyncManagerDirectionMismatch));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::UnsupportedPdoMapping));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::DataTypeBitLengthMismatch));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::DuplicatePdoAssignment));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::DuplicatePdoEntry));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::ProcessImageOverlap));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::SyncManagerSizeExceeded));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::MandatoryPdoNotSelected));
    QVERIFY(hasIssue(Data::ConfigurationIssueCode::MissingSyncManager));
}

void EtherCATCoreTests::testStartupAndDcConfigurationValidation()
{
    Data::StartupConfiguration startup;
    startup.parameters = {
        {Data::NodeId::create(),
         true,
         0,
         "IP",
         0x8000,
         1,
         Data::EtherCATDataType::UnsignedInteger16,
         "UINT",
         QByteArray::fromHex("0100"),
         "Mode"},
        {Data::NodeId::create(),
         true,
         1,
         "PS",
         0x8000,
         2,
         Data::EtherCATDataType::Boolean,
         "BOOL",
         QByteArray::fromHex("01"),
         "Enable"},
    };
    QVERIFY(Data::validateStartupConfiguration(startup).isEmpty());

    startup.parameters[1].order = 0;
    startup.parameters[1].rawValue.clear();
    const QList<Data::ConfigurationIssue> startupIssues = Data::validateStartupConfiguration(
        startup);
    const auto hasStartupIssue = [&startupIssues](Data::ConfigurationIssueCode code) {
        return std::any_of(
            startupIssues.cbegin(),
            startupIssues.cend(),
            [code](const Data::ConfigurationIssue &issue) { return issue.code == code; });
    };
    QVERIFY(hasStartupIssue(Data::ConfigurationIssueCode::DuplicateStartupOrder));
    QVERIFY(hasStartupIssue(Data::ConfigurationIssueCode::InvalidStartupValueSize));

    Data::DcConfiguration dc;
    dc.enabled = true;
    dc.modeName = "DC-Synchronous";
    dc.assignActivate = 0x0300;
    dc.sync0 = {true, 125000, -1000};
    dc.sync1 = {true, 125000, 1000};
    QVERIFY(Data::validateDcConfiguration(dc).isEmpty());

    dc.sync0.shiftTimeNs = 126000;
    dc.sync1.cycleTimeNs = 0;
    const QList<Data::ConfigurationIssue> dcIssues = Data::validateDcConfiguration(dc);
    const auto hasDcIssue = [&dcIssues](Data::ConfigurationIssueCode code) {
        return std::any_of(
            dcIssues.cbegin(), dcIssues.cend(), [code](const Data::ConfigurationIssue &issue) {
                return issue.code == code;
            });
    };
    QVERIFY(hasDcIssue(Data::ConfigurationIssueCode::DcShiftOutOfRange));
    QVERIFY(hasDcIssue(Data::ConfigurationIssueCode::InvalidDcCycle));
}

void EtherCATCoreTests::testDeviceDescriptionAndImportJobContract()
{
    Data::DeviceDescription description;
    description.summary
        = {Data::NodeId::create(),
           {2, 0x12345678, 0x00010002},
           "Servo Drive",
           "Drive-X",
           "Drives",
           true};
    description.syncManagers.append(
        {2, "Outputs", Data::SyncManagerDirection::MasterToSlave, 0x1000, 32, 0x24, true});
    description.rxPdos.append(
        {0x1600,
         "Command",
         Data::PdoDirection::Rx,
         2,
         true,
         true,
         {{0x6040, 0, "Controlword", 16, Data::EtherCATDataType::UnsignedInteger16, "UINT"}}});
    description.coe = {true, true, true, true, false};
    description.dcModes.append({"DC-Synchronous", 0x0300, 125000, 0, 0, 0});

    const Data::DeviceDescription copy = description;
    QCOMPARE(copy, description);
    QCOMPARE(copy.rxPdos.first().entries.first().bitLength, 16);

    TestDeviceImportJob job;
    QSignalSpy stateSpy(&job, &DeviceImportJob::stateChanged);
    QSignalSpy progressSpy(&job, &DeviceImportJob::progressChanged);
    QSignalSpy finishedSpy(&job, &DeviceImportJob::finished);
    job.start();
    QCOMPARE(job.state(), DeviceImportState::Running);
    QCOMPARE(job.progressValue(), 1);
    QCOMPARE(job.progressMaximum(), 2);
    job.cancel();
    QCOMPARE(job.state(), DeviceImportState::Finished);
    QVERIFY(job.result().canceled);
    QCOMPARE(stateSpy.count(), 3);
    QCOMPARE(progressSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 1);

    job.cancel();
    QCOMPARE(finishedSpy.count(), 1);
}

void EtherCATCoreTests::testDeviceAdapterValueSemantics()
{
    const Data::DeviceAdapterManifest qualified = testDeviceAdapterManifest();
    const Data::DeviceAdapterManifest copy = qualified;
    QCOMPARE(copy, qualified);

    QCOMPARE(qualified.id.value, QString("org.example.test.adapter/custom-drive"));
    QCOMPARE(
        qualified.capabilities.at(1).value, QString("example.test.capability/custom-diagnostics"));
    QCOMPARE(
        qualified.semanticSignals.constFirst().id.value,
        QString("urn:example.test:signal/custom.axis.target-velocity"));
    QCOMPARE(qualified.qualification, Data::DeviceAdapterQualification::Qualified);
    QVERIFY(qualified.signatureVerified);
    QVERIFY(qualified.realHardwareAllowed);

    QCOMPARE(qualified.match.vendorId, quint32(0x00a1b2c3));
    QCOMPARE(qualified.match.productCode, quint32(0x01020304));
    QCOMPARE(qualified.match.minimumRevision, quint32(0x00020003));
    QCOMPARE(qualified.match.maximumRevision, quint32(0x00020003));
    QCOMPARE(qualified.match.exactEsiSha256.size(), 32);
    QCOMPARE(
        qualified.match.exactEsiSha256.toHex(),
        QByteArray("1111111111111111111111111111111111111111111111111111111111111111"));

    const Data::SemanticSignalDefinition &signal = qualified.semanticSignals.constFirst();
    QCOMPARE(signal.direction, Data::SemanticSignalDirection::Output);
    QCOMPARE(signal.access, Data::SemanticSignalAccess::WriteOnly);
    QCOMPARE(signal.bindings.size(), 1);
    const Data::DeviceSignalBinding &binding = signal.bindings.constFirst();
    QCOMPARE(binding.kind, Data::DeviceSignalBindingKind::ProcessDataObject);
    QCOMPARE(binding.pdoDirection, Data::PdoDirection::Rx);
    QCOMPARE(binding.pdoIndex, quint16(0x1600));
    QCOMPARE(binding.objectIndex, quint16(0x7001));
    QCOMPARE(binding.objectSubIndex, quint8(0));
    QCOMPARE(binding.physicalType, Data::EtherCATDataType::Integer32);
    QCOMPARE(binding.bitWidth, 32);
    QCOMPARE(binding.byteOrder, Data::DeviceByteOrder::LittleEndian);
    QCOMPARE(signal.valueMetadata.unit, QString("rpm"));
    QCOMPARE(signal.valueMetadata.scale, 0.1);
    QCOMPARE(signal.valueMetadata.offset, -1.0);
    QVERIFY(signal.valueMetadata.hasMinimum);
    QCOMPARE(signal.valueMetadata.minimum, -3000.0);
    QVERIFY(signal.valueMetadata.hasMaximum);
    QCOMPARE(signal.valueMetadata.maximum, 3000.0);
    QVERIFY(signal.valueMetadata.hasStep);
    QCOMPARE(signal.valueMetadata.step, 0.1);
    QCOMPARE(
        signal.valueMetadata.enumValues,
        QList<Data::SemanticEnumValue>({{0, "stopped", "Stopped"}}));
    QVERIFY(signal.hasSafeValue);
    QCOMPARE(signal.safeValue, QVariant(0));
    QVERIFY(signal.manualControl.allowed);
    QVERIFY(signal.manualControl.requiresExclusiveControl);
    QVERIFY(signal.manualControl.holdToRun);
    QCOMPARE(signal.manualControl.commandTimeoutMs, quint32(250));
    QCOMPARE(signal.manualControl.timeoutAction, Data::ManualControlTimeoutAction::ControlledStop);

    Data::DeviceAdapterManifest changedRevision = qualified;
    changedRevision.match.minimumRevision = 0x00020004;
    QVERIFY(changedRevision != qualified);
    Data::DeviceAdapterManifest changedEsi = qualified;
    changedEsi.match.exactEsiSha256[0] = char(changedEsi.match.exactEsiSha256.at(0) ^ char(0xff));
    QVERIFY(changedEsi != qualified);

    Data::DeviceAdapterManifest candidate
        = testDeviceAdapterManifest(Data::DeviceAdapterQualification::Candidate, "2.2.0-rc1");
    QVERIFY(candidate != qualified);
    QVERIFY(!candidate.signatureVerified);
    QVERIFY(!candidate.realHardwareAllowed);
    QCOMPARE(candidate.qualification, Data::DeviceAdapterQualification::Candidate);
    Data::DeviceAdapterManifest mock
        = testDeviceAdapterManifest(Data::DeviceAdapterQualification::MockOnly, "mock-1");
    QCOMPARE(mock.qualification, Data::DeviceAdapterQualification::MockOnly);
    QVERIFY(!mock.realHardwareAllowed);
    QVERIFY(Data::DeviceAdapterQualification::Unqualified != candidate.qualification);
    QVERIFY(candidate.qualification != qualified.qualification);
    QVERIFY(qualified.qualification != mock.qualification);

    Data::DeviceAdapterResolutionRequest request;
    request.slaveId = Data::NodeId::create();
    request.device.summary.identity
        = {qualified.match.vendorId, qualified.match.productCode, qualified.match.minimumRevision};
    request.device.sourceSha256 = qualified.match.exactEsiSha256;
    request.expectedAdapterId = qualified.id;
    request.expectedAdapterVersion = qualified.version;
    request.expectedAdapterContentSha256 = qualified.contentSha256;
    request.allowCandidate = true;
    request.allowMock = false;
    request.requireRealHardwareQualification = false;
    QVERIFY(request.hasExpectedAdapterSelection());
    QVERIFY(request.hasValidExpectedAdapterSelection());
    Data::DeviceAdapterResolutionRequest invalidExpectedAdapter = request;
    invalidExpectedAdapter.expectedAdapterContentSha256.chop(1);
    QVERIFY(invalidExpectedAdapter.hasExpectedAdapterSelection());
    QVERIFY(!invalidExpectedAdapter.hasValidExpectedAdapterSelection());
    Data::DeviceAdapterResolutionRequest automaticAdapter = request;
    automaticAdapter.expectedAdapterId = {};
    automaticAdapter.expectedAdapterVersion.clear();
    automaticAdapter.expectedAdapterContentSha256.clear();
    QVERIFY(!automaticAdapter.hasExpectedAdapterSelection());
    QVERIFY(automaticAdapter.hasValidExpectedAdapterSelection());
    const Data::NodeId pdoId = Data::NodeId::create();
    const Data::NodeId entryId = Data::NodeId::create();
    const Data::NodeId syncManagerId = Data::NodeId::create();
    request.processImage.outputs.bitSize = 32;
    request.processImage.outputs.byteSize = 4;
    request.processImage.outputs.entries = {
        {pdoId,
         entryId,
         syncManagerId,
         binding.pdoIndex,
         binding.objectIndex,
         binding.objectSubIndex,
         "Target velocity",
         binding.pdoDirection,
         2,
         96,
         binding.bitWidth,
         12,
         0,
         binding.physicalType}};
    QCOMPARE(Data::DeviceAdapterResolutionRequest(request), request);

    Data::BoundSemanticSignal boundSignal;
    boundSignal.definition = signal;
    boundSignal.binding = binding;
    boundSignal.processImageEntryId = entryId;
    boundSignal.processImageBitOffset = 96;
    boundSignal.processImageBitLength = 32;

    Data::ResolvedDeviceModel model;
    model.slaveId = request.slaveId;
    model.identity = request.device.summary.identity;
    model.esiSha256 = request.device.sourceSha256;
    model.adapterId = qualified.id;
    model.adapterVersion = qualified.version;
    model.adapterContentSha256 = qualified.contentSha256;
    model.qualification = qualified.qualification;
    model.capabilities = qualified.capabilities;
    model.boundSignals = {boundSignal};
    model.warnings = {"Test-only resolved model"};
    model.complete = true;
    QCOMPARE(Data::ResolvedDeviceModel(model), model);
    QCOMPARE(model.boundSignals.constFirst().processImageEntryId, entryId);
    QCOMPARE(model.boundSignals.constFirst().processImageBitOffset, qint64(96));
    QCOMPARE(model.identity.revisionNumber, qualified.match.minimumRevision);
    QCOMPARE(model.esiSha256, qualified.match.exactEsiSha256);
    QCOMPARE(model.adapterContentSha256, qualified.contentSha256);

    const Data::DeviceAdapterResolutionResult result{true, model, {}};
    QCOMPARE(Data::DeviceAdapterResolutionResult(result), result);
    QVERIFY(result.resolved);
    QVERIFY(result.error.isEmpty());
}

void EtherCATCoreTests::testDeviceAdapterProviderContract()
{
    const Data::DeviceAdapterManifest qualified = testDeviceAdapterManifest();
    const Data::DeviceAdapterManifest candidate
        = testDeviceAdapterManifest(Data::DeviceAdapterQualification::Candidate, "2.2.0-rc1");
    TestDeviceAdapterProvider provider({qualified, candidate});

    QCOMPARE(int(ProviderKind::DeviceAdapter), 6);
    QCOMPARE(provider.kind(), ProviderKind::DeviceAdapter);
    QCOMPARE(provider.adapterManifests(), QList<Data::DeviceAdapterManifest>({qualified, candidate}));
    const std::optional<Data::DeviceAdapterManifest> exact
        = provider.adapterManifest(qualified.id, qualified.version);
    QVERIFY(exact);
    QCOMPARE(*exact, qualified);
    const std::optional<Data::DeviceAdapterManifest> candidateVersion
        = provider.adapterManifest(candidate.id, candidate.version);
    QVERIFY(candidateVersion);
    QCOMPARE(*candidateVersion, candidate);
    QVERIFY(!provider.adapterManifest(qualified.id, "missing-version"));
    QVERIFY(!provider.adapterManifest({"org.example.test.adapter/missing"}, qualified.version));

    Data::DeviceAdapterResolutionRequest request;
    request.slaveId = Data::NodeId::create();
    request.device.summary.identity
        = {qualified.match.vendorId, qualified.match.productCode, qualified.match.minimumRevision};
    request.device.sourceSha256 = qualified.match.exactEsiSha256;
    request.expectedAdapterId = qualified.id;
    request.expectedAdapterVersion = qualified.version;
    request.expectedAdapterContentSha256 = qualified.contentSha256;
    request.allowCandidate = false;
    request.allowMock = false;
    request.requireRealHardwareQualification = true;
    provider.resolutionResult.resolved = true;
    provider.resolutionResult.model.slaveId = request.slaveId;
    provider.resolutionResult.model.identity = request.device.summary.identity;
    provider.resolutionResult.model.esiSha256 = request.device.sourceSha256;
    provider.resolutionResult.model.adapterId = qualified.id;
    provider.resolutionResult.model.adapterVersion = qualified.version;
    provider.resolutionResult.model.adapterContentSha256 = qualified.contentSha256;
    provider.resolutionResult.model.qualification = qualified.qualification;
    provider.resolutionResult.model.complete = true;
    const Data::DeviceAdapterResolutionResult resolution = provider.resolveDevice(request);
    QVERIFY(resolution.resolved);
    QCOMPARE(resolution, provider.resolutionResult);
    QCOMPARE(provider.lastResolutionRequest, request);
    QCOMPARE(provider.resolutionCount, 1);

    QSignalSpy manifestsSpy(&provider, &DeviceAdapterProvider::adapterManifestsChanged);
    provider.replaceManifests({qualified});
    QCOMPARE(manifestsSpy.count(), 1);
    QCOMPARE(provider.adapterManifests(), QList<Data::DeviceAdapterManifest>({qualified}));
    QVERIFY(!provider.adapterManifest(candidate.id, candidate.version));

    QSignalSpy availabilitySpy(&provider, &Provider::availabilityChanged);
    QVERIFY(!provider.isAvailable());
    provider.setAvailable(true);
    QVERIFY(provider.isAvailable());
    QCOMPARE(availabilitySpy.count(), 1);

    ProviderRegistry *registry = ExtensionSystem::PluginManager::getObject<ProviderRegistry>();
    QVERIFY(registry);
    QSignalSpy addedSpy(registry, &ProviderRegistry::providerAdded);
    QSignalSpy removedSpy(registry, &ProviderRegistry::providerAboutToBeRemoved);
    ExtensionSystem::PluginManager::addObject(&provider);
    QScopeGuard removeProvider(
        [&provider] { ExtensionSystem::PluginManager::removeObject(&provider); });
    QCOMPARE(registry->provider(provider.id()), &provider);
    QCOMPARE(registry->providers(ProviderKind::DeviceAdapter), QList<Provider *>({&provider}));
    QCOMPARE(addedSpy.count(), 1);

    ExtensionSystem::PluginManager::removeObject(&provider);
    removeProvider.dismiss();
    QVERIFY(!registry->provider(provider.id()));
    QVERIFY(registry->providers(ProviderKind::DeviceAdapter).isEmpty());
    QCOMPARE(removedSpy.count(), 1);
}

void EtherCATCoreTests::testPropertyPageProviderContract()
{
    const PropertyPageContext
        context{Data::NodeId::create(), Data::NodeId::create(), WorkbenchNodeKind::Device, "Drive"};
    const PropertyPageContext copy = context;
    QCOMPARE(copy, context);

    TestPropertyPageProvider provider;
    QCOMPARE(provider.kind(), ProviderKind::PropertyPage);
    const QList<PropertyPageDescriptor> pages = provider.pages(context);
    QCOMPARE(
        pages, QList<PropertyPageDescriptor>({{Utils::Id("EtherCAT.Test.General"), "General", 10}}));

    std::unique_ptr<QWidget> page(provider.createPage(pages.first().id, nullptr));
    QVERIFY(page);
    provider.updatePage(pages.first().id, page.get(), context);
    QCOMPARE(page->objectName(), context.nodeId.toString());

    PropertyPageContext projectContext = context;
    projectContext.nodeKind = WorkbenchNodeKind::Project;
    QVERIFY(provider.pages(projectContext).isEmpty());
}

void EtherCATCoreTests::testWorkbenchDerivedNodeKinds()
{
    QCOMPARE(int(WorkbenchNodeKind::None), 0);
    QCOMPARE(int(WorkbenchNodeKind::Project), 1);
    QCOMPARE(int(WorkbenchNodeKind::Target), 2);
    QCOMPARE(int(WorkbenchNodeKind::Master), 3);
    QCOMPARE(int(WorkbenchNodeKind::DeviceRepository), 4);
    QCOMPARE(int(WorkbenchNodeKind::Device), 5);
    QCOMPARE(int(WorkbenchNodeKind::ConfiguredSlave), 6);
    QCOMPARE(int(WorkbenchNodeKind::Diagnostics), 7);
    QCOMPARE(int(WorkbenchNodeKind::Placeholder), 8);

    QCOMPARE(int(WorkbenchNodeKind::ProcessInputs), 9);
    QCOMPARE(int(WorkbenchNodeKind::ProcessOutputs), 10);
    QCOMPARE(int(WorkbenchNodeKind::RxPdoGroup), 11);
    QCOMPARE(int(WorkbenchNodeKind::TxPdoGroup), 12);
    QCOMPARE(int(WorkbenchNodeKind::Pdo), 13);
    QCOMPARE(int(WorkbenchNodeKind::PdoEntry), 14);
    QCOMPARE(int(WorkbenchNodeKind::Modules), 15);
    QCOMPARE(int(WorkbenchNodeKind::Module), 16);
    QCOMPARE(int(WorkbenchNodeKind::Channel), 17);

    const PropertyPageContext context{
        Data::NodeId::create(), Data::NodeId::create(), WorkbenchNodeKind::PdoEntry, "Controlword"};
    QCOMPARE(PropertyPageContext(context), context);
}

void EtherCATCoreTests::testControllerConnectionProviderContract()
{
    const Data::ControllerConnectionScope scope{Data::NodeId::create(), Data::NodeId::create()};
    QCOMPARE(Data::ControllerConnectionScope(scope), scope);
    TestControllerConnectionProvider provider;
    QCOMPARE(int(ProviderKind::ControllerConnection), 5);
    QCOMPARE(provider.kind(), ProviderKind::ControllerConnection);
    const QList<Data::ControllerConnectionProfile> profiles = provider.connectionProfiles(scope);
    QCOMPARE(profiles.size(), 1);
    const Data::ControllerConnectionProfile profile = profiles.constFirst();
    QVERIFY(!profile.id.isNull());
    QVERIFY(profile.configured);
    QVERIFY(profile.supported);
    QVERIFY(profile.defaultProfile);
    QCOMPARE(profile.endpointSummary, QString("192.168.3.101:15200"));
    QCOMPARE(Data::ControllerConnectionProfile(profile), profile);
    const Data::ControllerConnectionRequest request{scope, profile.id};
    QCOMPARE(Data::ControllerConnectionRequest(request), request);
    Data::ControllerConnectionProfileConfiguration configuration;
    configuration.profileId = profile.id;
    configuration.endpoint = profile.endpointSummary;
    configuration.placeholder = QStringLiteral("192.168.3.101:15200");
    configuration.editable = true;
    QCOMPARE(Data::ControllerConnectionProfileConfiguration(configuration), configuration);
    QVERIFY(!provider.connectionProfileConfiguration(scope, profile.id));
    const Utils::Result<> unsupportedEndpointEdit
        = provider.setConnectionProfileEndpoint(scope, profile.id, QStringLiteral("192.0.2.1"));
    QVERIFY(!unsupportedEndpointEdit);
    QCOMPARE(
        unsupportedEndpointEdit.error(),
        Tr::tr("This controller provider does not support editing connection profiles."));
    QVERIFY(!provider.supportsPackageDeployment());
    Data::ControllerPackageDeploymentRequest deploymentRequest;
    deploymentRequest.operationId = QStringLiteral("core-contract");
    deploymentRequest.artifact = QByteArray("signed-controller-package");
    deploymentRequest.configurationId = 813;
    QCOMPARE(Data::ControllerPackageDeploymentRequest(deploymentRequest), deploymentRequest);
    const Utils::Result<> unsupportedDeployment = provider.deployPackage(deploymentRequest);
    QVERIFY(!unsupportedDeployment);
    QCOMPARE(
        unsupportedDeployment.error(),
        Tr::tr("This controller provider does not support package deployment."));
    const Utils::Result<> unsupportedCancel = provider.cancelPackageDeployment(
        deploymentRequest.operationId);
    QVERIFY(!unsupportedCancel);
    QCOMPARE(
        unsupportedCancel.error(),
        Tr::tr("This controller provider does not support canceling package deployment."));
    QVERIFY(!provider.supportsRuntimeResources());
    QVERIFY(!provider.runtimeResourceCatalog());
    QVERIFY(!provider.runtimeResourceSnapshot());
    const Utils::Result<> unsupportedRuntimeRefresh = provider.refreshRuntimeResources();
    QVERIFY(!unsupportedRuntimeRefresh);
    QCOMPARE(
        unsupportedRuntimeRefresh.error(),
        Tr::tr("This controller provider does not support runtime resources."));
    QSignalSpy targetedFinishedSpy(
        &provider, &ControllerConnectionProvider::runtimeResourceSnapshotRequestFinished);
    const Utils::Result<> unsupportedTargetedRead = provider.requestRuntimeResourceSnapshot({});
    QVERIFY(!unsupportedTargetedRead);
    QCOMPARE(
        unsupportedTargetedRead.error(),
        Tr::tr("This controller provider does not support targeted runtime resource snapshots."));
    QCOMPARE(targetedFinishedSpy.count(), 0);

    QVERIFY(!provider.supportsRuntimeSemanticMappingAttestation());
    QVERIFY(!provider.runtimeSemanticMappingAttestation());
    QSignalSpy semanticAttestationChangedSpy(
        &provider, &ControllerConnectionProvider::runtimeSemanticMappingAttestationChanged);
    QSignalSpy semanticAttestationFinishedSpy(
        &provider,
        &ControllerConnectionProvider::runtimeSemanticMappingAttestationRequestFinished);
    const Utils::Result<> unsupportedSemanticAttestation
        = provider.requestRuntimeSemanticMappingAttestation({});
    QVERIFY(!unsupportedSemanticAttestation);
    QCOMPARE(
        unsupportedSemanticAttestation.error(),
        Tr::tr("This controller provider does not support semantic mapping attestation."));
    QCOMPARE(semanticAttestationChangedSpy.count(), 0);
    QCOMPARE(semanticAttestationFinishedSpy.count(), 0);

    RuntimeOutputFixture runtimeOutput;
    QVERIFY(!provider.supportsRuntimeOutputTransactions());
    QSignalSpy outputPolicyFinishedSpy(
        &provider, &ControllerConnectionProvider::runtimeOutputGroupPolicyRequestFinished);
    QSignalSpy outputStateChangedSpy(
        &provider, &ControllerConnectionProvider::runtimeOutputTransactionStateChanged);
    QSignalSpy outputStateFinishedSpy(
        &provider, &ControllerConnectionProvider::runtimeOutputTransactionStateRequestFinished);
    QSignalSpy outputTransactionFinishedSpy(
        &provider, &ControllerConnectionProvider::runtimeOutputTransactionFinished);
    const Utils::Result<> unsupportedOutputPolicy = provider.requestRuntimeOutputGroupPolicy(
        runtimeOutput.policyRequest);
    QVERIFY(!unsupportedOutputPolicy);
    QCOMPARE(
        unsupportedOutputPolicy.error(),
        Tr::tr("This controller provider does not support runtime output group policies."));
    const Utils::Result<> unsupportedOutputState = provider.requestRuntimeOutputTransactionState(
        runtimeOutput.stateRequest);
    QVERIFY(!unsupportedOutputState);
    QCOMPARE(
        unsupportedOutputState.error(),
        Tr::tr("This controller provider does not support runtime output transaction state."));
    const Utils::Result<> unsupportedOutputTransaction = provider.applyRuntimeOutputTransaction(
        runtimeOutput.transactionRequest);
    QVERIFY(!unsupportedOutputTransaction);
    QCOMPARE(
        unsupportedOutputTransaction.error(),
        Tr::tr("This controller provider does not support runtime output transactions."));
    QCOMPARE(outputPolicyFinishedSpy.count(), 0);
    QCOMPARE(outputStateChangedSpy.count(), 0);
    QCOMPARE(outputStateFinishedSpy.count(), 0);
    QCOMPARE(outputTransactionFinishedSpy.count(), 0);

    Data::ControllerPackageDeploymentProgress deploymentProgress;
    deploymentProgress.operationId = deploymentRequest.operationId;
    deploymentProgress.artifactSha256 = QByteArray(32, '\x5a');
    deploymentProgress.state = Data::ControllerPackageDeploymentState::Uploading;
    deploymentProgress.totalBytes = deploymentRequest.artifact.size();
    deploymentProgress.transferredBytes = 7;
    deploymentProgress.candidate = Data::ControllerPackageSelector{
        Data::ControllerSlot::B, 12, deploymentRequest.configurationId};
    deploymentProgress.previousActive
        = Data::ControllerPackageSelector{Data::ControllerSlot::A, 11, 810};
    Data::ControllerPackageDeploymentAuditEvent auditEvent;
    auditEvent.sequence = 1;
    auditEvent.operation = Data::ControllerOperation::UploadPackage;
    auditEvent.requestId = 42;
    auditEvent.detail = QStringLiteral("BulkBegin queued");
    auditEvent.occurredAt = QDateTime::currentDateTimeUtc();
    deploymentProgress.audit.append(auditEvent);
    QCOMPARE(
        Data::ControllerPackageSelector(*deploymentProgress.candidate),
        *deploymentProgress.candidate);
    QVERIFY(deploymentProgress.candidate->isValid());
    QCOMPARE(
        Data::ControllerPackageDeploymentAuditEvent(deploymentProgress.audit.constFirst()),
        auditEvent);
    QCOMPARE(Data::ControllerPackageDeploymentProgress(deploymentProgress), deploymentProgress);

    TestControllerConnectionProvider alternateProvider(
        "EtherCAT.Connection.VendorIpc",
        "Vendor IPC controller",
        "ipc://controller-1",
        {"primary"},
        profile.id);
    const Data::ControllerConnectionProfile alternateProfile
        = alternateProvider.connectionProfiles(scope).constFirst();
    QCOMPARE(alternateProfile.id, profile.id);
    alternateProvider.setAvailable(true);
    QVERIFY_RESULT(alternateProvider.connectToController(request));
    QCOMPARE(alternateProvider.connectionSnapshot().channels.size(), 1);
    QCOMPARE(alternateProvider.connectionSnapshot().channels.constFirst().id, QString("primary"));
    QCOMPARE(alternateProvider.connectionSnapshot().endpointSummary, QString("ipc://controller-1"));
    QVERIFY_RESULT(alternateProvider.disconnectFromController());

    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    QVERIFY(provider.connectionSnapshot().readOnly);
    QVERIFY(!provider.refreshController());

    QSignalSpy profilesSpy(&provider, &ControllerConnectionProvider::connectionProfilesChanged);
    QSignalSpy snapshotSpy(&provider, &ControllerConnectionProvider::connectionSnapshotChanged);
    QSignalSpy catalogSpy(&provider, &ControllerConnectionProvider::runtimeResourceCatalogChanged);
    QSignalSpy
        runtimeSnapshotSpy(&provider, &ControllerConnectionProvider::runtimeResourceSnapshotChanged);
    QCOMPARE(catalogSpy.count(), 0);
    QCOMPARE(runtimeSnapshotSpy.count(), 0);
    QVERIFY(!provider.isAvailable());
    QVERIFY(!provider.connectToController(request));
    QCOMPARE(snapshotSpy.count(), 0);
    provider.setAvailable(true);
    QVERIFY(provider.isAvailable());
    QVERIFY(!provider.connectToController({}));
    Data::ControllerConnectionRequest invalidRequest = request;
    invalidRequest.scope.projectId = {};
    QVERIFY(!provider.connectToController(invalidRequest));
    invalidRequest = request;
    invalidRequest.scope.masterId = {};
    QVERIFY(!provider.connectToController(invalidRequest));
    invalidRequest = request;
    invalidRequest.profileId = {};
    QVERIFY(!provider.connectToController(invalidRequest));
    invalidRequest.profileId = Data::NodeId::create();
    QVERIFY(invalidRequest.profileId != profile.id);
    QVERIFY(!provider.connectToController(invalidRequest));
    provider.setProfileConfigured(false);
    QCOMPARE(profilesSpy.count(), 1);
    const Data::ControllerConnectionProfile incompleteProfile
        = provider.connectionProfiles(scope).constFirst();
    QVERIFY(!incompleteProfile.configured);
    QVERIFY(!incompleteProfile.configurationIssue.isEmpty());
    QVERIFY(!provider.connectToController(request));
    provider.setProfileConfigured(true);
    QCOMPARE(profilesSpy.count(), 2);
    provider.setProfileSupported(false);
    QCOMPARE(profilesSpy.count(), 3);
    QVERIFY(!provider.connectionProfiles(scope).constFirst().supported);
    QVERIFY(!provider.connectToController(request));
    provider.setProfileSupported(true);
    QCOMPARE(profilesSpy.count(), 4);
    QVERIFY(provider.connectionProfiles({}).isEmpty());
    QCOMPARE(snapshotSpy.count(), 0);

    const Utils::Result<> connectResult = provider.connectToController(request);
    QVERIFY_RESULT(connectResult);
    const Data::ControllerConnectionSnapshot connecting = provider.connectionSnapshot();
    QCOMPARE(connecting.scope, request.scope);
    QCOMPARE(connecting.profileId, request.profileId);
    QCOMPARE(connecting.endpointSummary, profile.endpointSummary);
    QCOMPARE(connecting.state, Data::ControllerConnectionState::Connecting);
    QCOMPARE(connecting.sessionGeneration, quint64(1));
    QCOMPARE(connecting.channels.size(), 3);
    QCOMPARE(connecting.channels.at(0).id, QString("control"));
    QCOMPARE(connecting.channels.at(0).displayName, QString("CONTROL"));
    QCOMPARE(connecting.channels.at(0).state, Data::ControllerChannelState::Connecting);
    QCOMPARE(connecting.channels.at(0).maximumPayloadBytes, 4096);
    QCOMPARE(connecting.channels.at(1).id, QString("events"));
    QCOMPARE(connecting.channels.at(1).maximumPayloadBytes, 65536);
    QCOMPARE(connecting.channels.at(2).id, QString("bulk"));
    QCOMPARE(connecting.channels.at(2).maximumPayloadBytes, 65536);
    QVERIFY(connecting.readOnly);
    QCOMPARE(snapshotSpy.count(), 1);
    QVERIFY(!provider.connectToController(request));

    provider.completeHandshake();
    const Data::ControllerConnectionSnapshot connected = provider.connectionSnapshot();
    QCOMPARE(connected.state, Data::ControllerConnectionState::Connected);
    QCOMPARE(connected.protocolVersion, Data::ControllerProtocolVersion(1, 9));
    QVERIFY(connected.session);
    QCOMPARE(connected.session->sessionId, quint64(17));
    QCOMPARE(connected.session->bootId, quint64(42));
    QVERIFY(!connected.session->ownsControlLease);
    QVERIFY(connected.controllerState);
    QCOMPARE(connected.controllerState->serviceState, Data::ControllerServiceState::Shutdown);
    QVERIFY(connected.controllerState->ready);
    QVERIFY(connected.capability);
    QCOMPARE(connected.capability->maximumSlaves, 64);
    QCOMPARE(connected.capability->descriptorSha256.size(), 32);
    QVERIFY(connected.capability->controlLease);
    QVERIFY(connected.capability->resumablePush);
    QVERIFY(connected.capability->transactionalBulk);
    QVERIFY(!connected.capability->runtimeOutputTransactions);
    QVERIFY(connected.capability->firmwareUpdate);
    QVERIFY(connected.capability->coe);
    QVERIFY(connected.capability->distributedClocks);
    QVERIFY(connected.package);
    QCOMPARE(connected.package->activeSlot, Data::ControllerSlot::A);
    QCOMPARE(connected.package->activeConfigurationId, quint64(810));
    QCOMPARE(connected.package->controllerState, Data::ControllerPackageState::Empty);
    QVERIFY(connected.firmware);
    QCOMPARE(connected.firmware->state, Data::ControllerFirmwareState::Confirmed);
    QCOMPARE(connected.firmware->generation, quint64(2517));
    QCOMPARE(Data::ControllerConnectionSnapshot(connected), connected);
    QCOMPARE(snapshotSpy.count(), 2);

    provider.setAvailable(false);
    QVERIFY(!provider.refreshController());
    QCOMPARE(snapshotSpy.count(), 2);
    provider.setAvailable(true);
    const Utils::Result<> refreshResult = provider.refreshController();
    QVERIFY_RESULT(refreshResult);
    QVERIFY(provider.connectionSnapshot().updatedAt.isValid());
    QCOMPARE(snapshotSpy.count(), 3);

    provider.degradePush();
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Degraded);
    QCOMPARE(provider.connectionSnapshot().channels.at(1).state, Data::ControllerChannelState::Failed);
    QCOMPARE(snapshotSpy.count(), 4);
    const Utils::Result<> degradedRefreshResult = provider.refreshController();
    QVERIFY_RESULT(degradedRefreshResult);
    QCOMPARE(snapshotSpy.count(), 5);

    provider.failProtocol();
    const Data::ControllerConnectionSnapshot failed = provider.connectionSnapshot();
    QCOMPARE(failed.state, Data::ControllerConnectionState::Failed);
    QVERIFY(failed.lastError);
    QCOMPARE(failed.lastError->source, Data::ControllerErrorSource::Protocol);
    QCOMPARE(failed.lastError->channelId, QString("events"));
    QCOMPARE(failed.lastError->operation, Data::ControllerOperation::SubscribeEvents);
    QVERIFY(!failed.lastError->code);
    QVERIFY(!failed.lastError->operationResult);
    QCOMPARE(failed.lastError->codeName, QString("UNEXPECTED_PUSH_FRAME"));
    QVERIFY(!failed.lastError->sourceDetail);
    QVERIFY(failed.lastError->requestId);
    QCOMPARE(*failed.lastError->requestId, quint64(73));
    QCOMPARE(failed.lastError->retryDisposition, Data::ControllerRetryDisposition::Reconnect);
    QCOMPARE(snapshotSpy.count(), 6);

    Data::ControllerOperationError controllerError;
    controllerError.source = Data::ControllerErrorSource::Controller;
    controllerError.channelId = "control";
    controllerError.operation = Data::ControllerOperation::QueryState;
    controllerError.code = -6;
    controllerError.operationResult = -2;
    controllerError.sourceDetail = 17;
    controllerError.codeName = "BAD_MESSAGE";
    controllerError.retryAfterMs = 250;
    const Data::ControllerOperationError controllerErrorCopy = controllerError;
    QCOMPARE(controllerErrorCopy, controllerError);
    QCOMPARE(*controllerErrorCopy.sourceDetail, quint64(17));
    QCOMPARE(*controllerErrorCopy.retryAfterMs, 250);

    provider.setAvailable(false);
    const Utils::Result<> failedDisconnectResult = provider.disconnectFromController();
    QVERIFY_RESULT(failedDisconnectResult);
    QCOMPARE(provider.connectionSnapshot().state, Data::ControllerConnectionState::Disconnected);
    for (const Data::ControllerChannelStatus &channel : provider.connectionSnapshot().channels)
        QCOMPARE(channel.state, Data::ControllerChannelState::Disconnected);
    QCOMPARE(provider.connectionSnapshot().scope, request.scope);
    QCOMPARE(provider.connectionSnapshot().profileId, request.profileId);
    QVERIFY(!provider.connectionSnapshot().session);
    QVERIFY(!provider.connectionSnapshot().lastHeartbeatAt.isValid());
    QCOMPARE(provider.connectionSnapshot().sessionGeneration, quint64(2));
    QCOMPARE(snapshotSpy.count(), 7);
    QVERIFY(!provider.connectToController(request));
    QCOMPARE(snapshotSpy.count(), 7);

    provider.setAvailable(true);
    const Utils::Result<> reconnectResult = provider.connectToController(request);
    QVERIFY_RESULT(reconnectResult);
    QCOMPARE(provider.connectionSnapshot().sessionGeneration, quint64(3));
    QCOMPARE(snapshotSpy.count(), 8);
    const Utils::Result<> reconnectDisconnectResult = provider.disconnectFromController();
    QVERIFY_RESULT(reconnectDisconnectResult);
    QCOMPARE(provider.connectionSnapshot().sessionGeneration, quint64(4));
    QCOMPARE(snapshotSpy.count(), 9);
    const Utils::Result<> repeatedDisconnectResult = provider.disconnectFromController();
    QVERIFY_RESULT(repeatedDisconnectResult);
    QCOMPARE(snapshotSpy.count(), 9);
}

void EtherCATCoreTests::testScanProviderContract()
{
    TestScanProvider provider;
    QCOMPARE(provider.kind(), ProviderKind::Scan);
    QCOMPARE(provider.scanState(), Data::ScanState::Idle);
    QVERIFY(!provider.lastScanResult());

    QSignalSpy stateSpy(&provider, &ScanProvider::scanStateChanged);
    QSignalSpy progressSpy(&provider, &ScanProvider::scanProgressChanged);
    QSignalSpy finishedSpy(&provider, &ScanProvider::scanFinished);
    const Data::ScanRequest
        request{Data::NodeId::create(), Data::NodeId::create(), Data::ScanOperation::Slaves, {}};
    QVERIFY_RESULT(provider.startScan(request));
    QCOMPARE(provider.scanState(), Data::ScanState::Preparing);
    QCOMPARE(provider.scanProgress().maximum, 4);
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(progressSpy.count(), 1);

    QVERIFY(!provider.startScan(request));
    provider.complete();
    QCOMPARE(provider.scanState(), Data::ScanState::Completed);
    QVERIFY(provider.lastScanResult());
    QVERIFY(provider.lastScanResult()->snapshot.mock);
    QCOMPARE(provider.lastScanResult()->snapshot.slaves.first().serialNumber, quint32(17));
    QCOMPARE(provider.lastScanResult()->comparison.differences.size(), 1);
    QCOMPARE(finishedSpy.count(), 1);

    provider.clearScanResult();
    QCOMPARE(provider.scanState(), Data::ScanState::Idle);
    QVERIFY_RESULT(provider.startScan(request));
    provider.cancelScan();
    QCOMPARE(provider.scanState(), Data::ScanState::Cancelled);
    QCOMPARE(finishedSpy.count(), 2);
    provider.clearScanResult();
    QCOMPARE(provider.scanState(), Data::ScanState::Idle);
}

void EtherCATCoreTests::testDiagnosticsProviderContract()
{
    TestDiagnosticsProvider provider;
    QCOMPARE(provider.kind(), ProviderKind::Diagnostics);
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Stopped);
    QCOMPARE(provider.limits(), Data::DiagnosticsLimits(4, 5, 10, 100));
    QVERIFY(!provider.latestSnapshot());
    QVERIFY(provider.events().isEmpty());
    QVERIFY(provider.trendSamples().isEmpty());

    QSignalSpy stateSpy(&provider, &DiagnosticsProvider::streamStateChanged);
    QSignalSpy snapshotSpy(&provider, &DiagnosticsProvider::diagnosticsSnapshotChanged);
    QSignalSpy eventSpy(&provider, &DiagnosticsProvider::diagnosticEventsChanged);
    QSignalSpy trendSpy(&provider, &DiagnosticsProvider::diagnosticTrendChanged);
    QSignalSpy stoppedSpy(&provider, &DiagnosticsProvider::monitoringStopped);
    QVERIFY(!provider.startMonitoring({}));

    const Data::DiagnosticsRequest request{Data::NodeId::create(), Data::NodeId::create()};
    const Utils::Result<> startResult = provider.startMonitoring(request);
    QVERIFY_RESULT(startResult);
    QCOMPARE(provider.activeRequest(), request);
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Running);
    QCOMPARE(stateSpy.count(), 2);
    QVERIFY(!provider.startMonitoring(request));

    provider.publish();
    QVERIFY(provider.latestSnapshot());
    const Data::DiagnosticsSnapshot snapshot = *provider.latestSnapshot();
    const Data::DiagnosticsSnapshot snapshotCopy = snapshot;
    QCOMPARE(snapshotCopy, snapshot);
    QVERIFY(snapshot.mock);
    QCOMPARE(snapshot.masterState, Data::EtherCATState::Operational);
    QCOMPARE(snapshot.workingCounter.expected, quint32(4));
    QCOMPARE(snapshot.workingCounter.actual, quint32(3));
    QCOMPARE(snapshot.frameErrors.overflows, quint64(6));
    QCOMPARE(snapshot.distributedClock.offsetNs, qint64(17));
    QCOMPARE(snapshot.cycle.nominalCycleNs, qint64(125000));
    QCOMPARE(snapshot.slaves.size(), 1);
    QCOMPARE(provider.events().size(), 1);
    QCOMPARE(provider.events().first().repeatCount, quint32(2));
    QCOMPARE(provider.trendSamples().size(), 1);
    QCOMPARE(snapshotSpy.count(), 1);
    QCOMPARE(eventSpy.count(), 1);
    QCOMPARE(trendSpy.count(), 1);

    const Utils::Result<> modeResult = provider.requestRunMode(Data::DiagnosticsRunMode::Config);
    QVERIFY_RESULT(modeResult);
    QCOMPARE(provider.latestSnapshot()->runMode, Data::DiagnosticsRunMode::Config);
    QCOMPARE(snapshotSpy.count(), 2);

    const Data::NodeId alarmId = provider.events().first().id;
    const Utils::Result<> acknowledgeResult = provider.acknowledgeAlarm(alarmId);
    QVERIFY_RESULT(acknowledgeResult);
    QCOMPARE(provider.events().first().lifecycle, Data::AlarmLifecycle::Acknowledged);
    QVERIFY(!provider.acknowledgeAlarm(alarmId));
    provider.recoverAlarm();
    const Utils::Result<> clearResult = provider.clearRecoveredEvents();
    QVERIFY_RESULT(clearResult);
    QVERIFY(provider.events().isEmpty());
    QCOMPARE(eventSpy.count(), 4);

    provider.stopMonitoring();
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Stopped);
    QCOMPARE(provider.activeRequest(), Data::DiagnosticsRequest());
    QCOMPARE(stateSpy.count(), 4);
    QCOMPARE(stoppedSpy.count(), 1);
    provider.stopMonitoring();
    QCOMPARE(stoppedSpy.count(), 1);
    QVERIFY(!provider.requestRunMode(Data::DiagnosticsRunMode::Run));

    const Utils::Result<> restartResult = provider.startMonitoring(request);
    QVERIFY_RESULT(restartResult);
    provider.fail("Mock diagnostics source failed");
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Failed);
    QCOMPARE(provider.lastDiagnosticsError(), QString("Mock diagnostics source failed"));
    QCOMPARE(stoppedSpy.count(), 2);
    provider.stopMonitoring();
    QCOMPARE(provider.streamState(), Data::DiagnosticsStreamState::Stopped);
    QCOMPARE(stoppedSpy.count(), 2);
}

void EtherCATCoreTests::testSelectionServicePublishesStableIds()
{
    SelectionService *service = ExtensionSystem::PluginManager::getObject<SelectionService>();
    QVERIFY(service);
    service->clear();

    QSignalSpy changedSpy(service, &SelectionService::currentNodeChanged);
    const Data::NodeId selectedId = Data::NodeId::create();
    service->setCurrentNodeId(selectedId);
    QCOMPARE(service->currentNodeId(), selectedId);
    QCOMPARE(changedSpy.count(), 1);

    service->setCurrentNodeId(selectedId);
    QCOMPARE(changedSpy.count(), 1);

    service->clear();
    QVERIFY(service->currentNodeId().isNull());
    QCOMPARE(changedSpy.count(), 2);
}

void EtherCATCoreTests::testStateServiceAggregatesContributions()
{
    StateService *service = ExtensionSystem::PluginManager::getObject<StateService>();
    QVERIFY(service);
    service->clearAll();

    QSignalSpy aggregateSpy(service, &StateService::aggregateSeverityChanged);
    QVERIFY(!service->setStatus({}));
    QVERIFY(service->setStatus({"EtherCAT.Project", StatusSeverity::Busy, "Opening", {}}));
    QCOMPARE(service->aggregateSeverity(), StatusSeverity::Busy);
    QVERIFY(service->setStatus({"EtherCAT.Scan", StatusSeverity::Error, "Failed", "Mock"}));
    QCOMPARE(service->aggregateSeverity(), StatusSeverity::Error);
    QCOMPARE(service->statuses().size(), 2);
    QCOMPARE(service->statuses().at(0).sourceId, Utils::Id("EtherCAT.Project"));
    QCOMPARE(aggregateSpy.count(), 2);

    service->clearStatus("EtherCAT.Scan");
    QCOMPARE(service->aggregateSeverity(), StatusSeverity::Busy);
    service->clearAll();
    QCOMPARE(service->aggregateSeverity(), StatusSeverity::Ready);
    QVERIFY(service->statuses().isEmpty());
}

void EtherCATCoreTests::testProviderRegistryTracksObjectPool()
{
    ProviderRegistry *registry = ExtensionSystem::PluginManager::getObject<ProviderRegistry>();
    QVERIFY(registry);

    QSignalSpy addedSpy(registry, &ProviderRegistry::providerAdded);
    QSignalSpy removedSpy(registry, &ProviderRegistry::providerAboutToBeRemoved);
    TestScanProvider provider;
    TestPropertyPageProvider reentrantProvider;
    TestControllerConnectionProvider connectionProvider;
    TestControllerConnectionProvider alternateConnectionProvider(
        "EtherCAT.Connection.VendorIpc", "Vendor IPC controller", "ipc://controller-1", {"primary"});

    ExtensionSystem::PluginManager::addObject(&provider);
    QCOMPARE(registry->provider(provider.id()), &provider);
    QCOMPARE(registry->providers(ProviderKind::Scan), QList<Provider *>({&provider}));
    QCOMPARE(addedSpy.count(), 1);

    provider.setAvailable(true);
    QVERIFY(provider.isAvailable());

    ExtensionSystem::PluginManager::addObject(&reentrantProvider);
    QCOMPARE(registry->provider(reentrantProvider.id()), &reentrantProvider);
    QCOMPARE(addedSpy.count(), 2);

    ExtensionSystem::PluginManager::addObject(&connectionProvider);
    QCOMPARE(registry->provider(connectionProvider.id()), &connectionProvider);
    QCOMPARE(
        registry->providers(ProviderKind::ControllerConnection),
        QList<Provider *>({&connectionProvider}));
    QCOMPARE(addedSpy.count(), 3);

    ExtensionSystem::PluginManager::addObject(&alternateConnectionProvider);
    QCOMPARE(registry->provider(alternateConnectionProvider.id()), &alternateConnectionProvider);
    QCOMPARE(
        registry->providers(ProviderKind::ControllerConnection),
        QList<Provider *>({&connectionProvider, &alternateConnectionProvider}));
    QCOMPARE(addedSpy.count(), 4);

    bool departingProviderUnlinked = false;
    bool nestedRemovalObserved = false;
    connect(
        registry,
        &ProviderRegistry::providerAboutToBeRemoved,
        &reentrantProvider,
        [&](Provider *departingProvider) {
            if (departingProvider != &reentrantProvider)
                return;
            departingProviderUnlinked = !registry->provider(reentrantProvider.id());
            nestedRemovalObserved = true;
            ExtensionSystem::PluginManager::removeObject(&provider);
        },
        Qt::DirectConnection);
    ExtensionSystem::PluginManager::removeObject(&reentrantProvider);
    QVERIFY(departingProviderUnlinked);
    QVERIFY(nestedRemovalObserved);
    QVERIFY(!registry->provider(provider.id()));
    QVERIFY(!registry->provider(reentrantProvider.id()));
    QVERIFY(registry->providers(ProviderKind::Scan).isEmpty());
    QVERIFY(!registry->providers(ProviderKind::PropertyPage).contains(&reentrantProvider));
    QCOMPARE(removedSpy.count(), 2);

    ExtensionSystem::PluginManager::removeObject(&connectionProvider);
    QVERIFY(!registry->provider(connectionProvider.id()));
    QCOMPARE(
        registry->providers(ProviderKind::ControllerConnection),
        QList<Provider *>({&alternateConnectionProvider}));
    QCOMPARE(removedSpy.count(), 3);

    ExtensionSystem::PluginManager::removeObject(&alternateConnectionProvider);
    QVERIFY(!registry->provider(alternateConnectionProvider.id()));
    QVERIFY(registry->providers(ProviderKind::ControllerConnection).isEmpty());
    QCOMPARE(removedSpy.count(), 4);
}

void EtherCATCoreTests::testSettingsPageIsRegistered()
{
    const QList<::Core::IOptionsPage *> pages = ::Core::IOptionsPage::allOptionsPages();
    const auto found = std::find_if(pages.cbegin(), pages.cend(), [](const auto *page) {
        return page->id() == Constants::SETTINGS_GENERAL;
    });
    QVERIFY(found != pages.cend());
    QCOMPARE((*found)->category(), Utils::Id(Constants::SETTINGS_CATEGORY));
    QCOMPARE((*found)->displayCategory(), QString("EtherCAT"));
    QVERIFY((*found)->aspects().has_value());

    std::unique_ptr<::Core::IOptionsPageWidget> widget((*found)->createWidget());
    QVERIFY(widget);
    QVERIFY(!settings().showAdvancedProperties());
    QCOMPARE(settings().maximumRecentEvents(), 1000);
}

} // namespace EtherCAT::Core::Internal
