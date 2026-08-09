// Copyright (C) 2026 Kvell

#include "ethercatcoreconstants.h"
#include "ethercatcoresettings.h"
#include "ethercatcoretr.h"
#include "manualcontrolcontract.h"
#include "providerregistry.h"
#include "runtimepackagecompilerpreparationcoordinator.h"
#include "runtimepackagecompilerprojectrequestbuilder.h"
#include "runtimepackagecompilerprovider.h"
#include "selectionservice.h"
#include "stateservice.h"
#include "topologyservice.h"

#ifdef WITH_TESTS
#include "ethercatcoretests.h"
#endif

#include <coreplugin/dialogs/ioptionspage.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <ethercatdata/controllerconnection.h>
#include <ethercatdata/engineeringvalue.h>
#include <ethercatdata/manualcontrol.h>
#include <ethercatdata/runtimepackagecompiler.h>
#include <ethercatdata/runtimeoutputtransaction.h>
#include <ethercatdata/runtimeresource.h>
#include <ethercatdata/semanticmappingattestation.h>

#include <memory>

namespace EtherCAT::Core::Internal {

class EtherCATCorePlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EtherCATCore.json")

public:
    ~EtherCATCorePlugin() final;

    void initialize() final;
    void extensionsInitialized() final;
    ShutdownFlag aboutToShutdown() final;

private:
    void unregisterServices();

    std::unique_ptr<::Core::IOptionsPage> m_settingsPage;
    std::unique_ptr<SelectionService> m_selectionService;
    std::unique_ptr<StateService> m_stateService;
    std::unique_ptr<ProviderRegistry> m_providerRegistry;
    std::unique_ptr<TopologyService> m_topologyService;
    bool m_servicesRegistered = false;
};

EtherCATCorePlugin::~EtherCATCorePlugin()
{
    unregisterServices();
}

void EtherCATCorePlugin::initialize()
{
    qRegisterMetaType<Data::ControllerConnectionProfile>();
    qRegisterMetaType<Data::ControllerConnectionRequest>();
    qRegisterMetaType<Data::ControllerConnectionScope>();
    qRegisterMetaType<Data::ControllerConnectionSnapshot>();
    qRegisterMetaType<Data::ControllerConnectionState>();
    qRegisterMetaType<Data::ControllerOperationError>();
    qRegisterMetaType<Data::RuntimeResourceSnapshotRequest>();
    qRegisterMetaType<Data::RuntimeResourceSnapshotResult>();
    qRegisterMetaType<Data::RuntimeSemanticMappingTrust>();
    qRegisterMetaType<Data::RuntimeSemanticMappingProof>();
    qRegisterMetaType<Data::RuntimeSemanticMappingAttestationRequest>();
    qRegisterMetaType<Data::RuntimeSemanticMappingAttestation>();
    qRegisterMetaType<Data::RuntimeSemanticMappingAttestationResult>();
    qRegisterMetaType<Data::RuntimeOutputGroupPolicyResult>();
    qRegisterMetaType<Data::RuntimeOutputTransactionState>();
    qRegisterMetaType<Data::RuntimeOutputTransactionStateResult>();
    qRegisterMetaType<Data::RuntimeOutputTransactionResult>();
    qRegisterMetaType<Data::ExactRational>();
    qRegisterMetaType<Data::EngineeringValue>();
    qRegisterMetaType<Data::EngineeringTransform>();
    qRegisterMetaType<Data::ManualControlEnvelope>();
    qRegisterMetaType<Data::RuntimePackageCompilerJobResult>();
    qRegisterMetaType<Data::RuntimePackageCompilerResultStatus>();
    qRegisterMetaType<RuntimePackageCompilerJobState>();
    qRegisterMetaType<RuntimePackageCompilerJobCompletionError>();
    qRegisterMetaType<RuntimePackageCompilerPreparationPhase>();
    qRegisterMetaType<RuntimePackageCompilerPreparationStartRequest>();
    qRegisterMetaType<RuntimePackageCompilerPreparationRecord>();
    qRegisterMetaType<RuntimePackageCompilerPreparationSnapshot>();
    qRegisterMetaType<RuntimePackageCompilerPreparationDisposition>();
    qRegisterMetaType<RuntimePackageCompilerProjectRequestSeed>();
    qRegisterMetaType<EngineeringContractValidation>();
    qRegisterMetaType<EngineeringConversionResult>();
    qRegisterMetaType<ManualControlContractValidation>();
    qRegisterMetaType<Data::NodeId>();
    qRegisterMetaType<Data::DeviceDescription>();
    qRegisterMetaType<Data::DeviceImportResult>();
    qRegisterMetaType<Data::DeviceSummary>();
    qRegisterMetaType<DeviceImportState>();
    qRegisterMetaType<ProviderKind>();
    qRegisterMetaType<WorkbenchNodeKind>();
    qRegisterMetaType<PropertyPageContext>();
    qRegisterMetaType<PropertyPageDescriptor>();
    qRegisterMetaType<StatusEntry>();
    qRegisterMetaType<StatusSeverity>();
    qRegisterMetaType<TopologyEvidenceSource>();
    qRegisterMetaType<TopologyEvidenceFreshness>();
    qRegisterMetaType<TopologyLookupStatus>();
    qRegisterMetaType<RealTopologyGeneration>();
    qRegisterMetaType<MockTopologyGeneration>();
    qRegisterMetaType<TopologyGeneration>();
    qRegisterMetaType<TopologySelection>();
    qRegisterMetaType<TopologySnapshot>();
    qRegisterMetaType<TopologyLookupResult>();

    ::Core::IOptionsPage::registerCategory(Constants::SETTINGS_CATEGORY, Tr::tr("EtherCAT"), {});
    m_settingsPage = createSettingsPage();

    m_selectionService = std::make_unique<SelectionService>();
    m_stateService = std::make_unique<StateService>();
    ExtensionSystem::PluginManager::addObject(m_selectionService.get());
    ExtensionSystem::PluginManager::addObject(m_stateService.get());

    m_providerRegistry = std::make_unique<ProviderRegistry>();
    ExtensionSystem::PluginManager::addObject(m_providerRegistry.get());

    m_topologyService = std::make_unique<TopologyService>(m_providerRegistry.get());
    ExtensionSystem::PluginManager::addObject(m_topologyService.get());
    m_servicesRegistered = true;

#ifdef WITH_TESTS
    addTest<EtherCATCoreTests>();
#endif
}

void EtherCATCorePlugin::extensionsInitialized() {}

ExtensionSystem::IPlugin::ShutdownFlag EtherCATCorePlugin::aboutToShutdown()
{
    unregisterServices();
    return SynchronousShutdown;
}

void EtherCATCorePlugin::unregisterServices()
{
    if (!m_servicesRegistered)
        return;

    ExtensionSystem::PluginManager::removeObject(m_topologyService.get());
    m_topologyService.reset();

    ExtensionSystem::PluginManager::removeObject(m_providerRegistry.get());
    m_providerRegistry.reset();

    m_stateService->clearAll();
    ExtensionSystem::PluginManager::removeObject(m_stateService.get());
    m_stateService.reset();

    m_selectionService->clear();
    ExtensionSystem::PluginManager::removeObject(m_selectionService.get());
    m_selectionService.reset();

    m_settingsPage.reset();
    m_servicesRegistered = false;
}

} // namespace EtherCAT::Core::Internal

#include "ethercatcoreplugin.moc"
