// Copyright (C) 2026 Kvell

#include "deviceparameterspage.h"

#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"

#include <ethercatcore/deviceparametercontract.h>
#include <ethercatcore/manualcontrolcontract.h>
#include <ethercatcore/providerregistry.h>

#include <utils/infolabel.h>
#include <utils/stylehelper.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace EtherCAT::Workbench::Internal {
namespace {

const Data::OfflineSlaveConfiguration *configuredSlave(
    const Data::ProjectSnapshot &project, const Data::NodeId &slaveId)
{
    const auto slave = std::find_if(
        project.slaves.cbegin(),
        project.slaves.cend(),
        [&slaveId](const Data::OfflineSlaveConfiguration &candidate) {
            return candidate.id == slaveId;
        });
    return slave == project.slaves.cend() ? nullptr : &*slave;
}

QString rationalText(const Data::ExactRational &value)
{
    return QString::number(value.numerator) + '/' + QString::number(value.denominator);
}

QString engineeringValueText(const Data::EngineeringValue &value)
{
    switch (value.kind) {
    case Data::EngineeringValueKind::Boolean:
        return value.boolean ? Tr::tr("True") : Tr::tr("False");
    case Data::EngineeringValueKind::SignedInteger:
        return QString::number(value.signedInteger);
    case Data::EngineeringValueKind::UnsignedInteger:
        return QString::number(value.unsignedInteger);
    case Data::EngineeringValueKind::ExactRational:
        return rationalText(value.rational);
    case Data::EngineeringValueKind::Enumeration:
        return value.enumerationName;
    case Data::EngineeringValueKind::Invalid:
        break;
    }
    return {};
}

QString constraintText(const Data::EngineeringConstraint &constraint)
{
    QStringList parts;
    if (constraint.minimum)
        parts.append(Tr::tr("minimum %1").arg(rationalText(*constraint.minimum)));
    if (constraint.maximum)
        parts.append(Tr::tr("maximum %1").arg(rationalText(*constraint.maximum)));
    if (constraint.step) {
        QString step = Tr::tr("step %1").arg(rationalText(*constraint.step));
        if (constraint.stepOrigin)
            step += Tr::tr(" from %1").arg(rationalText(*constraint.stepOrigin));
        parts.append(step);
    }
    if (!constraint.enumeration.isEmpty()) {
        QStringList values;
        for (const Data::EngineeringEnumerationValue &entry : constraint.enumeration) {
            values.append(
                entry.displayName.isEmpty() ? entry.id
                                            : Tr::tr("%1 (%2)").arg(entry.displayName, entry.id));
        }
        parts.append(Tr::tr("values: %1").arg(values.join(", ")));
    }
    return parts.isEmpty() ? Tr::tr("no additional range constraint") : parts.join("; ");
}

QString objectAddress(const Data::DeviceParameterObjectBinding &object)
{
    return QString("0x%1:%2")
        .arg(object.index, 4, 16, QLatin1Char('0'))
        .arg(object.subIndex, 2, 16, QLatin1Char('0'));
}

QString configuredProjectionText(const Data::DeviceParameterDefinition &definition)
{
    const Data::DeviceParameterConfiguredProjection &projection = definition.configuredProjection;
    if (projection.kind == Data::DeviceParameterProjectionKind::ProjectOnly)
        return Tr::tr("Project only: %1").arg(projection.reason);
    if (projection.kind == Data::DeviceParameterProjectionKind::CoeStartupSdo && projection.object) {
        return Tr::tr("CoE Startup SDO %1 during %2")
            .arg(objectAddress(*projection.object), projection.transition);
    }
    return Tr::tr("Invalid signed projection");
}

QString parameterDescription(const Data::DeviceParameterDefinition &definition)
{
    QStringList lines;
    lines.append(definition.description);
    lines.append(Tr::tr("Identifier: %1").arg(definition.id));
    lines.append(definition.required ? Tr::tr("Required") : Tr::tr("Optional"));
    if (!definition.unit.isEmpty())
        lines.append(Tr::tr("Unit: %1").arg(definition.unit));
    lines.append(Tr::tr("Constraint: %1").arg(constraintText(definition.engineeringConstraint)));
    if (definition.engineeringDefaultValue) {
        lines.append(Tr::tr("Signed default (reference only, not selected automatically): %1")
                         .arg(engineeringValueText(*definition.engineeringDefaultValue)));
    } else {
        lines.append(Tr::tr("No signed default"));
    }
    lines.append(Tr::tr("Configured projection: %1").arg(configuredProjectionText(definition)));
    lines.removeAll({});
    return lines.join('\n');
}

const Data::DeviceParameterValue *configuredValue(
    const Data::DeviceParameterConfiguration &configuration, const QString &parameterId)
{
    const auto value = std::lower_bound(
        configuration.values.cbegin(),
        configuration.values.cend(),
        parameterId,
        [](const Data::DeviceParameterValue &candidate, const QString &id) {
            return candidate.parameterId < id;
        });
    return value != configuration.values.cend() && value->parameterId == parameterId ? &*value
                                                                                     : nullptr;
}

bool manifestDefinitionClosureIsUsable(
    const Data::DeviceAdapterManifest &manifest,
    const Data::OfflineSlaveConfiguration &slave,
    QString *rejection)
{
    const Core::ConfiguredDeviceParameterValidation validation
        = Core::validateConfiguredDeviceParameters(
            manifest, slave.esiSha256, slave.adapterSelection, {});
    if (validation.accepted()
        || validation.error == Core::ConfiguredDeviceParameterError::MissingRequiredParameter) {
        return true;
    }
    if (rejection)
        *rejection = validation.detail;
    return false;
}

struct ExactAdapterResolution
{
    Core::DeviceAdapterProvider *provider = nullptr;
    Data::DeviceAdapterManifest manifest;
};

std::optional<Data::DeviceDescription> exactDeviceDescription(
    WorkbenchController *controller,
    const Data::OfflineSlaveConfiguration &slave,
    QString *rejection)
{
    Core::DeviceRepositoryProvider *repository = controller ? controller->deviceRepository()
                                                            : nullptr;
    const std::optional<Data::DeviceDescription> device
        = repository && !slave.deviceDescriptionId.isNull()
              ? repository->device(slave.deviceDescriptionId)
              : std::nullopt;
    if (!device || device->summary.id != slave.deviceDescriptionId || !device->summary.supported
        || device->summary.identity != slave.identity || device->sourceSha256 != slave.esiSha256
        || slave.esiSha256.size() != 32) {
        if (rejection) {
            *rejection = Tr::tr("The exact supported ESI device description is unavailable or "
                                "does not match the configured slave.");
        }
        return std::nullopt;
    }
    return device;
}

bool providerResolvesExactSlave(
    Core::DeviceAdapterProvider *provider,
    const Data::DeviceAdapterManifest &manifest,
    const Data::OfflineSlaveConfiguration &slave,
    const Data::DeviceDescription &device,
    QString *rejection)
{
    const Data::ConfigurationValidation processData = Data::validateProcessDataConfiguration(
        slave.processData);
    if (processData.hasErrors()) {
        if (rejection)
            *rejection = Tr::tr("The configured process image is invalid.");
        return false;
    }

    Data::DeviceAdapterResolutionRequest request;
    request.slaveId = slave.id;
    request.device = device;
    request.processImage = processData.processImage;
    request.expectedAdapterId = slave.adapterSelection.adapterId;
    request.expectedAdapterVersion = slave.adapterSelection.adapterVersion;
    request.expectedAdapterContentSha256 = slave.adapterSelection.adapterContentSha256;
    request.processDataProfileId = slave.adapterSelection.processDataProfileId;
    request.moduleAssignments = slave.adapterSelection.moduleAssignments;
    request.allowCandidate = false;
    request.allowMock = false;
    request.requireRealHardwareQualification = true;

    const Data::DeviceAdapterResolutionResult resolution = provider->resolveDevice(request);
    const Data::ResolvedDeviceModel &model = resolution.model;
    if (!resolution.resolved || !model.complete || model.slaveId != slave.id
        || model.identity != slave.identity || model.esiSha256 != slave.esiSha256
        || model.adapterId != manifest.id || model.adapterVersion != manifest.version
        || model.adapterContentSha256 != manifest.contentSha256
        || model.qualification != Data::DeviceAdapterQualification::Qualified
        || model.processDataProfileId != slave.adapterSelection.processDataProfileId
        || model.moduleAssignments != slave.adapterSelection.moduleAssignments) {
        if (rejection) {
            *rejection = resolution.error.isEmpty()
                             ? Tr::tr("The provider did not resolve a complete exact Qualified "
                                      "device model for the configured ESI, profile, and modules.")
                             : resolution.error;
        }
        return false;
    }
    return true;
}

bool manifestMatchesExactDevice(
    const Data::DeviceAdapterManifest &manifest,
    const Data::OfflineSlaveConfiguration &slave,
    const Data::DeviceDescription &device,
    QString *rejection)
{
    const Data::DeviceIdentity &identity = device.summary.identity;
    if (manifest.match.vendorId != identity.vendorId
        || manifest.match.productCode != identity.productCode
        || identity.revisionNumber < manifest.match.minimumRevision
        || identity.revisionNumber > manifest.match.maximumRevision
        || manifest.match.exactEsiSha256 != device.sourceSha256
        || manifest.match.exactEsiSha256 != slave.esiSha256) {
        if (rejection) {
            *rejection = Tr::tr("The exact Adapter match does not authorize this configured "
                                "slave's vendor, product, revision, and ESI hash.");
        }
        return false;
    }
    return true;
}

QList<ExactAdapterResolution> exactAdapterResolutions(
    WorkbenchController *controller,
    const Data::OfflineSlaveConfiguration &slave,
    QString *firstRejection)
{
    QList<ExactAdapterResolution> result;
    if (firstRejection)
        firstRejection->clear();
    if (!controller || !controller->providerRegistry())
        return result;
    QString deviceRejection;
    const std::optional<Data::DeviceDescription> device
        = exactDeviceDescription(controller, slave, &deviceRejection);
    if (!device) {
        if (firstRejection)
            *firstRejection = deviceRejection;
        return result;
    }

    for (Core::Provider *candidate :
         controller->providerRegistry()->providers(Core::ProviderKind::DeviceAdapter)) {
        auto provider = qobject_cast<Core::DeviceAdapterProvider *>(candidate);
        if (!provider || !provider->isAvailable())
            continue;
        const std::optional<Data::DeviceAdapterManifest> manifest = provider->adapterManifest(
            slave.adapterSelection.adapterId, slave.adapterSelection.adapterVersion);
        if (!manifest || manifest->contentSha256 != slave.adapterSelection.adapterContentSha256)
            continue;
        QString rejection;
        if (!manifestMatchesExactDevice(*manifest, slave, *device, &rejection)
            || !manifestDefinitionClosureIsUsable(*manifest, slave, &rejection)
            || !providerResolvesExactSlave(provider, *manifest, slave, *device, &rejection)) {
            if (firstRejection && firstRejection->isEmpty())
                *firstRejection = rejection;
            continue;
        }
        result.append({provider, *manifest});
    }
    return result;
}

QWidget *createEditor(
    const Data::DeviceParameterDefinition &definition,
    const Data::DeviceParameterValue *configured,
    QWidget *parent)
{
    const QString defaultText = definition.engineeringDefaultValue
                                    ? engineeringValueText(*definition.engineeringDefaultValue)
                                    : QString();
    if (definition.valueKind == Data::EngineeringValueKind::Boolean) {
        auto editor = new QComboBox(parent);
        editor->addItem(Tr::tr("Not configured"), -1);
        editor->addItem(Tr::tr("False"), 0);
        editor->addItem(Tr::tr("True"), 1);
        if (configured)
            editor->setCurrentIndex(configured->value.boolean ? 2 : 1);
        return editor;
    }
    if (definition.valueKind == Data::EngineeringValueKind::Enumeration) {
        auto editor = new QComboBox(parent);
        editor->addItem(Tr::tr("Not configured"), QString());
        for (const Data::EngineeringEnumerationValue &entry :
             definition.engineeringConstraint.enumeration) {
            editor->addItem(entry.displayName.isEmpty() ? entry.id : entry.displayName, entry.id);
        }
        if (configured) {
            const int index = editor->findData(configured->value.enumerationName);
            editor->setCurrentIndex(index >= 0 ? index : 0);
        }
        return editor;
    }
    auto editor = new QLineEdit(parent);
    if (configured)
        editor->setText(engineeringValueText(configured->value));
    if (!defaultText.isEmpty()) {
        editor->setPlaceholderText(Tr::tr("Not configured; signed default: %1").arg(defaultText));
    } else {
        editor->setPlaceholderText(Tr::tr("Not configured"));
    }
    return editor;
}

std::optional<Data::EngineeringValue> editorValue(
    const Data::DeviceParameterDefinition &definition, QWidget *editor, QString *rejection)
{
    rejection->clear();
    if (auto combo = qobject_cast<QComboBox *>(editor)) {
        if (combo->currentIndex() <= 0)
            return std::nullopt;
        if (definition.valueKind == Data::EngineeringValueKind::Boolean)
            return Data::EngineeringValue::fromBoolean(combo->currentData().toInt() != 0);
        if (definition.valueKind == Data::EngineeringValueKind::Enumeration)
            return Data::EngineeringValue::fromEnumeration(combo->currentData().toString());
        *rejection = Tr::tr("The signed parameter kind has no supported editor.");
        return std::nullopt;
    }
    auto lineEdit = qobject_cast<QLineEdit *>(editor);
    if (!lineEdit) {
        *rejection = Tr::tr("The parameter editor is unavailable.");
        return std::nullopt;
    }
    const QString text = lineEdit->text().trimmed();
    if (text.isEmpty())
        return std::nullopt;
    bool ok = false;
    switch (definition.valueKind) {
    case Data::EngineeringValueKind::SignedInteger: {
        const qint64 value = text.toLongLong(&ok, 10);
        if (ok)
            return Data::EngineeringValue::fromSignedInteger(value);
        *rejection = Tr::tr("Enter a base-10 signed integer.");
        return std::nullopt;
    }
    case Data::EngineeringValueKind::UnsignedInteger: {
        if (text.startsWith('-')) {
            *rejection = Tr::tr("Enter a base-10 unsigned integer.");
            return std::nullopt;
        }
        const quint64 value = text.toULongLong(&ok, 10);
        if (ok)
            return Data::EngineeringValue::fromUnsignedInteger(value);
        *rejection = Tr::tr("Enter a base-10 unsigned integer.");
        return std::nullopt;
    }
    case Data::EngineeringValueKind::ExactRational: {
        const QStringList parts = text.split('/');
        if (parts.size() > 2 || parts.isEmpty()) {
            *rejection = Tr::tr("Enter an exact rational as numerator/denominator.");
            return std::nullopt;
        }
        const qint64 numerator = parts.at(0).trimmed().toLongLong(&ok, 10);
        if (!ok) {
            *rejection = Tr::tr("Enter an exact rational as numerator/denominator.");
            return std::nullopt;
        }
        qint64 denominator = 1;
        if (parts.size() == 2) {
            denominator = parts.at(1).trimmed().toLongLong(&ok, 10);
            if (!ok) {
                *rejection = Tr::tr("Enter an exact rational as numerator/denominator.");
                return std::nullopt;
            }
        }
        const Core::ExactRationalResult normalized
            = Core::normalizedExactRational(numerator, denominator);
        if (!normalized.validation.accepted() || !normalized.value) {
            *rejection = normalized.validation.detail;
            return std::nullopt;
        }
        return Data::EngineeringValue::fromExactRational(*normalized.value);
    }
    case Data::EngineeringValueKind::Boolean:
    case Data::EngineeringValueKind::Enumeration:
    case Data::EngineeringValueKind::Invalid:
        *rejection = Tr::tr("The signed parameter kind has no supported editor.");
        return std::nullopt;
    }
    *rejection = Tr::tr("The signed parameter kind has no supported editor.");
    return std::nullopt;
}

QString observedText(const Data::DeviceParameterDefinition &definition)
{
    if (definition.observedSource.kind == Data::DeviceParameterObservedSourceKind::CoeSdoUpload)
        return Tr::tr("Not captured");
    if (definition.observedSource.kind == Data::DeviceParameterObservedSourceKind::Unavailable)
        return Tr::tr("Unavailable");
    return Tr::tr("Unavailable");
}

QString observedSourceText(const Data::DeviceParameterDefinition &definition)
{
    if (definition.observedSource.kind == Data::DeviceParameterObservedSourceKind::CoeSdoUpload
        && definition.observedSource.object) {
        return Tr::tr("Signed CoE SDO upload source %1; no read has been requested")
            .arg(objectAddress(*definition.observedSource.object));
    }
    if (definition.observedSource.kind == Data::DeviceParameterObservedSourceKind::Unavailable)
        return definition.observedSource.reason;
    return Tr::tr("The signed observed source is invalid.");
}

QString verificationStatusText(const Data::DeviceParameterDefinition &definition)
{
    if (definition.observedSource.kind == Data::DeviceParameterObservedSourceKind::CoeSdoUpload)
        return Tr::tr("Not captured");
    return Tr::tr("Unavailable");
}

QString verificationStatusToolTip(const Data::DeviceParameterDefinition &definition)
{
    if (definition.observedSource.kind == Data::DeviceParameterObservedSourceKind::CoeSdoUpload) {
        return Tr::tr("No observed evidence has been captured, so Project intent cannot be "
                      "compared with the device.");
    }
    if (definition.observedSource.kind == Data::DeviceParameterObservedSourceKind::Unavailable) {
        return Tr::tr("The signed Adapter declares observed evidence unavailable: %1")
            .arg(definition.observedSource.reason);
    }
    return Tr::tr("The signed observed source is invalid, so verification is unavailable.");
}

const Data::AxisParameterEvidenceRecord *observedRecord(
    const Data::DeviceParameterDefinition &definition,
    const Data::AxisParameterEvidence &evidence)
{
    if (definition.observedSource.kind != Data::DeviceParameterObservedSourceKind::CoeSdoUpload
        || !definition.observedSource.object) {
        return nullptr;
    }
    const Data::DeviceParameterObjectBinding &object = *definition.observedSource.object;
    const auto record = std::find_if(
        evidence.records.cbegin(),
        evidence.records.cend(),
        [&object](const Data::AxisParameterEvidenceRecord &candidate) {
            return candidate.index == object.index && candidate.subIndex == object.subIndex;
        });
    return record == evidence.records.cend() ? nullptr : &*record;
}

bool topologyIdentityMatches(
    const Data::ControllerTopologySlave &topology,
    const Data::AxisParameterEvidenceTargetResult &target)
{
    return topology.position == target.position
           && topology.stationAddress == target.stationAddress
           && topology.vendorId == target.vendorId && topology.productCode == target.productCode
           && topology.revision == target.revision && topology.serial == target.serial;
}

bool evidenceBatchMatchesTopology(
    const Data::AxisParameterEvidenceBatch &batch,
    const Data::ControllerTopologySnapshot &topology)
{
    if (!batch.isValid() || batch.scope != topology.scope
        || batch.sessionGeneration != topology.sessionGeneration
        || batch.sessionId != topology.sessionId || batch.bootId != topology.bootId
        || batch.topologyRequestId != topology.requestId
        || batch.topologyResponseSequence != topology.responseSequence
        || batch.topologyCaptureSequence != topology.topologyCaptureSequence
        || batch.topologyCompletedTimeNs != topology.topologyCompletedTimeNs
        || batch.topologyPayloadSha256 != topology.topologyPayloadSha256
        || batch.targets.size() != topology.slaves.size()) {
        return false;
    }
    for (qsizetype index = 0; index < batch.targets.size(); ++index) {
        if (!topologyIdentityMatches(topology.slaves.at(index), batch.targets.at(index)))
            return false;
    }
    return true;
}

bool isRealEvidenceSnapshot(
    const Data::ControllerConnectionSnapshot &snapshot,
    const Data::ControllerConnectionScope &scope,
    const Data::NodeId &profileId)
{
    if (snapshot.scope != scope || snapshot.profileId != profileId || snapshot.mock
        || (snapshot.state != Data::ControllerConnectionState::Connected
            && snapshot.state != Data::ControllerConnectionState::Degraded)
        || !snapshot.sessionGeneration || !snapshot.session || !snapshot.session->sessionId
        || !snapshot.session->bootId || !snapshot.capability
        || !snapshot.capability->topologyEvidence
        || !snapshot.capability->axisParameterEvidence || !snapshot.topology) {
        return false;
    }
    const Data::ControllerTopologySnapshot &topology = *snapshot.topology;
    return topology.hasCompleteProvenance() && !topology.result
           && topology.scope == scope && topology.sessionGeneration == snapshot.sessionGeneration
           && topology.sessionId == snapshot.session->sessionId
           && topology.bootId == snapshot.session->bootId && !topology.cpu1RequestSequence
           && !topology.cpu1CompletedTimeNs && topology.topologyCaptureSequence
           && topology.topologyCompletedTimeNs && topology.topologyPayloadSha256.size() == 32
           && topology.respondingCount == quint32(topology.slaves.size());
}

bool configuredSlaveMatchesEvidence(
    const Data::OfflineSlaveConfiguration &slave,
    const Data::ControllerTopologySlave &topology,
    const Data::AxisParameterEvidenceTargetResult &target)
{
    if (slave.position < 0
        || slave.position > int(std::numeric_limits<quint16>::max())) {
        return false;
    }
    const quint16 position = quint16(slave.position);
    if (topology.position != quint32(position) || target.position != position
        || !topologyIdentityMatches(topology, target)
        || target.stationAddress != slave.stationAddress
        || target.vendorId != slave.identity.vendorId
        || target.productCode != slave.identity.productCode
        || target.revision != slave.identity.revisionNumber
        || target.serial != slave.serialNumber || topology.alias != slave.alias
        || topology.aliasValidity != Data::ControllerTopologyEvidenceValidity::Valid
        || topology.aliasProvenance != Data::ControllerTopologyEvidenceProvenance::Observed
        || topology.aliasSource != Data::ControllerTopologyEvidenceSource::EscStationAlias
        || topology.moduleProvenance
               != Data::ControllerTopologyEvidenceProvenance::DeviceReported
        || (topology.moduleSource != Data::ControllerTopologyEvidenceSource::SiiMailbox
            && topology.moduleSource
                   != Data::ControllerTopologyEvidenceSource::CoeDetectedModules)
        || (topology.moduleValidity != Data::ControllerTopologyEvidenceValidity::Valid
            && topology.moduleValidity
                   != Data::ControllerTopologyEvidenceValidity::Unavailable)
        || topology.modules.size() != slave.adapterSelection.moduleAssignments.size()) {
        return false;
    }
    if (!topology.modules.isEmpty()
        && topology.moduleValidity != Data::ControllerTopologyEvidenceValidity::Valid) {
        return false;
    }
    for (qsizetype index = 0; index < topology.modules.size(); ++index) {
        const Data::ControllerTopologyModuleEvidence &observed = topology.modules.at(index);
        const Data::DeviceModuleAssignment &configured
            = slave.adapterSelection.moduleAssignments.at(index);
        if (configured.slot < 0 || configured.slot > std::numeric_limits<quint16>::max()
            || observed.slot != quint16(configured.slot)
            || observed.moduleIdent != configured.moduleIdent
            || observed.validity != Data::ControllerTopologyEvidenceValidity::Valid
            || observed.provenance
                   != Data::ControllerTopologyEvidenceProvenance::DeviceReported
            || observed.source != Data::ControllerTopologyEvidenceSource::CoeDetectedModules) {
            return false;
        }
    }
    return true;
}

} // namespace

DeviceParametersPage::DeviceParametersPage(WorkbenchController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_summary(new QLabel(this))
    , m_feedback(new Utils::InfoLabel(this))
    , m_table(new QTreeWidget(this))
    , m_reload(new QPushButton(Tr::tr("Reload from Project"), this))
    , m_apply(new QPushButton(Tr::tr("Apply to Project"), this))
{
    setObjectName("EtherCATWorkbenchDeviceParametersPage");
    m_summary->setObjectName("EtherCATDeviceParametersSummary");
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_feedback->setObjectName("EtherCATDeviceParametersFeedback");
    m_feedback->setAccessibleName(Tr::tr("Device parameter edit feedback"));
    m_feedback->setElideMode(Qt::ElideNone);
    m_feedback->setWordWrap(true);
    m_table->setObjectName("EtherCATDeviceParametersTable");
    m_table->setAccessibleName(Tr::tr("Configured and observed device parameters"));
    m_table->setColumnCount(5);
    m_table->setHeaderLabels(
        {Tr::tr("Parameter"),
         Tr::tr("Configured"),
         Tr::tr("Observed"),
         Tr::tr("Source"),
         Tr::tr("Verification Status")});
    m_table->setAlternatingRowColors(true);
    m_table->setRootIsDecorated(false);
    m_table->setUniformRowHeights(false);
    m_table->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_reload->setObjectName("EtherCATDeviceParametersReload");
    m_reload->setAccessibleName(Tr::tr("Discard drafts and reload device parameters"));
    m_apply->setObjectName("EtherCATDeviceParametersApply");
    m_apply->setAccessibleName(Tr::tr("Apply configured device parameters to the Project"));

    auto buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(QMargins());
    buttonLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_reload);
    buttonLayout->addWidget(m_apply);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(m_summary);
    layout->addWidget(m_feedback);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttonLayout);

    connect(m_reload, &QPushButton::clicked, this, &DeviceParametersPage::reloadFromProject);
    connect(m_apply, &QPushButton::clicked, this, &DeviceParametersPage::applyConfiguration);
    if (m_controller) {
        connect(
            m_controller,
            &WorkbenchController::controllerConnectionChanged,
            this,
            &DeviceParametersPage::rebindObservationProvider);
        if (Core::ProviderRegistry *registry = m_controller->providerRegistry()) {
            connect(
                registry,
                &Core::ProviderRegistry::providerAdded,
                this,
                [this](Core::Provider *provider) {
                    if (qobject_cast<Core::DeviceAdapterProvider *>(provider))
                        scheduleContextRefresh();
                    if (qobject_cast<Core::ControllerConnectionProvider *>(provider))
                        rebindObservationProvider();
                });
            connect(
                registry,
                &Core::ProviderRegistry::providerAboutToBeRemoved,
                this,
                [this](Core::Provider *provider) {
                    if (qobject_cast<Core::DeviceAdapterProvider *>(provider))
                        scheduleContextRefresh();
                    if (provider == m_observationProvider) {
                        resetObservationProvider();
                        clearObservedPresentation();
                        QMetaObject::invokeMethod(
                            this,
                            &DeviceParametersPage::rebindObservationProvider,
                            Qt::QueuedConnection);
                    }
                });
            bindAdapterAuthoritySignals();
        }
    }
    clearBaseline(
        Tr::tr("Select a configured EtherCAT device to edit its signed parameters."),
        Tr::tr("No configured device is selected."));
}

void DeviceParametersPage::clearBaseline(const QString &summary, const QString &detail)
{
    resetObservationProvider();
    m_baselineProject.reset();
    m_baselineManifest.reset();
    m_baselineProvider.clear();
    m_baselineSlaveId = {};
    m_rows.clear();
    m_table->clear();
    m_table->setEnabled(false);
    m_reload->setEnabled(false);
    m_apply->setEnabled(false);
    m_stale = false;
    m_summary->setText(summary);
    m_feedback->setType(Utils::InfoLabelType::Warning);
    m_feedback->setText(detail);
}

void DeviceParametersPage::clearObservedPresentation()
{
    const Data::OfflineSlaveConfiguration *slave
        = m_baselineProject ? configuredSlave(*m_baselineProject, m_baselineSlaveId) : nullptr;
    for (const ParameterRow &row : std::as_const(m_rows)) {
        if (!row.item)
            continue;
        row.item->setText(2, observedText(row.definition));
        row.item->setToolTip(2, Tr::tr("No accepted real controller evidence is available."));
        row.item->setText(3, observedSourceText(row.definition));
        row.item->setToolTip(3, observedSourceText(row.definition));

        bool draftUnverified = false;
        if (slave && row.editor) {
            QString rejection;
            const std::optional<Data::EngineeringValue> draft
                = editorValue(row.definition, row.editor, &rejection);
            const Data::DeviceParameterValue *stored
                = configuredValue(slave->deviceParameters, row.definition.id);
            draftUnverified = !rejection.isEmpty() || bool(draft) != bool(stored)
                              || (draft && stored && *draft != stored->value);
        }
        if (draftUnverified) {
            row.item->setText(4, Tr::tr("Unverified"));
            row.item->setToolTip(
                4,
                Tr::tr("The visible editor differs from Project-owned intent. Real evidence is "
                       "never compared with an unapplied draft."));
        } else {
            row.item->setText(4, verificationStatusText(row.definition));
            row.item->setToolTip(4, verificationStatusToolTip(row.definition));
        }
    }
}

void DeviceParametersPage::resetObservationProvider()
{
    for (const QMetaObject::Connection &connection : std::as_const(m_observationConnections))
        disconnect(connection);
    m_observationConnections.clear();
    m_observationProvider.clear();
    ++m_observationGeneration;
    if (!m_observationGeneration)
        ++m_observationGeneration;
    m_observationRefreshPending = false;
    m_observationRefreshScheduled = false;
}

void DeviceParametersPage::rebindObservationProvider()
{
    resetObservationProvider();
    clearObservedPresentation();
    if (!m_controller || !m_baselineProject || !m_baselineManifest || m_stale)
        return;
    const Data::OfflineSlaveConfiguration *slave
        = configuredSlave(*m_baselineProject, m_baselineSlaveId);
    if (!slave)
        return;
    const Data::ControllerConnectionScope scope{m_baselineProject->id, slave->masterId};
    const ControllerConnectionSelection selection = m_controller->controllerConnectionSelection(
        scope);
    if (!selection.providerExplicitlySelected || !selection.providerId.isValid()
        || !selection.profileExplicitlySelected || selection.profileId.isNull()) {
        return;
    }
    Core::ControllerConnectionProvider *provider = m_controller->controllerConnectionProvider(
        scope);
    if (!provider || provider->id() != selection.providerId)
        return;

    m_observationProvider = provider;
    const quint64 generation = m_observationGeneration;
    const QPointer<Core::ControllerConnectionProvider> guardedProvider(provider);
    const auto changed = [this, guardedProvider, generation] {
        scheduleObservedRefresh(guardedProvider, generation);
    };
    m_observationConnections.append(connect(
        provider,
        &Core::ControllerConnectionProvider::axisParameterEvidenceBatchChanged,
        this,
        changed));
    m_observationConnections.append(connect(
        provider,
        &Core::ControllerConnectionProvider::connectionSnapshotChanged,
        this,
        changed));
    m_observationConnections.append(connect(
        provider,
        &Core::Provider::availabilityChanged,
        this,
        [changed](bool) { changed(); }));
    m_observationConnections.append(connect(
        provider,
        &QObject::destroyed,
        this,
        [this, generation] {
            if (generation == m_observationGeneration)
                rebindObservationProvider();
        }));
    refreshObservedPresentation();
}

void DeviceParametersPage::scheduleObservedRefresh(
    Core::ControllerConnectionProvider *expectedProvider, quint64 expectedGeneration)
{
    if (expectedProvider
        && (m_observationProvider != expectedProvider
            || m_observationGeneration != expectedGeneration)) {
        return;
    }
    clearObservedPresentation();
    if (m_observationRefreshInProgress) {
        m_observationRefreshPending = true;
        return;
    }
    if (m_observationRefreshScheduled)
        return;
    m_observationRefreshScheduled = true;
    const QPointer<Core::ControllerConnectionProvider> guardedProvider = m_observationProvider;
    const quint64 generation = m_observationGeneration;
    QMetaObject::invokeMethod(
        this,
        [this, guardedProvider, generation] {
            if (guardedProvider != m_observationProvider || generation != m_observationGeneration)
                return;
            m_observationRefreshScheduled = false;
            refreshObservedPresentation();
        },
        Qt::QueuedConnection);
}

void DeviceParametersPage::bindAdapterAuthoritySignals()
{
    for (const QMetaObject::Connection &connection : std::as_const(m_adapterAuthorityConnections))
        disconnect(connection);
    m_adapterAuthorityConnections.clear();
    if (!m_controller || !m_controller->providerRegistry())
        return;
    for (Core::Provider *candidate :
         m_controller->providerRegistry()->providers(Core::ProviderKind::DeviceAdapter)) {
        auto provider = qobject_cast<Core::DeviceAdapterProvider *>(candidate);
        if (!provider)
            continue;
        m_adapterAuthorityConnections.append(connect(
            provider,
            &Core::DeviceAdapterProvider::adapterManifestsChanged,
            this,
            &DeviceParametersPage::scheduleContextRefresh));
        m_adapterAuthorityConnections.append(connect(
            provider,
            &Core::Provider::availabilityChanged,
            this,
            [this](bool) { scheduleContextRefresh(); }));
        m_adapterAuthorityConnections.append(connect(
            provider,
            &QObject::destroyed,
            this,
            [this] { scheduleContextRefresh(); }));
    }
}

void DeviceParametersPage::scheduleContextRefresh()
{
    resetObservationProvider();
    clearObservedPresentation();
    if (m_contextRefreshScheduled)
        return;
    m_contextRefreshScheduled = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_contextRefreshScheduled = false;
            bindAdapterAuthoritySignals();
            setContext(m_context);
        },
        Qt::QueuedConnection);
}

void DeviceParametersPage::refreshObservedPresentation()
{
    if (m_observationRefreshInProgress) {
        m_observationRefreshPending = true;
        return;
    }
    m_observationRefreshInProgress = true;
    m_observationRefreshPending = false;
    clearObservedPresentation();
    if (m_observationProvider) {
        for (const ParameterRow &row : std::as_const(m_rows)) {
            if (!row.item
                || row.definition.observedSource.kind
                       != Data::DeviceParameterObservedSourceKind::CoeSdoUpload) {
                continue;
            }
            row.item->setText(2, Tr::tr("Unavailable"));
            row.item->setToolTip(
                2, Tr::tr("The selected real provider has no accepted evidence for this value."));
            if (row.item->text(4) != Tr::tr("Unverified")) {
                row.item->setText(4, Tr::tr("Unavailable"));
                row.item->setToolTip(
                    4,
                    Tr::tr("Project intent cannot be compared without accepted real evidence."));
            }
        }
    }

    do {
        if (!m_controller || !m_controller->projectService() || !m_baselineProject
            || !m_baselineManifest || !m_baselineProvider || !m_observationProvider || m_stale) {
            break;
        }
        const quint64 generation = m_observationGeneration;
        const QPointer<Core::ControllerConnectionProvider> provider = m_observationProvider;
        const std::optional<Data::ProjectSnapshot> project
            = m_controller->projectService()->project(m_context.projectId);
        if (!project || !project->valid || *project != *m_baselineProject)
            break;
        const Data::OfflineSlaveConfiguration *slave = configuredSlave(*project, m_baselineSlaveId);
        if (!slave || slave->position < 0
            || slave->position > int(std::numeric_limits<quint16>::max())) {
            break;
        }
        const Data::ControllerConnectionScope scope{project->id, slave->masterId};
        const ControllerConnectionSelection selection
            = m_controller->controllerConnectionSelection(scope);
        if (!selection.providerExplicitlySelected || !selection.providerId.isValid()
            || !selection.profileExplicitlySelected || selection.profileId.isNull()
            || !provider || provider->id() != selection.providerId
            || m_controller->controllerConnectionProvider(scope) != provider
            || !provider->isAvailable()) {
            break;
        }
        const bool supportsEvidence = provider->supportsAxisParameterEvidence();
        if (!provider || !supportsEvidence)
            break;

        const Data::ControllerConnectionSnapshot snapshot = provider->connectionSnapshot();
        if (!provider || snapshot.protocolVersion.major != 1 || snapshot.protocolVersion.minor < 16
            || !isRealEvidenceSnapshot(snapshot, scope, selection.profileId)) {
            break;
        }
        const Data::ControllerTopologySnapshot &topology = *snapshot.topology;
        const std::optional<Data::AxisParameterEvidenceBatch> batch
            = provider->axisParameterEvidenceBatch();
        if (!provider || !batch || !evidenceBatchMatchesTopology(*batch, topology))
            break;

        const quint16 position = quint16(slave->position);
        const auto topologyTarget = std::find_if(
            topology.slaves.cbegin(),
            topology.slaves.cend(),
            [position](const Data::ControllerTopologySlave &candidate) {
                return candidate.position == position;
            });
        const auto batchTarget = std::find_if(
            batch->targets.cbegin(),
            batch->targets.cend(),
            [position](const Data::AxisParameterEvidenceTargetResult &candidate) {
                return candidate.position == position;
            });
        if (topologyTarget == topology.slaves.cend() || batchTarget == batch->targets.cend()
            || std::count_if(
                   topology.slaves.cbegin(),
                   topology.slaves.cend(),
                   [position](const Data::ControllerTopologySlave &candidate) {
                       return candidate.position == position;
                   })
                   != 1
            || std::count_if(
                   batch->targets.cbegin(),
                   batch->targets.cend(),
                   [position](const Data::AxisParameterEvidenceTargetResult &candidate) {
                       return candidate.position == position;
                   })
                   != 1
            || !configuredSlaveMatchesEvidence(*slave, *topologyTarget, *batchTarget)) {
            break;
        }

        QString authorityRejection;
        const QList<ExactAdapterResolution> resolutions
            = exactAdapterResolutions(m_controller, *slave, &authorityRejection);
        if (!provider || !m_baselineProvider || resolutions.size() != 1
            || resolutions.constFirst().provider != m_baselineProvider
            || resolutions.constFirst().manifest != *m_baselineManifest) {
            break;
        }

        const Data::ControllerConnectionSnapshot postSnapshot = provider->connectionSnapshot();
        if (!provider)
            break;
        const std::optional<Data::AxisParameterEvidenceBatch> postBatch
            = provider->axisParameterEvidenceBatch();
        if (!provider)
            break;
        const std::optional<Data::ProjectSnapshot> postProject
            = m_controller->projectService()->project(m_context.projectId);
        const ControllerConnectionSelection postSelection
            = m_controller->controllerConnectionSelection(scope);
        Core::ControllerConnectionProvider *postProvider
            = m_controller->controllerConnectionProvider(scope);
        const bool postSupportsEvidence = provider->supportsAxisParameterEvidence();
        if (!provider)
            break;
        const bool postAvailable = provider->isAvailable();
        const bool postAdapterAvailable = m_baselineProvider && m_baselineProvider->isAvailable();
        if (generation != m_observationGeneration || provider != m_observationProvider
            || !postProject || *postProject != *project || postSelection != selection
            || postProvider != provider || !postAvailable || !postSupportsEvidence
            || postSnapshot != snapshot || postBatch != batch
            || !postAdapterAvailable
            || m_baselineManifest != resolutions.constFirst().manifest) {
            break;
        }
        const Data::OfflineSlaveConfiguration *postSlave = configuredSlave(
            *postProject, m_baselineSlaveId);
        if (!postSlave || *postSlave != *slave)
            break;

        struct Presentation
        {
            QString observed;
            QString observedToolTip;
            QString source;
            QString sourceToolTip;
            QString verification;
            QString verificationToolTip;
        };
        QList<Presentation> presentations;
        presentations.reserve(m_rows.size());
        const Data::AxisParameterEvidence *evidence
            = batchTarget->evidence ? &*batchTarget->evidence : nullptr;
        for (const ParameterRow &row : std::as_const(m_rows)) {
            Presentation presentation;
            presentation.observed = observedText(row.definition);
            presentation.observedToolTip
                = Tr::tr("No accepted observed value is available for this parameter.");
            presentation.source = observedSourceText(row.definition);
            presentation.sourceToolTip = presentation.source;
            presentation.verification = verificationStatusText(row.definition);
            presentation.verificationToolTip = verificationStatusToolTip(row.definition);

            const Data::DeviceParameterValue *stored
                = configuredValue(postSlave->deviceParameters, row.definition.id);
            const bool coeObserved
                = row.definition.observedSource.kind
                      == Data::DeviceParameterObservedSourceKind::CoeSdoUpload
                  && row.definition.observedSource.object;
            if (coeObserved
                && batchTarget->outcome
                       == Data::AxisParameterEvidenceTargetOutcome::ControllerError) {
                const QString typedError
                    = Tr::tr("AxisParameterEvidence request %1 failed: status %2, operation %3, "
                             "detail 0x%4.")
                          .arg(batchTarget->requestId)
                          .arg(*batchTarget->status)
                          .arg(*batchTarget->operationResult)
                          .arg(*batchTarget->detail, 16, 16, QLatin1Char('0'));
                presentation.observed = Tr::tr("Unavailable");
                presentation.observedToolTip = typedError;
                presentation.source = Tr::tr("Real controller typed error");
                presentation.sourceToolTip = typedError;
                presentation.verification = Tr::tr("Unavailable");
                presentation.verificationToolTip = typedError;
            } else if (
                coeObserved
                && batchTarget->outcome
                       == Data::AxisParameterEvidenceTargetOutcome::TimedOut) {
                const QString timeout
                    = Tr::tr("AxisParameterEvidence request %1 timed out; no observed value was "
                             "published.")
                          .arg(batchTarget->requestId);
                presentation.observed = Tr::tr("Unavailable");
                presentation.observedToolTip = timeout;
                presentation.source = Tr::tr("Real controller request timed out");
                presentation.sourceToolTip = timeout;
                presentation.verification = Tr::tr("Unavailable");
                presentation.verificationToolTip = timeout;
            }

            const Data::AxisParameterEvidenceRecord *record
                = evidence ? observedRecord(row.definition, *evidence) : nullptr;
            if (record) {
                const Core::DeviceParameterObservationResult observation
                    = Core::evaluateDeviceParameterObservation(
                        row.definition,
                        stored ? std::optional<Data::EngineeringValue>(stored->value) : std::nullopt,
                        *record);
                presentation.source = Tr::tr(
                                          "Real controller evidence %1 · capture %2 · sequence %3")
                                          .arg(objectAddress(
                                              *row.definition.observedSource.object))
                                          .arg(batch->topologyCaptureSequence)
                                          .arg(evidence->evidenceSequence);
                presentation.sourceToolTip
                    = Tr::tr("Read-only AxisParameterEvidence profile %1/%2. No SDO was requested "
                             "by this page.")
                          .arg(batch->profileId)
                          .arg(batch->profileVersion);
                if (observation.isAvailable()) {
                    presentation.observed = engineeringValueText(*observation.observedValue);
                    presentation.observedToolTip = observation.detail;
                    switch (observation.state) {
                    case Core::DeviceParameterObservationState::Match:
                        presentation.verification = Tr::tr("Match");
                        break;
                    case Core::DeviceParameterObservationState::Mismatch:
                        presentation.verification = Tr::tr("Mismatch");
                        break;
                    case Core::DeviceParameterObservationState::NotConfigured:
                        presentation.verification = Tr::tr("Not configured");
                        break;
                    case Core::DeviceParameterObservationState::Unavailable:
                        break;
                    }
                    presentation.verificationToolTip = observation.detail;
                } else {
                    QString recordState;
                    switch (record->state) {
                    case Data::AxisParameterEvidenceRecordState::Valid:
                        recordState = Tr::tr("VALID");
                        break;
                    case Data::AxisParameterEvidenceRecordState::SdoAbort:
                        recordState = Tr::tr("SDO_ABORT");
                        break;
                    case Data::AxisParameterEvidenceRecordState::SizeMismatch:
                        recordState = Tr::tr("SIZE_MISMATCH");
                        break;
                    case Data::AxisParameterEvidenceRecordState::ReadFailed:
                        recordState = Tr::tr("READ_FAILED");
                        break;
                    }
                    const QString typedFailure
                        = Tr::tr("%1 Record state %2, abort 0x%3, operation %4, detail 0x%5.")
                              .arg(observation.detail, recordState)
                              .arg(record->abortCode, 8, 16, QLatin1Char('0'))
                              .arg(record->operationResult)
                              .arg(record->detail, 16, 16, QLatin1Char('0'));
                    presentation.observed = Tr::tr("Unavailable");
                    presentation.observedToolTip = typedFailure;
                    presentation.sourceToolTip = typedFailure;
                    presentation.verification = Tr::tr("Unavailable");
                    presentation.verificationToolTip = typedFailure;
                }
            }

            QString draftRejection;
            const std::optional<Data::EngineeringValue> draft
                = editorValue(row.definition, row.editor, &draftRejection);
            const bool draftUnverified
                = !draftRejection.isEmpty() || bool(draft) != bool(stored)
                  || (draft && stored && *draft != stored->value);
            if (draftUnverified) {
                presentation.verification = Tr::tr("Unverified");
                presentation.verificationToolTip
                    = Tr::tr("The visible editor differs from Project-owned intent. Real evidence "
                             "is never compared with an unapplied draft.");
            }
            presentations.append(std::move(presentation));
        }

        if (generation != m_observationGeneration || provider != m_observationProvider
            || presentations.size() != m_rows.size()) {
            break;
        }
        for (qsizetype index = 0; index < m_rows.size(); ++index) {
            QTreeWidgetItem *item = m_rows.at(index).item;
            if (!item)
                continue;
            const Presentation &presentation = presentations.at(index);
            item->setText(2, presentation.observed);
            item->setToolTip(2, presentation.observedToolTip);
            item->setText(3, presentation.source);
            item->setToolTip(3, presentation.sourceToolTip);
            item->setText(4, presentation.verification);
            item->setToolTip(4, presentation.verificationToolTip);
        }
    } while (false);

    m_observationRefreshInProgress = false;
    if (m_observationRefreshPending) {
        m_observationRefreshPending = false;
        scheduleObservedRefresh();
    }
}

void DeviceParametersPage::setContext(const Core::PropertyPageContext &context)
{
    clearObservedPresentation();
    if (m_applyInProgress) {
        m_context = context;
        return;
    }
    const bool preserveDraft = !m_forceReload && hasEditorDraft();
    const auto rejectContext =
        [this, &context, preserveDraft](const QString &summary, const QString &detail) {
            if (preserveDraft && m_baselineProject) {
                markDraftStale(context, detail);
            } else {
                m_context = context;
                clearBaseline(summary, detail);
            }
        };
    if (context.nodeKind != Core::WorkbenchNodeKind::ConfiguredSlave || !m_controller
        || !m_controller->projectService() || !m_controller->providerRegistry()
        || !m_controller->deviceRepository()) {
        rejectContext(
            Tr::tr("Device parameters are available only for a configured Project slave."),
            Tr::tr("No editable configured-device parameter context is available."));
        return;
    }

    const std::optional<Data::ProjectSnapshot> project = m_controller->projectService()->project(
        context.projectId);
    const Data::OfflineSlaveConfiguration *slave = project && project->valid
                                                       ? configuredSlave(*project, context.nodeId)
                                                       : nullptr;
    if (!project || !project->valid || !slave) {
        rejectContext(
            Tr::tr("The selected configured device is no longer present in the Project."),
            Tr::tr("Refresh the Project selection before editing parameters."));
        return;
    }

    QString resolutionRejection;
    const QList<ExactAdapterResolution> resolutions
        = exactAdapterResolutions(m_controller, *slave, &resolutionRejection);
    if (resolutions.size() > 1) {
        rejectContext(
            Tr::tr("The selected Adapter definition is ambiguous."),
            Tr::tr("Multiple providers independently resolved the same exact Adapter token. "
                   "Editing is disabled even when their returned manifests are identical."));
        return;
    }
    if (resolutions.isEmpty()) {
        const QString detail = resolutionRejection.isEmpty()
                                   ? Tr::tr("No exact, independently authorized Qualified "
                                            "Adapter v4 parameter definition is available for "
                                            "this ESI, Adapter token, and process-data profile.")
                                   : Tr::tr("The exact Adapter parameter definition or resolved "
                                            "device model was rejected: %1")
                                         .arg(resolutionRejection);
        rejectContext(
            Tr::tr("This device does not currently support signed Device Parameters."), detail);
        return;
    }
    Core::DeviceAdapterProvider *selectedProvider = resolutions.constFirst().provider;
    const Data::DeviceAdapterManifest &selectedManifest = resolutions.constFirst().manifest;
    if (selectedManifest.parameterDefinitions.isEmpty()) {
        rejectContext(
            Tr::tr("The exact authorized Adapter defines no configurable device parameters."),
            Tr::tr("There are no signed definitions to edit. No defaults or device values are "
                   "inferred."));
        return;
    }

    for (const Data::DeviceParameterValue &value : slave->deviceParameters.values) {
        const auto definition = std::lower_bound(
            selectedManifest.parameterDefinitions.cbegin(),
            selectedManifest.parameterDefinitions.cend(),
            value.parameterId,
            [](const Data::DeviceParameterDefinition &candidate, const QString &id) {
                return candidate.id < id;
            });
        if (definition == selectedManifest.parameterDefinitions.cend()
            || definition->id != value.parameterId || definition->valueKind != value.value.kind) {
            rejectContext(
                Tr::tr("The stored configured parameters no longer match the exact Adapter."),
                Tr::tr("Editing is disabled so an unknown or differently typed stored value "
                       "cannot be discarded silently."));
            return;
        }
    }

    const std::optional<Data::ProjectSnapshot> postResolutionProject
        = m_controller->projectService()->project(context.projectId);
    if (!postResolutionProject || *postResolutionProject != *project) {
        rejectContext(
            Tr::tr("The Project changed while the signed Adapter was being resolved."),
            Tr::tr("The local draft was not applied. Reload the current Project before editing "
                   "device parameters."));
        return;
    }

    const bool sameBaseline = !m_stale && m_context == context && m_baselineProject
                              && *m_baselineProject == *project && m_baselineManifest
                              && *m_baselineManifest == selectedManifest
                              && m_baselineProvider == selectedProvider
                              && m_baselineSlaveId == slave->id;
    if (sameBaseline) {
        m_reload->setEnabled(true);
        bindAdapterAuthoritySignals();
        rebindObservationProvider();
        return;
    }
    if (preserveDraft) {
        markDraftStale(
            context,
            Tr::tr("The Project, ESI, Adapter, profile, module assignment, or provider authority "
                   "changed after this draft was entered."));
        return;
    }

    m_context = context;
    m_baselineProject = *project;
    m_baselineManifest = selectedManifest;
    m_baselineProvider = selectedProvider;
    m_baselineSlaveId = slave->id;
    m_stale = false;
    bindAdapterAuthoritySignals();
    m_summary->setText(
        Tr::tr("Edit Project-owned intent from exact authorized Adapter %1 %2. Signed defaults "
               "are reference-only and are never selected automatically. Observed values are "
               "read-only evidence; this page performs no scan, SDO upload, controller command, "
               "deployment, network, or hardware access.")
            .arg(selectedManifest.displayName, selectedManifest.version));
    rebuildRows();
    rebindObservationProvider();
}

void DeviceParametersPage::rebuildRows()
{
    m_rebuilding = true;
    m_rows.clear();
    m_table->clear();
    if (!m_baselineProject || !m_baselineManifest) {
        m_rebuilding = false;
        return;
    }
    const Data::OfflineSlaveConfiguration *slave
        = configuredSlave(*m_baselineProject, m_baselineSlaveId);
    if (!slave) {
        m_rebuilding = false;
        clearBaseline(
            Tr::tr("The configured device baseline is unavailable."),
            Tr::tr("Refresh the Project selection before editing parameters."));
        return;
    }
    for (const Data::DeviceParameterDefinition &definition :
         m_baselineManifest->parameterDefinitions) {
        const Data::DeviceParameterValue *stored
            = configuredValue(slave->deviceParameters, definition.id);
        auto item = new QTreeWidgetItem(m_table);
        const QString name = definition.required
                                 ? Tr::tr("%1 (required)").arg(definition.displayName)
                                 : Tr::tr("%1 (optional)").arg(definition.displayName);
        item->setText(0, name);
        item->setToolTip(0, parameterDescription(definition));
        item->setData(0, Qt::UserRole, definition.id);
        item->setText(2, observedText(definition));
        item->setToolTip(2, Tr::tr("No observed value is stored or inferred by this page."));
        item->setText(3, observedSourceText(definition));
        item->setToolTip(3, observedSourceText(definition));
        item->setText(4, verificationStatusText(definition));
        item->setToolTip(4, verificationStatusToolTip(definition));
        QWidget *editor = createEditor(definition, stored, m_table);
        editor->setObjectName("EtherCATDeviceParameterConfigured_" + definition.id);
        editor->setAccessibleName(Tr::tr("Configured value for %1").arg(definition.displayName));
        editor->setToolTip(parameterDescription(definition));
        m_table->setItemWidget(item, 1, editor);
        m_rows.append({definition, editor, item});
        if (auto combo = qobject_cast<QComboBox *>(editor)) {
            connect(
                combo,
                &QComboBox::currentIndexChanged,
                this,
                &DeviceParametersPage::updateDraftPresentation);
        } else if (auto lineEdit = qobject_cast<QLineEdit *>(editor)) {
            connect(
                lineEdit,
                &QLineEdit::textChanged,
                this,
                &DeviceParametersPage::updateDraftPresentation);
        }
    }
    m_table->setEnabled(true);
    m_reload->setEnabled(true);
    m_rebuilding = false;
    updateDraftPresentation();
}

DeviceParametersPage::CandidateConfiguration DeviceParametersPage::candidateConfiguration() const
{
    CandidateConfiguration result;
    if (!m_baselineManifest) {
        result.firstError = Tr::tr("No exact signed Adapter definition is available.");
        return result;
    }
    for (const ParameterRow &row : m_rows) {
        QString parseError;
        const std::optional<Data::EngineeringValue> value
            = editorValue(row.definition, row.editor, &parseError);
        if (!parseError.isEmpty()) {
            result.parameterErrors.insert(row.definition.id, parseError);
            if (result.firstError.isEmpty())
                result.firstError = Tr::tr("%1: %2").arg(row.definition.displayName, parseError);
            continue;
        }
        if (!value) {
            if (row.definition.required) {
                const QString error = Tr::tr("A configured value is required.");
                result.parameterErrors.insert(row.definition.id, error);
                if (result.firstError.isEmpty()) {
                    result.firstError = Tr::tr("%1: %2").arg(row.definition.displayName, error);
                }
            }
            continue;
        }
        const Core::EngineeringContractValidation constrained
            = Core::validateEngineeringValueAgainstConstraint(
                *value, row.definition.engineeringConstraint);
        if (!constrained.accepted()) {
            result.parameterErrors.insert(row.definition.id, constrained.detail);
            if (result.firstError.isEmpty()) {
                result.firstError
                    = Tr::tr("%1: %2").arg(row.definition.displayName, constrained.detail);
            }
            continue;
        }
        result.configuration.values.append({row.definition.id, *value});
    }
    if (result.firstError.isEmpty()) {
        const Data::OfflineSlaveConfiguration *slave
            = m_baselineProject ? configuredSlave(*m_baselineProject, m_baselineSlaveId) : nullptr;
        if (!slave) {
            result.firstError = Tr::tr("The configured device baseline is unavailable.");
            return result;
        }
        const Core::ConfiguredDeviceParameterValidation validation
            = Core::validateConfiguredDeviceParameters(
                *m_baselineManifest,
                slave->esiSha256,
                slave->adapterSelection,
                result.configuration);
        if (!validation.accepted()) {
            result.firstError = validation.detail;
            if (!validation.parameterId.isEmpty())
                result.parameterErrors.insert(validation.parameterId, validation.detail);
        }
    }
    return result;
}

bool DeviceParametersPage::hasEditorDraft() const
{
    if (!m_baselineProject || !m_baselineManifest || m_rows.isEmpty())
        return false;
    const Data::OfflineSlaveConfiguration *slave
        = configuredSlave(*m_baselineProject, m_baselineSlaveId);
    if (!slave)
        return true;
    for (const ParameterRow &row : m_rows) {
        QString parseError;
        const std::optional<Data::EngineeringValue> draft
            = editorValue(row.definition, row.editor, &parseError);
        if (!parseError.isEmpty())
            return true;
        const Data::DeviceParameterValue *stored
            = configuredValue(slave->deviceParameters, row.definition.id);
        if (draft && (!stored || stored->value != *draft))
            return true;
        if (!draft && stored)
            return true;
    }
    return false;
}

void DeviceParametersPage::markDraftStale(
    const Core::PropertyPageContext &context, const QString &reason)
{
    m_context = context;
    m_stale = true;
    resetObservationProvider();
    clearObservedPresentation();
    m_table->setEnabled(false);
    m_reload->setEnabled(true);
    m_apply->setEnabled(false);
    m_summary->setText(
        Tr::tr("The visible local device-parameter draft is stale and has not been applied."));
    m_feedback->setType(Utils::InfoLabelType::Error);
    m_feedback->setText(
        Tr::tr("%1 Discard the preserved draft and reload the current Project before editing or "
               "applying parameters.")
            .arg(reason));
}

void DeviceParametersPage::reloadFromProject()
{
    m_forceReload = true;
    setContext(m_context);
    m_forceReload = false;
}

void DeviceParametersPage::updateDraftPresentation()
{
    if (m_rebuilding || !m_baselineProject || !m_baselineManifest)
        return;
    if (m_stale) {
        m_apply->setEnabled(false);
        return;
    }
    const Data::OfflineSlaveConfiguration *slave
        = configuredSlave(*m_baselineProject, m_baselineSlaveId);
    if (!slave) {
        m_apply->setEnabled(false);
        return;
    }
    const CandidateConfiguration candidate = candidateConfiguration();
    for (const ParameterRow &row : m_rows) {
        if (!row.item)
            continue;
        const QString error = candidate.parameterErrors.value(row.definition.id);
        QString draftStatus;
        if (!error.isEmpty()) {
            draftStatus = Tr::tr("Local draft invalid: %1").arg(error);
        } else {
            const Data::DeviceParameterValue *draft
                = configuredValue(candidate.configuration, row.definition.id);
            const Data::DeviceParameterValue *stored
                = configuredValue(slave->deviceParameters, row.definition.id);
            if (draft && stored && *draft == *stored) {
                draftStatus = Tr::tr("The local editor matches the Project-owned intent.");
            } else if (draft || stored) {
                draftStatus = Tr::tr("The local editor contains unapplied Project intent.");
            } else {
                draftStatus = Tr::tr("No Project-owned intent is entered in this editor.");
            }
        }
        const QString editorToolTip = parameterDescription(row.definition) + '\n' + draftStatus;
        row.item->setToolTip(1, editorToolTip);
        if (row.editor)
            row.editor->setToolTip(editorToolTip);
    }
    if (!candidate.valid()) {
        m_feedback->setType(Utils::InfoLabelType::Error);
        m_feedback->setText(Tr::tr("Cannot apply: %1").arg(candidate.firstError));
        m_apply->setEnabled(false);
        scheduleObservedRefresh();
        return;
    }
    const bool changed = candidate.configuration != slave->deviceParameters;
    m_apply->setEnabled(changed);
    if (changed) {
        m_feedback->setType(Utils::InfoLabelType::Warning);
        m_feedback->setText(
            Tr::tr("Unapplied local intent. Apply rechecks the complete Project and exact signed "
                   "Adapter before storing it."));
    } else {
        m_feedback->setType(Utils::InfoLabelType::Information);
        m_feedback->setText(
            Tr::tr("Configured values match the Project. Observed evidence remains read-only and "
                   "is never substituted for configured intent."));
    }
    scheduleObservedRefresh();
}

void DeviceParametersPage::applyConfiguration()
{
    const CandidateConfiguration candidate = candidateConfiguration();
    if (m_stale) {
        markDraftStale(
            m_context,
            Tr::tr("The preserved local draft is already stale and cannot be revalidated."));
        return;
    }
    if (!candidate.valid()) {
        m_feedback->setType(Utils::InfoLabelType::Error);
        m_feedback->setText(Tr::tr("Apply rejected: %1").arg(candidate.firstError));
        m_apply->setEnabled(false);
        return;
    }
    if (!m_baselineProject || !m_baselineManifest || !m_baselineProvider || !m_controller
        || !m_controller->projectService() || !m_controller->providerRegistry()) {
        markDraftStale(m_context, Tr::tr("The signed parameter authority baseline is unavailable."));
        return;
    }

    const std::optional<Data::ProjectSnapshot> freshProject
        = m_controller->projectService()->project(m_context.projectId);
    if (!freshProject || !freshProject->valid || *freshProject != *m_baselineProject) {
        markDraftStale(m_context, Tr::tr("The Project changed after this page was loaded."));
        return;
    }
    const Data::OfflineSlaveConfiguration *freshSlave
        = configuredSlave(*freshProject, m_baselineSlaveId);
    if (!freshSlave) {
        markDraftStale(m_context, Tr::tr("The configured device changed or was removed."));
        return;
    }

    QString resolutionRejection;
    const QList<ExactAdapterResolution> resolutions
        = exactAdapterResolutions(m_controller, *freshSlave, &resolutionRejection);
    if (resolutions.size() != 1 || resolutions.constFirst().provider != m_baselineProvider
        || resolutions.constFirst().manifest != *m_baselineManifest) {
        QString reason;
        if (resolutions.size() > 1) {
            reason = Tr::tr("Multiple providers now resolve the same exact Adapter token.");
        } else if (!resolutionRejection.isEmpty()) {
            reason = Tr::tr("Exact Qualified device resolution failed: %1").arg(resolutionRejection);
        } else {
            reason = Tr::tr("The unique exact Adapter provider or manifest changed.");
        }
        markDraftStale(m_context, reason);
        return;
    }
    const Core::ConfiguredDeviceParameterValidation validation
        = Core::validateConfiguredDeviceParameters(
            resolutions.constFirst().manifest,
            freshSlave->esiSha256,
            freshSlave->adapterSelection,
            candidate.configuration);
    if (!validation.accepted()) {
        markDraftStale(
            m_context,
            Tr::tr("The exact signed Adapter validation changed or failed: %1")
                .arg(validation.detail));
        return;
    }

    // Provider calls above are allowed to emit synchronous signals. Re-read the complete Project
    // after all manifest and resolution work, then call the compare-and-set mutation immediately.
    const std::optional<Data::ProjectSnapshot> setterProject
        = m_controller->projectService()->project(m_context.projectId);
    const Data::OfflineSlaveConfiguration *setterSlave
        = setterProject ? configuredSlave(*setterProject, m_baselineSlaveId) : nullptr;
    if (!setterProject || !setterProject->valid || *setterProject != *m_baselineProject
        || !setterSlave) {
        markDraftStale(m_context, Tr::tr("The Project changed during Adapter verification."));
        return;
    }
    if (candidate.configuration == setterSlave->deviceParameters) {
        updateDraftPresentation();
        return;
    }

    m_applyInProgress = true;
    const Utils::Result<> result = m_controller->projectService()->setDeviceParameterConfiguration(
        setterProject->id,
        setterSlave->id,
        setterSlave->esiSha256,
        setterSlave->adapterSelection,
        candidate.configuration);
    m_applyInProgress = false;
    if (!result) {
        markDraftStale(
            m_context,
            Tr::tr("The Project rejected the compare-and-set mutation: %1").arg(result.error()));
        return;
    }

    m_forceReload = true;
    setContext(m_context);
    m_forceReload = false;
    if (m_baselineProject && m_baselineManifest && !m_stale) {
        m_feedback->setType(Utils::InfoLabelType::Ok);
        m_feedback->setText(
            Tr::tr("Configured device parameters were stored in the Project Undo/Redo history. "
                   "No device or controller value was read or written."));
        m_apply->setEnabled(false);
    }
}

} // namespace EtherCAT::Workbench::Internal
