// Copyright (C) 2026 Kvell

#pragma once

#include "devicedescription.h"
#include "engineeringvalue.h"
#include "ethercatdata_global.h"
#include "nodeid.h"
#include "offlineconfiguration.h"
#include "semanticids.h"

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace EtherCAT::Data {

enum class DeviceAdapterQualification {
    Unqualified,
    Candidate,
    Qualified,
    MockOnly,
    Revoked,
};

enum class DeviceAdapterContractVersion {
    Unknown,
    V1,
    V2,
    V3,
};

constexpr bool isValidDeviceAdapterContractVersion(DeviceAdapterContractVersion version)
{
    return version == DeviceAdapterContractVersion::V1
           || version == DeviceAdapterContractVersion::V2
           || version == DeviceAdapterContractVersion::V3;
}

struct ETHERCATDATA_EXPORT DeviceAdapterMatch
{
    quint32 vendorId = 0;
    quint32 productCode = 0;
    quint32 minimumRevision = 0;
    quint32 maximumRevision = 0;
    QByteArray exactEsiSha256;

    friend bool operator==(const DeviceAdapterMatch &, const DeviceAdapterMatch &) = default;
};

enum class SemanticSignalDirection { Input, Output, Bidirectional };
enum class SemanticSignalAccess { ReadOnly, WriteOnly, ReadWrite };
enum class SemanticSignalExposure { Public, ActionOnly, Internal };
enum class DeviceByteOrder { LittleEndian, BigEndian };
enum class DeviceSignalBindingKind { ProcessDataObject, ObjectDictionary };

struct ETHERCATDATA_EXPORT DeviceSignalBinding
{
    DeviceSignalBindingKind kind = DeviceSignalBindingKind::ProcessDataObject;
    PdoDirection pdoDirection = PdoDirection::Tx;
    quint16 pdoIndex = 0;
    quint16 objectIndex = 0;
    quint8 objectSubIndex = 0;
    EtherCATDataType physicalType = EtherCATDataType::Unknown;
    int bitWidth = 0;
    DeviceByteOrder byteOrder = DeviceByteOrder::LittleEndian;
    bool slotRelative = false;

    friend bool operator==(const DeviceSignalBinding &, const DeviceSignalBinding &) = default;
};

struct ETHERCATDATA_EXPORT SemanticEnumValue
{
    qint64 value = 0;
    QString name;
    QString displayName;

    friend bool operator==(const SemanticEnumValue &, const SemanticEnumValue &) = default;
};

struct ETHERCATDATA_EXPORT SemanticValueMetadata
{
    QString unit;
    double scale = 1.0;
    double offset = 0.0;
    bool hasMinimum = false;
    double minimum = 0.0;
    bool hasMaximum = false;
    double maximum = 0.0;
    bool hasStep = false;
    double step = 0.0;
    QList<SemanticEnumValue> enumValues;

    friend bool operator==(const SemanticValueMetadata &, const SemanticValueMetadata &) = default;
};

enum class ManualControlTimeoutAction {
    RejectFurtherWrites,
    HoldLastValue,
    RestoreSafeValue,
    ControlledStop,
};

struct ETHERCATDATA_EXPORT ManualControlPolicy
{
    QString policyId;
    bool allowed = false;
    bool requiresExclusiveControl = true;
    bool holdToRun = false;
    quint32 commandTimeoutMs = 0;
    ManualControlTimeoutAction timeoutAction = ManualControlTimeoutAction::RejectFurtherWrites;

    friend bool operator==(const ManualControlPolicy &, const ManualControlPolicy &) = default;
};

struct ETHERCATDATA_EXPORT SemanticSignalDefinition
{
    SemanticSignalId id;
    QString displayName;
    QString description;
    QList<DeviceCapabilityId> capabilities;
    SemanticSignalDirection direction = SemanticSignalDirection::Input;
    SemanticSignalAccess access = SemanticSignalAccess::ReadOnly;
    SemanticSignalExposure exposure = SemanticSignalExposure::Public;
    bool requiredForComplete = true;
    QList<DeviceSignalBinding> bindings;
    SemanticValueMetadata valueMetadata;
    std::optional<EngineeringTransform> engineeringTransform;
    std::optional<EngineeringValue> engineeringSafeValue;
    bool hasSafeValue = false;
    QVariant safeValue;
    ManualControlPolicy manualControl;

    friend bool operator==(const SemanticSignalDefinition &, const SemanticSignalDefinition &)
        = default;
};

struct ETHERCATDATA_EXPORT ProcessDataProfile
{
    QString id;
    QString signedPdoProfileId;
    QString signedDcProfileId;
    QList<quint16> rxPdoIndices;
    QList<quint16> txPdoIndices;
    QList<SemanticSignalId> requiredSignals;

    friend bool operator==(const ProcessDataProfile &, const ProcessDataProfile &) = default;
};

struct ETHERCATDATA_EXPORT DeviceModuleAssignment
{
    int slot = -1;
    quint32 moduleIdent = 0;
    quint16 objectIndexOffset = 0;
    quint16 pdoIndexOffset = 0;

    friend bool operator==(const DeviceModuleAssignment &, const DeviceModuleAssignment &) = default;
};

struct ETHERCATDATA_EXPORT DeviceModuleProfile
{
    QString id;
    quint32 moduleIdent = 0;
    QString typeName;
    QString moduleClass;
    QList<DeviceCapabilityId> capabilities;
    QList<SemanticSignalDefinition> slotRelativeSignals;

    friend bool operator==(const DeviceModuleProfile &, const DeviceModuleProfile &) = default;
};

struct ETHERCATDATA_EXPORT DeviceControlActionParameter
{
    QString id;
    QString displayName;
    QString description;
    EtherCATDataType dataType = EtherCATDataType::Unknown;
    SemanticValueMetadata valueMetadata;
    std::optional<EngineeringConstraint> engineeringConstraint;
    bool required = true;
    bool hasDefaultValue = false;
    QVariant defaultValue;
    std::optional<EngineeringValue> engineeringDefaultValue;

    friend bool operator==(
        const DeviceControlActionParameter &, const DeviceControlActionParameter &) = default;
};

enum class DeviceControlValueSource { Invalid, Literal, Parameter };

struct ETHERCATDATA_EXPORT DeviceControlValue
{
    DeviceControlValueSource source = DeviceControlValueSource::Invalid;
    QVariant literalValue;
    std::optional<EngineeringValue> engineeringLiteralValue;
    QString parameterId;

    friend bool operator==(const DeviceControlValue &, const DeviceControlValue &) = default;
};

struct ETHERCATDATA_EXPORT DeviceControlGroupAssignment
{
    SemanticSignalId signalId;
    DeviceControlValue value;

    friend bool operator==(
        const DeviceControlGroupAssignment &, const DeviceControlGroupAssignment &) = default;
};

enum class DeviceControlGroupRecovery {
    Invalid,
    ReturnTask,
    HoldSafe,
};

struct ETHERCATDATA_EXPORT DeviceControlConsistencyGroup
{
    QString id;
    QList<SemanticSignalId> members;
    DeviceControlGroupRecovery recovery = DeviceControlGroupRecovery::Invalid;
    quint32 maximumTtlCycles = 0;

    friend bool operator==(
        const DeviceControlConsistencyGroup &, const DeviceControlConsistencyGroup &) = default;
};

enum class DeviceControlFailureDisposition {
    Invalid,
    ReturnTask,
    HoldSafe,
    HoldOperationalFault,
};

enum class DeviceControlActionQualification {
    Unqualified,
    Qualified,
};

enum class DeviceControlStepKind {
    WriteSignal,
    WriteGroup,
    WaitMaskedEquals,
    WaitAbsoluteAtMost,
    WaitCycles,
    Delay,
};

struct ETHERCATDATA_EXPORT DeviceControlStep
{
    DeviceControlStepKind kind = DeviceControlStepKind::WriteSignal;
    SemanticSignalId signalId;
    DeviceControlValue value;
    DeviceControlValue mask;
    QString consistencyGroupId;
    QList<DeviceControlGroupAssignment> assignments;
    quint32 timeoutMs = 0;
    quint32 timeoutCycles = 0;

    friend bool operator==(const DeviceControlStep &, const DeviceControlStep &) = default;
};

struct ETHERCATDATA_EXPORT DeviceControlAction
{
    // This is deterministic binder/compiler input, not a Provider-side network command sequence.
    SemanticActionId id;
    QString displayName;
    QString description;
    bool enabled = false;
    DeviceControlActionQualification signedQualification
        = DeviceControlActionQualification::Unqualified;
    QString disabledReason;
    bool requiresExclusiveControl = true;
    bool requiresDc = true;
    bool holdToRun = true;
    quint32 commandTtlMs = 0;
    QStringList runtimeConditions;
    ManualControlTimeoutAction failureAction = ManualControlTimeoutAction::ControlledStop;
    ManualControlTimeoutAction timeoutAction = ManualControlTimeoutAction::ControlledStop;
    QList<SemanticActionId> allowedReleaseActionIds;
    QList<SemanticActionId> allowedTimeoutActionIds;
    QList<SemanticActionId> allowedFailureActionIds;
    QList<SemanticSignalId> requiredSignals;
    QList<SemanticSignalId> optionalSignals;
    QByteArray expectedSignedDefinitionSha256;
    QStringList signedPdoProfileIds;
    QList<DeviceControlConsistencyGroup> consistencyGroups;
    DeviceControlFailureDisposition failureDisposition = DeviceControlFailureDisposition::Invalid;
    QList<DeviceControlActionParameter> parameters;
    QList<DeviceControlStep> steps;

    friend bool operator==(const DeviceControlAction &, const DeviceControlAction &) = default;
};

struct ETHERCATDATA_EXPORT DeviceAdapterProvenance
{
    QString sourceId;
    QString sourceVersion;
    QString sourceLocation;
    QByteArray sourceSha256;

    friend bool operator==(const DeviceAdapterProvenance &, const DeviceAdapterProvenance &)
        = default;
};

struct ETHERCATDATA_EXPORT DeviceAdapterControllerTarget
{
    QString adapterId;
    QString adapterVersion;
    QByteArray adapterSha256;
    QByteArray esiSha256;

    friend bool operator==(
        const DeviceAdapterControllerTarget &, const DeviceAdapterControllerTarget &) = default;
};

struct ETHERCATDATA_EXPORT DeviceAdapterManifest
{
    DeviceAdapterContractVersion contractVersion = DeviceAdapterContractVersion::Unknown;
    DeviceAdapterId id;
    QString version;
    QString displayName;
    QString description;
    DeviceAdapterQualification qualification = DeviceAdapterQualification::Unqualified;
    int matchPriority = 0;
    DeviceAdapterMatch match;
    QList<DeviceCapabilityId> capabilities;
    QList<SemanticSignalDefinition> semanticSignals;
    QList<ProcessDataProfile> processDataProfiles;
    QList<DeviceModuleProfile> moduleProfiles;
    QList<DeviceControlAction> controlActions;
    DeviceAdapterProvenance provenance;
    DeviceAdapterControllerTarget controllerAdapterTarget;
    QByteArray contentSha256;
    QByteArray evidenceSha256;
    bool signatureVerified = false;
    bool realHardwareAllowed = false;

    friend bool operator==(const DeviceAdapterManifest &, const DeviceAdapterManifest &) = default;
};

struct ETHERCATDATA_EXPORT DeviceAdapterResolutionRequest
{
    NodeId slaveId;
    DeviceDescription device;
    ProcessImagePreview processImage;
    DeviceAdapterId expectedAdapterId;
    QString expectedAdapterVersion;
    QByteArray expectedAdapterContentSha256;
    QString processDataProfileId;
    QList<DeviceModuleAssignment> moduleAssignments;
    bool allowCandidate = false;
    bool allowMock = false;
    bool requireRealHardwareQualification = false;

    bool hasExpectedAdapterSelection() const
    {
        return !expectedAdapterId.value.isEmpty() || !expectedAdapterVersion.isEmpty()
               || !expectedAdapterContentSha256.isEmpty();
    }

    bool hasValidExpectedAdapterSelection() const
    {
        if (!hasExpectedAdapterSelection())
            return true;
        return !expectedAdapterId.value.isEmpty()
               && expectedAdapterId.value == expectedAdapterId.value.trimmed()
               && !expectedAdapterVersion.isEmpty()
               && expectedAdapterVersion == expectedAdapterVersion.trimmed()
               && expectedAdapterContentSha256.size() == 32;
    }

    friend bool operator==(
        const DeviceAdapterResolutionRequest &, const DeviceAdapterResolutionRequest &) = default;
};

struct ETHERCATDATA_EXPORT BoundSemanticSignal
{
    SemanticSignalDefinition definition;
    DeviceSignalBinding binding;
    NodeId processImageEntryId;
    qint64 processImageBitOffset = -1;
    int processImageBitLength = 0;
    int slot = -1;
    quint32 moduleIdent = 0;

    friend bool operator==(const BoundSemanticSignal &, const BoundSemanticSignal &) = default;
};

struct ETHERCATDATA_EXPORT ResolvedDeviceModel
{
    NodeId slaveId;
    DeviceIdentity identity;
    QByteArray esiSha256;
    DeviceAdapterId adapterId;
    QString adapterVersion;
    QByteArray adapterContentSha256;
    DeviceAdapterQualification qualification = DeviceAdapterQualification::Unqualified;
    QList<DeviceCapabilityId> capabilities;
    QString processDataProfileId;
    QList<DeviceModuleAssignment> moduleAssignments;
    QList<BoundSemanticSignal> boundSignals;
    QStringList warnings;
    bool complete = false;

    friend bool operator==(const ResolvedDeviceModel &, const ResolvedDeviceModel &) = default;
};

struct ETHERCATDATA_EXPORT DeviceAdapterResolutionResult
{
    bool resolved = false;
    ResolvedDeviceModel model;
    QString error;

    friend bool operator==(
        const DeviceAdapterResolutionResult &, const DeviceAdapterResolutionResult &) = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterQualification)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterContractVersion)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterMatch)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticSignalDirection)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticSignalAccess)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticSignalExposure)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceByteOrder)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceSignalBindingKind)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceSignalBinding)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticEnumValue)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticValueMetadata)
Q_DECLARE_METATYPE(EtherCAT::Data::ManualControlTimeoutAction)
Q_DECLARE_METATYPE(EtherCAT::Data::ManualControlPolicy)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticSignalDefinition)
Q_DECLARE_METATYPE(EtherCAT::Data::ProcessDataProfile)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceModuleAssignment)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceModuleProfile)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceControlActionParameter)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceControlValueSource)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceControlValue)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceControlGroupAssignment)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceControlGroupRecovery)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceControlConsistencyGroup)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceControlFailureDisposition)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceControlActionQualification)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceControlStepKind)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceControlStep)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceControlAction)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterProvenance)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterControllerTarget)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterManifest)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterResolutionRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::BoundSemanticSignal)
Q_DECLARE_METATYPE(EtherCAT::Data::ResolvedDeviceModel)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterResolutionResult)
