// Copyright (C) 2026 Embed Labs

#include "runtimepackagecompilercompilerecoverycodec.h"

#include <ethercatcore/runtimepackagecompilercodec.h>

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <bit>
#include <limits>
#include <type_traits>

namespace EtherCAT::ProjectCompiler {

namespace {

constexpr qsizetype maximumRecoveryBytes = 96 * 1024 * 1024;
constexpr qsizetype maximumPayloadBytes = 64 * 1024 * 1024;
constexpr qsizetype maximumProjectBytes = 16 * 1024 * 1024;
constexpr qsizetype maximumArtifactBytes = 8 * 1024 * 1024;
constexpr qsizetype maximumTextBytes = 256 * 1024;
constexpr quint32 maximumCollectionItems = 4096;
constexpr char payloadMagic[] = {'E', 'L', 'P', 'C', 'R', 'Q', '0', '1'};
constexpr quint32 minimumPayloadVersion = 1;
constexpr quint32 currentPayloadVersion = 3;

QByteArray sha256(QByteArrayView bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

class BinaryWriter
{
public:
    void raw(QByteArrayView value)
    {
        if (!m_valid || value.size() > maximumPayloadBytes - m_bytes.size()) {
            m_valid = false;
            return;
        }
        m_bytes.append(value);
    }

    void u8(quint8 value) { raw(QByteArrayView(reinterpret_cast<const char *>(&value), 1)); }

    void u16(quint16 value)
    {
        const char bytes[]{char(value >> 8), char(value)};
        raw(QByteArrayView(bytes, qsizetype(sizeof(bytes))));
    }

    void u32(quint32 value)
    {
        const char bytes[]{char(value >> 24), char(value >> 16), char(value >> 8), char(value)};
        raw(QByteArrayView(bytes, qsizetype(sizeof(bytes))));
    }

    void u64(quint64 value)
    {
        const char bytes[]{
            char(value >> 56),
            char(value >> 48),
            char(value >> 40),
            char(value >> 32),
            char(value >> 24),
            char(value >> 16),
            char(value >> 8),
            char(value),
        };
        raw(QByteArrayView(bytes, qsizetype(sizeof(bytes))));
    }

    void i32(qint32 value) { u32(std::bit_cast<quint32>(value)); }
    void i64(qint64 value) { u64(std::bit_cast<quint64>(value)); }

    void boolean(bool value) { u8(value ? 1 : 0); }

    void bytes(QByteArrayView value, qsizetype maximum = maximumArtifactBytes)
    {
        if (!m_valid || value.size() < 0 || value.size() > maximum
            || quint64(value.size()) > std::numeric_limits<quint32>::max()) {
            m_valid = false;
            return;
        }
        u32(quint32(value.size()));
        raw(value);
    }

    void string(const QString &value)
    {
        const QByteArray utf8 = value.toUtf8();
        if (QString::fromUtf8(utf8) != value) {
            m_valid = false;
            return;
        }
        bytes(utf8, maximumTextBytes);
    }

    void count(qsizetype value)
    {
        if (value < 0 || quint64(value) > maximumCollectionItems) {
            m_valid = false;
            return;
        }
        u32(quint32(value));
    }

    template<typename Enum>
    void enumeration(Enum value, quint32 maximum)
    {
        static_assert(std::is_enum_v<Enum>);
        using Underlying = std::underlying_type_t<Enum>;
        const Underlying underlying = static_cast<Underlying>(value);
        if constexpr (std::is_signed_v<Underlying>) {
            if (underlying < 0) {
                m_valid = false;
                return;
            }
        }
        using Unsigned = std::make_unsigned_t<Underlying>;
        const quint64 encoded = quint64(static_cast<Unsigned>(underlying));
        if (encoded > maximum) {
            m_valid = false;
            return;
        }
        u32(quint32(encoded));
    }

    bool isValid() const { return m_valid && m_bytes.size() <= maximumPayloadBytes; }
    QByteArray take() { return isValid() ? std::move(m_bytes) : QByteArray{}; }

private:
    QByteArray m_bytes;
    bool m_valid = true;
};

class BinaryReader
{
public:
    explicit BinaryReader(QByteArrayView bytes)
        : m_bytes(bytes)
    {}

    QByteArray raw(qsizetype size)
    {
        if (!m_valid || size < 0 || size > m_bytes.size() - m_offset) {
            m_valid = false;
            return {};
        }
        QByteArray result(m_bytes.data() + m_offset, size);
        m_offset += size;
        return result;
    }

    quint8 u8()
    {
        const QByteArray value = raw(1);
        return value.size() == 1 ? quint8(value.at(0)) : 0;
    }

    quint16 u16()
    {
        const QByteArray value = raw(2);
        if (value.size() != 2)
            return 0;
        return quint16(quint8(value.at(0))) << 8 | quint8(value.at(1));
    }

    quint32 u32()
    {
        const QByteArray value = raw(4);
        if (value.size() != 4)
            return 0;
        return quint32(quint8(value.at(0))) << 24 | quint32(quint8(value.at(1))) << 16
               | quint32(quint8(value.at(2))) << 8 | quint8(value.at(3));
    }

    quint64 u64()
    {
        const QByteArray value = raw(8);
        if (value.size() != 8)
            return 0;
        quint64 result = 0;
        for (char byte : value)
            result = (result << 8) | quint8(byte);
        return result;
    }

    qint32 i32() { return std::bit_cast<qint32>(u32()); }
    qint64 i64() { return std::bit_cast<qint64>(u64()); }

    bool boolean()
    {
        const quint8 value = u8();
        if (value > 1)
            m_valid = false;
        return value == 1;
    }

    QByteArray bytes(qsizetype maximum = maximumArtifactBytes)
    {
        const quint32 size = u32();
        if (!m_valid || quint64(size) > quint64(maximum)) {
            m_valid = false;
            return {};
        }
        return raw(size);
    }

    QString string()
    {
        const QByteArray utf8 = bytes(maximumTextBytes);
        const QString result = QString::fromUtf8(utf8);
        if (result.toUtf8() != utf8)
            m_valid = false;
        return result;
    }

    quint32 count()
    {
        const quint32 result = u32();
        if (result > maximumCollectionItems)
            m_valid = false;
        return result;
    }

    template<typename Enum>
    Enum enumeration(quint32 maximum)
    {
        static_assert(std::is_enum_v<Enum>);
        const quint32 value = u32();
        if (value > maximum)
            m_valid = false;
        return Enum(value);
    }

    bool atEnd() const { return m_valid && m_offset == m_bytes.size(); }
    bool isValid() const { return m_valid; }
    qsizetype remaining() const { return m_valid ? m_bytes.size() - m_offset : 0; }
    void invalidate() { m_valid = false; }

private:
    QByteArrayView m_bytes;
    qsizetype m_offset = 0;
    bool m_valid = true;
};

void writeNodeId(BinaryWriter &writer, const Data::NodeId &id)
{
    writer.string(id.toString());
}

Data::NodeId readNodeId(BinaryReader &reader)
{
    const QString text = reader.string();
    const Data::NodeId id = Data::NodeId::fromString(text);
    if (id.toString() != text)
        reader.raw(-1);
    return id;
}

void writeSha(BinaryWriter &writer, const Data::RuntimePackageCompilerSha256 &value)
{
    writer.bytes(value.value(), 32);
}

Data::RuntimePackageCompilerSha256 readSha(BinaryReader &reader)
{
    return Data::RuntimePackageCompilerSha256{reader.bytes(32)};
}

void writeCanonical(BinaryWriter &writer, const Data::RuntimePackageCompilerCanonicalJson &canonical)
{
    writer.bytes(canonical.exactBytes(), maximumArtifactBytes);
}

Data::RuntimePackageCompilerCanonicalJson readCanonical(BinaryReader &reader)
{
    return Data::RuntimePackageCompilerCanonicalJson::fromExactBytes(
        reader.bytes(maximumArtifactBytes));
}

template<typename Value, typename Writer>
void writeList(BinaryWriter &binary, const QList<Value> &values, Writer writer)
{
    binary.count(values.size());
    for (const Value &value : values)
        writer(binary, value);
}

template<typename Value, typename Reader>
QList<Value> readList(BinaryReader &binary, Reader reader)
{
    const quint32 count = binary.count();
    if (!binary.isValid() || quint64(count) > quint64(binary.remaining() / 4)) {
        binary.invalidate();
        return {};
    }
    QList<Value> result;
    result.reserve(qsizetype(count));
    for (quint32 index = 0; index < count && binary.isValid(); ++index)
        result.append(reader(binary));
    return result;
}

void writeStringMap(BinaryWriter &writer, const QMap<QString, QString> &values)
{
    writer.count(values.size());
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        writer.string(it.key());
        writer.string(it.value());
    }
}

QMap<QString, QString> readStringMap(BinaryReader &reader)
{
    const quint32 count = reader.count();
    if (!reader.isValid() || quint64(count) > quint64(reader.remaining() / 8)) {
        reader.invalidate();
        return {};
    }
    QMap<QString, QString> result;
    QString previous;
    for (quint32 index = 0; index < count && reader.isValid(); ++index) {
        const QString key = reader.string();
        const QString value = reader.string();
        if ((index && key <= previous) || result.contains(key))
            reader.raw(-1);
        result.insert(key, value);
        previous = key;
    }
    return result;
}

void writeIdentity(BinaryWriter &writer, const Data::DeviceIdentity &identity)
{
    writer.u32(identity.vendorId);
    writer.u32(identity.productCode);
    writer.u32(identity.revisionNumber);
}

Data::DeviceIdentity readIdentity(BinaryReader &reader)
{
    return {reader.u32(), reader.u32(), reader.u32()};
}

void writeModule(BinaryWriter &writer, const Data::DeviceModuleAssignment &module)
{
    writer.i32(qint32(module.slot));
    writer.u32(module.moduleIdent);
    writer.u16(module.objectIndexOffset);
    writer.u16(module.pdoIndexOffset);
}

Data::DeviceModuleAssignment readModule(BinaryReader &reader)
{
    return {reader.i32(), reader.u32(), reader.u16(), reader.u16()};
}

void writeRational(BinaryWriter &writer, const Data::ExactRational &value)
{
    writer.i64(value.numerator);
    writer.i64(value.denominator);
}

Data::ExactRational readRational(BinaryReader &reader)
{
    return {reader.i64(), reader.i64()};
}

void writeOptionalRational(BinaryWriter &writer, const std::optional<Data::ExactRational> &value)
{
    writer.boolean(value.has_value());
    if (value)
        writeRational(writer, *value);
}

std::optional<Data::ExactRational> readOptionalRational(BinaryReader &reader)
{
    if (!reader.boolean())
        return std::nullopt;
    return readRational(reader);
}

void writeEngineeringConstraint(BinaryWriter &writer, const Data::EngineeringConstraint &constraint)
{
    writeOptionalRational(writer, constraint.minimum);
    writeOptionalRational(writer, constraint.maximum);
    writeOptionalRational(writer, constraint.step);
    writeOptionalRational(writer, constraint.stepOrigin);
    writeList(
        writer,
        constraint.enumeration,
        [](BinaryWriter &binary, const Data::EngineeringEnumerationValue &value) {
            binary.string(value.id);
            binary.string(value.displayName);
            writeRational(binary, value.value);
        });
}

Data::EngineeringConstraint readEngineeringConstraint(BinaryReader &reader)
{
    Data::EngineeringConstraint result;
    result.minimum = readOptionalRational(reader);
    result.maximum = readOptionalRational(reader);
    result.step = readOptionalRational(reader);
    result.stepOrigin = readOptionalRational(reader);
    result.enumeration
        = readList<Data::EngineeringEnumerationValue>(reader, [](BinaryReader &binary) {
              return Data::EngineeringEnumerationValue{
                  binary.string(), binary.string(), readRational(binary)};
          });
    return result;
}

void writeEngineeringValue(BinaryWriter &writer, const Data::EngineeringValue &value)
{
    writer.enumeration(value.kind, 5);
    writer.boolean(value.boolean);
    writer.i64(value.signedInteger);
    writer.u64(value.unsignedInteger);
    writeRational(writer, value.rational);
    writer.string(value.enumerationName);
}

Data::EngineeringValue readEngineeringValue(BinaryReader &reader)
{
    Data::EngineeringValue result;
    result.kind = reader.enumeration<Data::EngineeringValueKind>(5);
    result.boolean = reader.boolean();
    result.signedInteger = reader.i64();
    result.unsignedInteger = reader.u64();
    result.rational = readRational(reader);
    result.enumerationName = reader.string();
    return result;
}

void writeDeviceParameterConfiguration(
    BinaryWriter &writer, const Data::DeviceParameterConfiguration &configuration)
{
    writeList(
        writer,
        configuration.values,
        [](BinaryWriter &binary, const Data::DeviceParameterValue &parameter) {
            binary.string(parameter.parameterId);
            writeEngineeringValue(binary, parameter.value);
        });
}

Data::DeviceParameterConfiguration readDeviceParameterConfiguration(BinaryReader &reader)
{
    Data::DeviceParameterConfiguration result;
    result.values = readList<Data::DeviceParameterValue>(reader, [](BinaryReader &binary) {
        return Data::DeviceParameterValue{binary.string(), readEngineeringValue(binary)};
    });
    return result;
}

template<typename Value, typename Writer>
void writeOptional(BinaryWriter &writer, const std::optional<Value> &value, Writer writeValue)
{
    writer.boolean(value.has_value());
    if (value)
        writeValue(writer, *value);
}

template<typename Value, typename Reader>
std::optional<Value> readOptional(BinaryReader &reader, Reader readValue)
{
    if (!reader.boolean())
        return std::nullopt;
    return readValue(reader);
}

void writeManualTiming(BinaryWriter &writer, const Data::ManualCommandTiming &timing)
{
    writer.u32(timing.commandTtlMs);
    writer.u32(timing.refreshTimeoutMs);
    writer.u32(timing.maxContinuousHoldMs);
}

Data::ManualCommandTiming readManualTiming(BinaryReader &reader)
{
    return {reader.u32(), reader.u32(), reader.u32()};
}

void writeManualControl(BinaryWriter &writer, const Data::ManualControlEnvelope &envelope)
{
    writer.boolean(envelope.enabled);
    writeList(writer, envelope.signalEnvelopes, [](BinaryWriter &binary, const auto &signal) {
        binary.string(signal.signalId.value);
        binary.boolean(signal.enabled);
        binary.boolean(signal.holdToRun);
        writeManualTiming(binary, signal.timing);
        writeEngineeringConstraint(binary, signal.allowedRange);
        writeOptional(binary, signal.safeValue, writeEngineeringValue);
        writeList(
            binary,
            signal.consistencyGroupSafeValues,
            [](BinaryWriter &nested, const Data::ManualSignalSafeValue &safe) {
                nested.string(safe.signalId.value);
                writeEngineeringValue(nested, safe.value);
            });
    });
    writeList(writer, envelope.actionEnvelopes, [](BinaryWriter &binary, const auto &action) {
        binary.string(action.actionId.value);
        binary.boolean(action.enabled);
        binary.boolean(action.holdToRun);
        writeManualTiming(binary, action.timing);
        writeList(
            binary,
            action.parameters,
            [](BinaryWriter &nested, const Data::ManualActionParameterEnvelope &parameter) {
                nested.string(parameter.parameterId);
                writeEngineeringConstraint(nested, parameter.allowedRange);
                writeOptional(nested, parameter.defaultValue, writeEngineeringValue);
            });
        binary.string(action.releaseActionId.value);
        binary.string(action.timeoutActionId.value);
        binary.string(action.failureActionId.value);
    });
}

Data::ManualControlEnvelope readManualControl(BinaryReader &reader)
{
    Data::ManualControlEnvelope result;
    result.enabled = reader.boolean();
    result.signalEnvelopes = readList<Data::ManualSignalEnvelope>(reader, [](BinaryReader &binary) {
        Data::ManualSignalEnvelope signal;
        signal.signalId.value = binary.string();
        signal.enabled = binary.boolean();
        signal.holdToRun = binary.boolean();
        signal.timing = readManualTiming(binary);
        signal.allowedRange = readEngineeringConstraint(binary);
        signal.safeValue = readOptional<Data::EngineeringValue>(binary, readEngineeringValue);
        signal.consistencyGroupSafeValues
            = readList<Data::ManualSignalSafeValue>(binary, [](BinaryReader &nested) {
                  Data::ManualSignalSafeValue safe;
                  safe.signalId.value = nested.string();
                  safe.value = readEngineeringValue(nested);
                  return safe;
              });
        return signal;
    });
    result.actionEnvelopes = readList<Data::ManualActionEnvelope>(reader, [](BinaryReader &binary) {
        Data::ManualActionEnvelope action;
        action.actionId.value = binary.string();
        action.enabled = binary.boolean();
        action.holdToRun = binary.boolean();
        action.timing = readManualTiming(binary);
        action.parameters
            = readList<Data::ManualActionParameterEnvelope>(binary, [](BinaryReader &nested) {
                  Data::ManualActionParameterEnvelope parameter;
                  parameter.parameterId = nested.string();
                  parameter.allowedRange = readEngineeringConstraint(nested);
                  parameter.defaultValue
                      = readOptional<Data::EngineeringValue>(nested, readEngineeringValue);
                  return parameter;
              });
        action.releaseActionId.value = binary.string();
        action.timeoutActionId.value = binary.string();
        action.failureActionId.value = binary.string();
        return action;
    });
    return result;
}

void writeSyncManager(BinaryWriter &writer, const Data::SyncManagerConfiguration &configuration)
{
    writeNodeId(writer, configuration.id);
    writer.i32(qint32(configuration.index));
    writer.string(configuration.name);
    writer.enumeration(configuration.direction, 2);
    writer.boolean(configuration.enabled);
    writer.i32(qint32(configuration.sizeLimitBytes));
}

Data::SyncManagerConfiguration readSyncManager(BinaryReader &reader)
{
    Data::SyncManagerConfiguration result;
    result.id = readNodeId(reader);
    result.index = reader.i32();
    result.name = reader.string();
    result.direction = reader.enumeration<Data::SyncManagerDirection>(2);
    result.enabled = reader.boolean();
    result.sizeLimitBytes = reader.i32();
    return result;
}

void writePdoEntry(BinaryWriter &writer, const Data::PdoEntryConfiguration &entry)
{
    writeNodeId(writer, entry.id);
    writer.u16(entry.index);
    writer.u8(entry.subIndex);
    writer.string(entry.name);
    writer.i32(qint32(entry.bitLength));
    writer.enumeration(entry.dataType, 13);
    writer.string(entry.rawDataType);
    writer.i64(entry.requestedBitOffset);
    writer.boolean(entry.mappingSupported);
    writer.boolean(entry.padding);
}

Data::PdoEntryConfiguration readPdoEntry(BinaryReader &reader)
{
    Data::PdoEntryConfiguration result;
    result.id = readNodeId(reader);
    result.index = reader.u16();
    result.subIndex = reader.u8();
    result.name = reader.string();
    result.bitLength = reader.i32();
    result.dataType = reader.enumeration<Data::EtherCATDataType>(13);
    result.rawDataType = reader.string();
    result.requestedBitOffset = reader.i64();
    result.mappingSupported = reader.boolean();
    result.padding = reader.boolean();
    return result;
}

void writePdo(BinaryWriter &writer, const Data::PdoConfiguration &pdo)
{
    writeNodeId(writer, pdo.id);
    writer.u16(pdo.index);
    writer.string(pdo.name);
    writer.enumeration(pdo.direction, 1);
    writer.i32(qint32(pdo.syncManager));
    writer.boolean(pdo.selected);
    writer.boolean(pdo.fixed);
    writer.boolean(pdo.mandatory);
    writer.boolean(pdo.defaultSelected);
    writer.boolean(pdo.mappingSupported);
    writer.string(pdo.predefinedGroup);
    writeList(writer, pdo.entries, writePdoEntry);
}

Data::PdoConfiguration readPdo(BinaryReader &reader)
{
    Data::PdoConfiguration result;
    result.id = readNodeId(reader);
    result.index = reader.u16();
    result.name = reader.string();
    result.direction = reader.enumeration<Data::PdoDirection>(1);
    result.syncManager = reader.i32();
    result.selected = reader.boolean();
    result.fixed = reader.boolean();
    result.mandatory = reader.boolean();
    result.defaultSelected = reader.boolean();
    result.mappingSupported = reader.boolean();
    result.predefinedGroup = reader.string();
    result.entries = readList<Data::PdoEntryConfiguration>(reader, readPdoEntry);
    return result;
}

void writeProcessData(BinaryWriter &writer, const Data::ProcessDataConfiguration &configuration)
{
    writeList(writer, configuration.syncManagers, writeSyncManager);
    writeList(writer, configuration.pdos, writePdo);
}

Data::ProcessDataConfiguration readProcessData(BinaryReader &reader)
{
    return {
        readList<Data::SyncManagerConfiguration>(reader, readSyncManager),
        readList<Data::PdoConfiguration>(reader, readPdo),
    };
}

void writeStartupParameter(BinaryWriter &writer, const Data::StartupParameterConfiguration &parameter)
{
    writeNodeId(writer, parameter.id);
    writer.boolean(parameter.enabled);
    writer.i32(qint32(parameter.order));
    writer.string(parameter.transition);
    writer.u16(parameter.index);
    writer.u8(parameter.subIndex);
    writer.enumeration(parameter.dataType, 13);
    writer.string(parameter.rawDataType);
    writer.bytes(parameter.rawValue, maximumArtifactBytes);
    writer.string(parameter.comment);
}

Data::StartupParameterConfiguration readStartupParameter(BinaryReader &reader)
{
    Data::StartupParameterConfiguration result;
    result.id = readNodeId(reader);
    result.enabled = reader.boolean();
    result.order = reader.i32();
    result.transition = reader.string();
    result.index = reader.u16();
    result.subIndex = reader.u8();
    result.dataType = reader.enumeration<Data::EtherCATDataType>(13);
    result.rawDataType = reader.string();
    result.rawValue = reader.bytes(maximumArtifactBytes);
    result.comment = reader.string();
    return result;
}

void writeDcSignal(BinaryWriter &writer, const Data::DcSignalConfiguration &signal)
{
    writer.boolean(signal.enabled);
    writer.i64(signal.cycleTimeNs);
    writer.i64(signal.shiftTimeNs);
}

Data::DcSignalConfiguration readDcSignal(BinaryReader &reader)
{
    return {reader.boolean(), reader.i64(), reader.i64()};
}

void writeDcConfiguration(BinaryWriter &writer, const Data::DcConfiguration &configuration)
{
    writer.boolean(configuration.enabled);
    writer.string(configuration.modeName);
    writer.u16(configuration.assignActivate);
    writeDcSignal(writer, configuration.sync0);
    writeDcSignal(writer, configuration.sync1);
    writer.boolean(configuration.potentialReferenceClock);
}

Data::DcConfiguration readDcConfiguration(BinaryReader &reader)
{
    Data::DcConfiguration result;
    result.enabled = reader.boolean();
    result.modeName = reader.string();
    result.assignActivate = reader.u16();
    result.sync0 = readDcSignal(reader);
    result.sync1 = readDcSignal(reader);
    result.potentialReferenceClock = reader.boolean();
    return result;
}

void writeAdapterSelection(BinaryWriter &writer, const Data::DeviceAdapterProjectSelection &selection)
{
    writer.string(selection.adapterId.value);
    writer.string(selection.adapterVersion);
    writer.bytes(selection.adapterContentSha256, 32);
    writer.string(selection.processDataProfileId);
    writeList(writer, selection.moduleAssignments, writeModule);
}

Data::DeviceAdapterProjectSelection readAdapterSelection(BinaryReader &reader)
{
    Data::DeviceAdapterProjectSelection result;
    result.adapterId.value = reader.string();
    result.adapterVersion = reader.string();
    result.adapterContentSha256 = reader.bytes(32);
    result.processDataProfileId = reader.string();
    result.moduleAssignments = readList<Data::DeviceModuleAssignment>(reader, readModule);
    return result;
}

void writeOfflineSlave(
    BinaryWriter &writer,
    const Data::OfflineSlaveConfiguration &slave,
    quint32 payloadVersion)
{
    writeNodeId(writer, slave.id);
    writeNodeId(writer, slave.masterId);
    writer.i32(qint32(slave.position));
    writeIdentity(writer, slave.identity);
    writer.u32(slave.serialNumber);
    writer.u16(slave.alias);
    writer.string(slave.name);
    writeNodeId(writer, slave.deviceDescriptionId);
    writeProcessData(writer, slave.processData);
    writeList(writer, slave.startup.parameters, writeStartupParameter);
    writeDcConfiguration(writer, slave.dc);
    writer.bytes(slave.esiSha256, 32);
    writeAdapterSelection(writer, slave.adapterSelection);
    writeManualControl(writer, slave.manualControlEnvelope);
    writer.u16(slave.stationAddress);
    if (payloadVersion >= 2)
        writeDeviceParameterConfiguration(writer, slave.deviceParameters);
}

Data::OfflineSlaveConfiguration readOfflineSlave(BinaryReader &reader, quint32 payloadVersion)
{
    Data::OfflineSlaveConfiguration result;
    result.id = readNodeId(reader);
    result.masterId = readNodeId(reader);
    result.position = reader.i32();
    result.identity = readIdentity(reader);
    result.serialNumber = reader.u32();
    result.alias = reader.u16();
    result.name = reader.string();
    result.deviceDescriptionId = readNodeId(reader);
    result.processData = readProcessData(reader);
    result.startup.parameters
        = readList<Data::StartupParameterConfiguration>(reader, readStartupParameter);
    result.dc = readDcConfiguration(reader);
    result.esiSha256 = reader.bytes(32);
    result.adapterSelection = readAdapterSelection(reader);
    result.manualControlEnvelope = readManualControl(reader);
    result.stationAddress = reader.u16();
    if (payloadVersion >= 2)
        result.deviceParameters = readDeviceParameterConfiguration(reader);
    return result;
}

void writeProjectNode(BinaryWriter &writer, const Data::ProjectNodeSnapshot &node)
{
    writeNodeId(writer, node.id);
    writeNodeId(writer, node.parentId);
    writer.enumeration(node.kind, 3);
    writer.string(node.name);
}

Data::ProjectNodeSnapshot readProjectNode(BinaryReader &reader)
{
    return {
        readNodeId(reader),
        readNodeId(reader),
        reader.enumeration<Data::ProjectNodeKind>(3),
        reader.string(),
    };
}

void writeBindingReference(
    BinaryWriter &writer, const Data::SemanticBindingArtifactReference &reference)
{
    writer.string(reference.artifactId);
    writer.bytes(reference.artifactSha256, 32);
    writer.bytes(reference.projectConfigurationSha256, 32);
    writeList(
        writer,
        reference.projectDeviceBindings,
        [](BinaryWriter &binary, const Data::SemanticProjectDeviceBinding &binding) {
            writeNodeId(binary, binding.slaveId);
            binary.string(binding.projectDeviceId);
        });
}

Data::SemanticBindingArtifactReference readBindingReference(BinaryReader &reader)
{
    Data::SemanticBindingArtifactReference result;
    result.artifactId = reader.string();
    result.artifactSha256 = reader.bytes(32);
    result.projectConfigurationSha256 = reader.bytes(32);
    result.projectDeviceBindings
        = readList<Data::SemanticProjectDeviceBinding>(reader, [](BinaryReader &binary) {
              return Data::SemanticProjectDeviceBinding{readNodeId(binary), binary.string()};
          });
    return result;
}

void writeProjectSnapshot(
    BinaryWriter &writer, const Data::ProjectSnapshot &snapshot, quint32 payloadVersion)
{
    writeNodeId(writer, snapshot.id);
    writer.string(snapshot.name);
    writer.i32(qint32(snapshot.formatVersion));
    writer.string(snapshot.createdBy);
    writeList(writer, snapshot.nodes, writeProjectNode);
    writer.boolean(snapshot.modified);
    writer.boolean(snapshot.valid);
    writer.boolean(snapshot.migrated);
    writer.string(snapshot.error);
    writeList(
        writer,
        snapshot.slaves,
        [payloadVersion](BinaryWriter &binary, const Data::OfflineSlaveConfiguration &slave) {
            writeOfflineSlave(binary, slave, payloadVersion);
        });
    writer.enumeration(snapshot.masterConfiguration.timingMode, 2);
    writer.u32(snapshot.masterConfiguration.cyclePeriodNs);
    writeBindingReference(writer, snapshot.masterBindingArtifact);
}

Data::ProjectSnapshot readProjectSnapshot(BinaryReader &reader, quint32 payloadVersion)
{
    Data::ProjectSnapshot result;
    result.id = readNodeId(reader);
    result.name = reader.string();
    result.formatVersion = reader.i32();
    result.createdBy = reader.string();
    result.nodes = readList<Data::ProjectNodeSnapshot>(reader, readProjectNode);
    result.modified = reader.boolean();
    result.valid = reader.boolean();
    result.migrated = reader.boolean();
    result.error = reader.string();
    result.slaves = readList<Data::OfflineSlaveConfiguration>(
        reader, [payloadVersion](BinaryReader &binary) {
            return readOfflineSlave(binary, payloadVersion);
        });
    result.masterConfiguration.timingMode = reader.enumeration<Data::MasterTimingMode>(2);
    result.masterConfiguration.cyclePeriodNs = reader.u32();
    result.masterBindingArtifact = readBindingReference(reader);
    return result;
}

void writeCompilerPdoEntry(BinaryWriter &writer, const Data::RuntimePackageCompilerPdoEntry &entry)
{
    writer.string(entry.fieldId);
    writer.u16(entry.index);
    writer.u8(entry.subIndex);
    writer.u16(entry.bitLength);
    writer.string(entry.dataType);
}

Data::RuntimePackageCompilerPdoEntry readCompilerPdoEntry(BinaryReader &reader)
{
    return {reader.string(), reader.u16(), reader.u8(), reader.u16(), reader.string()};
}

void writeCompilerPdo(BinaryWriter &writer, const Data::RuntimePackageCompilerPdoMapping &mapping)
{
    writer.string(mapping.id);
    writer.enumeration(mapping.direction, 2);
    writer.u16(mapping.pdoIndex);
    writer.u8(mapping.syncManager);
    writer.boolean(mapping.fixed);
    writeList(writer, mapping.entries, writeCompilerPdoEntry);
}

Data::RuntimePackageCompilerPdoMapping readCompilerPdo(BinaryReader &reader)
{
    Data::RuntimePackageCompilerPdoMapping result;
    result.id = reader.string();
    result.direction = reader.enumeration<Data::RuntimePackageCompilerPdoDirection>(2);
    result.pdoIndex = reader.u16();
    result.syncManager = reader.u8();
    result.fixed = reader.boolean();
    result.entries = readList<Data::RuntimePackageCompilerPdoEntry>(reader, readCompilerPdoEntry);
    return result;
}

void writeCompilerStartup(BinaryWriter &writer, const Data::RuntimePackageCompilerStartupSdo &sdo)
{
    writer.u16(sdo.sequence);
    writer.string(sdo.id);
    writer.boolean(sdo.enabled);
    writer.enumeration(sdo.stage, 3);
    writer.u16(sdo.index);
    writer.u8(sdo.subIndex);
    if (std::holds_alternative<qint64>(sdo.value)) {
        writer.u8(1);
        writer.i64(std::get<qint64>(sdo.value));
    } else {
        writer.u8(2);
        writer.u64(std::get<quint64>(sdo.value));
    }
    writer.u8(sdo.valueBytes);
    writer.boolean(sdo.completeAccess);
    writer.u64(sdo.timeoutNs);
    writer.u8(sdo.retryCount);
    writer.enumeration(sdo.failureAction, 3);
    writer.boolean(sdo.persistent);
    writer.boolean(sdo.requiresPowerCycle);
}

Data::RuntimePackageCompilerStartupSdo readCompilerStartup(BinaryReader &reader)
{
    Data::RuntimePackageCompilerStartupSdo result;
    result.sequence = reader.u16();
    result.id = reader.string();
    result.enabled = reader.boolean();
    result.stage = reader.enumeration<Data::RuntimePackageCompilerStartupStage>(3);
    result.index = reader.u16();
    result.subIndex = reader.u8();
    const quint8 valueKind = reader.u8();
    if (valueKind == 1)
        result.value = reader.i64();
    else if (valueKind == 2)
        result.value = reader.u64();
    else
        reader.raw(-1);
    result.valueBytes = reader.u8();
    result.completeAccess = reader.boolean();
    result.timeoutNs = reader.u64();
    result.retryCount = reader.u8();
    result.failureAction = reader.enumeration<Data::RuntimePackageCompilerStartupFailureAction>(3);
    result.persistent = reader.boolean();
    result.requiresPowerCycle = reader.boolean();
    return result;
}

void writeCompilerDc(BinaryWriter &writer, const Data::RuntimePackageCompilerDcProjection &projection)
{
    writer.boolean(projection.enabled);
    writeOptional(writer, projection.signedDcProfileId, [](BinaryWriter &binary, const QString &value) {
        binary.string(value);
    });
    writeOptional(writer, projection.mode, [](BinaryWriter &binary, const QString &value) {
        binary.string(value);
    });
    writer.u16(projection.assignActivate);
    writer.u64(projection.sync0CycleNs);
    writer.i64(projection.sync0ShiftNs);
    writer.u64(projection.sync1CycleNs);
    writer.i64(projection.sync1ShiftNs);
    writer.boolean(projection.referenceClock);
}

Data::RuntimePackageCompilerDcProjection readCompilerDc(BinaryReader &reader)
{
    Data::RuntimePackageCompilerDcProjection result;
    result.enabled = reader.boolean();
    result.signedDcProfileId = readOptional<QString>(reader, [](BinaryReader &binary) {
        return binary.string();
    });
    result.mode = readOptional<QString>(reader, [](BinaryReader &binary) {
        return binary.string();
    });
    result.assignActivate = reader.u16();
    result.sync0CycleNs = reader.u64();
    result.sync0ShiftNs = reader.i64();
    result.sync1CycleNs = reader.u64();
    result.sync1ShiftNs = reader.i64();
    result.referenceClock = reader.boolean();
    return result;
}

void writeCompilerManualEnvelope(
    BinaryWriter &writer, const Data::RuntimePackageCompilerManualEnvelope &envelope)
{
    writer.boolean(envelope.enabled);
    writer.u16(envelope.maximumTtlCycles);
    writer.u16(envelope.refreshCycles);
    writer.u16(envelope.maximumHoldCycles);
    writer.enumeration(envelope.timeoutAction, 3);
    writer.enumeration(envelope.releaseAction, 3);
    writer.enumeration(envelope.failureAction, 3);
}

Data::RuntimePackageCompilerManualEnvelope readCompilerManualEnvelope(BinaryReader &reader)
{
    Data::RuntimePackageCompilerManualEnvelope result;
    result.enabled = reader.boolean();
    result.maximumTtlCycles = reader.u16();
    result.refreshCycles = reader.u16();
    result.maximumHoldCycles = reader.u16();
    result.timeoutAction = reader.enumeration<Data::RuntimePackageCompilerManualRecoveryAction>(3);
    result.releaseAction = reader.enumeration<Data::RuntimePackageCompilerManualRecoveryAction>(3);
    result.failureAction = reader.enumeration<Data::RuntimePackageCompilerManualRecoveryAction>(3);
    return result;
}

void writeCompilerDevice(
    BinaryWriter &writer, const Data::RuntimePackageCompilerDeviceProjection &device)
{
    writeNodeId(writer, device.projectSlaveNodeId);
    writer.string(device.slaveNodeId);
    writer.string(device.projectDeviceId);
    writer.i32(qint32(device.position));
    writer.u16(device.stationAddress);
    writer.u16(device.alias);
    writeIdentity(writer, device.identity);
    writer.u32(device.serialNumber);
    writeSha(writer, device.esiSha256);
    writer.string(device.targetProfileId);
    writer.string(device.adapterId);
    writer.string(device.adapterVersion);
    writeSha(writer, device.adapterSha256);
    writer.string(device.pdoProfileId);
    writeOptional(writer, device.signedDcProfileId, [](BinaryWriter &binary, const QString &value) {
        binary.string(value);
    });
    writeList(writer, device.pdoMappings, writeCompilerPdo);
    writeList(writer, device.startupSdos, writeCompilerStartup);
    writeCompilerDc(writer, device.dc);
    writeList(writer, device.moduleAssignments, writeModule);
    writeStringMap(writer, device.componentBindingIds);
    writeStringMap(writer, device.semanticBindingIds);
    writeStringMap(writer, device.semanticActionBindingIds);
    writer.enumeration(device.symbolMode, 3);
    writeStringMap(writer, device.symbols);
    writeCompilerManualEnvelope(writer, device.manualEnvelope);
}

Data::RuntimePackageCompilerDeviceProjection readCompilerDevice(BinaryReader &reader)
{
    Data::RuntimePackageCompilerDeviceProjection result;
    result.projectSlaveNodeId = readNodeId(reader);
    result.slaveNodeId = reader.string();
    result.projectDeviceId = reader.string();
    result.position = reader.i32();
    result.stationAddress = reader.u16();
    result.alias = reader.u16();
    result.identity = readIdentity(reader);
    result.serialNumber = reader.u32();
    result.esiSha256 = readSha(reader);
    result.targetProfileId = reader.string();
    result.adapterId = reader.string();
    result.adapterVersion = reader.string();
    result.adapterSha256 = readSha(reader);
    result.pdoProfileId = reader.string();
    result.signedDcProfileId = readOptional<QString>(reader, [](BinaryReader &binary) {
        return binary.string();
    });
    result.pdoMappings = readList<Data::RuntimePackageCompilerPdoMapping>(reader, readCompilerPdo);
    result.startupSdos
        = readList<Data::RuntimePackageCompilerStartupSdo>(reader, readCompilerStartup);
    result.dc = readCompilerDc(reader);
    result.moduleAssignments = readList<Data::DeviceModuleAssignment>(reader, readModule);
    result.componentBindingIds = readStringMap(reader);
    result.semanticBindingIds = readStringMap(reader);
    result.semanticActionBindingIds = readStringMap(reader);
    result.symbolMode = reader.enumeration<Data::RuntimePackageCompilerSymbolMode>(3);
    result.symbols = readStringMap(reader);
    result.manualEnvelope = readCompilerManualEnvelope(reader);
    return result;
}

void writeArtifact(
    BinaryWriter &writer,
    const Data::RuntimePackageCompilerSourceArtifact &artifact,
    quint32 maximumKind = 10);
Data::RuntimePackageCompilerSourceArtifact readArtifact(
    BinaryReader &reader, quint32 maximumKind = 10);

void writeCompilerParameterObservedEvidence(
    BinaryWriter &writer,
    const Data::RuntimePackageCompilerParameterObservedEvidence &evidence)
{
    writer.u64(evidence.bootId);
    writer.u32(evidence.topologyCaptureSequence);
    writer.u32(evidence.evidenceSequence);
    writer.u64(evidence.completedTimeNs);
    writeSha(writer, evidence.profileSha256);
    writer.i32(qint32(evidence.position));
    writer.u16(evidence.stationAddress);
    writeIdentity(writer, evidence.identity);
    writer.u32(evidence.serialNumber);
    writer.u16(evidence.index);
    writer.u8(evidence.subIndex);
    writer.bytes(evidence.rawValue, 8);
    writer.i64(evidence.value);
}

Data::RuntimePackageCompilerParameterObservedEvidence
readCompilerParameterObservedEvidence(BinaryReader &reader)
{
    Data::RuntimePackageCompilerParameterObservedEvidence result;
    result.bootId = reader.u64();
    result.topologyCaptureSequence = reader.u32();
    result.evidenceSequence = reader.u32();
    result.completedTimeNs = reader.u64();
    result.profileSha256 = readSha(reader);
    result.position = reader.i32();
    result.stationAddress = reader.u16();
    result.identity = readIdentity(reader);
    result.serialNumber = reader.u32();
    result.index = reader.u16();
    result.subIndex = reader.u8();
    result.rawValue = reader.bytes(8);
    result.value = reader.i64();
    return result;
}

void writeCompilerDeviceParameter(
    BinaryWriter &writer, const Data::RuntimePackageCompilerDeviceParameter &parameter)
{
    writer.string(parameter.parameterId);
    writeSha(writer, parameter.definitionSha256);
    writer.i64(parameter.configuredValue);
    writeOptional(
        writer,
        parameter.observedEvidence,
        writeCompilerParameterObservedEvidence);
}

Data::RuntimePackageCompilerDeviceParameter readCompilerDeviceParameter(BinaryReader &reader)
{
    Data::RuntimePackageCompilerDeviceParameter result;
    result.parameterId = reader.string();
    result.definitionSha256 = readSha(reader);
    result.configuredValue = reader.i64();
    result.observedEvidence
        = readOptional<Data::RuntimePackageCompilerParameterObservedEvidence>(
            reader, readCompilerParameterObservedEvidence);
    return result;
}

void writeVersionThreeExtensions(
    BinaryWriter &writer, const Data::RuntimePackageCompilerCompileRequest &request)
{
    writeList(
        writer,
        request.projectProjection.devices,
        [](BinaryWriter &binary, const Data::RuntimePackageCompilerDeviceProjection &device) {
            writeList(binary, device.deviceParameters, writeCompilerDeviceParameter);
        });
    writeOptional(
        writer,
        request.sourceArtifacts.parameterContractBundle,
        [](BinaryWriter &binary, const Data::RuntimePackageCompilerSourceArtifact &artifact) {
            writeArtifact(binary, artifact, 11);
        });
}

void readVersionThreeExtensions(
    BinaryReader &reader, Data::RuntimePackageCompilerCompileRequest *request)
{
    const QList<QList<Data::RuntimePackageCompilerDeviceParameter>> parameters
        = readList<QList<Data::RuntimePackageCompilerDeviceParameter>>(
            reader, [](BinaryReader &binary) {
                return readList<Data::RuntimePackageCompilerDeviceParameter>(
                    binary, readCompilerDeviceParameter);
            });
    if (parameters.size() != request->projectProjection.devices.size()) {
        reader.invalidate();
        return;
    }
    for (qsizetype index = 0; index < parameters.size(); ++index)
        request->projectProjection.devices[index].deviceParameters = parameters.at(index);
    request->sourceArtifacts.parameterContractBundle
        = readOptional<Data::RuntimePackageCompilerSourceArtifact>(
            reader, [](BinaryReader &binary) { return readArtifact(binary, 11); });
}

void writeProjectProjection(
    BinaryWriter &writer, const Data::RuntimePackageCompilerProjectProjection &project)
{
    writeNodeId(writer, project.projectNodeId);
    writeNodeId(writer, project.masterProjectNodeId);
    writer.string(project.projectId);
    writer.string(project.masterNodeId);
    writer.u64(project.documentRevision);
    writer.enumeration(project.timingMode, 2);
    writer.u64(project.cyclePeriodNs);
    writer.u32(project.linkSpeedMbps);
    writeList(writer, project.devices, writeCompilerDevice);
    writeCanonical(writer, project.uiMetadata);
}

Data::RuntimePackageCompilerProjectProjection readProjectProjection(BinaryReader &reader)
{
    Data::RuntimePackageCompilerProjectProjection result;
    result.projectNodeId = readNodeId(reader);
    result.masterProjectNodeId = readNodeId(reader);
    result.projectId = reader.string();
    result.masterNodeId = reader.string();
    result.documentRevision = reader.u64();
    result.timingMode = reader.enumeration<Data::MasterTimingMode>(2);
    result.cyclePeriodNs = reader.u64();
    result.linkSpeedMbps = reader.u32();
    result.devices
        = readList<Data::RuntimePackageCompilerDeviceProjection>(reader, readCompilerDevice);
    result.uiMetadata = readCanonical(reader);
    return result;
}

void writeTopologySlave(
    BinaryWriter &writer, const Data::RuntimePackageCompilerTopologySlaveEvidence &slave)
{
    writeNodeId(writer, slave.projectSlaveNodeId);
    writer.i32(qint32(slave.position));
    writer.u16(slave.stationAddress);
    writer.u16(slave.alias);
    writeIdentity(writer, slave.identity);
    writer.u32(slave.serialNumber);
    writeList(writer, slave.moduleAssignments, writeModule);
}

Data::RuntimePackageCompilerTopologySlaveEvidence readTopologySlave(BinaryReader &reader)
{
    Data::RuntimePackageCompilerTopologySlaveEvidence result;
    result.projectSlaveNodeId = readNodeId(reader);
    result.position = reader.i32();
    result.stationAddress = reader.u16();
    result.alias = reader.u16();
    result.identity = readIdentity(reader);
    result.serialNumber = reader.u32();
    result.moduleAssignments = readList<Data::DeviceModuleAssignment>(reader, readModule);
    return result;
}

void writeTopology(
    BinaryWriter &writer, const Data::RuntimePackageCompilerFreshTopologyEvidence &topology)
{
    writeNodeId(writer, topology.scope.projectId);
    writeNodeId(writer, topology.scope.masterId);
    writer.u64(topology.sessionGeneration);
    writer.u64(topology.sessionId);
    writer.string(topology.evidenceId);
    writer.u64(topology.captureBootId);
    writer.u64(topology.captureSequence);
    writer.u64(topology.capturedAtNs);
    writer.u64(topology.expiresAtNs);
    writer.u64(topology.cyclePeriodNs);
    writer.u32(topology.linkSpeedMbps);
    writeList(writer, topology.slaves, writeTopologySlave);
    writeCanonical(writer, topology.canonicalEvidence);
}

Data::RuntimePackageCompilerFreshTopologyEvidence readTopology(BinaryReader &reader)
{
    Data::RuntimePackageCompilerFreshTopologyEvidence result;
    result.scope.projectId = readNodeId(reader);
    result.scope.masterId = readNodeId(reader);
    result.sessionGeneration = reader.u64();
    result.sessionId = reader.u64();
    result.evidenceId = reader.string();
    result.captureBootId = reader.u64();
    result.captureSequence = reader.u64();
    result.capturedAtNs = reader.u64();
    result.expiresAtNs = reader.u64();
    result.cyclePeriodNs = reader.u64();
    result.linkSpeedMbps = reader.u32();
    result.slaves
        = readList<Data::RuntimePackageCompilerTopologySlaveEvidence>(reader, readTopologySlave);
    result.canonicalEvidence = readCanonical(reader);
    return result;
}

void writeArtifact(
    BinaryWriter &writer,
    const Data::RuntimePackageCompilerSourceArtifact &artifact,
    quint32 maximumKind)
{
    writer.enumeration(artifact.kind, maximumKind);
    writer.string(artifact.relativePath);
    writer.bytes(artifact.exactBytes, maximumArtifactBytes);
    writeSha(writer, artifact.sha256);
}

Data::RuntimePackageCompilerSourceArtifact readArtifact(
    BinaryReader &reader, quint32 maximumKind)
{
    Data::RuntimePackageCompilerSourceArtifact result;
    result.kind = reader.enumeration<Data::RuntimePackageCompilerSourceArtifactKind>(maximumKind);
    result.relativePath = reader.string();
    result.exactBytes = reader.bytes(maximumArtifactBytes);
    result.sha256 = readSha(reader);
    return result;
}

void writeArtifacts(
    BinaryWriter &writer, const Data::RuntimePackageCompilerSourceArtifacts &artifacts)
{
    writeArtifact(writer, artifacts.topologyEvidence);
    writeArtifact(writer, artifacts.targetProfile);
    writeArtifact(writer, artifacts.targetProfileSignature);
    writeArtifact(writer, artifacts.productionPublicKey);
    writeArtifact(writer, artifacts.adapterBundle);
    writeArtifact(writer, artifacts.policyTemplate);
    writeArtifact(writer, artifacts.controllerFeatures);
    writeArtifact(writer, artifacts.runtimeSource);
}

Data::RuntimePackageCompilerSourceArtifacts readArtifacts(BinaryReader &reader)
{
    Data::RuntimePackageCompilerSourceArtifacts result;
    result.topologyEvidence = readArtifact(reader);
    result.targetProfile = readArtifact(reader);
    result.targetProfileSignature = readArtifact(reader);
    result.productionPublicKey = readArtifact(reader);
    result.adapterBundle = readArtifact(reader);
    result.policyTemplate = readArtifact(reader);
    result.controllerFeatures = readArtifact(reader);
    result.runtimeSource = readArtifact(reader);
    return result;
}

void writeDeviceSource(
    BinaryWriter &writer,
    const Data::RuntimePackageCompilerDeviceSourceEvidence &source,
    quint32 payloadVersion)
{
    writeNodeId(writer, source.projectSlaveNodeId);
    writeArtifact(writer, source.originalEsi);
    writeArtifact(writer, source.adapterSourceFile);
    writer.enumeration(source.projectAdapterContractVersion, payloadVersion >= 3 ? 4 : 3);
    writer.string(source.projectAdapterId.value);
    writer.string(source.projectAdapterVersion);
    writeSha(writer, source.projectAdapterContentSha256);
    writer.string(source.projectControllerAdapterTarget.adapterId);
    writer.string(source.projectControllerAdapterTarget.adapterVersion);
    writer.bytes(source.projectControllerAdapterTarget.adapterSha256, 32);
    writer.bytes(source.projectControllerAdapterTarget.esiSha256, 32);
    writer.string(source.projectPdoProfileId);
    writer.string(source.projectSignedPdoProfileId);
    writer.string(source.projectSignedDcProfileId);
    writer.string(source.adapterId);
    writer.string(source.adapterVersion);
    writeSha(writer, source.adapterCanonicalSha256);
    writer.string(source.pdoProfileId);
    writeOptional(writer, source.signedDcProfileId, [](BinaryWriter &binary, const QString &value) {
        binary.string(value);
    });
    writer.boolean(source.explicitNoDc);
}

Data::RuntimePackageCompilerDeviceSourceEvidence readDeviceSource(
    BinaryReader &reader, quint32 payloadVersion)
{
    Data::RuntimePackageCompilerDeviceSourceEvidence result;
    result.projectSlaveNodeId = readNodeId(reader);
    result.originalEsi = readArtifact(reader);
    result.adapterSourceFile = readArtifact(reader);
    result.projectAdapterContractVersion = reader.enumeration<Data::DeviceAdapterContractVersion>(
        payloadVersion >= 3 ? 4 : 3);
    result.projectAdapterId.value = reader.string();
    result.projectAdapterVersion = reader.string();
    result.projectAdapterContentSha256 = readSha(reader);
    result.projectControllerAdapterTarget.adapterId = reader.string();
    result.projectControllerAdapterTarget.adapterVersion = reader.string();
    result.projectControllerAdapterTarget.adapterSha256 = reader.bytes(32);
    result.projectControllerAdapterTarget.esiSha256 = reader.bytes(32);
    result.projectPdoProfileId = reader.string();
    result.projectSignedPdoProfileId = reader.string();
    result.projectSignedDcProfileId = reader.string();
    result.adapterId = reader.string();
    result.adapterVersion = reader.string();
    result.adapterCanonicalSha256 = readSha(reader);
    result.pdoProfileId = reader.string();
    result.signedDcProfileId = readOptional<QString>(reader, [](BinaryReader &binary) {
        return binary.string();
    });
    result.explicitNoDc = reader.boolean();
    return result;
}

void writeTarget(
    BinaryWriter &writer, const Data::RuntimePackageCompilerSignedTargetProfileEvidence &target)
{
    writer.string(target.profileId);
    writer.u64(target.policyRevision);
    writeCanonical(writer, target.canonicalProfile);
    writeSha(writer, target.capabilityDescriptorSha256);
    writeSha(writer, target.controllerFeaturesSha256);
    writeSha(writer, target.adapterBundleSha256);
    writer.u32(target.cpu1AbiVersion);
    writer.u32(target.fpgaAbiVersion);
    writer.bytes(target.signature, maximumArtifactBytes);
    writeSha(writer, target.signingKeyIdSha256);
    writer.boolean(target.productionSigned);
}

Data::RuntimePackageCompilerSignedTargetProfileEvidence readTarget(BinaryReader &reader)
{
    Data::RuntimePackageCompilerSignedTargetProfileEvidence result;
    result.profileId = reader.string();
    result.policyRevision = reader.u64();
    result.canonicalProfile = readCanonical(reader);
    result.capabilityDescriptorSha256 = readSha(reader);
    result.controllerFeaturesSha256 = readSha(reader);
    result.adapterBundleSha256 = readSha(reader);
    result.cpu1AbiVersion = reader.u32();
    result.fpgaAbiVersion = reader.u32();
    result.signature = reader.bytes(maximumArtifactBytes);
    result.signingKeyIdSha256 = readSha(reader);
    result.productionSigned = reader.boolean();
    return result;
}

QByteArray encodePayload(
    const RuntimePackageCompilerCompileRecovery &recovery,
    quint32 payloadVersion = currentPayloadVersion)
{
    if (payloadVersion < minimumPayloadVersion || payloadVersion > currentPayloadVersion)
        return {};
    const Data::RuntimePackageCompilerCompileRequest &request = recovery.request;
    BinaryWriter writer;
    writer.raw(QByteArrayView(payloadMagic, qsizetype(sizeof(payloadMagic))));
    writer.u32(payloadVersion);
    writer.string(request.operationId.value());
    writer.string(request.intentId);
    writer.u64(request.configurationId);
    writer.u64(request.buildTimestampNs);
    writer.u64(request.compileTimeNs);
    writer.u32(request.manifestFormatVersion);
    writer.string(request.contractIdentity.contractId);
    writer.u32(request.contractIdentity.contractVersion);
    writeSha(writer, request.contractIdentity.schemaBundleSha256);

    const Data::RuntimePackageCompilerProjectSnapshotEvidence &snapshot
        = request.projectSnapshotEvidence;
    writeProjectSnapshot(writer, snapshot.snapshot(), payloadVersion);
    writer.bytes(recovery.serializedProject, maximumProjectBytes);
    writeSha(writer, snapshot.serializedProjectSha256());
    writer.u64(snapshot.documentRevisionNumber());
    writer.bytes(snapshot.documentRevision().value(), maximumArtifactBytes);
    writer.bytes(snapshot.originalBinding().value(), maximumArtifactBytes);

    writeProjectProjection(writer, request.projectProjection);
    writeTopology(writer, request.topologyEvidence);
    writeArtifacts(writer, request.sourceArtifacts);
    writeList(
        writer,
        request.deviceSourceEvidence,
        [payloadVersion](
            BinaryWriter &binary,
            const Data::RuntimePackageCompilerDeviceSourceEvidence &source) {
            writeDeviceSource(binary, source, payloadVersion);
        });
    writeTarget(writer, request.targetProfile);
    if (payloadVersion >= 3)
        writeVersionThreeExtensions(writer, request);
    return writer.take();
}

Utils::Result<RuntimePackageCompilerCompileRecovery> decodePayload(
    QByteArrayView payload, quint32 *decodedPayloadVersion)
{
    if (payload.isEmpty() || payload.size() > maximumPayloadBytes)
        return Utils::ResultError(QStringLiteral("Compiler recovery payload size is invalid."));
    BinaryReader reader(payload);
    if (reader.raw(sizeof(payloadMagic)) != QByteArray(payloadMagic, qsizetype(sizeof(payloadMagic)))) {
        return Utils::ResultError(QStringLiteral("Compiler recovery payload header is invalid."));
    }
    const quint32 payloadVersion = reader.u32();
    if (payloadVersion < minimumPayloadVersion || payloadVersion > currentPayloadVersion) {
        return Utils::ResultError(QStringLiteral("Compiler recovery payload header is invalid."));
    }

    Data::RuntimePackageCompilerCompileRequest request;
    request.operationId = Data::RuntimePackageCompilerOperationId{reader.string()};
    request.intentId = reader.string();
    request.configurationId = reader.u64();
    request.buildTimestampNs = reader.u64();
    request.compileTimeNs = reader.u64();
    request.manifestFormatVersion = reader.u32();
    request.contractIdentity.contractId = reader.string();
    request.contractIdentity.contractVersion = reader.u32();
    request.contractIdentity.schemaBundleSha256 = readSha(reader);

    Data::ProjectSnapshot snapshot = readProjectSnapshot(reader, payloadVersion);
    QByteArray serializedProject = reader.bytes(maximumProjectBytes);
    const Data::RuntimePackageCompilerSha256 expectedSerializedSha = readSha(reader);
    const quint64 documentRevisionNumber = reader.u64();
    const Data::RuntimePackageActivationDocumentRevisionToken documentRevision{
        reader.bytes(maximumArtifactBytes)};
    const Data::RuntimePackageActivationOriginalBindingToken originalBinding{
        reader.bytes(maximumArtifactBytes)};
    const Data::RuntimePackageActivationProjectCapture capture{
        std::move(snapshot),
        serializedProject,
        documentRevisionNumber,
        documentRevision,
        originalBinding,
    };
    request.projectSnapshotEvidence = Data::RuntimePackageCompilerProjectSnapshotEvidence{capture};
    if (request.projectSnapshotEvidence.serializedProjectSha256() != expectedSerializedSha)
        reader.raw(-1);

    request.projectProjection = readProjectProjection(reader);
    request.topologyEvidence = readTopology(reader);
    request.sourceArtifacts = readArtifacts(reader);
    request.deviceSourceEvidence
        = readList<Data::RuntimePackageCompilerDeviceSourceEvidence>(
            reader, [payloadVersion](BinaryReader &binary) {
                return readDeviceSource(binary, payloadVersion);
            });
    request.targetProfile = readTarget(reader);
    if (payloadVersion >= 3)
        readVersionThreeExtensions(reader, &request);

    RuntimePackageCompilerCompileRecovery recovery{std::move(request), std::move(serializedProject)};
    if (!reader.atEnd() || !recovery.isValid())
        return Utils::ResultError(QStringLiteral("Compiler recovery payload is invalid."));
    *decodedPayloadVersion = payloadVersion;
    return recovery;
}

const QSet<QString> recoveryKeys{
    QStringLiteral("compile_request_sha256"),
    QStringLiteral("format"),
    QStringLiteral("format_version"),
    QStringLiteral("payload_base64"),
    QStringLiteral("payload_sha256"),
};

bool hasExactKeys(const QJsonObject &object, const QSet<QString> &expected)
{
    QSet<QString> actual;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        actual.insert(it.key());
    return actual == expected;
}

std::optional<QByteArray> decodeSha(const QJsonObject &object, QStringView key)
{
    const QJsonValue value = object.value(key);
    if (!value.isString())
        return std::nullopt;
    static const QRegularExpression lowerSha(QStringLiteral("^[0-9a-f]{64}$"));
    const QString text = value.toString();
    if (!lowerSha.match(text).hasMatch())
        return std::nullopt;
    const QByteArray result = QByteArray::fromHex(text.toLatin1());
    return result.size() == 32 ? std::optional(result) : std::nullopt;
}

QByteArray encodeRecoveryJson(
    QByteArrayView payload, const Data::RuntimePackageCompilerSha256 &compileRequestSha256)
{
    const QJsonObject root{
        {QStringLiteral("compile_request_sha256"),
         QString::fromLatin1(compileRequestSha256.value().toHex())},
        {QStringLiteral("format"), QStringLiteral("embed-labs-runtime-package-compile-recovery-v1")},
        {QStringLiteral("format_version"), 1},
        {QStringLiteral("payload_base64"),
         QString::fromLatin1(QByteArray(payload.data(), payload.size()).toBase64())},
        {QStringLiteral("payload_sha256"), QString::fromLatin1(sha256(payload).toHex())},
    };
    QByteArray result = QJsonDocument(root).toJson(QJsonDocument::Compact);
    result.append('\n');
    return result;
}

} // namespace

bool RuntimePackageCompilerCompileRecovery::isValid() const
{
    const Data::RuntimePackageCompilerProjectSnapshotEvidence &evidence
        = request.projectSnapshotEvidence;
    return request.isValid() && !serializedProject.isEmpty()
           && serializedProject.size() <= maximumProjectBytes
           && sha256(serializedProject) == evidence.serializedProjectSha256().value()
           && evidence.originalBinding()
                  == Data::runtimePackageActivationBindingToken(
                      evidence.snapshot().masterBindingArtifact);
}

Utils::Result<QByteArray> encodeRuntimePackageCompilerCompileRecovery(
    const RuntimePackageCompilerCompileRecovery &recovery)
{
    if (!recovery.isValid())
        return Utils::ResultError(QStringLiteral("Compiler recovery input is invalid."));
    const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> canonicalCompileRequest
        = Core::encodeRuntimePackageCompilerCompileRequest(recovery.request);
    if (!canonicalCompileRequest)
        return Utils::ResultError(canonicalCompileRequest.error());
    const QByteArray payload = encodePayload(recovery);
    if (payload.isEmpty())
        return Utils::ResultError(QStringLiteral("Compiler recovery payload exceeds its limits."));
    QByteArray canonicalRecovery = encodeRecoveryJson(payload, canonicalCompileRequest->sha256());
    if (canonicalRecovery.isEmpty() || canonicalRecovery.size() > maximumRecoveryBytes)
        return Utils::ResultError(QStringLiteral("Compiler recovery JSON exceeds its limits."));
    return canonicalRecovery;
}

Utils::Result<RuntimePackageCompilerCompileRecovery> decodeRuntimePackageCompilerCompileRecovery(
    QByteArrayView canonicalRecovery,
    const Data::RuntimePackageCompilerSha256 &expectedRecoverySha256)
{
    if (!expectedRecoverySha256.isValid() || canonicalRecovery.isEmpty()
        || canonicalRecovery.size() > maximumRecoveryBytes || canonicalRecovery.back() != '\n'
        || sha256(canonicalRecovery) != expectedRecoverySha256.value()) {
        return Utils::ResultError(
            QStringLiteral("Compiler recovery JSON size or ledger digest is invalid."));
    }
    const QByteArray exactBytes(canonicalRecovery.data(), canonicalRecovery.size());
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(exactBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return Utils::ResultError(QStringLiteral("Compiler recovery JSON is invalid."));
    const QJsonObject root = document.object();
    const QJsonValue version = root.value(QStringLiteral("format_version"));
    if (!hasExactKeys(root, recoveryKeys)
        || root.value(QStringLiteral("format")).toString()
               != QStringLiteral("embed-labs-runtime-package-compile-recovery-v1")
        || !version.isDouble() || version.toInteger(-1) != 1
        || !root.value(QStringLiteral("payload_base64")).isString()) {
        return Utils::ResultError(QStringLiteral("Compiler recovery JSON header is invalid."));
    }
    const std::optional<QByteArray> expectedPayloadSha
        = decodeSha(root, QStringLiteral("payload_sha256"));
    const std::optional<QByteArray> expectedCompileSha
        = decodeSha(root, QStringLiteral("compile_request_sha256"));
    if (!expectedPayloadSha || !expectedCompileSha) {
        return Utils::ResultError(QStringLiteral("Compiler recovery digests are invalid."));
    }

    const QByteArray encodedPayload
        = root.value(QStringLiteral("payload_base64")).toString().toLatin1();
    if (encodedPayload.isEmpty() || encodedPayload.size() > ((maximumPayloadBytes + 2) / 3) * 4) {
        return Utils::ResultError(QStringLiteral("Compiler recovery payload encoding is invalid."));
    }
    const QByteArray payload
        = QByteArray::fromBase64(encodedPayload, QByteArray::AbortOnBase64DecodingErrors);
    if (payload.isEmpty() || payload.size() > maximumPayloadBytes
        || payload.toBase64() != encodedPayload || sha256(payload) != *expectedPayloadSha) {
        return Utils::ResultError(QStringLiteral("Compiler recovery payload integrity is invalid."));
    }
    quint32 payloadVersion = 0;
    const Utils::Result<RuntimePackageCompilerCompileRecovery> recovery
        = decodePayload(payload, &payloadVersion);
    if (!recovery)
        return Utils::ResultError(recovery.error());
    const Utils::Result<Data::RuntimePackageCompilerCanonicalJson> canonicalCompileRequest
        = Core::encodeRuntimePackageCompilerCompileRequest(recovery->request);
    if (!canonicalCompileRequest
        || canonicalCompileRequest->sha256().value() != *expectedCompileSha) {
        return Utils::ResultError(QStringLiteral("Compiler recovery request digest is invalid."));
    }
    if (encodePayload(*recovery, payloadVersion) != payload
        || encodeRecoveryJson(payload, canonicalCompileRequest->sha256()) != exactBytes) {
        return Utils::ResultError(QStringLiteral("Compiler recovery JSON is not canonical."));
    }
    return *recovery;
}

} // namespace EtherCAT::ProjectCompiler
