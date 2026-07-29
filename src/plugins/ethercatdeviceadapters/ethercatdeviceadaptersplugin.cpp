// Copyright (C) 2026 Embed Labs

#include "adapterpackagerepository.h"

#ifdef WITH_TESTS
#include "ethercatdeviceadapterstests.h"
#endif

#include <coreplugin/icore.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <memory>

namespace EtherCAT::DeviceAdapters::Internal {

class EtherCATDeviceAdaptersPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EtherCATDeviceAdapters.json")

public:
    ~EtherCATDeviceAdaptersPlugin() final;

    void initialize() final;
    ShutdownFlag aboutToShutdown() final;

private:
    void unregisterRepository();

    std::unique_ptr<AdapterPackageRepository> m_repository;
    bool m_registered = false;
};

EtherCATDeviceAdaptersPlugin::~EtherCATDeviceAdaptersPlugin()
{
    unregisterRepository();
}

void EtherCATDeviceAdaptersPlugin::initialize()
{
    m_repository = std::make_unique<AdapterPackageRepository>(
        ::Core::ICore::resourcePath("ethercat/adapters"));
    ExtensionSystem::PluginManager::addObject(m_repository.get());
    m_registered = true;

#ifdef WITH_TESTS
    addTest<EtherCATDeviceAdaptersTests>();
#endif
}

ExtensionSystem::IPlugin::ShutdownFlag EtherCATDeviceAdaptersPlugin::aboutToShutdown()
{
    unregisterRepository();
    return SynchronousShutdown;
}

void EtherCATDeviceAdaptersPlugin::unregisterRepository()
{
    if (!m_registered)
        return;
    ExtensionSystem::PluginManager::removeObject(m_repository.get());
    m_repository.reset();
    m_registered = false;
}

} // namespace EtherCAT::DeviceAdapters::Internal

#include "ethercatdeviceadaptersplugin.moc"
