// Copyright (C) 2026 Kvell

#include "mockscanprovider.h"

#include "ethercatscanconstants.h"
#include "ethercatscantr.h"
#include "topologycomparison.h"

#include <extensionsystem/pluginmanager.h>

#include <QUuid>

#include <algorithm>

namespace EtherCAT::Scan::Internal {

MockScanProvider::MockScanProvider(QObject *parent)
    : Core::ScanProvider(Constants::PROVIDER_ID, Tr::tr("Local Mock EtherCAT scanner"), parent)
{
    m_projectService = ExtensionSystem::PluginManager::getObject<Core::ProjectService>();
    m_deviceRepository
        = ExtensionSystem::PluginManager::getObject<Core::DeviceRepositoryProvider>();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &MockScanProvider::advance);
    if (m_projectService) {
        connect(
            m_projectService,
            &Core::ProjectService::projectAboutToBeRemoved,
            this,
            [this](const Data::NodeId &projectId) {
                if (isActive() && m_request.projectId == projectId)
                    cancelScan();
            });
    }
    setAvailable(m_projectService && m_deviceRepository);
}

MockScanProvider::~MockScanProvider()
{
    shutdown();
}

Data::ScanState MockScanProvider::scanState() const
{
    return m_progress.state;
}

Data::ScanProgress MockScanProvider::scanProgress() const
{
    return m_progress;
}

std::optional<Data::ScanResult> MockScanProvider::lastScanResult() const
{
    return m_result;
}

QString MockScanProvider::lastScanError() const
{
    return m_error;
}

Utils::Result<> MockScanProvider::startScan(const Data::ScanRequest &request)
{
    if (m_shuttingDown || !isAvailable())
        return Utils::ResultError(Tr::tr("The local Mock scanner is unavailable."));
    if (m_progress.state != Data::ScanState::Idle)
        return Utils::ResultError(Tr::tr("Clear the previous Mock scan before starting again."));
    if (request.projectId.isNull() || request.masterId.isNull())
        return Utils::ResultError(Tr::tr("Select an EtherCAT project or master before scanning."));
    if (request.operation == Data::ScanOperation::SelectedBranch
        && request.branchNodeId.isNull()) {
        return Utils::ResultError(Tr::tr("Select a configured branch before rescanning it."));
    }
    const std::optional<Data::ProjectSnapshot> project
        = m_projectService->project(request.projectId);
    if (!project)
        return Utils::ResultError(Tr::tr("The selected EtherCAT project is no longer open."));
    const bool masterExists = std::any_of(
        project->nodes.cbegin(), project->nodes.cend(), [&request](const auto &node) {
            return node.id == request.masterId && node.kind == Data::ProjectNodeKind::Master;
        });
    if (!masterExists)
        return Utils::ResultError(Tr::tr("The selected EtherCAT master no longer exists."));
    const auto selectedBranch = std::find_if(
        project->slaves.cbegin(), project->slaves.cend(), [&request](const auto &slave) {
            return slave.id == request.branchNodeId && slave.masterId == request.masterId;
        });
    if (request.operation == Data::ScanOperation::SelectedBranch
        && request.branchNodeId != request.masterId
        && selectedBranch == project->slaves.cend()) {
        return Utils::ResultError(
            Tr::tr("The selected EtherCAT branch no longer exists under this master."));
    }

    m_request = request;
    m_result.reset();
    m_error.clear();
    m_discoveredSlaves.clear();
    m_expectedSlaves = request.operation == Data::ScanOperation::Interfaces
                           ? QList<Data::ScannedSlave>()
                           : scenarioSlaves();
    if (request.operation == Data::ScanOperation::SelectedBranch
        && request.branchNodeId != request.masterId) {
        const int branchPosition = selectedBranch->position;
        m_expectedSlaves.erase(
            std::remove_if(
                m_expectedSlaves.begin(),
                m_expectedSlaves.end(),
                [branchPosition](const Data::ScannedSlave &slave) {
                    return slave.position != branchPosition;
                }),
            m_expectedSlaves.end());
    }
    if (m_scenario == MockScanScenario::RevisionMismatch && !m_expectedSlaves.isEmpty())
        ++m_expectedSlaves[0].identity.revisionNumber;
    m_progress = {};
    m_progress.maximum = m_expectedSlaves.size() + 5;
    enterState(Data::ScanState::Preparing, 0, Tr::tr("Preparing local Mock scan"));
    scheduleAdvance();
    return Utils::ResultOk;
}

void MockScanProvider::cancelScan()
{
    if (!isActive())
        return;
    m_timer.stop();
    m_result.reset();
    m_error.clear();
    enterState(
        Data::ScanState::Cancelled,
        m_progress.value,
        Tr::tr("Local Mock scan cancelled"));
    emit scanResultChanged();
    emit scanFinished(m_progress.state);
}

void MockScanProvider::clearScanResult()
{
    if (isActive())
        return;
    m_timer.stop();
    m_request = {};
    m_result.reset();
    m_expectedSlaves.clear();
    m_discoveredSlaves.clear();
    m_error.clear();
    m_progress = {};
    emit scanResultChanged();
    emit scanProgressChanged(m_progress);
    emit scanStateChanged(m_progress.state);
}

MockScanScenario MockScanProvider::scenario() const
{
    return m_scenario;
}

void MockScanProvider::setScenario(MockScanScenario scenario)
{
    if (m_scenario == scenario || isActive())
        return;
    m_scenario = scenario;
    emit scenarioChanged(m_scenario);
}

void MockScanProvider::setStepIntervalForTests(int milliseconds)
{
    m_testInterval = milliseconds;
}

Utils::Result<> MockScanProvider::compareWithCurrentProject()
{
    if (!m_result || !m_result->snapshot.complete)
        return Utils::ResultError(Tr::tr("No completed Mock topology is available to compare."));
    if (m_result->snapshot.operation == Data::ScanOperation::Interfaces) {
        return Utils::ResultError(
            Tr::tr("A Mock interface scan has no slave topology to compare."));
    }
    const std::optional<Data::ProjectSnapshot> project
        = m_projectService ? m_projectService->project(m_result->snapshot.projectId) : std::nullopt;
    if (!project)
        return Utils::ResultError(Tr::tr("The scanned EtherCAT project is no longer open."));
    m_result->comparison = compareTopology(
        *project, m_result->snapshot.masterId, m_result->snapshot);
    emit scanResultChanged();
    return Utils::ResultOk;
}

void MockScanProvider::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    if (isActive())
        cancelScan();
    m_timer.stop();
    setAvailable(false);
}

bool MockScanProvider::isActive() const
{
    switch (m_progress.state) {
    case Data::ScanState::Preparing:
    case Data::ScanState::ScanningMaster:
    case Data::ScanState::ScanningSlaves:
    case Data::ScanState::BuildingSnapshot:
    case Data::ScanState::Comparing:
        return true;
    default:
        return false;
    }
}

int MockScanProvider::stepInterval() const
{
    if (m_testInterval >= 0)
        return m_testInterval;
    return m_scenario == MockScanScenario::Slow ? 350 : 40;
}

void MockScanProvider::scheduleAdvance()
{
    if (!m_shuttingDown && isActive())
        m_timer.start(stepInterval());
}

void MockScanProvider::advance()
{
    switch (m_progress.state) {
    case Data::ScanState::Preparing:
        enterState(Data::ScanState::ScanningMaster, 1, Tr::tr("Scanning Mock master interface"));
        break;
    case Data::ScanState::ScanningMaster:
        if (m_request.operation == Data::ScanOperation::Interfaces) {
            enterState(
                Data::ScanState::BuildingSnapshot,
                m_progress.maximum - 2,
                Tr::tr("Building Mock interface snapshot"));
        } else {
            enterState(
                Data::ScanState::ScanningSlaves,
                2,
                Tr::tr("Scanning Mock EtherCAT slaves"));
        }
        break;
    case Data::ScanState::ScanningSlaves:
        discoverNextSlave();
        break;
    case Data::ScanState::BuildingSnapshot:
        buildSnapshot();
        if (m_request.operation == Data::ScanOperation::Interfaces) {
            enterState(
                Data::ScanState::Completed,
                m_progress.maximum,
                Tr::tr("Mock interface scan completed"));
            emit scanFinished(m_progress.state);
            return;
        }
        enterState(
            Data::ScanState::Comparing,
            m_progress.maximum - 1,
            Tr::tr("Comparing Mock topology with the offline project"));
        break;
    case Data::ScanState::Comparing:
        completeComparison();
        return;
    default:
        return;
    }
    scheduleAdvance();
}

void MockScanProvider::enterState(
    Data::ScanState state, int value, const QString &detail)
{
    const bool stateChanged = m_progress.state != state;
    m_progress.state = state;
    m_progress.value = qBound(0, value, m_progress.maximum);
    m_progress.discoveredSlaves = m_discoveredSlaves.size();
    m_progress.detail = detail;
    emit scanProgressChanged(m_progress);
    if (stateChanged)
        emit scanStateChanged(m_progress.state);
}

void MockScanProvider::discoverNextSlave()
{
    if (m_scenario == MockScanScenario::PartialFailure && m_discoveredSlaves.size() == 2) {
        fail(Tr::tr("MOCK partial failure after discovering two slaves."));
        return;
    }
    if (m_discoveredSlaves.size() < m_expectedSlaves.size()) {
        m_discoveredSlaves.append(m_expectedSlaves.at(m_discoveredSlaves.size()));
        enterState(
            Data::ScanState::ScanningSlaves,
            2 + m_discoveredSlaves.size(),
            Tr::tr("Discovered Mock slave %1 of %2")
                .arg(m_discoveredSlaves.size())
                .arg(m_expectedSlaves.size()));
        if (m_discoveredSlaves.size() < m_expectedSlaves.size())
            return;
    }
    enterState(
        Data::ScanState::BuildingSnapshot,
        m_progress.maximum - 2,
        Tr::tr("Building immutable Mock topology snapshot"));
}

void MockScanProvider::buildSnapshot()
{
    Data::ScanSnapshot snapshot;
    snapshot.id = Data::NodeId::create();
    snapshot.projectId = m_request.projectId;
    snapshot.masterId = m_request.masterId;
    snapshot.capturedAt = QDateTime::currentDateTimeUtc();
    snapshot.interfaces = {{"mock0",
                            Tr::tr("Mock EtherCAT Interface"),
                            Tr::tr("Local simulation; no network device is accessed"),
                            true}};
    snapshot.slaves = m_discoveredSlaves;
    snapshot.complete = true;
    snapshot.mock = true;
    snapshot.operation = m_request.operation;
    snapshot.branchNodeId = m_request.branchNodeId;
    if (m_scenario != MockScanScenario::Normal) {
        snapshot.warnings.append(
            Tr::tr("This result was generated by a non-default local Mock scenario."));
    }
    Data::ScanResult result;
    result.snapshot = snapshot;
    m_result = result;
    emit scanResultChanged();
}

void MockScanProvider::completeComparison()
{
    const Utils::Result<> comparisonResult = compareWithCurrentProject();
    if (!comparisonResult) {
        fail(comparisonResult.error());
        return;
    }
    enterState(Data::ScanState::Completed, m_progress.maximum, Tr::tr("Mock scan completed"));
    emit scanFinished(m_progress.state);
}

void MockScanProvider::fail(const QString &error)
{
    m_timer.stop();
    m_result.reset();
    m_error = error;
    enterState(Data::ScanState::Failed, m_progress.value, error);
    emit scanResultChanged();
    emit scanFinished(m_progress.state);
}

QList<Data::ScannedSlave> MockScanProvider::scenarioSlaves() const
{
    QList<Data::ScannedSlave> result;
    const QList<Data::DeviceSummary> devices = m_deviceRepository ? m_deviceRepository->devices()
                                                                  : QList<Data::DeviceSummary>();
    const int count = 4;
    result.reserve(count);
    for (int position = 0; position < count; ++position) {
        Data::DeviceIdentity identity{2, quint32(0x1000 + position), 1};
        QString name = Tr::tr("Mock EtherCAT Slave %1").arg(position + 1);
        Data::NodeId descriptionId;
        if (position < devices.size()) {
            identity = devices.at(position).identity;
            name = Tr::tr("Mock %1").arg(devices.at(position).name);
            descriptionId = devices.at(position).id;
        }
        const quint32 serialNumber = quint32(101 + position);
        result.append({stableScannedId(position, identity, serialNumber),
                       position,
                       identity,
                       serialNumber,
                       0,
                       name,
                       descriptionId});
    }

    if (m_scenario == MockScanScenario::DuplicateDevice && result.size() >= 2) {
        result[1].identity = result[0].identity;
        result[1].serialNumber = result[0].serialNumber;
        result[1].alias = result[0].alias;
        result[1].name = Tr::tr("Mock duplicate of %1").arg(result[0].name);
    }
    if (m_scenario == MockScanScenario::RevisionMismatch && !result.isEmpty()) {
        const std::optional<Data::ProjectSnapshot> project
            = m_projectService ? m_projectService->project(m_request.projectId) : std::nullopt;
        if (project) {
            QList<Data::OfflineSlaveConfiguration> configured;
            for (const Data::OfflineSlaveConfiguration &slave : project->slaves) {
                if (slave.masterId == m_request.masterId)
                    configured.append(slave);
            }
            if (!configured.isEmpty()) {
                std::sort(
                    configured.begin(), configured.end(), [](const auto &left, const auto &right) {
                        return left.position < right.position;
                    });
                result.clear();
                for (const Data::OfflineSlaveConfiguration &slave : configured) {
                    result.append({stableScannedId(
                                       slave.position, slave.identity, slave.serialNumber),
                                   slave.position,
                                   slave.identity,
                                   slave.serialNumber,
                                   slave.alias,
                                   slave.name,
                                   slave.deviceDescriptionId});
                }
            }
        }
    }
    return result;
}

Data::NodeId MockScanProvider::stableScannedId(
    int position, const Data::DeviceIdentity &identity, quint32 serialNumber) const
{
    static const QUuid namespaceId("{75374a56-275d-5d47-bda0-69f732df59b3}");
    const QString physicalKey
        = serialNumber != 0 ? QString("serial:%1").arg(serialNumber)
                            : QString("position:%1:revision:%2")
                                  .arg(position)
                                  .arg(identity.revisionNumber);
    const QByteArray key = QString("%1:%2:%3:%4")
                               .arg(m_request.masterId.toString())
                               .arg(identity.vendorId)
                               .arg(identity.productCode)
                               .arg(physicalKey)
                               .toUtf8();
    return Data::NodeId::fromString(QUuid::createUuidV5(namespaceId, key).toString());
}

} // namespace EtherCAT::Scan::Internal
