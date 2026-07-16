// Copyright (C) 2026 Kvell

#pragma once

#include "devicedescription.h"
#include "ethercatdata_global.h"
#include "nodeid.h"

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>

namespace EtherCAT::Data {

enum class ConfigurationIssueSeverity { Information, Warning, Error };

enum class ConfigurationIssueCode {
    DuplicateSyncManager,
    MissingSyncManager,
    DisabledSyncManager,
    SyncManagerDirectionMismatch,
    MandatoryPdoNotSelected,
    DuplicatePdoAssignment,
    UnsupportedPdoMapping,
    InvalidPdoIndex,
    EmptyPdo,
    InvalidPdoEntryBitLength,
    DataTypeBitLengthMismatch,
    UnknownDataType,
    DuplicatePdoEntry,
    InvalidProcessImageOffset,
    ProcessImageOverlap,
    SyncManagerSizeExceeded,
    InvalidStartupOrder,
    DuplicateStartupOrder,
    MissingStartupTransition,
    InvalidStartupIndex,
    InvalidStartupValueSize,
    MissingDcMode,
    DcSignalWithoutMode,
    DcSync1RequiresSync0,
    InvalidDcCycle,
    DcShiftOutOfRange,
};

struct ETHERCATDATA_EXPORT ConfigurationIssue
{
    ConfigurationIssueCode code = ConfigurationIssueCode::InvalidPdoIndex;
    ConfigurationIssueSeverity severity = ConfigurationIssueSeverity::Error;
    NodeId sourceId;
    QString field;
    QString message;

    friend bool operator==(const ConfigurationIssue &, const ConfigurationIssue &) = default;
};

struct ETHERCATDATA_EXPORT SyncManagerConfiguration
{
    NodeId id;
    int index = -1;
    QString name;
    SyncManagerDirection direction = SyncManagerDirection::Unknown;
    bool enabled = false;
    int sizeLimitBytes = 0;

    friend bool operator==(const SyncManagerConfiguration &, const SyncManagerConfiguration &)
        = default;
};

struct ETHERCATDATA_EXPORT PdoEntryConfiguration
{
    NodeId id;
    quint16 index = 0;
    quint8 subIndex = 0;
    QString name;
    int bitLength = 0;
    EtherCATDataType dataType = EtherCATDataType::Unknown;
    QString rawDataType;
    qint64 requestedBitOffset = -1;
    bool mappingSupported = true;
    bool padding = false;

    friend bool operator==(const PdoEntryConfiguration &, const PdoEntryConfiguration &) = default;
};

struct ETHERCATDATA_EXPORT PdoConfiguration
{
    NodeId id;
    quint16 index = 0;
    QString name;
    PdoDirection direction = PdoDirection::Rx;
    int syncManager = -1;
    bool selected = false;
    bool fixed = false;
    bool mandatory = false;
    bool defaultSelected = false;
    bool mappingSupported = true;
    QString predefinedGroup;
    QList<PdoEntryConfiguration> entries;

    friend bool operator==(const PdoConfiguration &, const PdoConfiguration &) = default;
};

struct ETHERCATDATA_EXPORT ProcessDataConfiguration
{
    QList<SyncManagerConfiguration> syncManagers;
    QList<PdoConfiguration> pdos;

    friend bool operator==(const ProcessDataConfiguration &, const ProcessDataConfiguration &)
        = default;
};

struct ETHERCATDATA_EXPORT ProcessImageEntry
{
    NodeId pdoId;
    NodeId entryId;
    NodeId syncManagerId;
    quint16 pdoIndex = 0;
    quint16 index = 0;
    quint8 subIndex = 0;
    QString name;
    PdoDirection direction = PdoDirection::Rx;
    int syncManager = -1;
    qint64 bitOffset = 0;
    int bitLength = 0;
    qint64 byteOffset = 0;
    int bitOffsetInByte = 0;
    EtherCATDataType dataType = EtherCATDataType::Unknown;

    friend bool operator==(const ProcessImageEntry &, const ProcessImageEntry &) = default;
};

struct ETHERCATDATA_EXPORT ProcessImageDirection
{
    PdoDirection direction = PdoDirection::Rx;
    qint64 bitSize = 0;
    qint64 byteSize = 0;
    QList<ProcessImageEntry> entries;

    friend bool operator==(const ProcessImageDirection &, const ProcessImageDirection &) = default;
};

struct ETHERCATDATA_EXPORT ProcessImagePreview
{
    ProcessImageDirection inputs{PdoDirection::Tx, 0, 0, {}};
    ProcessImageDirection outputs{PdoDirection::Rx, 0, 0, {}};

    friend bool operator==(const ProcessImagePreview &, const ProcessImagePreview &) = default;
};

struct ETHERCATDATA_EXPORT ConfigurationValidation
{
    QList<ConfigurationIssue> issues;
    ProcessImagePreview processImage;

    bool hasErrors() const;

    friend bool operator==(const ConfigurationValidation &, const ConfigurationValidation &)
        = default;
};

struct ETHERCATDATA_EXPORT StartupParameterConfiguration
{
    NodeId id;
    bool enabled = true;
    int order = 0;
    QString transition;
    quint16 index = 0;
    quint8 subIndex = 0;
    EtherCATDataType dataType = EtherCATDataType::Unknown;
    QString rawDataType;
    QByteArray rawValue;
    QString comment;

    friend bool operator==(
        const StartupParameterConfiguration &, const StartupParameterConfiguration &) = default;
};

struct ETHERCATDATA_EXPORT StartupConfiguration
{
    QList<StartupParameterConfiguration> parameters;

    friend bool operator==(const StartupConfiguration &, const StartupConfiguration &) = default;
};

struct ETHERCATDATA_EXPORT DcSignalConfiguration
{
    bool enabled = false;
    qint64 cycleTimeNs = 0;
    qint64 shiftTimeNs = 0;

    friend bool operator==(const DcSignalConfiguration &, const DcSignalConfiguration &) = default;
};

struct ETHERCATDATA_EXPORT DcConfiguration
{
    bool enabled = false;
    QString modeName;
    quint16 assignActivate = 0;
    DcSignalConfiguration sync0;
    DcSignalConfiguration sync1;
    bool potentialReferenceClock = false;

    friend bool operator==(const DcConfiguration &, const DcConfiguration &) = default;
};

ETHERCATDATA_EXPORT ConfigurationValidation
validateProcessDataConfiguration(const ProcessDataConfiguration &configuration);
ETHERCATDATA_EXPORT QList<ConfigurationIssue> validateStartupConfiguration(
    const StartupConfiguration &configuration);
ETHERCATDATA_EXPORT QList<ConfigurationIssue> validateDcConfiguration(
    const DcConfiguration &configuration);

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::ConfigurationIssueSeverity)
Q_DECLARE_METATYPE(EtherCAT::Data::ConfigurationIssueCode)
Q_DECLARE_METATYPE(EtherCAT::Data::ConfigurationIssue)
Q_DECLARE_METATYPE(EtherCAT::Data::SyncManagerConfiguration)
Q_DECLARE_METATYPE(EtherCAT::Data::PdoEntryConfiguration)
Q_DECLARE_METATYPE(EtherCAT::Data::PdoConfiguration)
Q_DECLARE_METATYPE(EtherCAT::Data::ProcessDataConfiguration)
Q_DECLARE_METATYPE(EtherCAT::Data::ProcessImageEntry)
Q_DECLARE_METATYPE(EtherCAT::Data::ProcessImageDirection)
Q_DECLARE_METATYPE(EtherCAT::Data::ProcessImagePreview)
Q_DECLARE_METATYPE(EtherCAT::Data::ConfigurationValidation)
Q_DECLARE_METATYPE(EtherCAT::Data::StartupParameterConfiguration)
Q_DECLARE_METATYPE(EtherCAT::Data::StartupConfiguration)
Q_DECLARE_METATYPE(EtherCAT::Data::DcSignalConfiguration)
Q_DECLARE_METATYPE(EtherCAT::Data::DcConfiguration)
