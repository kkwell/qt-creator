// Copyright (C) 2026 Kvell

#pragma once

#include "devicedescription.h"
#include "ethercatdata_global.h"
#include "nodeid.h"

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace EtherCAT::Data {

enum class ScanOperation { Interfaces, Slaves, SelectedBranch };

enum class ScanState {
    Idle,
    Preparing,
    ScanningMaster,
    ScanningSlaves,
    BuildingSnapshot,
    Comparing,
    Completed,
    Cancelled,
    Failed,
};

enum class TopologyDifferenceKind {
    Added,
    Missing,
    PositionChanged,
    VendorMismatch,
    ProductMismatch,
    RevisionMismatch,
    SerialMismatch,
    AliasMismatch,
    DuplicateDevice,
    PdoConfiguration,
    DcConfiguration,
};

enum class DifferenceSeverity { Information, Warning, Blocking };

struct ETHERCATDATA_EXPORT ScanRequest
{
    NodeId projectId;
    NodeId masterId;
    ScanOperation operation = ScanOperation::Slaves;
    NodeId branchNodeId;

    friend bool operator==(const ScanRequest &, const ScanRequest &) = default;
};

struct ETHERCATDATA_EXPORT ScanProgress
{
    ScanState state = ScanState::Idle;
    int value = 0;
    int maximum = 0;
    int discoveredSlaves = 0;
    QString detail;

    friend bool operator==(const ScanProgress &, const ScanProgress &) = default;
};

struct ETHERCATDATA_EXPORT ScannedInterface
{
    QString id;
    QString name;
    QString description;
    bool usable = true;

    friend bool operator==(const ScannedInterface &, const ScannedInterface &) = default;
};

struct ETHERCATDATA_EXPORT ScannedSlave
{
    NodeId id;
    int position = -1;
    DeviceIdentity identity;
    quint32 serialNumber = 0;
    quint16 alias = 0;
    QString name;
    NodeId deviceDescriptionId;

    friend bool operator==(const ScannedSlave &, const ScannedSlave &) = default;
};

struct ETHERCATDATA_EXPORT ScanSnapshot
{
    NodeId id;
    NodeId projectId;
    NodeId masterId;
    QDateTime capturedAt;
    QList<ScannedInterface> interfaces;
    QList<ScannedSlave> slaves;
    QStringList warnings;
    bool complete = false;
    bool mock = false;

    friend bool operator==(const ScanSnapshot &, const ScanSnapshot &) = default;
};

struct ETHERCATDATA_EXPORT TopologyDifference
{
    TopologyDifferenceKind kind = TopologyDifferenceKind::Added;
    DifferenceSeverity severity = DifferenceSeverity::Information;
    NodeId offlineSlaveId;
    NodeId scannedSlaveId;
    int offlinePosition = -1;
    int scannedPosition = -1;
    QString summary;
    QString detail;

    friend bool operator==(const TopologyDifference &, const TopologyDifference &) = default;
};

struct ETHERCATDATA_EXPORT TopologyComparison
{
    NodeId projectId;
    NodeId masterId;
    QList<TopologyDifference> differences;
    bool exactMatch = false;
    bool acceptAllowed = false;

    friend bool operator==(const TopologyComparison &, const TopologyComparison &) = default;
};

struct ETHERCATDATA_EXPORT ScanResult
{
    ScanSnapshot snapshot;
    TopologyComparison comparison;

    friend bool operator==(const ScanResult &, const ScanResult &) = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::ScanOperation)
Q_DECLARE_METATYPE(EtherCAT::Data::ScanState)
Q_DECLARE_METATYPE(EtherCAT::Data::TopologyDifferenceKind)
Q_DECLARE_METATYPE(EtherCAT::Data::DifferenceSeverity)
Q_DECLARE_METATYPE(EtherCAT::Data::ScanRequest)
Q_DECLARE_METATYPE(EtherCAT::Data::ScanProgress)
Q_DECLARE_METATYPE(EtherCAT::Data::ScannedInterface)
Q_DECLARE_METATYPE(EtherCAT::Data::ScannedSlave)
Q_DECLARE_METATYPE(EtherCAT::Data::ScanSnapshot)
Q_DECLARE_METATYPE(EtherCAT::Data::TopologyDifference)
Q_DECLARE_METATYPE(EtherCAT::Data::TopologyComparison)
Q_DECLARE_METATYPE(EtherCAT::Data::ScanResult)
