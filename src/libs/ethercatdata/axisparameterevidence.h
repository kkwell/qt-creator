// Copyright (C) 2026 Kvell

#pragma once

#include "controllerconnection.h"
#include "ethercatdata_global.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMetaType>

#include <array>
#include <optional>

namespace EtherCAT::Data {

inline constexpr quint32 FixedAxisParameterEvidenceProfileId = 1;
inline constexpr quint16 FixedAxisParameterEvidenceProfileVersion = 1;
inline constexpr quint32 AxisParameterEvidenceRequiredFlags = 0x1f;

struct FixedAxisParameterEvidenceRecord
{
    quint16 index;
    quint8 subIndex;
    quint8 valueBytes;
};

inline constexpr std::array<FixedAxisParameterEvidenceRecord, 8>
    FixedAxisParameterEvidenceRecords{{
        {0x2000, 0x01, 2},
        {0x2000, 0x05, 2},
        {0x2000, 0x06, 2},
        {0x6091, 0x01, 4},
        {0x6091, 0x02, 4},
        {0x2006, 0x09, 2},
        {0x2006, 0x0a, 2},
        {0x607f, 0x00, 4},
    }};

inline QByteArray fixedAxisParameterEvidenceProfileSha256()
{
    static const QByteArray sha256 = QByteArray::fromHex(
        QByteArrayLiteral("7e73372de645920ef2da33454e1b7195476f8760a2a44f612803e67a21c7f77e"));
    return sha256;
}

inline bool isAxisParameterEvidenceResultTupleValid(
    qint32 status, qint32 operationResult, quint64 detail)
{
    switch (status) {
    case 0:
        return operationResult == 0 && detail == 0;
    case -6:
        return operationResult >= -4 && operationResult <= -1
               && detail == quint64(-operationResult);
    case -14:
        return operationResult == -5 && detail == 5;
    case -15:
        return operationResult >= -8 && operationResult <= -6
               && detail == quint64(-operationResult);
    case -16:
        return operationResult == -9 && detail == 9;
    default:
        return false;
    }
}

enum class AxisParameterEvidenceRecordState : quint8 {
    Valid = 1,
    SdoAbort = 2,
    SizeMismatch = 3,
    ReadFailed = 4,
};

enum class AxisParameterEvidenceEncoding : quint8 {
    None = 0,
    RawLittleEndian = 1,
};

struct ETHERCATDATA_EXPORT AxisParameterEvidenceRecord
{
    quint16 ordinal = 0;
    quint16 index = 0;
    quint8 subIndex = 0;
    AxisParameterEvidenceRecordState state = AxisParameterEvidenceRecordState::ReadFailed;
    quint8 valueBytes = 0;
    AxisParameterEvidenceEncoding encoding = AxisParameterEvidenceEncoding::None;
    quint32 abortCode = 0;
    qint32 operationResult = 0;
    QByteArray rawValue;
    quint64 detail = 0;

    bool isValid() const
    {
        if (ordinal >= FixedAxisParameterEvidenceRecords.size())
            return false;
        if (state == AxisParameterEvidenceRecordState::Valid) {
            return valueBytes == FixedAxisParameterEvidenceRecords.at(ordinal).valueBytes
                   && encoding == AxisParameterEvidenceEncoding::RawLittleEndian
                   && rawValue.size() == valueBytes && !abortCode && !operationResult && !detail;
        }
        if (state != AxisParameterEvidenceRecordState::SdoAbort
            && state != AxisParameterEvidenceRecordState::SizeMismatch
            && state != AxisParameterEvidenceRecordState::ReadFailed) {
            return false;
        }
        if (valueBytes || encoding != AxisParameterEvidenceEncoding::None || !rawValue.isEmpty()
            || operationResult >= 0) {
            return false;
        }
        if (state == AxisParameterEvidenceRecordState::SdoAbort)
            return abortCode != 0 && !detail;
        return !abortCode && detail != 0;
    }

    friend bool operator==(const AxisParameterEvidenceRecord &,
                           const AxisParameterEvidenceRecord &)
        = default;
};

struct ETHERCATDATA_EXPORT AxisParameterEvidence
{
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    quint64 sessionId = 0;
    quint64 bootId = 0;
    quint64 requestId = 0;
    quint64 responseSequence = 0;
    quint64 controllerTimestampNs = 0;
    quint64 topologyRequestId = 0;
    quint64 topologyResponseSequence = 0;
    quint32 topologyCaptureSequence = 0;
    quint64 topologyCompletedTimeNs = 0;
    QByteArray topologyPayloadSha256;
    quint32 evidenceSequence = 0;
    quint64 completedTimeNs = 0;
    quint16 position = 0;
    quint16 stationAddress = 0;
    quint32 vendorId = 0;
    quint32 productCode = 0;
    quint32 revision = 0;
    quint32 serial = 0;
    quint32 profileId = 0;
    quint16 profileVersion = 0;
    QByteArray profileSha256;
    quint32 flags = 0;
    quint64 detail = 0;
    QList<AxisParameterEvidenceRecord> records;
    QDateTime receivedAt;

    bool isValid() const
    {
        if (scope.projectId.isNull() || scope.masterId.isNull() || !sessionGeneration
            || !sessionId || !bootId || !requestId || !responseSequence
            || !topologyRequestId || !topologyResponseSequence || !topologyCaptureSequence
            || !topologyCompletedTimeNs || topologyPayloadSha256.size() != 32
            || topologyPayloadSha256 == QByteArray(32, '\0') || !evidenceSequence
            || completedTimeNs <= topologyCompletedTimeNs || !stationAddress || !vendorId
            || !productCode || profileId != FixedAxisParameterEvidenceProfileId
            || profileVersion != FixedAxisParameterEvidenceProfileVersion
            || profileSha256 != fixedAxisParameterEvidenceProfileSha256()
            || flags != AxisParameterEvidenceRequiredFlags || detail
            || records.size() != qsizetype(FixedAxisParameterEvidenceRecords.size())
            || !receivedAt.isValid()) {
            return false;
        }
        for (qsizetype i = 0; i < records.size(); ++i) {
            const AxisParameterEvidenceRecord &record = records.at(i);
            const FixedAxisParameterEvidenceRecord &expected
                = FixedAxisParameterEvidenceRecords.at(size_t(i));
            if (record.ordinal != i || record.index != expected.index
                || record.subIndex != expected.subIndex || !record.isValid()) {
                return false;
            }
        }
        return true;
    }

    friend bool operator==(const AxisParameterEvidence &, const AxisParameterEvidence &) = default;
};

enum class AxisParameterEvidenceTargetOutcome : quint8 {
    Evidence,
    ControllerError,
    TimedOut,
};

struct ETHERCATDATA_EXPORT AxisParameterEvidenceTargetResult
{
    quint16 position = 0;
    quint16 stationAddress = 0;
    quint32 vendorId = 0;
    quint32 productCode = 0;
    quint32 revision = 0;
    quint32 serial = 0;
    quint64 requestId = 0;
    AxisParameterEvidenceTargetOutcome outcome
        = AxisParameterEvidenceTargetOutcome::TimedOut;
    std::optional<qint32> status;
    std::optional<qint32> operationResult;
    std::optional<quint64> detail;
    std::optional<AxisParameterEvidence> evidence;

    bool isValid() const
    {
        if (!stationAddress || !vendorId || !productCode || !requestId)
            return false;
        if (outcome == AxisParameterEvidenceTargetOutcome::Evidence) {
            return status == 0 && operationResult == 0 && detail == 0 && evidence
                   && evidence->isValid() && evidence->requestId == requestId
                   && evidence->position == position
                   && evidence->stationAddress == stationAddress
                   && evidence->vendorId == vendorId && evidence->productCode == productCode
                   && evidence->revision == revision && evidence->serial == serial;
        }
        if (evidence)
            return false;
        if (outcome == AxisParameterEvidenceTargetOutcome::ControllerError) {
            return status && *status != 0 && operationResult && detail
                   && isAxisParameterEvidenceResultTupleValid(
                       *status, *operationResult, *detail);
        }
        return outcome == AxisParameterEvidenceTargetOutcome::TimedOut && !status
               && !operationResult && !detail;
    }

    friend bool operator==(
        const AxisParameterEvidenceTargetResult &, const AxisParameterEvidenceTargetResult &)
        = default;
};

struct ETHERCATDATA_EXPORT AxisParameterEvidenceBatch
{
    ControllerConnectionScope scope;
    quint64 sessionGeneration = 0;
    quint64 sessionId = 0;
    quint64 bootId = 0;
    quint64 topologyRequestId = 0;
    quint64 topologyResponseSequence = 0;
    quint32 topologyCaptureSequence = 0;
    quint64 topologyCompletedTimeNs = 0;
    QByteArray topologyPayloadSha256;
    quint32 profileId = 0;
    quint16 profileVersion = 0;
    QByteArray profileSha256;
    QList<AxisParameterEvidenceTargetResult> targets;
    QDateTime completedAt;

    bool isValid() const
    {
        if (scope.projectId.isNull() || scope.masterId.isNull() || !sessionGeneration
            || !sessionId || !bootId || !topologyRequestId || !topologyResponseSequence
            || !topologyCaptureSequence || !topologyCompletedTimeNs
            || topologyPayloadSha256.size() != 32
            || topologyPayloadSha256 == QByteArray(32, '\0')
            || profileId != FixedAxisParameterEvidenceProfileId
            || profileVersion != FixedAxisParameterEvidenceProfileVersion
            || profileSha256 != fixedAxisParameterEvidenceProfileSha256()
            || targets.size() > 64 || !completedAt.isValid()) {
            return false;
        }
        quint16 previousPosition = 0;
        quint64 previousRequestId = 0;
        quint32 previousEvidenceSequence = 0;
        bool first = true;
        for (const AxisParameterEvidenceTargetResult &target : targets) {
            if (!target.isValid() || (!first && target.position <= previousPosition)
                || target.requestId <= previousRequestId) {
                return false;
            }
            if (target.evidence) {
                const AxisParameterEvidence &evidence = *target.evidence;
                if (evidence.evidenceSequence <= previousEvidenceSequence
                    || evidence.scope != scope || evidence.sessionGeneration != sessionGeneration
                    || evidence.sessionId != sessionId || evidence.bootId != bootId
                    || evidence.topologyRequestId != topologyRequestId
                    || evidence.topologyResponseSequence != topologyResponseSequence
                    || evidence.topologyCaptureSequence != topologyCaptureSequence
                    || evidence.topologyCompletedTimeNs != topologyCompletedTimeNs
                    || evidence.topologyPayloadSha256 != topologyPayloadSha256
                    || evidence.profileId != profileId || evidence.profileVersion != profileVersion
                    || evidence.profileSha256 != profileSha256) {
                    return false;
                }
                previousEvidenceSequence = evidence.evidenceSequence;
            }
            previousPosition = target.position;
            previousRequestId = target.requestId;
            first = false;
        }
        return true;
    }

    friend bool operator==(
        const AxisParameterEvidenceBatch &, const AxisParameterEvidenceBatch &)
        = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::AxisParameterEvidenceRecordState)
Q_DECLARE_METATYPE(EtherCAT::Data::AxisParameterEvidenceEncoding)
Q_DECLARE_METATYPE(EtherCAT::Data::AxisParameterEvidenceRecord)
Q_DECLARE_METATYPE(EtherCAT::Data::AxisParameterEvidence)
Q_DECLARE_METATYPE(EtherCAT::Data::AxisParameterEvidenceTargetOutcome)
Q_DECLARE_METATYPE(EtherCAT::Data::AxisParameterEvidenceTargetResult)
Q_DECLARE_METATYPE(EtherCAT::Data::AxisParameterEvidenceBatch)
