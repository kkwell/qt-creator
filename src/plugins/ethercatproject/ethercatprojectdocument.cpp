// Copyright (C) 2026 Kvell

#include "ethercatprojectdocument.h"

#include "ethercatprojectconstants.h"
#include "ethercatprojectformat.h"
#include "ethercatprojecttr.h"

#include <utils/fileutils.h>

#include <QSet>
#include <QSignalBlocker>
#include <QUndoCommand>

#include <algorithm>
#include <utility>

namespace EtherCAT::Project::Internal {

class RenameProjectCommand final : public QUndoCommand
{
public:
    RenameProjectCommand(
        EtherCATProjectDocument *document, const QString &oldName, const QString &newName)
        : m_document(document)
        , m_oldName(oldName)
        , m_newName(newName)
    {
        setText(Tr::tr("Rename EtherCAT project"));
    }

    void undo() final { m_document->applyProjectName(m_oldName); }
    void redo() final { m_document->applyProjectName(m_newName); }

private:
    EtherCATProjectDocument *m_document;
    QString m_oldName;
    QString m_newName;
};

class RenameStructuralNodeCommand final : public QUndoCommand
{
public:
    RenameStructuralNodeCommand(
        EtherCATProjectDocument *document,
        const Data::NodeId &nodeId,
        const QString &oldName,
        const QString &newName)
        : m_document(document)
        , m_nodeId(nodeId)
        , m_oldName(oldName)
        , m_newName(newName)
    {
        setText(Tr::tr("Rename EtherCAT configuration node"));
    }

    void undo() final { m_document->applyStructuralNodeName(m_nodeId, m_oldName); }
    void redo() final { m_document->applyStructuralNodeName(m_nodeId, m_newName); }

private:
    EtherCATProjectDocument *m_document;
    Data::NodeId m_nodeId;
    QString m_oldName;
    QString m_newName;
};

class UpdateMasterConfigurationCommand final : public QUndoCommand
{
public:
    UpdateMasterConfigurationCommand(
        EtherCATProjectDocument *document,
        const Data::MasterConfiguration &oldConfiguration,
        const Data::MasterConfiguration &newConfiguration,
        const Data::SemanticBindingArtifactReference &oldBindingArtifact)
        : m_document(document)
        , m_oldConfiguration(oldConfiguration)
        , m_newConfiguration(newConfiguration)
        , m_oldBindingArtifact(oldBindingArtifact)
    {
        setText(Tr::tr("Configure EtherCAT master cycle"));
    }

    void undo() final
    {
        m_document->applyMasterConfiguration(m_oldConfiguration);
        m_document->applyMasterBindingArtifact(m_oldBindingArtifact);
    }
    void redo() final
    {
        m_document->applyMasterConfiguration(m_newConfiguration);
        m_document->applyMasterBindingArtifact({});
    }

private:
    EtherCATProjectDocument *m_document;
    Data::MasterConfiguration m_oldConfiguration;
    Data::MasterConfiguration m_newConfiguration;
    Data::SemanticBindingArtifactReference m_oldBindingArtifact;
};

class UpdateMasterBindingArtifactCommand final : public QUndoCommand
{
public:
    UpdateMasterBindingArtifactCommand(
        EtherCATProjectDocument *document,
        const Data::SemanticBindingArtifactReference &oldReference,
        const Data::SemanticBindingArtifactReference &newReference)
        : m_document(document)
        , m_oldReference(oldReference)
        , m_newReference(newReference)
    {
        setText(Tr::tr("Set EtherCAT semantic binding artifact"));
    }

    void undo() final { m_document->applyMasterBindingArtifact(m_oldReference); }
    void redo() final { m_document->applyMasterBindingArtifact(m_newReference); }

private:
    EtherCATProjectDocument *m_document;
    Data::SemanticBindingArtifactReference m_oldReference;
    Data::SemanticBindingArtifactReference m_newReference;
};

class ReplaceOfflineSlavesCommand final : public QUndoCommand
{
public:
    ReplaceOfflineSlavesCommand(
        EtherCATProjectDocument *document,
        const Data::NodeId &masterId,
        const QList<Data::OfflineSlaveConfiguration> &oldSlaves,
        const QList<Data::OfflineSlaveConfiguration> &newSlaves,
        const Data::SemanticBindingArtifactReference &oldBindingArtifact)
        : m_document(document)
        , m_masterId(masterId)
        , m_oldSlaves(oldSlaves)
        , m_newSlaves(newSlaves)
        , m_oldBindingArtifact(oldBindingArtifact)
    {
        setText(Tr::tr("Replace offline EtherCAT slaves"));
    }

    void undo() final
    {
        m_document->applyOfflineSlaves(m_masterId, m_oldSlaves);
        m_document->applyMasterBindingArtifact(m_oldBindingArtifact);
    }
    void redo() final
    {
        m_document->applyOfflineSlaves(m_masterId, m_newSlaves);
        m_document->applyMasterBindingArtifact({});
    }

private:
    EtherCATProjectDocument *m_document;
    Data::NodeId m_masterId;
    QList<Data::OfflineSlaveConfiguration> m_oldSlaves;
    QList<Data::OfflineSlaveConfiguration> m_newSlaves;
    Data::SemanticBindingArtifactReference m_oldBindingArtifact;
};

class UpdateOfflineSlaveCommand final : public QUndoCommand
{
public:
    UpdateOfflineSlaveCommand(
        EtherCATProjectDocument *document,
        const Data::OfflineSlaveConfiguration &oldSlave,
        const Data::OfflineSlaveConfiguration &newSlave,
        const Data::SemanticBindingArtifactReference &oldBindingArtifact,
        const QString &text)
        : m_document(document)
        , m_oldSlave(oldSlave)
        , m_newSlave(newSlave)
        , m_oldBindingArtifact(oldBindingArtifact)
    {
        setText(text);
    }

    void undo() final
    {
        m_document->applyOfflineSlave(m_oldSlave);
        m_document->applyMasterBindingArtifact(m_oldBindingArtifact);
    }
    void redo() final
    {
        m_document->applyOfflineSlave(m_newSlave);
        m_document->applyMasterBindingArtifact({});
    }

private:
    EtherCATProjectDocument *m_document;
    Data::OfflineSlaveConfiguration m_oldSlave;
    Data::OfflineSlaveConfiguration m_newSlave;
    Data::SemanticBindingArtifactReference m_oldBindingArtifact;
};

static QList<Data::OfflineSlaveConfiguration> slavesForMaster(
    const Data::ProjectSnapshot &snapshot, const Data::NodeId &masterId)
{
    QList<Data::OfflineSlaveConfiguration> result;
    for (const Data::OfflineSlaveConfiguration &slave : snapshot.slaves) {
        if (slave.masterId == masterId)
            result.append(slave);
    }
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.position < right.position;
    });
    return result;
}

static void applyOfflineSlavesToSnapshot(
    Data::ProjectSnapshot &snapshot,
    const Data::NodeId &masterId,
    const QList<Data::OfflineSlaveConfiguration> &slaves)
{
    snapshot.slaves.removeIf([&masterId](const auto &slave) { return slave.masterId == masterId; });
    snapshot.slaves.append(slaves);
    std::sort(snapshot.slaves.begin(), snapshot.slaves.end(), [](const auto &left, const auto &right) {
        const int masterOrder = left.masterId.toString().compare(right.masterId.toString());
        return masterOrder == 0 ? left.position < right.position : masterOrder < 0;
    });

    snapshot.nodes.removeIf(
        [](const auto &node) { return node.kind == Data::ProjectNodeKind::Slave; });
    for (const Data::OfflineSlaveConfiguration &slave : std::as_const(snapshot.slaves)) {
        snapshot.nodes.append({slave.id, slave.masterId, Data::ProjectNodeKind::Slave, slave.name});
    }
}

static Utils::Result<> insertConfigurationId(
    QSet<Data::NodeId> &ids, const Data::NodeId &id, const QString &objectName)
{
    if (id.isNull())
        return Utils::ResultError(Tr::tr("%1 has an invalid stable ID.").arg(objectName));
    if (ids.contains(id)) {
        return Utils::ResultError(
            Tr::tr("%1 reuses a stable ID that is already present in the project.").arg(objectName));
    }
    ids.insert(id);
    return Utils::ResultOk;
}

static bool adapterSelectionIsEmpty(const Data::DeviceAdapterProjectSelection &selection)
{
    return selection.adapterId.value.isEmpty() && selection.adapterVersion.isEmpty()
           && selection.adapterContentSha256.isEmpty()
           && selection.processDataProfileId.isEmpty() && selection.moduleAssignments.isEmpty();
}

static Utils::Result<Data::DeviceAdapterProjectSelection> normalizeAdapterSelection(
    const Data::DeviceAdapterProjectSelection &selection, const QByteArray &esiSha256)
{
    if (adapterSelectionIsEmpty(selection))
        return Data::DeviceAdapterProjectSelection();
    if (selection.adapterId.value.isEmpty()
        || selection.adapterId.value != selection.adapterId.value.trimmed()
        || selection.adapterVersion.isEmpty()
        || selection.adapterVersion != selection.adapterVersion.trimmed()
        || selection.adapterContentSha256.size() != 32
        || selection.processDataProfileId.isEmpty()
        || selection.processDataProfileId != selection.processDataProfileId.trimmed()) {
        return Utils::ResultError(
            Tr::tr("The device adapter selection is incomplete or invalid."));
    }
    if (esiSha256.size() != 32) {
        return Utils::ResultError(
            Tr::tr("A device adapter selection requires an ESI SHA-256 digest."));
    }

    Data::DeviceAdapterProjectSelection normalized = selection;
    QSet<int> moduleSlots;
    for (const Data::DeviceModuleAssignment &assignment : normalized.moduleAssignments) {
        if (assignment.slot < 0) {
            return Utils::ResultError(
                Tr::tr("Device adapter module slots must be non-negative."));
        }
        if (!assignment.moduleIdent) {
            return Utils::ResultError(
                Tr::tr("Device adapter module assignments require a non-zero ModuleIdent."));
        }
        if (moduleSlots.contains(assignment.slot)) {
            return Utils::ResultError(
                Tr::tr("Device adapter module slots must be unique."));
        }
        moduleSlots.insert(assignment.slot);
    }
    std::sort(
        normalized.moduleAssignments.begin(),
        normalized.moduleAssignments.end(),
        [](const auto &left, const auto &right) { return left.slot < right.slot; });
    return normalized;
}

static bool bindingArtifactIsEmpty(
    const Data::SemanticBindingArtifactReference &reference)
{
    return reference.artifactId.isEmpty() && reference.artifactSha256.isEmpty()
           && reference.projectConfigurationSha256.isEmpty();
}

static Utils::Result<> validateBindingArtifact(
    const Data::SemanticBindingArtifactReference &reference)
{
    if (bindingArtifactIsEmpty(reference))
        return Utils::ResultOk;
    if (reference.artifactId.isEmpty() || reference.artifactId != reference.artifactId.trimmed()
        || reference.artifactSha256.size() != 32
        || reference.projectConfigurationSha256.size() != 32) {
        return Utils::ResultError(
            Tr::tr("The semantic binding artifact reference is incomplete or invalid."));
    }
    return Utils::ResultOk;
}

static Utils::Result<> validateProjectConfigurations(const Data::ProjectSnapshot &snapshot)
{
    if (snapshot.masterConfiguration.timingMode != Data::MasterTimingMode::Unassigned
        && snapshot.masterConfiguration.timingMode != Data::MasterTimingMode::FreeRun
        && snapshot.masterConfiguration.timingMode != Data::MasterTimingMode::DistributedClocks) {
        return Utils::ResultError(Tr::tr("The EtherCAT master timing mode is unsupported."));
    }
    if (snapshot.masterConfiguration.timingMode == Data::MasterTimingMode::Unassigned) {
        if (snapshot.masterConfiguration.cyclePeriodNs) {
            return Utils::ResultError(
                Tr::tr("An unassigned EtherCAT timing mode cannot have a cycle period."));
        }
    } else if (!snapshot.masterConfiguration.cyclePeriodNs) {
        return Utils::ResultError(
            Tr::tr("FreeRun and Distributed Clocks require a non-zero cycle period."));
    }

    QSet<Data::NodeId> ids;
    for (const Data::ProjectNodeSnapshot &node : snapshot.nodes)
        ids.insert(node.id);

    for (const Data::OfflineSlaveConfiguration &slave : snapshot.slaves) {
        if (!slave.esiSha256.isEmpty() && slave.esiSha256.size() != 32) {
            return Utils::ResultError(
                Tr::tr("ESI SHA-256 digests must contain exactly 32 bytes."));
        }
        const auto normalizedSelection
            = normalizeAdapterSelection(slave.adapterSelection, slave.esiSha256);
        if (!normalizedSelection)
            return Utils::ResultError(normalizedSelection.error());
        if (*normalizedSelection != slave.adapterSelection) {
            return Utils::ResultError(
                Tr::tr("Device adapter module assignments must use canonical slot order."));
        }

        constexpr qint64 maximumExactJsonInteger = qint64(1) << 53;
        for (const Data::SyncManagerConfiguration &syncManager : slave.processData.syncManagers) {
            if (syncManager.index < 0 || syncManager.sizeLimitBytes < 0) {
                return Utils::ResultError(
                    Tr::tr("Sync Manager index and byte-size limit must be non-negative."));
            }
        }
        for (const Data::PdoConfiguration &pdo : slave.processData.pdos) {
            for (const Data::PdoEntryConfiguration &entry : pdo.entries) {
                if (entry.requestedBitOffset > maximumExactJsonInteger) {
                    return Utils::ResultError(
                        Tr::tr("PDO entry offsets must fit the exact JSON integer range."));
                }
            }
        }

        const Data::ConfigurationValidation processValidation
            = Data::validateProcessDataConfiguration(slave.processData);
        if (processValidation.hasErrors()) {
            const auto error = std::find_if(
                processValidation.issues.cbegin(),
                processValidation.issues.cend(),
                [](const Data::ConfigurationIssue &issue) {
                    return issue.severity == Data::ConfigurationIssueSeverity::Error;
                });
            return Utils::ResultError(
                Tr::tr("Process Data for '%1' is invalid: %2").arg(slave.name, error->message));
        }

        const QList<Data::ConfigurationIssue> startupIssues = Data::validateStartupConfiguration(
            slave.startup);
        const auto startupError = std::find_if(
            startupIssues.cbegin(), startupIssues.cend(), [](const Data::ConfigurationIssue &issue) {
                return issue.severity == Data::ConfigurationIssueSeverity::Error;
            });
        if (startupError != startupIssues.cend()) {
            return Utils::ResultError(
                Tr::tr("Startup for '%1' is invalid: %2").arg(slave.name, startupError->message));
        }

        const QList<Data::ConfigurationIssue> dcIssues = Data::validateDcConfiguration(slave.dc);
        const auto dcError = std::find_if(
            dcIssues.cbegin(), dcIssues.cend(), [](const Data::ConfigurationIssue &issue) {
                return issue.severity == Data::ConfigurationIssueSeverity::Error;
            });
        if (dcError != dcIssues.cend()) {
            return Utils::ResultError(
                Tr::tr("DC for '%1' is invalid: %2").arg(slave.name, dcError->message));
        }

        for (const Data::SyncManagerConfiguration &syncManager : slave.processData.syncManagers) {
            if (const Utils::Result<> result
                = insertConfigurationId(ids, syncManager.id, Tr::tr("Sync Manager"));
                !result) {
                return result;
            }
        }
        for (const Data::PdoConfiguration &pdo : slave.processData.pdos) {
            if (const Utils::Result<> result = insertConfigurationId(ids, pdo.id, Tr::tr("PDO"));
                !result) {
                return result;
            }
            for (const Data::PdoEntryConfiguration &entry : pdo.entries) {
                if (const Utils::Result<> result
                    = insertConfigurationId(ids, entry.id, Tr::tr("PDO entry"));
                    !result) {
                    return result;
                }
            }
        }
        for (const Data::StartupParameterConfiguration &parameter : slave.startup.parameters) {
            if (const Utils::Result<> result
                = insertConfigurationId(ids, parameter.id, Tr::tr("Startup parameter"));
                !result) {
                return result;
            }
        }
    }
    return validateBindingArtifact(snapshot.masterBindingArtifact);
}

EtherCATProjectDocument::EtherCATProjectDocument(QObject *parent)
    : ::Core::IDocument(parent)
{
    setId(Constants::DOCUMENT_ID);
    setMimeType(Constants::MIME_TYPE);
    setSuspendAllowed(false);

    connect(&m_undoStack, &QUndoStack::indexChanged, this, [this] { publishSnapshot(); });
    connect(&m_undoStack, &QUndoStack::cleanChanged, this, [this] { publishSnapshot(); });
}

Utils::Result<> EtherCATProjectDocument::load(const Utils::FilePath &filePath)
{
    setFilePath(filePath);
    m_migrationBackupPath = {};

    const Utils::Result<QByteArray> contents = filePath.fileContents();
    if (!contents) {
        if (!m_snapshot.valid)
            setInvalidSnapshot(filePath.completeBaseName(), contents.error());
        return Utils::ResultError(contents.error());
    }

    const Utils::Result<LoadedProject> loaded = parseProject(*contents, filePath.completeBaseName());
    if (!loaded) {
        if (!m_snapshot.valid)
            setInvalidSnapshot(filePath.completeBaseName(), loaded.error());
        return Utils::ResultError(loaded.error());
    }
    if (m_snapshot.valid && !m_snapshot.id.isNull() && loaded->snapshot.id != m_snapshot.id) {
        return Utils::ResultError(
            Tr::tr("The project ID changed while the EtherCAT project was open."));
    }

    {
        const QSignalBlocker blocker(&m_undoStack);
        m_undoStack.clear();
        m_snapshot = loaded->snapshot;
    }
    m_migrationPending = loaded->migrationRequired;
    m_migrationSourceVersion = loaded->migrationRequired ? loaded->sourceFormatVersion : -1;
    m_snapshot.modified = isModified();
    publishSnapshot();
    return Utils::ResultOk;
}

const Data::ProjectSnapshot &EtherCATProjectDocument::snapshot() const
{
    return m_snapshot;
}

QUndoStack *EtherCATProjectDocument::undoStack()
{
    return &m_undoStack;
}

Utils::FilePath EtherCATProjectDocument::migrationBackupPath() const
{
    return m_migrationBackupPath;
}

Utils::Result<> EtherCATProjectDocument::renameProject(const QString &name)
{
    if (!m_snapshot.valid)
        return Utils::ResultError(Tr::tr("Cannot edit an invalid EtherCAT project."));

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty())
        return Utils::ResultError(Tr::tr("Project name cannot be empty."));
    if (trimmedName == m_snapshot.name)
        return Utils::ResultOk;

    m_undoStack.push(new RenameProjectCommand(this, m_snapshot.name, trimmedName));
    return Utils::ResultOk;
}

Utils::Result<> EtherCATProjectDocument::renameStructuralNode(
    const Data::NodeId &nodeId, const QString &name)
{
    if (!m_snapshot.valid)
        return Utils::ResultError(Tr::tr("Cannot edit an invalid EtherCAT project."));

    const auto node = std::find_if(
        m_snapshot.nodes.cbegin(), m_snapshot.nodes.cend(), [&nodeId](const auto &entry) {
            return entry.id == nodeId;
        });
    if (node == m_snapshot.nodes.cend())
        return Utils::ResultError(Tr::tr("The requested EtherCAT project node does not exist."));
    if (node->kind != Data::ProjectNodeKind::Target
        && node->kind != Data::ProjectNodeKind::Master) {
        return Utils::ResultError(
            Tr::tr("Only EtherCAT target and master nodes can be renamed by this command."));
    }

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty())
        return Utils::ResultError(Tr::tr("EtherCAT target and master names cannot be empty."));
    if (trimmedName == node->name)
        return Utils::ResultOk;

    m_undoStack.push(
        new RenameStructuralNodeCommand(this, nodeId, node->name, trimmedName));
    return Utils::ResultOk;
}

Utils::Result<> EtherCATProjectDocument::setMasterConfiguration(
    const Data::NodeId &masterId, const Data::MasterConfiguration &configuration)
{
    if (!m_snapshot.valid)
        return Utils::ResultError(Tr::tr("Cannot edit an invalid EtherCAT project."));

    const auto master = std::find_if(
        m_snapshot.nodes.cbegin(), m_snapshot.nodes.cend(), [&masterId](const auto &node) {
            return node.id == masterId && node.kind == Data::ProjectNodeKind::Master;
        });
    if (master == m_snapshot.nodes.cend())
        return Utils::ResultError(Tr::tr("The requested EtherCAT master does not exist."));
    if (configuration == m_snapshot.masterConfiguration)
        return Utils::ResultOk;

    const Data::MasterConfiguration oldConfiguration = m_snapshot.masterConfiguration;
    const Data::SemanticBindingArtifactReference oldBindingArtifact
        = m_snapshot.masterBindingArtifact;
    Data::ProjectSnapshot candidate = m_snapshot;
    candidate.masterConfiguration = configuration;
    candidate.masterBindingArtifact = {};
    if (const Utils::Result<> validation = validateProjectConfigurations(candidate); !validation)
        return validation;

    m_undoStack.push(new UpdateMasterConfigurationCommand(
        this, oldConfiguration, configuration, oldBindingArtifact));
    return Utils::ResultOk;
}

Utils::Result<> EtherCATProjectDocument::replaceOfflineSlaves(
    const Data::NodeId &masterId, const QList<Data::OfflineSlaveConfiguration> &slaves)
{
    if (!m_snapshot.valid)
        return Utils::ResultError(Tr::tr("Cannot edit an invalid EtherCAT project."));

    const auto master = std::find_if(
        m_snapshot.nodes.cbegin(), m_snapshot.nodes.cend(), [&masterId](const auto &node) {
            return node.id == masterId && node.kind == Data::ProjectNodeKind::Master;
        });
    if (master == m_snapshot.nodes.cend())
        return Utils::ResultError(Tr::tr("The requested EtherCAT master does not exist."));

    QSet<Data::NodeId> structuralIds;
    for (const Data::ProjectNodeSnapshot &node : std::as_const(m_snapshot.nodes)) {
        if (node.kind != Data::ProjectNodeKind::Slave)
            structuralIds.insert(node.id);
    }
    for (const Data::OfflineSlaveConfiguration &existing : std::as_const(m_snapshot.slaves)) {
        if (existing.masterId != masterId)
            structuralIds.insert(existing.id);
    }

    QList<Data::OfflineSlaveConfiguration> normalized = slaves;
    QSet<Data::NodeId> ids;
    QSet<int> positions;
    for (Data::OfflineSlaveConfiguration &slave : normalized) {
        slave.name = slave.name.trimmed();
        if (slave.id.isNull())
            return Utils::ResultError(Tr::tr("An offline slave has an invalid node ID."));
        if (slave.masterId != masterId) {
            return Utils::ResultError(
                Tr::tr("An offline slave belongs to a different EtherCAT master."));
        }
        if (ids.contains(slave.id) || structuralIds.contains(slave.id))
            return Utils::ResultError(Tr::tr("Offline slave node IDs must be unique."));
        if (slave.position < 0 || positions.contains(slave.position))
            return Utils::ResultError(Tr::tr("Offline slave positions must be unique and valid."));
        if (slave.name.isEmpty())
            return Utils::ResultError(Tr::tr("Offline slave names cannot be empty."));
        if (slave.identity.vendorId == 0 || slave.identity.productCode == 0) {
            return Utils::ResultError(
                Tr::tr("Offline slaves require non-zero Vendor ID and Product Code values."));
        }
        if (!slave.esiSha256.isEmpty() && slave.esiSha256.size() != 32) {
            return Utils::ResultError(
                Tr::tr("ESI SHA-256 digests must contain exactly 32 bytes."));
        }
        const auto normalizedSelection
            = normalizeAdapterSelection(slave.adapterSelection, slave.esiSha256);
        if (!normalizedSelection)
            return Utils::ResultError(normalizedSelection.error());
        slave.adapterSelection = *normalizedSelection;
        ids.insert(slave.id);
        positions.insert(slave.position);
    }
    std::sort(normalized.begin(), normalized.end(), [](const auto &left, const auto &right) {
        return left.position < right.position;
    });

    const QList<Data::OfflineSlaveConfiguration> oldSlaves = slavesForMaster(m_snapshot, masterId);
    if (oldSlaves == normalized)
        return Utils::ResultOk;
    const Data::SemanticBindingArtifactReference oldBindingArtifact
        = m_snapshot.masterBindingArtifact;
    Data::ProjectSnapshot candidate = m_snapshot;
    applyOfflineSlavesToSnapshot(candidate, masterId, normalized);
    candidate.masterBindingArtifact = {};
    if (const Utils::Result<> validation = validateProjectConfigurations(candidate); !validation)
        return validation;

    m_undoStack.push(new ReplaceOfflineSlavesCommand(
        this, masterId, oldSlaves, normalized, oldBindingArtifact));
    return Utils::ResultOk;
}

Utils::Result<> EtherCATProjectDocument::setProcessDataConfiguration(
    const Data::NodeId &slaveId, const Data::ProcessDataConfiguration &configuration)
{
    if (!m_snapshot.valid)
        return Utils::ResultError(Tr::tr("Cannot edit an invalid EtherCAT project."));
    const auto slave = std::find_if(
        m_snapshot.slaves.cbegin(), m_snapshot.slaves.cend(), [&slaveId](const auto &entry) {
            return entry.id == slaveId;
        });
    if (slave == m_snapshot.slaves.cend())
        return Utils::ResultError(Tr::tr("The requested offline slave does not exist."));
    if (slave->processData == configuration)
        return Utils::ResultOk;

    const Data::OfflineSlaveConfiguration oldSlave = *slave;
    const Data::SemanticBindingArtifactReference oldBindingArtifact
        = m_snapshot.masterBindingArtifact;
    Data::OfflineSlaveConfiguration updated = oldSlave;
    updated.processData = configuration;
    Data::ProjectSnapshot candidate = m_snapshot;
    *std::find_if(candidate.slaves.begin(), candidate.slaves.end(), [&slaveId](const auto &entry) {
        return entry.id == slaveId;
    }) = updated;
    candidate.masterBindingArtifact = {};
    if (const Utils::Result<> validation = validateProjectConfigurations(candidate); !validation)
        return validation;

    m_undoStack.push(new UpdateOfflineSlaveCommand(
        this,
        oldSlave,
        updated,
        oldBindingArtifact,
        Tr::tr("Configure EtherCAT Process Data")));
    return Utils::ResultOk;
}

Utils::Result<> EtherCATProjectDocument::setStartupConfiguration(
    const Data::NodeId &slaveId, const Data::StartupConfiguration &configuration)
{
    if (!m_snapshot.valid)
        return Utils::ResultError(Tr::tr("Cannot edit an invalid EtherCAT project."));
    const auto slave = std::find_if(
        m_snapshot.slaves.cbegin(), m_snapshot.slaves.cend(), [&slaveId](const auto &entry) {
            return entry.id == slaveId;
        });
    if (slave == m_snapshot.slaves.cend())
        return Utils::ResultError(Tr::tr("The requested offline slave does not exist."));
    if (slave->startup == configuration)
        return Utils::ResultOk;

    const Data::OfflineSlaveConfiguration oldSlave = *slave;
    const Data::SemanticBindingArtifactReference oldBindingArtifact
        = m_snapshot.masterBindingArtifact;
    Data::OfflineSlaveConfiguration updated = oldSlave;
    updated.startup = configuration;
    Data::ProjectSnapshot candidate = m_snapshot;
    *std::find_if(candidate.slaves.begin(), candidate.slaves.end(), [&slaveId](const auto &entry) {
        return entry.id == slaveId;
    }) = updated;
    candidate.masterBindingArtifact = {};
    if (const Utils::Result<> validation = validateProjectConfigurations(candidate); !validation)
        return validation;

    m_undoStack.push(new UpdateOfflineSlaveCommand(
        this,
        oldSlave,
        updated,
        oldBindingArtifact,
        Tr::tr("Configure EtherCAT Startup")));
    return Utils::ResultOk;
}

Utils::Result<> EtherCATProjectDocument::setDcConfiguration(
    const Data::NodeId &slaveId, const Data::DcConfiguration &configuration)
{
    if (!m_snapshot.valid)
        return Utils::ResultError(Tr::tr("Cannot edit an invalid EtherCAT project."));
    const auto slave = std::find_if(
        m_snapshot.slaves.cbegin(), m_snapshot.slaves.cend(), [&slaveId](const auto &entry) {
            return entry.id == slaveId;
        });
    if (slave == m_snapshot.slaves.cend())
        return Utils::ResultError(Tr::tr("The requested offline slave does not exist."));
    if (slave->dc == configuration)
        return Utils::ResultOk;

    const Data::OfflineSlaveConfiguration oldSlave = *slave;
    const Data::SemanticBindingArtifactReference oldBindingArtifact
        = m_snapshot.masterBindingArtifact;
    Data::OfflineSlaveConfiguration updated = oldSlave;
    updated.dc = configuration;
    Data::ProjectSnapshot candidate = m_snapshot;
    *std::find_if(candidate.slaves.begin(), candidate.slaves.end(), [&slaveId](const auto &entry) {
        return entry.id == slaveId;
    }) = updated;
    candidate.masterBindingArtifact = {};
    if (const Utils::Result<> validation = validateProjectConfigurations(candidate); !validation)
        return validation;

    m_undoStack.push(new UpdateOfflineSlaveCommand(
        this,
        oldSlave,
        updated,
        oldBindingArtifact,
        Tr::tr("Configure EtherCAT Distributed Clocks")));
    return Utils::ResultOk;
}

Utils::Result<> EtherCATProjectDocument::setDeviceAdapterSelection(
    const Data::NodeId &slaveId,
    const QByteArray &esiSha256,
    const Data::DeviceAdapterProjectSelection &selection)
{
    if (!m_snapshot.valid)
        return Utils::ResultError(Tr::tr("Cannot edit an invalid EtherCAT project."));
    const auto slave = std::find_if(
        m_snapshot.slaves.cbegin(), m_snapshot.slaves.cend(), [&slaveId](const auto &entry) {
            return entry.id == slaveId;
        });
    if (slave == m_snapshot.slaves.cend())
        return Utils::ResultError(Tr::tr("The requested offline slave does not exist."));
    if (!esiSha256.isEmpty() && esiSha256.size() != 32) {
        return Utils::ResultError(
            Tr::tr("ESI SHA-256 digests must contain exactly 32 bytes."));
    }

    const auto normalizedSelection = normalizeAdapterSelection(selection, esiSha256);
    if (!normalizedSelection)
        return Utils::ResultError(normalizedSelection.error());
    if (slave->esiSha256 == esiSha256 && slave->adapterSelection == *normalizedSelection)
        return Utils::ResultOk;

    const Data::OfflineSlaveConfiguration oldSlave = *slave;
    const Data::SemanticBindingArtifactReference oldBindingArtifact
        = m_snapshot.masterBindingArtifact;
    Data::OfflineSlaveConfiguration updated = oldSlave;
    updated.esiSha256 = esiSha256;
    updated.adapterSelection = *normalizedSelection;
    Data::ProjectSnapshot candidate = m_snapshot;
    *std::find_if(candidate.slaves.begin(), candidate.slaves.end(), [&slaveId](const auto &entry) {
        return entry.id == slaveId;
    }) = updated;
    candidate.masterBindingArtifact = {};
    if (const Utils::Result<> validation = validateProjectConfigurations(candidate); !validation)
        return validation;

    m_undoStack.push(new UpdateOfflineSlaveCommand(
        this,
        oldSlave,
        updated,
        oldBindingArtifact,
        Tr::tr("Select EtherCAT device adapter")));
    return Utils::ResultOk;
}

Utils::Result<> EtherCATProjectDocument::setMasterBindingArtifact(
    const Data::SemanticBindingArtifactReference &reference)
{
    if (!m_snapshot.valid)
        return Utils::ResultError(Tr::tr("Cannot edit an invalid EtherCAT project."));
    if (const Utils::Result<> validation = validateBindingArtifact(reference); !validation)
        return validation;
    if (m_snapshot.masterBindingArtifact == reference)
        return Utils::ResultOk;

    Data::ProjectSnapshot candidate = m_snapshot;
    candidate.masterBindingArtifact = reference;
    if (const Utils::Result<> validation = validateProjectConfigurations(candidate); !validation)
        return validation;

    m_undoStack.push(new UpdateMasterBindingArtifactCommand(
        this, m_snapshot.masterBindingArtifact, reference));
    return Utils::ResultOk;
}

QByteArray EtherCATProjectDocument::contents() const
{
    return serializeProject(m_snapshot);
}

bool EtherCATProjectDocument::isModified() const
{
    return m_migrationPending || !m_undoStack.isClean();
}

bool EtherCATProjectDocument::isSaveAsAllowed() const
{
    return false;
}

bool EtherCATProjectDocument::shouldAutoSave() const
{
    return false;
}

::Core::IDocument::ReloadBehavior EtherCATProjectDocument::reloadBehavior(
    ChangeTrigger trigger, ChangeType type) const
{
    Q_UNUSED(type)
    if (trigger == TriggerExternal && isModified())
        return BehaviorAsk;
    return BehaviorSilent;
}

Utils::Result<> EtherCATProjectDocument::reload(ReloadFlag flag, ChangeType type)
{
    Q_UNUSED(type)
    if (flag == FlagIgnore)
        return Utils::ResultOk;
    return load(filePath());
}

Utils::Result<> EtherCATProjectDocument::saveImpl(const Utils::FilePath &filePath, SaveOption option)
{
    Q_UNUSED(option)
    if (!m_snapshot.valid)
        return Utils::ResultError(Tr::tr("Cannot save an invalid EtherCAT project."));

    const Utils::FilePath destination = filePath.isEmpty() ? this->filePath() : filePath;
    if (destination.isEmpty())
        return Utils::ResultError(Tr::tr("No EtherCAT project file path was specified."));
    if (destination != this->filePath()) {
        return Utils::ResultError(Tr::tr("Save As is not supported for an open EtherCAT project."));
    }

    if (m_migrationPending && destination == this->filePath()) {
        const Utils::Result<> backupResult = createMigrationBackup(this->filePath());
        if (!backupResult)
            return backupResult;
    }

    Utils::FileSaver saver(destination, QIODevice::Text);
    if (!saver.write(serializeProject(m_snapshot)))
        return Utils::ResultError(saver.errorString());
    const Utils::Result<> saveResult = saver.finalize();
    if (!saveResult)
        return saveResult;

    m_migrationPending = false;
    m_migrationSourceVersion = -1;
    m_snapshot.migrated = false;
    m_undoStack.setClean();
    publishSnapshot();
    return Utils::ResultOk;
}

void EtherCATProjectDocument::applyProjectName(const QString &name)
{
    m_snapshot.name = name;
    for (Data::ProjectNodeSnapshot &node : m_snapshot.nodes) {
        if (node.kind == Data::ProjectNodeKind::Project) {
            node.name = name;
            break;
        }
    }
}

void EtherCATProjectDocument::applyStructuralNodeName(
    const Data::NodeId &nodeId, const QString &name)
{
    const auto node = std::find_if(
        m_snapshot.nodes.begin(), m_snapshot.nodes.end(), [&nodeId](const auto &entry) {
            return entry.id == nodeId;
        });
    if (node != m_snapshot.nodes.end())
        node->name = name;
}

void EtherCATProjectDocument::applyMasterConfiguration(
    const Data::MasterConfiguration &configuration)
{
    m_snapshot.masterConfiguration = configuration;
}

void EtherCATProjectDocument::applyMasterBindingArtifact(
    const Data::SemanticBindingArtifactReference &reference)
{
    m_snapshot.masterBindingArtifact = reference;
}

void EtherCATProjectDocument::applyOfflineSlaves(
    const Data::NodeId &masterId, const QList<Data::OfflineSlaveConfiguration> &slaves)
{
    applyOfflineSlavesToSnapshot(m_snapshot, masterId, slaves);
}

void EtherCATProjectDocument::applyOfflineSlave(const Data::OfflineSlaveConfiguration &slave)
{
    const auto current = std::find_if(
        m_snapshot.slaves.begin(), m_snapshot.slaves.end(), [&slave](const auto &entry) {
            return entry.id == slave.id;
        });
    if (current != m_snapshot.slaves.end())
        *current = slave;
}

void EtherCATProjectDocument::publishSnapshot()
{
    m_snapshot.modified = isModified();
    emit changed();
    emit contentsChanged();
    emit snapshotChanged(m_snapshot);
}

void EtherCATProjectDocument::setInvalidSnapshot(const QString &fallbackName, const QString &error)
{
    {
        const QSignalBlocker blocker(&m_undoStack);
        m_undoStack.clear();
        m_snapshot = createProjectSnapshot(fallbackName, {});
    }
    m_snapshot.valid = false;
    m_snapshot.error = error;
    m_migrationPending = false;
    m_migrationSourceVersion = -1;
    publishSnapshot();
}

Utils::Result<> EtherCATProjectDocument::createMigrationBackup(const Utils::FilePath &sourcePath)
{
    if (!m_migrationBackupPath.isEmpty())
        return Utils::ResultOk;

    const QString suffix = QString(".v%1.bak").arg(m_migrationSourceVersion);
    Utils::FilePath candidate = sourcePath.stringAppended(suffix);
    for (int index = 1; candidate.exists(); ++index)
        candidate = sourcePath.stringAppended(QString("%1.%2").arg(suffix).arg(index));

    const Utils::Result<> copyResult = sourcePath.copyFile(candidate);
    if (!copyResult) {
        return Utils::ResultError(
            Tr::tr("Could not create migration backup '%1': %2")
                .arg(candidate.toUserOutput(), copyResult.error()));
    }
    m_migrationBackupPath = candidate;
    return Utils::ResultOk;
}

} // namespace EtherCAT::Project::Internal
