// Copyright (C) 2026 Kvell

#include "ethercatcoretests.h"

#include "ethercatcoreconstants.h"
#include "ethercatcoresettings.h"
#include "providerregistry.h"
#include "providers.h"
#include "selectionservice.h"
#include "stateservice.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <ethercatdata/nodeid.h>
#include <ethercatdata/projectsnapshot.h>

#include <QSignalSpy>
#include <QTest>

#include <algorithm>

namespace EtherCAT::Core::Internal {

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
    const Data::NodeId projectId = Data::NodeId::create();
    const Data::NodeId targetId = Data::NodeId::create();
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
    };

    const Data::ProjectSnapshot copy = snapshot;
    QCOMPARE(copy, snapshot);
    snapshot.nodes[1].name = "Offline Target";
    QVERIFY(copy != snapshot);
    QCOMPARE(copy.nodes[1].parentId, projectId);
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
    ScanProvider provider("EtherCAT.Scan.Mock", "Mock scanner");

    ExtensionSystem::PluginManager::addObject(&provider);
    QCOMPARE(registry->provider(provider.id()), &provider);
    QCOMPARE(registry->providers(ProviderKind::Scan), QList<Provider *>({&provider}));
    QCOMPARE(addedSpy.count(), 1);

    provider.setAvailable(true);
    QVERIFY(provider.isAvailable());

    ExtensionSystem::PluginManager::removeObject(&provider);
    QVERIFY(!registry->provider(provider.id()));
    QVERIFY(registry->providers(ProviderKind::Scan).isEmpty());
    QCOMPARE(removedSpy.count(), 1);
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
