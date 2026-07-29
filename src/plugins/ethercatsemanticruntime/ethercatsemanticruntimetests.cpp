// Copyright (C) 2026 Embed Labs

#include "ethercatsemanticruntimetests.h"

#include "semanticruntimeexecutor.h"

#include <extensionsystem/pluginmanager.h>

#include <QSignalSpy>
#include <QTest>

#include <algorithm>

namespace EtherCAT::SemanticRuntime::Internal {

class TestProjectService final : public Core::ProjectService
{
public:
    TestProjectService()
        : ProjectService("EtherCAT.SemanticRuntime.Tests.Project", "Semantic Runtime test projects")
    {
        setAvailable(true);
    }

    QList<Data::ProjectSnapshot> projects() const final { return m_projects; }

    std::optional<Data::ProjectSnapshot> project(const Data::NodeId &projectId) const final
    {
        const auto found = std::find_if(
            m_projects.cbegin(),
            m_projects.cend(),
            [&projectId](const Data::ProjectSnapshot &candidate) {
                return candidate.id == projectId;
            });
        if (found == m_projects.cend())
            return std::nullopt;
        return *found;
    }

    Data::NodeId activeProjectId() const final { return m_activeProjectId; }
    bool managesProject(const QObject *) const final { return false; }

    Utils::Result<> activateProject(const Data::NodeId &projectId) final
    {
        if (!project(projectId))
            return Utils::ResultError("Unknown test project");
        const Data::NodeId previous = m_activeProjectId;
        m_activeProjectId = projectId;
        emit activeProjectChanged(previous, m_activeProjectId);
        return Utils::ResultOk;
    }

    Utils::Result<> renameProject(const Data::NodeId &, const QString &) final
    {
        return unsupported();
    }

    Utils::Result<> saveProject(const Data::NodeId &) final { return unsupported(); }
    Utils::Result<> undoProject(const Data::NodeId &) final { return unsupported(); }
    Utils::Result<> redoProject(const Data::NodeId &) final { return unsupported(); }

    Utils::Result<> setMasterConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::MasterConfiguration &) final
    {
        return unsupported();
    }

    Utils::Result<> replaceOfflineSlaves(
        const Data::NodeId &,
        const Data::NodeId &,
        const QList<Data::OfflineSlaveConfiguration> &) final
    {
        return unsupported();
    }

    Utils::Result<> setProcessDataConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::ProcessDataConfiguration &) final
    {
        return unsupported();
    }

    Utils::Result<> setStartupConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::StartupConfiguration &) final
    {
        return unsupported();
    }

    Utils::Result<> setDcConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::DcConfiguration &) final
    {
        return unsupported();
    }

    bool canUndoProject(const Data::NodeId &) const final { return false; }
    bool canRedoProject(const Data::NodeId &) const final { return false; }

    Utils::Result<> renameStructuralNode(
        const Data::NodeId &, const Data::NodeId &, const QString &) final
    {
        return unsupported();
    }

    Utils::Result<> setDeviceAdapterSelection(
        const Data::NodeId &,
        const Data::NodeId &,
        const QByteArray &,
        const Data::DeviceAdapterProjectSelection &) final
    {
        return unsupported();
    }

    Utils::Result<> setMasterBindingArtifact(
        const Data::NodeId &, const Data::SemanticBindingArtifactReference &) final
    {
        return unsupported();
    }

    void addProject(const Data::ProjectSnapshot &project)
    {
        m_projects.append(project);
        emit projectAdded(project);
    }

    void changeProject(const Data::ProjectSnapshot &project)
    {
        for (Data::ProjectSnapshot &candidate : m_projects) {
            if (candidate.id != project.id)
                continue;
            candidate = project;
            emit projectChanged(project);
            return;
        }
    }

    void removeProject(const Data::NodeId &projectId)
    {
        for (qsizetype index = 0; index < m_projects.size(); ++index) {
            if (m_projects.at(index).id != projectId)
                continue;
            emit projectAboutToBeRemoved(projectId);
            m_projects.removeAt(index);
            return;
        }
    }

private:
    static Utils::Result<> unsupported()
    {
        return Utils::ResultError("Test project mutation is not implemented");
    }

    QList<Data::ProjectSnapshot> m_projects;
    Data::NodeId m_activeProjectId;
};

class CountingControllerProvider final : public Core::ControllerConnectionProvider
{
public:
    explicit CountingControllerProvider(Utils::Id id)
        : ControllerConnectionProvider(id, QStringLiteral("Semantic Runtime counting controller"))
    {}

    QList<Data::ControllerConnectionProfile> connectionProfiles(
        const Data::ControllerConnectionScope &) const final
    {
        return {};
    }

    Utils::Result<> setConnectionProfileEndpoint(
        const Data::ControllerConnectionScope &, const Data::NodeId &, const QString &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    Data::ControllerConnectionSnapshot connectionSnapshot() const final
    {
        ++snapshotReads;
        return snapshot;
    }

    Utils::Result<> connectToController(const Data::ControllerConnectionRequest &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    Utils::Result<> disconnectFromController() final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    Utils::Result<> refreshController() final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    bool supportsControlCommand(Data::ControllerControlCommand) const final { return true; }

    Utils::Result<> executeControlCommand(const Data::ControllerControlRequest &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    bool supportsPackageDeployment() const final { return true; }

    Utils::Result<> deployPackage(const Data::ControllerPackageDeploymentRequest &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    Utils::Result<> cancelPackageDeployment(const QString &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    bool supportsRuntimeResources() const final { return runtimeResourcesSupported; }

    std::optional<Data::RuntimeResourceCatalog> runtimeResourceCatalog() const final
    {
        ++catalogReads;
        return catalog;
    }

    std::optional<Data::RuntimeResourceSnapshot> runtimeResourceSnapshot() const final
    {
        ++resourceSnapshotReads;
        return std::nullopt;
    }

    Utils::Result<> refreshRuntimeResources() final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    Utils::Result<> requestRuntimeResourceSnapshot(const Data::RuntimeResourceSnapshotRequest &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    void publishSnapshot(const Data::ControllerConnectionSnapshot &value)
    {
        snapshot = value;
        emit connectionSnapshotChanged();
    }

    void publishCatalog(const std::optional<Data::RuntimeResourceCatalog> &value)
    {
        catalog = value;
        emit runtimeResourceCatalogChanged();
    }

    void setRuntimeResourcesSupported(bool supported)
    {
        runtimeResourcesSupported = supported;
        emit runtimeResourceCatalogChanged();
    }

    mutable int snapshotReads = 0;
    mutable int catalogReads = 0;
    mutable int resourceSnapshotReads = 0;
    int mutationCalls = 0;
    bool runtimeResourcesSupported = true;
    Data::ControllerConnectionSnapshot snapshot;
    std::optional<Data::RuntimeResourceCatalog> catalog;

private:
    static Utils::Result<> rejectedMutation()
    {
        return Utils::ResultError("Counting controller must not be mutated");
    }
};

class RegisteredObject
{
public:
    explicit RegisteredObject(QObject *object)
        : m_object(object)
    {
        ExtensionSystem::PluginManager::addObject(m_object);
    }

    ~RegisteredObject() { remove(); }

    void remove()
    {
        if (!m_object)
            return;
        ExtensionSystem::PluginManager::removeObject(m_object);
        m_object = nullptr;
    }

private:
    QObject *m_object = nullptr;
};

static Data::ControllerConnectionScope projectScope(const Data::ProjectSnapshot &project)
{
    for (const Data::ProjectNodeSnapshot &node : project.nodes) {
        if (node.kind == Data::ProjectNodeKind::Master)
            return {project.id, node.id};
    }
    return {};
}

static Data::SemanticBindingArtifactReference bindingArtifact()
{
    return {
        QStringLiteral("binding/embed-labs/runtime/1"),
        QByteArray(32, '\x6b'),
        QByteArray(32, '\x7c'),
    };
}

static Data::ProjectSnapshot testProject(bool includeBinding = true, bool includeAdapters = false)
{
    Data::ProjectSnapshot project;
    project.id = Data::NodeId::create();
    project.name = QStringLiteral("Semantic Runtime Test");
    project.formatVersion = 4;
    project.createdBy = QStringLiteral("EtherCATSemanticRuntimeTests");
    project.valid = true;

    const Data::NodeId masterId = Data::NodeId::create();
    project.nodes = {
        {project.id, {}, Data::ProjectNodeKind::Project, project.name},
        {masterId, project.id, Data::ProjectNodeKind::Master, QStringLiteral("Master")},
    };
    if (includeBinding)
        project.masterBindingArtifact = bindingArtifact();

    if (includeAdapters) {
        Data::OfflineSlaveConfiguration xb6;
        xb6.id = Data::NodeId::create();
        xb6.masterId = masterId;
        xb6.position = 0;
        xb6.name = QStringLiteral("XB6-EC0002");
        xb6.esiSha256 = QByteArray(32, '\x11');
        xb6.adapterSelection = {
            Data::DeviceAdapterId{QStringLiteral("org.embedlabs.adapter.solidot.xb6-ec0002.rev1")},
            QStringLiteral("0.1.0"),
            QByteArray(32, '\x21'),
            QStringLiteral("do16-default"),
            {},
        };

        Data::OfflineSlaveConfiguration sv630n;
        sv630n.id = Data::NodeId::create();
        sv630n.masterId = masterId;
        sv630n.position = 1;
        sv630n.name = QStringLiteral("SV630N_1Axis_03716");
        sv630n.esiSha256 = QByteArray(32, '\x12');
        sv630n.adapterSelection = {
            Data::DeviceAdapterId{
                QStringLiteral("org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000")},
            QStringLiteral("1.4.0"),
            QByteArray(32, '\x22'),
            QStringLiteral("sync0-125us"),
            {},
        };
        project.slaves = {xb6, sv630n};
    }
    return project;
}

static Data::ControllerConnectionSnapshot connectedSnapshot(
    const Data::ControllerConnectionScope &scope, quint64 sessionGeneration)
{
    Data::ControllerConnectionSnapshot snapshot;
    snapshot.scope = scope;
    snapshot.state = Data::ControllerConnectionState::Connected;
    snapshot.sessionGeneration = sessionGeneration;
    snapshot.readOnly = false;
    return snapshot;
}

static Data::RuntimeResourceCatalogEpoch catalogEpoch(quint64 runtimeGeneration = 5)
{
    Data::RuntimeResourceCatalogEpoch epoch;
    epoch.controllerBootId = 0x1122334455667788;
    epoch.activePackageSlot = Data::ControllerSlot::A;
    epoch.activePackageGeneration = 3;
    epoch.configurationId = 813;
    epoch.topologyGeneration = 4;
    epoch.runtimeGeneration = runtimeGeneration;
    epoch.catalogRevision = 6;
    epoch.topologyIdentity = QByteArray(32, '\x33');
    return epoch;
}

static Data::RuntimeResourceCatalog resourceCatalog(
    const Data::ControllerConnectionScope &scope,
    quint64 sessionGeneration,
    const Data::RuntimeResourceCatalogEpoch &epoch = catalogEpoch())
{
    Data::RuntimeResourceCatalog catalog;
    catalog.scope = scope;
    catalog.sessionGeneration = sessionGeneration;
    catalog.epoch = epoch;

    Data::RuntimeResourceDescriptor descriptor;
    descriptor.id.value = QByteArray::fromHex("1000000000000001");
    descriptor.componentInstanceId.value = QByteArray::fromHex("2000000000000001");
    descriptor.consistencyGroupId.value = QByteArray::fromHex("00000001");
    descriptor.primitiveType = Data::RuntimeResourcePrimitiveType::UnsignedInteger;
    descriptor.bitWidth = 16;
    descriptor.direction = Data::RuntimeResourceDirection::Input;
    descriptor.access = Data::RuntimeResourceAccess::ReadOnly;
    catalog.resources = {descriptor};
    return catalog;
}

static QString detailFor(SemanticRuntimeContextIssue issue)
{
    return semanticRuntimeContextIssueDetail(issue);
}

void EtherCATSemanticRuntimeTests::testPublishesOneProductionService()
{
    QList<Core::SemanticRuntimeService *> services;
    for (QObject *object : ExtensionSystem::PluginManager::allObjects()) {
        if (auto *service = qobject_cast<Core::SemanticRuntimeService *>(object))
            services.append(service);
    }

    QCOMPARE(services.size(), 1);
    QCOMPARE(
        ExtensionSystem::PluginManager::getObject<Core::SemanticRuntimeService>(),
        services.constFirst());
    QVERIFY(qobject_cast<SemanticRuntimeExecutor *>(services.constFirst()));
}

void EtherCATSemanticRuntimeTests::testProjectAndProviderLifecycle()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    SemanticRuntimeExecutor executor(&projects, registry);
    QSignalSpy contextsSpy(&executor, &Core::SemanticRuntimeService::contextsChanged);
    QVERIFY(contextsSpy.isValid());
    QVERIFY(executor.contexts().isEmpty());

    const Data::ProjectSnapshot project = testProject();
    const Data::ControllerConnectionScope scope = projectScope(project);
    projects.addProject(project);
    QCOMPARE(executor.contexts().size(), 1);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));

    projects.setAvailable(false);
    QVERIFY(executor.contexts().isEmpty());
    projects.setAvailable(true);
    QCOMPARE(executor.contexts().size(), 1);

    CountingControllerProvider provider("EtherCAT.SemanticRuntime.Tests.Lifecycle");
    provider.publishSnapshot(connectedSnapshot(scope, 7));
    provider.publishCatalog(resourceCatalog(scope, 7));
    RegisteredObject registration(&provider);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));

    provider.setAvailable(true);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    registration.remove();
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));

    projects.removeProject(project.id);
    QVERIFY(executor.contexts().isEmpty());
    QVERIFY(contextsSpy.count() >= 6);
    QCOMPARE(provider.mutationCalls, 0);
}

void EtherCATSemanticRuntimeTests::testStrictProviderCardinality()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    const Data::ProjectSnapshot project = testProject();
    const Data::ControllerConnectionScope scope = projectScope(project);
    projects.addProject(project);
    SemanticRuntimeExecutor executor(&projects, registry);

    CountingControllerProvider first("EtherCAT.SemanticRuntime.Tests.Cardinality.First");
    first.publishSnapshot(connectedSnapshot(scope, 9));
    first.publishCatalog(resourceCatalog(scope, 9));
    first.setAvailable(true);
    RegisteredObject firstRegistration(&first);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    CountingControllerProvider second("EtherCAT.SemanticRuntime.Tests.Cardinality.Second");
    second.publishSnapshot(connectedSnapshot(scope, 10));
    second.publishCatalog(resourceCatalog(scope, 10));
    second.setAvailable(true);
    RegisteredObject secondRegistration(&second);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderAmbiguous));

    secondRegistration.remove();
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    RegisteredObject secondRegistrationAgain(&second);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderAmbiguous));

    second.setAvailable(false);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    first.setAvailable(false);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));
    QCOMPARE(first.mutationCalls, 0);
    QCOMPARE(second.mutationCalls, 0);
}

void EtherCATSemanticRuntimeTests::testScopeSessionAndEpochInvalidation()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    const Data::ProjectSnapshot project = testProject();
    const Data::ControllerConnectionScope scope = projectScope(project);
    projects.addProject(project);
    SemanticRuntimeExecutor executor(&projects, registry);

    CountingControllerProvider provider("EtherCAT.SemanticRuntime.Tests.Epoch");
    provider.publishSnapshot(connectedSnapshot(scope, 11));
    const Data::RuntimeResourceCatalogEpoch firstEpoch = catalogEpoch(5);
    provider.publishCatalog(resourceCatalog(scope, 11, firstEpoch));
    provider.setAvailable(true);
    RegisteredObject registration(&provider);

    QCOMPARE(executor.contexts().constFirst().sessionGeneration, quint64(11));
    QCOMPARE(executor.contexts().constFirst().epoch, firstEpoch);

    const Data::ControllerConnectionScope otherScope{
        Data::NodeId::create(),
        Data::NodeId::create(),
    };
    provider.publishSnapshot(connectedSnapshot(otherScope, 12));
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));

    provider.publishSnapshot(connectedSnapshot(scope, 12));
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourceCatalogStale));
    QCOMPARE(executor.contexts().constFirst().epoch, Data::RuntimeResourceCatalogEpoch());

    const Data::RuntimeResourceCatalogEpoch secondEpoch = catalogEpoch(6);
    provider.publishCatalog(resourceCatalog(scope, 12, secondEpoch));
    QCOMPARE(executor.contexts().constFirst().sessionGeneration, quint64(12));
    QCOMPARE(executor.contexts().constFirst().epoch, secondEpoch);

    Data::RuntimeResourceCatalogEpoch incompleteEpoch = secondEpoch;
    incompleteEpoch.catalogRevision = 0;
    provider.publishCatalog(resourceCatalog(scope, 12, incompleteEpoch));
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourceEpochIncomplete));
    QCOMPARE(executor.contexts().constFirst().epoch, Data::RuntimeResourceCatalogEpoch());
    QCOMPARE(provider.mutationCalls, 0);
}

void EtherCATSemanticRuntimeTests::testCandidateAdaptersNeverWrite()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    const Data::ProjectSnapshot project = testProject(true, true);
    const Data::ControllerConnectionScope scope = projectScope(project);
    QCOMPARE(project.slaves.size(), 2);
    QCOMPARE(
        project.slaves.at(0).adapterSelection.adapterId.value,
        QString("org.embedlabs.adapter.solidot.xb6-ec0002.rev1"));
    QCOMPARE(
        project.slaves.at(1).adapterSelection.adapterId.value,
        QString("org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000"));
    projects.addProject(project);
    SemanticRuntimeExecutor executor(&projects, registry);

    CountingControllerProvider provider("EtherCAT.SemanticRuntime.Tests.Candidates");
    provider.publishSnapshot(connectedSnapshot(scope, 14));
    provider.publishCatalog(resourceCatalog(scope, 14));
    provider.setAvailable(true);
    RegisteredObject registration(&provider);

    const Data::SemanticRuntimeContext context = executor.contexts().constFirst();
    QVERIFY(!context.complete);
    QCOMPARE(context.detail, detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));
    QVERIFY(context.signalStates.isEmpty());
    QVERIFY(context.actionStates.isEmpty());

    Data::SemanticOperationRequest request;
    request.operationId.value = QStringLiteral("operation/candidate-adapters/1");
    request.target.controllerId = context.controllerId;
    request.target.scope = scope;
    Data::SemanticRuntimeActor actor;
    actor.id = QStringLiteral("test-user");

    const Data::SemanticOperationRecord submitted = executor.submit(request, actor);
    QCOMPARE(submitted.state, Data::SemanticOperationState::Rejected);
    QVERIFY(!submitted.executionAttempted);

    Data::SemanticOperationApprovalRequest approval;
    approval.operationId = request.operationId;
    const Data::SemanticOperationRecord approved = executor.approve(approval, actor);
    QCOMPARE(approved.state, Data::SemanticOperationState::Rejected);
    QVERIFY(!approved.executionAttempted);
    QVERIFY(!executor.operation(request.operationId));
    QVERIFY(executor.audit(context.controllerId).isEmpty());
    QCOMPARE(provider.mutationCalls, 0);
}

void EtherCATSemanticRuntimeTests::testMissingCatalogAndProofFailClosed()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    Data::ProjectSnapshot project = testProject();
    const Data::ControllerConnectionScope scope = projectScope(project);
    projects.addProject(project);
    SemanticRuntimeExecutor executor(&projects, registry);

    CountingControllerProvider provider("EtherCAT.SemanticRuntime.Tests.FailClosed");
    provider.publishSnapshot(connectedSnapshot(scope, 16));
    provider.setAvailable(true);
    RegisteredObject registration(&provider);

    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourceCatalogUnavailable));

    Data::RuntimeResourceCatalog emptyCatalog = resourceCatalog(scope, 16);
    emptyCatalog.resources.clear();
    provider.publishCatalog(emptyCatalog);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourceCatalogEmpty));

    provider.publishCatalog(resourceCatalog(scope, 16));
    const Data::SemanticRuntimeContext proofless = executor.contexts().constFirst();
    QVERIFY(!proofless.complete);
    QCOMPARE(proofless.bindingVerification.state, Data::SemanticBindingVerificationState::Unverified);
    QCOMPARE(
        proofless.detail, detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    project.masterBindingArtifact = {};
    projects.changeProject(project);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::BindingArtifactMissing));

    project.masterBindingArtifact = bindingArtifact();
    project.masterBindingArtifact.artifactSha256.chop(1);
    projects.changeProject(project);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::BindingArtifactInvalid));

    project.masterBindingArtifact = bindingArtifact();
    projects.changeProject(project);
    provider.setRuntimeResourcesSupported(false);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourcesUnsupported));
    QCOMPARE(provider.mutationCalls, 0);
}

} // namespace EtherCAT::SemanticRuntime::Internal
