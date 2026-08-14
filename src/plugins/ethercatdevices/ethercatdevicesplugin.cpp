// Copyright (C) 2026 Kvell

#include "devicerepository.h"
#include "ethercatdevicesconstants.h"
#ifdef WITH_TESTS
#include "ethercatdevicestests.h"
#endif

#include <coreplugin/icore.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <algorithm>
#include <memory>

namespace EtherCAT::Devices::Internal {

static Utils::FilePaths esiXmlFiles(const Utils::FilePath &directory)
{
    Utils::FilePaths files = directory.dirEntries(
        Utils::DirFilterFlag::Files | Utils::DirFilterFlag::NoDotAndDotDot);
    files.erase(
        std::remove_if(files.begin(), files.end(), [](const Utils::FilePath &filePath) {
            return filePath.suffix().compare("xml", Qt::CaseInsensitive) != 0;
        }),
        files.end());
    std::sort(files.begin(), files.end(), [](const auto &left, const auto &right) {
        return left.toUrlishString() < right.toUrlishString();
    });
    return files;
}

class EtherCATDevicesPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "EtherCATDevices.json")

public:
    ~EtherCATDevicesPlugin() final;

    void initialize() final;
    bool delayedInitialize() final;
    ShutdownFlag aboutToShutdown() final;

private:
    void unregisterRepository();

    std::unique_ptr<DeviceRepository> m_repository;
    bool m_repositoryRegistered = false;
};

EtherCATDevicesPlugin::~EtherCATDevicesPlugin()
{
    unregisterRepository();
}

void EtherCATDevicesPlugin::initialize()
{
    const Utils::FilePath repositoryRoot = ::Core::ICore::userResourcePath("ethercat/esi");
    m_repository = std::make_unique<DeviceRepository>(repositoryRoot);
    ExtensionSystem::PluginManager::addObject(m_repository.get());
    m_repositoryRegistered = true;

#ifdef WITH_TESTS
    addTest<EtherCATDevicesTests>();
#endif
}

bool EtherCATDevicesPlugin::delayedInitialize()
{
    if (!m_repository || !m_repository->isAvailable())
        return true;

    m_repository->rebuildIndex();
    Utils::FilePaths libraryFiles
        = esiXmlFiles(::Core::ICore::resourcePath("ethercat/esi"));
    const Utils::FilePath userLibrary
        = ::Core::ICore::userResourcePath("ethercat/esi/library");
    if (userLibrary.ensureWritableDir())
        libraryFiles.append(esiXmlFiles(userLibrary));
    if (!libraryFiles.isEmpty())
        m_repository->importFiles(libraryFiles);
    return true;
}

ExtensionSystem::IPlugin::ShutdownFlag EtherCATDevicesPlugin::aboutToShutdown()
{
    unregisterRepository();
    return SynchronousShutdown;
}

void EtherCATDevicesPlugin::unregisterRepository()
{
    if (!m_repositoryRegistered)
        return;
    m_repository->shutdown();
    ExtensionSystem::PluginManager::removeObject(m_repository.get());
    m_repository.reset();
    m_repositoryRegistered = false;
}

} // namespace EtherCAT::Devices::Internal

#include "ethercatdevicesplugin.moc"
