// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatdata_global.h"
#include "nodeid.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace EtherCAT::Data {

enum class EtherCATDataType {
    Unknown,
    Boolean,
    Integer8,
    UnsignedInteger8,
    Integer16,
    UnsignedInteger16,
    Integer32,
    UnsignedInteger32,
    Integer64,
    UnsignedInteger64,
    Real32,
    Real64,
    VisibleString,
    OctetString,
};

enum class PdoDirection { Rx, Tx };
enum class SyncManagerDirection { Unknown, MasterToSlave, SlaveToMaster };
enum class ParameterAccess { ReadOnly, WriteOnly, ReadWrite };

struct ETHERCATDATA_EXPORT DeviceIdentity
{
    quint32 vendorId = 0;
    quint32 productCode = 0;
    quint32 revisionNumber = 0;

    friend bool operator==(const DeviceIdentity &, const DeviceIdentity &) = default;
};

struct ETHERCATDATA_EXPORT PdoEntryDescription
{
    quint16 index = 0;
    bool indexDependsOnSlot = false;
    quint8 subIndex = 0;
    QString name;
    int bitLength = 0;
    EtherCATDataType dataType = EtherCATDataType::Unknown;
    QString rawDataType;

    friend bool operator==(const PdoEntryDescription &, const PdoEntryDescription &) = default;
};

struct ETHERCATDATA_EXPORT PdoDescription
{
    quint16 index = 0;
    bool indexDependsOnSlot = false;
    QString name;
    PdoDirection direction = PdoDirection::Rx;
    int syncManager = -1;
    bool fixed = false;
    bool mandatory = false;
    QList<PdoEntryDescription> entries;

    friend bool operator==(const PdoDescription &, const PdoDescription &) = default;
};

struct ETHERCATDATA_EXPORT ModuleClassDescription
{
    QString identifier;
    QString name;

    friend bool operator==(const ModuleClassDescription &, const ModuleClassDescription &) = default;
};

struct ETHERCATDATA_EXPORT ModuleSlotConstraintDescription
{
    QString name;
    int minimumInstances = 0;
    int maximumInstances = 0;
    QList<ModuleClassDescription> allowedModuleClasses;

    friend bool operator==(
        const ModuleSlotConstraintDescription &, const ModuleSlotConstraintDescription &) = default;
};

struct ETHERCATDATA_EXPORT ModulePdoGroupDescription
{
    int index = -1;
    int alignment = 0;
    bool hasRxPdo = false;
    quint16 rxPdoIndex = 0;
    bool hasTxPdo = false;
    quint16 txPdoIndex = 0;

    friend bool operator==(
        const ModulePdoGroupDescription &, const ModulePdoGroupDescription &) = default;
};

struct ETHERCATDATA_EXPORT ModuleParameterEnumValueDescription
{
    QString name;
    QString value;

    friend bool operator==(
        const ModuleParameterEnumValueDescription &,
        const ModuleParameterEnumValueDescription &) = default;
};

struct ETHERCATDATA_EXPORT ModuleParameterDescription
{
    quint8 subIndex = 0;
    QString name;
    QString rawDataType;
    EtherCATDataType dataType = EtherCATDataType::Unknown;
    int bitLength = 0;
    int bitOffset = 0;
    ParameterAccess access = ParameterAccess::ReadOnly;
    bool setting = false;
    bool hasDefaultData = false;
    QByteArray defaultData;
    bool hasMinimumData = false;
    QByteArray minimumData;
    bool hasMaximumData = false;
    QByteArray maximumData;
    QList<ModuleParameterEnumValueDescription> enumValues;

    friend bool operator==(
        const ModuleParameterDescription &, const ModuleParameterDescription &) = default;
};

struct ETHERCATDATA_EXPORT ModuleParameterObjectDescription
{
    quint16 index = 0;
    bool indexDependsOnSlot = false;
    QString name;
    QString rawDataType;
    int bitLength = 0;
    ParameterAccess access = ParameterAccess::ReadOnly;
    QString category;
    QList<ModuleParameterDescription> parameters;

    friend bool operator==(
        const ModuleParameterObjectDescription &,
        const ModuleParameterObjectDescription &) = default;
};

struct ETHERCATDATA_EXPORT ModuleDescription
{
    quint32 moduleIdent = 0;
    QString typeName;
    QString name;
    QString moduleClass;
    int modulePdoGroupIndex = -1;
    QList<PdoDescription> rxPdos;
    QList<PdoDescription> txPdos;
    QList<ModuleParameterObjectDescription> parameterObjects;
    bool parameterConfigurationSupported = true;
    QStringList parameterWarnings;

    friend bool operator==(const ModuleDescription &, const ModuleDescription &) = default;
};

struct ETHERCATDATA_EXPORT ModuleCatalogDescription
{
    bool available = false;
    bool downloadModuleIdentList = false;
    int slotIndexIncrement = 0;
    int slotPdoIncrement = 0;
    QList<ModuleSlotConstraintDescription> slotConstraints;
    QList<ModulePdoGroupDescription> pdoGroups;
    QList<ModuleDescription> modules;

    friend bool operator==(
        const ModuleCatalogDescription &, const ModuleCatalogDescription &) = default;
};

struct ETHERCATDATA_EXPORT SyncManagerDescription
{
    int index = -1;
    QString name;
    SyncManagerDirection direction = SyncManagerDirection::Unknown;
    quint16 startAddress = 0;
    quint16 defaultSize = 0;
    quint8 controlByte = 0;
    bool enabled = false;

    friend bool operator==(const SyncManagerDescription &, const SyncManagerDescription &) = default;
};

struct ETHERCATDATA_EXPORT CoeCapabilities
{
    bool supported = false;
    bool sdoInfo = false;
    bool pdoAssign = false;
    bool pdoConfiguration = false;
    bool completeAccess = false;

    friend bool operator==(const CoeCapabilities &, const CoeCapabilities &) = default;
};

struct ETHERCATDATA_EXPORT StartupParameterDescription
{
    QString transition;
    quint16 index = 0;
    quint8 subIndex = 0;
    QByteArray data;
    QString comment;

    friend bool operator==(const StartupParameterDescription &, const StartupParameterDescription &)
        = default;
};

struct ETHERCATDATA_EXPORT DcModeDescription
{
    QString name;
    quint16 assignActivate = 0;
    qint64 cycleTimeSync0Ns = 0;
    qint64 shiftTimeSync0Ns = 0;
    qint64 cycleTimeSync1Ns = 0;
    qint64 shiftTimeSync1Ns = 0;

    friend bool operator==(const DcModeDescription &, const DcModeDescription &) = default;
};

struct ETHERCATDATA_EXPORT SynchronizationTypeCapabilities
{
    bool outputTypesDeclared = false;
    quint16 outputSupportedTypes = 0;
    bool inputTypesDeclared = false;
    quint16 inputSupportedTypes = 0;

    friend bool operator==(
        const SynchronizationTypeCapabilities &, const SynchronizationTypeCapabilities &) = default;
};

struct ETHERCATDATA_EXPORT DeviceSummary
{
    NodeId id;
    DeviceIdentity identity;
    QString name;
    QString typeName;
    QString group;
    bool supported = true;

    friend bool operator==(const DeviceSummary &, const DeviceSummary &) = default;
};

struct ETHERCATDATA_EXPORT DeviceDescription
{
    DeviceSummary summary;
    QList<SyncManagerDescription> syncManagers;
    QList<PdoDescription> rxPdos;
    QList<PdoDescription> txPdos;
    CoeCapabilities coe;
    QList<StartupParameterDescription> startupParameters;
    QList<DcModeDescription> dcModes;
    SynchronizationTypeCapabilities synchronizationTypes;
    ModuleCatalogDescription moduleCatalog;
    QString sourcePath;
    QByteArray sourceSha256;
    QDateTime importedAt;
    QStringList warnings;
    QStringList unsupportedFeatures;

    friend bool operator==(const DeviceDescription &, const DeviceDescription &) = default;
};

struct ETHERCATDATA_EXPORT DeviceFilter
{
    QString text;
    quint32 vendorId = 0;
    bool filterByVendor = false;
    QString group;
    bool supportedOnly = false;

    friend bool operator==(const DeviceFilter &, const DeviceFilter &) = default;
};

struct ETHERCATDATA_EXPORT DeviceImportError
{
    QString sourcePath;
    QString message;

    friend bool operator==(const DeviceImportError &, const DeviceImportError &) = default;
};

struct ETHERCATDATA_EXPORT DeviceImportResult
{
    int requestedFiles = 0;
    int importedDevices = 0;
    int updatedDevices = 0;
    int duplicateFiles = 0;
    int failedFiles = 0;
    bool canceled = false;
    QList<NodeId> affectedDeviceIds;
    QList<DeviceImportError> errors;

    friend bool operator==(const DeviceImportResult &, const DeviceImportResult &) = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::EtherCATDataType)
Q_DECLARE_METATYPE(EtherCAT::Data::PdoDirection)
Q_DECLARE_METATYPE(EtherCAT::Data::SyncManagerDirection)
Q_DECLARE_METATYPE(EtherCAT::Data::ParameterAccess)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceIdentity)
Q_DECLARE_METATYPE(EtherCAT::Data::PdoEntryDescription)
Q_DECLARE_METATYPE(EtherCAT::Data::PdoDescription)
Q_DECLARE_METATYPE(EtherCAT::Data::ModuleClassDescription)
Q_DECLARE_METATYPE(EtherCAT::Data::ModuleSlotConstraintDescription)
Q_DECLARE_METATYPE(EtherCAT::Data::ModulePdoGroupDescription)
Q_DECLARE_METATYPE(EtherCAT::Data::ModuleParameterEnumValueDescription)
Q_DECLARE_METATYPE(EtherCAT::Data::ModuleParameterDescription)
Q_DECLARE_METATYPE(EtherCAT::Data::ModuleParameterObjectDescription)
Q_DECLARE_METATYPE(EtherCAT::Data::ModuleDescription)
Q_DECLARE_METATYPE(EtherCAT::Data::ModuleCatalogDescription)
Q_DECLARE_METATYPE(EtherCAT::Data::SyncManagerDescription)
Q_DECLARE_METATYPE(EtherCAT::Data::CoeCapabilities)
Q_DECLARE_METATYPE(EtherCAT::Data::StartupParameterDescription)
Q_DECLARE_METATYPE(EtherCAT::Data::DcModeDescription)
Q_DECLARE_METATYPE(EtherCAT::Data::SynchronizationTypeCapabilities)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceSummary)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceDescription)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceFilter)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceImportError)
Q_DECLARE_METATYPE(EtherCAT::Data::DeviceImportResult)
