// Copyright (C) 2026 Embed Labs

#include "runtimepackagecompiler.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <utility>
#include <vector>

namespace EtherCAT::Data {

namespace {

bool isCanonicalText(const QString &value, qsizetype maximumSize)
{
    if (value.isEmpty() || value.size() > maximumSize || value != value.trimmed())
        return false;
    return std::none_of(value.cbegin(), value.cend(), [](QChar character) {
        return character.isNull() || character.category() == QChar::Other_Control;
    });
}

bool isStableId(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:/-]{0,191}$"));
    return pattern.match(value).hasMatch();
}

bool isOperationId(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"));
    return pattern.match(value).hasMatch();
}

bool isSafeRelativePath(const QString &value)
{
    if (value.isEmpty() || value.size() > 256 || value.startsWith(QLatin1Char('/'))
        || value.contains(QLatin1Char('\\'))) {
        return false;
    }
    const QStringList parts = value.split(QLatin1Char('/'));
    if (parts.contains(QString()) || parts.contains(QStringLiteral(".."))
        || parts.contains(QStringLiteral("."))) {
        return false;
    }
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._/-]{0,255}$"));
    return pattern.match(value).hasMatch();
}

bool isValidSha256(const QByteArray &value)
{
    if (value.size() != QCryptographicHash::hashLength(QCryptographicHash::Sha256))
        return false;
    quint8 aggregate = 0;
    for (char byte : value)
        aggregate |= quint8(byte);
    return aggregate != 0;
}

bool isNonzeroBytes(const QByteArray &value, qsizetype exactSize)
{
    if (value.size() != exactSize)
        return false;
    quint8 aggregate = 0;
    for (char byte : value)
        aggregate |= quint8(byte);
    return aggregate != 0;
}

bool sha256Matches(const QByteArray &bytes, const RuntimePackageCompilerSha256 &expected)
{
    return !bytes.isEmpty() && expected.isValid()
           && QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) == expected.value();
}

class CanonicalJsonParser
{
public:
    explicit CanonicalJsonParser(QByteArrayView input)
        : m_input(input)
    {}

    bool parseRootObject() { return parseObject(0) && m_offset == m_input.size(); }

    std::optional<QByteArray> rootCanonicalValue(QByteArrayView key)
    {
        m_offset = 0;
        if (!consume('{') || consume('}'))
            return std::nullopt;

        std::optional<QByteArray> result;
        std::vector<quint32> previousKey;
        bool hasPreviousKey = false;
        while (true) {
            std::vector<quint32> decodedKey;
            if (!parseString(&decodedKey) || !consume(':'))
                return std::nullopt;
            if (hasPreviousKey
                && !std::lexicographical_compare(
                    previousKey.cbegin(),
                    previousKey.cend(),
                    decodedKey.cbegin(),
                    decodedKey.cend())) {
                return std::nullopt;
            }
            previousKey = decodedKey;
            hasPreviousKey = true;

            const qsizetype valueOffset = m_offset;
            if (!parseValue(1))
                return std::nullopt;
            if (decodedMatchesAscii(decodedKey, key)) {
                if (result)
                    return std::nullopt;
                result = QByteArray(m_input.data() + valueOffset, m_offset - valueOffset);
            }

            if (consume('}'))
                return m_offset == m_input.size() ? result : std::nullopt;
            if (!consume(','))
                return std::nullopt;
        }
    }

    std::optional<quint64> rootUnsignedInteger(QByteArrayView key)
    {
        const std::optional<QByteArray> value = rootCanonicalValue(key);
        if (!value || value->isEmpty()
            || !std::all_of(value->cbegin(), value->cend(), [](char character) {
                   return character >= '0' && character <= '9';
               })) {
            return std::nullopt;
        }
        bool ok = false;
        const quint64 result = value->toULongLong(&ok);
        return ok ? std::optional<quint64>{result} : std::nullopt;
    }

private:
    static constexpr int maximumDepth = 256;

    static bool decodedMatchesAscii(
        const std::vector<quint32> &decoded, QByteArrayView ascii)
    {
        if (decoded.size() != size_t(ascii.size()))
            return false;
        for (qsizetype index = 0; index < ascii.size(); ++index) {
            if (decoded.at(size_t(index)) != quint8(ascii.at(index)))
                return false;
        }
        return true;
    }

    bool atEnd() const { return m_offset >= m_input.size(); }

    char current() const { return atEnd() ? '\0' : m_input[m_offset]; }

    bool consume(char expected)
    {
        if (current() != expected)
            return false;
        ++m_offset;
        return true;
    }

    bool consumeLiteral(QByteArrayView literal)
    {
        if (m_offset + literal.size() > m_input.size()
            || m_input.sliced(m_offset, literal.size()) != literal) {
            return false;
        }
        m_offset += literal.size();
        return true;
    }

    static int lowerHexValue(char character)
    {
        if (character >= '0' && character <= '9')
            return character - '0';
        if (character >= 'a' && character <= 'f')
            return character - 'a' + 10;
        return -1;
    }

    bool parseHexCodeUnit(quint16 *codeUnit)
    {
        if (m_offset + 4 > m_input.size())
            return false;
        quint16 value = 0;
        for (int index = 0; index < 4; ++index) {
            const int nibble = lowerHexValue(m_input[m_offset + index]);
            if (nibble < 0)
                return false;
            value = quint16((value << 4) | quint16(nibble));
        }
        m_offset += 4;
        *codeUnit = value;
        return true;
    }

    bool parseUnicodeEscape(std::vector<quint32> *decoded)
    {
        quint16 first = 0;
        if (!parseHexCodeUnit(&first))
            return false;

        // Python's ensure_ascii encoder uses the short escapes below and
        // emits printable ASCII literally. Accepting their \u spellings would
        // therefore admit more than one encoding for the same JSON string.
        if (first <= 0x1f) {
            switch (first) {
            case '\b':
            case '\t':
            case '\n':
            case '\f':
            case '\r':
                return false;
            default:
                break;
            }
        } else if (first >= 0x20 && first <= 0x7e) {
            return false;
        }

        quint32 codePoint = first;
        // json.loads combines an adjacent UTF-16 pair but preserves an
        // unpaired surrogate, and json.dumps reproduces either form.
        if (first >= 0xd800 && first <= 0xdbff && m_offset + 6 <= m_input.size()
            && m_input[m_offset] == '\\' && m_input[m_offset + 1] == 'u') {
            quint16 second = 0;
            bool secondIsLowerHex = true;
            for (int index = 0; index < 4; ++index) {
                const int nibble = lowerHexValue(m_input[m_offset + 2 + index]);
                if (nibble < 0) {
                    secondIsLowerHex = false;
                    break;
                }
                second = quint16((second << 4) | quint16(nibble));
            }
            if (secondIsLowerHex && second >= 0xdc00 && second <= 0xdfff) {
                m_offset += 6;
                codePoint = 0x10000 + ((quint32(first) - 0xd800) << 10)
                            + (quint32(second) - 0xdc00);
            }
        }

        if (decoded)
            decoded->push_back(codePoint);
        return true;
    }

    bool parseString(std::vector<quint32> *decoded = nullptr)
    {
        if (!consume('"'))
            return false;
        while (!atEnd()) {
            const unsigned char character = static_cast<unsigned char>(current());
            if (character == '"') {
                ++m_offset;
                return true;
            }
            if (character == '\\') {
                ++m_offset;
                if (atEnd())
                    return false;
                const char escaped = current();
                ++m_offset;
                quint32 codePoint = 0;
                switch (escaped) {
                case '"':
                    codePoint = '"';
                    break;
                case '\\':
                    codePoint = '\\';
                    break;
                case 'b':
                    codePoint = '\b';
                    break;
                case 't':
                    codePoint = '\t';
                    break;
                case 'n':
                    codePoint = '\n';
                    break;
                case 'f':
                    codePoint = '\f';
                    break;
                case 'r':
                    codePoint = '\r';
                    break;
                case 'u':
                    if (!parseUnicodeEscape(decoded))
                        return false;
                    continue;
                default:
                    return false;
                }
                if (decoded)
                    decoded->push_back(codePoint);
                continue;
            }
            // ensure_ascii emits only printable ASCII directly. DEL and every
            // non-ASCII code point use a lowercase \u escape.
            if (character < 0x20 || character >= 0x7f)
                return false;
            ++m_offset;
            if (decoded)
                decoded->push_back(character);
        }
        return false;
    }

    bool parseInteger()
    {
        if (consume('-')) {
            if (current() < '1' || current() > '9')
                return false;
        } else if (consume('0')) {
            return true;
        } else if (current() < '1' || current() > '9') {
            return false;
        }

        ++m_offset;
        while (current() >= '0' && current() <= '9')
            ++m_offset;
        return true;
    }

    bool parseArray(int depth)
    {
        if (depth > maximumDepth || !consume('['))
            return false;
        if (consume(']'))
            return true;
        while (parseValue(depth + 1)) {
            if (consume(']'))
                return true;
            if (!consume(','))
                return false;
        }
        return false;
    }

    bool parseObject(int depth)
    {
        if (depth > maximumDepth || !consume('{'))
            return false;
        if (consume('}'))
            return true;

        std::vector<quint32> previousKey;
        bool hasPreviousKey = false;
        while (true) {
            std::vector<quint32> key;
            if (!parseString(&key))
                return false;
            if (hasPreviousKey
                && !std::lexicographical_compare(
                    previousKey.cbegin(), previousKey.cend(), key.cbegin(), key.cend())) {
                return false;
            }
            previousKey = std::move(key);
            hasPreviousKey = true;

            if (!consume(':') || !parseValue(depth + 1))
                return false;
            if (consume('}'))
                return true;
            if (!consume(','))
                return false;
        }
    }

    bool parseValue(int depth)
    {
        if (depth > maximumDepth)
            return false;
        switch (current()) {
        case '{':
            return parseObject(depth);
        case '[':
            return parseArray(depth);
        case '"':
            return parseString();
        case 't':
            return consumeLiteral(QByteArrayView("true"));
        case 'f':
            return consumeLiteral(QByteArrayView("false"));
        case 'n':
            return consumeLiteral(QByteArrayView("null"));
        default:
            return current() == '-' || (current() >= '0' && current() <= '9') ? parseInteger()
                                                                              : false;
        }
    }

    QByteArrayView m_input;
    qsizetype m_offset = 0;
};

bool commandIsKnown(RuntimePackageCompilerCommand command)
{
    switch (command) {
    case RuntimePackageCompilerCommand::Compile:
    case RuntimePackageCompilerCommand::Finalize:
    case RuntimePackageCompilerCommand::Query:
    case RuntimePackageCompilerCommand::Verify:
        return true;
    case RuntimePackageCompilerCommand::Unknown:
        return false;
    }
    return false;
}

bool statusIsTransportable(RuntimePackageCompilerResultStatus status)
{
    switch (status) {
    case RuntimePackageCompilerResultStatus::Unknown:
    case RuntimePackageCompilerResultStatus::Succeeded:
    case RuntimePackageCompilerResultStatus::DomainFailed:
    case RuntimePackageCompilerResultStatus::Canceled:
        return true;
    }
    return false;
}

bool diagnosticCategoryIsTransportable(RuntimePackageCompilerDiagnosticCategory category)
{
    switch (category) {
    case RuntimePackageCompilerDiagnosticCategory::Unknown:
    case RuntimePackageCompilerDiagnosticCategory::Input:
    case RuntimePackageCompilerDiagnosticCategory::Topology:
    case RuntimePackageCompilerDiagnosticCategory::Esi:
    case RuntimePackageCompilerDiagnosticCategory::Slave:
    case RuntimePackageCompilerDiagnosticCategory::Pdo:
    case RuntimePackageCompilerDiagnosticCategory::Sdo:
    case RuntimePackageCompilerDiagnosticCategory::Dc:
    case RuntimePackageCompilerDiagnosticCategory::Adapter:
    case RuntimePackageCompilerDiagnosticCategory::Capability:
    case RuntimePackageCompilerDiagnosticCategory::Timing:
    case RuntimePackageCompilerDiagnosticCategory::Signing:
    case RuntimePackageCompilerDiagnosticCategory::Configuration:
    case RuntimePackageCompilerDiagnosticCategory::Security:
    case RuntimePackageCompilerDiagnosticCategory::Canceled:
    case RuntimePackageCompilerDiagnosticCategory::Internal:
    case RuntimePackageCompilerDiagnosticCategory::Path:
    case RuntimePackageCompilerDiagnosticCategory::Idempotency:
        return true;
    }
    return false;
}

bool diagnosticSeverityIsKnown(RuntimePackageCompilerDiagnosticSeverity severity)
{
    switch (severity) {
    case RuntimePackageCompilerDiagnosticSeverity::Information:
    case RuntimePackageCompilerDiagnosticSeverity::Warning:
    case RuntimePackageCompilerDiagnosticSeverity::Error:
        return true;
    }
    return false;
}

bool diagnosticStageIsKnown(const QString &stage)
{
    static const QSet<QString> stages{
        QStringLiteral("input"),
        QStringLiteral("topology"),
        QStringLiteral("esi"),
        QStringLiteral("adapter"),
        QStringLiteral("pdo"),
        QStringLiteral("sdo"),
        QStringLiteral("dc"),
        QStringLiteral("capability"),
        QStringLiteral("timing"),
        QStringLiteral("configuration"),
        QStringLiteral("signing"),
        QStringLiteral("security"),
        QStringLiteral("cancelled"),
        QStringLiteral("internal"),
    };
    return stages.contains(stage);
}

bool sourceArtifactKindIsKnown(RuntimePackageCompilerSourceArtifactKind kind)
{
    switch (kind) {
    case RuntimePackageCompilerSourceArtifactKind::TopologyEvidence:
    case RuntimePackageCompilerSourceArtifactKind::TargetProfile:
    case RuntimePackageCompilerSourceArtifactKind::TargetProfileSignature:
    case RuntimePackageCompilerSourceArtifactKind::ProductionPublicKey:
    case RuntimePackageCompilerSourceArtifactKind::AdapterBundle:
    case RuntimePackageCompilerSourceArtifactKind::PolicyTemplate:
    case RuntimePackageCompilerSourceArtifactKind::ControllerFeatures:
    case RuntimePackageCompilerSourceArtifactKind::RuntimeSource:
    case RuntimePackageCompilerSourceArtifactKind::OriginalEsi:
    case RuntimePackageCompilerSourceArtifactKind::AdapterSourceFile:
        return true;
    case RuntimePackageCompilerSourceArtifactKind::Unknown:
        return false;
    }
    return false;
}

bool sourceArtifactReferenceIsValid(const RuntimePackageCompilerSourceArtifact &artifact)
{
    return sourceArtifactKindIsKnown(artifact.kind) && isSafeRelativePath(artifact.relativePath)
           && !artifact.exactBytes.isEmpty() && artifact.exactBytes.size() <= 16 * 1024 * 1024
           && artifact.sha256.isValid();
}

bool sourceArtifactReferencesAreValid(const RuntimePackageCompilerSourceArtifacts &sources)
{
    const QList<const RuntimePackageCompilerSourceArtifact *> artifacts{
        &sources.topologyEvidence,
        &sources.targetProfile,
        &sources.targetProfileSignature,
        &sources.productionPublicKey,
        &sources.adapterBundle,
        &sources.policyTemplate,
        &sources.controllerFeatures,
        &sources.runtimeSource,
    };
    QSet<QString> paths;
    for (const RuntimePackageCompilerSourceArtifact *artifact : artifacts) {
        if (!sourceArtifactReferenceIsValid(*artifact) || paths.contains(artifact->relativePath))
            return false;
        paths.insert(artifact->relativePath);
    }
    return sources.topologyEvidence.kind
               == RuntimePackageCompilerSourceArtifactKind::TopologyEvidence
           && sources.targetProfile.kind
                  == RuntimePackageCompilerSourceArtifactKind::TargetProfile
           && sources.targetProfileSignature.kind
                  == RuntimePackageCompilerSourceArtifactKind::TargetProfileSignature
           && sources.productionPublicKey.kind
                  == RuntimePackageCompilerSourceArtifactKind::ProductionPublicKey
           && sources.adapterBundle.kind
                  == RuntimePackageCompilerSourceArtifactKind::AdapterBundle
           && sources.policyTemplate.kind
                  == RuntimePackageCompilerSourceArtifactKind::PolicyTemplate
           && sources.controllerFeatures.kind
                  == RuntimePackageCompilerSourceArtifactKind::ControllerFeatures
           && sources.runtimeSource.kind
                  == RuntimePackageCompilerSourceArtifactKind::RuntimeSource;
}

bool pdoDirectionIsKnown(RuntimePackageCompilerPdoDirection direction)
{
    return direction == RuntimePackageCompilerPdoDirection::Input
           || direction == RuntimePackageCompilerPdoDirection::Output;
}

bool startupStageIsKnown(RuntimePackageCompilerStartupStage stage)
{
    return stage == RuntimePackageCompilerStartupStage::PreOperational
           || stage == RuntimePackageCompilerStartupStage::SafeOperational
           || stage == RuntimePackageCompilerStartupStage::Operational;
}

bool startupFailureActionIsKnown(RuntimePackageCompilerStartupFailureAction action)
{
    return action == RuntimePackageCompilerStartupFailureAction::Abort
           || action == RuntimePackageCompilerStartupFailureAction::Warn
           || action == RuntimePackageCompilerStartupFailureAction::Continue;
}

bool manualRecoveryActionIsKnown(RuntimePackageCompilerManualRecoveryAction action)
{
    return action == RuntimePackageCompilerManualRecoveryAction::HoldSafe
           || action == RuntimePackageCompilerManualRecoveryAction::ReturnToTask
           || action == RuntimePackageCompilerManualRecoveryAction::Stop;
}

bool symbolModeIsKnown(RuntimePackageCompilerSymbolMode mode)
{
    return mode == RuntimePackageCompilerSymbolMode::ReportOnly
           || mode == RuntimePackageCompilerSymbolMode::Requested
           || mode == RuntimePackageCompilerSymbolMode::All;
}

bool stableValues(const QMap<QString, QString> &values)
{
    return std::all_of(values.cbegin(), values.cend(), [](const QString &value) {
        return isStableId(value);
    });
}

bool componentBindingKeysAreValid(const QMap<QString, QString> &values)
{
    return std::all_of(values.keyBegin(), values.keyEnd(), [](const QString &key) {
        if (key.isEmpty())
            return false;
        return std::all_of(key.cbegin(), key.cend(), [](QChar character) {
            return character.isDigit();
        });
    });
}

QString compilerDataType(EtherCATDataType type, const QString &rawType)
{
    switch (type) {
    case EtherCATDataType::Boolean:
        return QStringLiteral("BOOL");
    case EtherCATDataType::Integer8:
        return QStringLiteral("SINT");
    case EtherCATDataType::UnsignedInteger8:
        return QStringLiteral("USINT");
    case EtherCATDataType::Integer16:
        return QStringLiteral("INT");
    case EtherCATDataType::UnsignedInteger16:
        return QStringLiteral("UINT");
    case EtherCATDataType::Integer32:
        return QStringLiteral("DINT");
    case EtherCATDataType::UnsignedInteger32:
        return QStringLiteral("UDINT");
    case EtherCATDataType::Integer64:
        return QStringLiteral("LINT");
    case EtherCATDataType::UnsignedInteger64:
        return QStringLiteral("ULINT");
    case EtherCATDataType::Real32:
        return QStringLiteral("REAL");
    case EtherCATDataType::Real64:
        return QStringLiteral("LREAL");
    case EtherCATDataType::VisibleString:
        return QStringLiteral("STRING");
    case EtherCATDataType::OctetString:
        return QStringLiteral("OCTET_STRING");
    case EtherCATDataType::Unknown:
        return rawType;
    }
    return {};
}

bool pdoProjectionMatches(
    const QList<RuntimePackageCompilerPdoMapping> &projection,
    const ProcessDataConfiguration &configuration)
{
    QList<const PdoConfiguration *> selected;
    for (const PdoConfiguration &pdo : configuration.pdos) {
        if (pdo.selected)
            selected.append(&pdo);
    }
    if (selected.size() != projection.size())
        return false;
    for (qsizetype mappingIndex = 0; mappingIndex < projection.size(); ++mappingIndex) {
        const RuntimePackageCompilerPdoMapping &typed = projection.at(mappingIndex);
        const PdoConfiguration &current = *selected.at(mappingIndex);
        const RuntimePackageCompilerPdoDirection currentDirection
            = current.direction == PdoDirection::Tx ? RuntimePackageCompilerPdoDirection::Input
                                                    : RuntimePackageCompilerPdoDirection::Output;
        if (typed.direction != currentDirection || typed.pdoIndex != current.index
            || typed.syncManager != current.syncManager || typed.fixed != current.fixed
            || typed.entries.size() != current.entries.size()) {
            return false;
        }
        for (qsizetype entryIndex = 0; entryIndex < typed.entries.size(); ++entryIndex) {
            const RuntimePackageCompilerPdoEntry &projected = typed.entries.at(entryIndex);
            const PdoEntryConfiguration &entry = current.entries.at(entryIndex);
            if (projected.index != entry.index || projected.subIndex != entry.subIndex
                || projected.bitLength != entry.bitLength
                || projected.dataType != compilerDataType(entry.dataType, entry.rawDataType)) {
                return false;
            }
        }
    }
    return true;
}

QString startupStageName(RuntimePackageCompilerStartupStage stage)
{
    switch (stage) {
    case RuntimePackageCompilerStartupStage::PreOperational:
        return QStringLiteral("preop");
    case RuntimePackageCompilerStartupStage::SafeOperational:
        return QStringLiteral("safeop");
    case RuntimePackageCompilerStartupStage::Operational:
        return QStringLiteral("op");
    case RuntimePackageCompilerStartupStage::Unknown:
        return {};
    }
    return {};
}

QByteArray startupValueBytes(const RuntimePackageCompilerStartupSdo &sdo)
{
    if (sdo.valueBytes == 0 || sdo.valueBytes > 8)
        return {};
    quint64 bits = std::holds_alternative<qint64>(sdo.value) ? quint64(std::get<qint64>(sdo.value))
                                                             : std::get<quint64>(sdo.value);
    QByteArray result(sdo.valueBytes, '\0');
    for (qsizetype index = result.size(); index > 0; --index) {
        result[index - 1] = char(bits & 0xff);
        bits >>= 8;
    }
    return result;
}

bool startupProjectionMatches(
    const QList<RuntimePackageCompilerStartupSdo> &projection,
    const StartupConfiguration &configuration)
{
    if (projection.size() != configuration.parameters.size())
        return false;
    for (qsizetype index = 0; index < projection.size(); ++index) {
        const RuntimePackageCompilerStartupSdo &projected = projection.at(index);
        const StartupParameterConfiguration &current = configuration.parameters.at(index);
        if (projected.sequence != current.order || projected.enabled != current.enabled
            || startupStageName(projected.stage) != current.transition
            || projected.index != current.index || projected.subIndex != current.subIndex
            || startupValueBytes(projected) != current.rawValue) {
            return false;
        }
    }
    return true;
}

bool dcProjectionMatches(
    const RuntimePackageCompilerDcProjection &projection, const DcConfiguration &configuration)
{
    const std::optional<QString> currentMode = configuration.modeName.isEmpty()
                                                   ? std::nullopt
                                                   : std::optional<QString>{configuration.modeName};
    return projection.enabled == configuration.enabled && projection.mode == currentMode
           && projection.assignActivate == configuration.assignActivate
           && projection.sync0CycleNs == quint64(configuration.sync0.cycleTimeNs)
           && projection.sync0ShiftNs == configuration.sync0.shiftTimeNs
           && projection.sync1CycleNs == quint64(configuration.sync1.cycleTimeNs)
           && projection.sync1ShiftNs == configuration.sync1.shiftTimeNs
           && projection.referenceClock == configuration.potentialReferenceClock;
}

bool projectContainsMaster(const ProjectSnapshot &project, const NodeId &masterId)
{
    return std::any_of(project.nodes.cbegin(), project.nodes.cend(), [&masterId](const auto &node) {
        return node.id == masterId && node.kind == ProjectNodeKind::Master;
    });
}

const RuntimePackageCompilerDeviceSourceEvidence *findDeviceSourceEvidence(
    const QList<RuntimePackageCompilerDeviceSourceEvidence> &sources,
    const NodeId &projectSlaveNodeId)
{
    const auto found
        = std::find_if(sources.cbegin(), sources.cend(), [&projectSlaveNodeId](const auto &source) {
              return source.projectSlaveNodeId == projectSlaveNodeId;
          });
    return found == sources.cend() ? nullptr : &*found;
}

bool resultHasErrorDiagnostic(const RuntimePackageCompilerResultEnvelope &envelope)
{
    return std::any_of(
        envelope.diagnostics.cbegin(),
        envelope.diagnostics.cend(),
        [](const RuntimePackageCompilerDiagnostic &diagnostic) {
            return diagnostic.severity == RuntimePackageCompilerDiagnosticSeverity::Error;
        });
}

bool backendStatusMatches(
    RuntimePackageCompilerCommand command,
    RuntimePackageCompilerResultStatus status,
    const QString &backendStatus)
{
    if (status == RuntimePackageCompilerResultStatus::Succeeded) {
        switch (command) {
        case RuntimePackageCompilerCommand::Compile:
            return backendStatus == QStringLiteral("awaiting_signature");
        case RuntimePackageCompilerCommand::Finalize:
            return backendStatus == QStringLiteral("complete");
        case RuntimePackageCompilerCommand::Query:
            return backendStatus == QStringLiteral("state");
        case RuntimePackageCompilerCommand::Verify:
            return backendStatus == QStringLiteral("pass");
        case RuntimePackageCompilerCommand::Unknown:
            return false;
        }
    }
    if (status == RuntimePackageCompilerResultStatus::DomainFailed)
        return backendStatus == QStringLiteral("fail");
    if (status == RuntimePackageCompilerResultStatus::Canceled)
        return backendStatus == QStringLiteral("cancelled");
    return status == RuntimePackageCompilerResultStatus::Unknown;
}

QJsonObject canonicalObject(const RuntimePackageCompilerCanonicalJson &json)
{
    return QJsonDocument::fromJson(json.exactBytes()).object();
}

bool objectHasExactKeys(const QJsonObject &object, const QSet<QString> &expected)
{
    if (object.size() != expected.size())
        return false;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!expected.contains(it.key()))
            return false;
    }
    return true;
}

bool jsonSha256Matches(
    const QJsonObject &object, const QString &key, const RuntimePackageCompilerSha256 &sha256)
{
    return sha256.isValid()
           && object.value(key).toString() == QString::fromLatin1(sha256.value().toHex());
}

bool jsonUnsignedIntegerMatches(
    const RuntimePackageCompilerCanonicalJson &json, const QByteArray &key, quint64 expected)
{
    if (!json.isValid())
        return false;
    const QByteArray &bytes = json.exactBytes();
    const auto actual = CanonicalJsonParser(
                            QByteArrayView(bytes.constData(), bytes.size() - 1))
                            .rootUnsignedInteger(QByteArrayView(key));
    return actual && *actual == expected;
}

bool jsonSha256IsValid(const QJsonObject &object, const QString &key)
{
    const QString value = object.value(key).toString();
    const RuntimePackageCompilerSha256 sha256{QByteArray::fromHex(value.toLatin1())};
    return sha256.isValid() && value == QString::fromLatin1(sha256.value().toHex());
}

QByteArray detachedSigningReceiptProjection(const QJsonObject &object, quint64 signingPolicyRevision)
{
    QByteArray result{"{\"format\":\"ethercat-ecpkg-sign-response-v1\",\"format_version\":1,"};
    result += "\"manifest_sha256\":\"";
    result += object.value(QStringLiteral("manifest_sha256")).toString().toLatin1();
    result += "\",\"operation_id\":\"";
    result += object.value(QStringLiteral("operation_id")).toString().toLatin1();
    result += "\",\"policy_revision\":";
    result += QByteArray::number(signingPolicyRevision);
    result += ",\"request_sha256\":\"";
    result += object.value(QStringLiteral("request_sha256")).toString().toLatin1();
    result += "\",\"signature_hex\":\"";
    result += object.value(QStringLiteral("signature_hex")).toString().toLatin1();
    result += "\",\"signing_key_id\":\"";
    result += object.value(QStringLiteral("signing_key_id")).toString().toLatin1();
    result += "\"}\n";
    return result;
}

bool detachedSigningRequestMatches(
    const RuntimePackageCompilerCanonicalJson &canonical,
    const RuntimePackageCompilerOperationId &operationId,
    const RuntimePackageCompilerSha256 &manifestSha256,
    const RuntimePackageCompilerSha256 &intentSha256,
    const RuntimePackageCompilerSha256 &targetProfileSha256,
    const RuntimePackageCompilerSha256 &effectiveProjectCompanionSha256)
{
    const QJsonObject object = canonicalObject(canonical);
    static const QSet<QString> expectedKeys{
        QStringLiteral("effective_project_companion_sha256"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("intent_sha256"),
        QStringLiteral("manifest_sha256"),
        QStringLiteral("operation_id"),
        QStringLiteral("policy_revision"),
        QStringLiteral("signing_key_id"),
        QStringLiteral("target_profile_sha256"),
    };
    const QByteArray &bytes = canonical.exactBytes();
    const auto policyRevision = CanonicalJsonParser(
                                    QByteArrayView(bytes.constData(), bytes.size() - 1))
                                    .rootUnsignedInteger(QByteArrayView("policy_revision"));
    return objectHasExactKeys(object, expectedKeys)
           && object.value(QStringLiteral("format")).toString()
                  == QStringLiteral("ethercat-ecpkg-sign-request-v1")
           && object.value(QStringLiteral("format_version")).toInt() == 1
           && object.value(QStringLiteral("operation_id")).toString() == operationId.value()
           && jsonSha256Matches(object, QStringLiteral("manifest_sha256"), manifestSha256)
           && jsonSha256Matches(object, QStringLiteral("intent_sha256"), intentSha256)
           && jsonSha256Matches(object, QStringLiteral("target_profile_sha256"), targetProfileSha256)
           && jsonSha256Matches(
               object,
               QStringLiteral("effective_project_companion_sha256"),
               effectiveProjectCompanionSha256)
           && jsonSha256IsValid(object, QStringLiteral("signing_key_id")) && policyRevision
           && *policyRevision != 0;
}

bool detachedSigningResponseMatches(
    const RuntimePackageCompilerCanonicalJson &canonical,
    const RuntimePackageCompilerOperationId &operationId,
    const RuntimePackageCompilerSha256 &signRequestSha256,
    const RuntimePackageCompilerSha256 &manifestSha256,
    const RuntimePackageCompilerSha256 &signingKeyIdSha256,
    quint64 signingPolicyRevision)
{
    const QJsonObject object = canonicalObject(canonical);
    static const QSet<QString> expectedKeys{
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("manifest_sha256"),
        QStringLiteral("operation_id"),
        QStringLiteral("policy_revision"),
        QStringLiteral("receipt_sha256"),
        QStringLiteral("request_sha256"),
        QStringLiteral("signature_hex"),
        QStringLiteral("signing_key_id"),
    };
    const QString signature = object.value(QStringLiteral("signature_hex")).toString();
    const bool signatureIsLowerHex
        = signature.size() == 128
          && std::all_of(signature.cbegin(), signature.cend(), [](QChar character) {
                 return (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
                        || (character >= QLatin1Char('a') && character <= QLatin1Char('f'));
             });
    const QByteArray receiptProjection
        = detachedSigningReceiptProjection(object, signingPolicyRevision);
    const QString expectedReceipt = QString::fromLatin1(
        QCryptographicHash::hash(receiptProjection, QCryptographicHash::Sha256).toHex());
    return objectHasExactKeys(object, expectedKeys)
           && object.value(QStringLiteral("format")).toString()
                  == QStringLiteral("ethercat-ecpkg-sign-response-v1")
           && object.value(QStringLiteral("format_version")).toInt() == 1
           && object.value(QStringLiteral("operation_id")).toString() == operationId.value()
           && jsonUnsignedIntegerMatches(
               canonical, QByteArray("policy_revision"), signingPolicyRevision)
           && jsonSha256Matches(object, QStringLiteral("request_sha256"), signRequestSha256)
           && jsonSha256Matches(object, QStringLiteral("manifest_sha256"), manifestSha256)
           && jsonSha256Matches(object, QStringLiteral("signing_key_id"), signingKeyIdSha256)
           && jsonSha256IsValid(object, QStringLiteral("receipt_sha256"))
           && object.value(QStringLiteral("receipt_sha256")).toString() == expectedReceipt
           && signatureIsLowerHex;
}

bool canonicalTopologyMatchesTyped(
    const RuntimePackageCompilerFreshTopologyEvidence &topology)
{
    const QJsonObject evidence = canonicalObject(topology.canonicalEvidence);
    static const QSet<QString> evidenceKeys{
        QStringLiteral("capture_boot_id"),
        QStringLiteral("capture_sequence"),
        QStringLiteral("captured_at_ns"),
        QStringLiteral("evidence_id"),
        QStringLiteral("expires_at_ns"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("matched_scan"),
    };
    if (!objectHasExactKeys(evidence, evidenceKeys)
        || evidence.value(QStringLiteral("format")).toString()
               != QStringLiteral("ethercat-discover-topology-evidence-v1")
        || evidence.value(QStringLiteral("format_version")).toInt() != 1
        || evidence.value(QStringLiteral("evidence_id")).toString() != topology.evidenceId
        || evidence.value(QStringLiteral("capture_boot_id")).toString()
               != QStringLiteral("0x%1").arg(
                   topology.captureBootId, 16, 16, QLatin1Char('0'))
        || !jsonUnsignedIntegerMatches(
            topology.canonicalEvidence,
            QByteArray("capture_sequence"),
            topology.captureSequence)
        || !jsonUnsignedIntegerMatches(
            topology.canonicalEvidence,
            QByteArray("captured_at_ns"),
            topology.capturedAtNs)
        || !jsonUnsignedIntegerMatches(
            topology.canonicalEvidence,
            QByteArray("expires_at_ns"),
            topology.expiresAtNs)) {
        return false;
    }

    const QJsonObject matched = evidence.value(QStringLiteral("matched_scan")).toObject();
    static const QSet<QString> matchedKeys{
        QStringLiteral("cycle_period_ns"),
        QStringLiteral("format"),
        QStringLiteral("link_speed_mbps"),
        QStringLiteral("slaves"),
    };
    if (!objectHasExactKeys(matched, matchedKeys)
        || matched.value(QStringLiteral("format")).toString()
               != QStringLiteral("ethercat-esi-match-v1")
        || matched.value(QStringLiteral("cycle_period_ns")).toInteger(-1)
               != qint64(topology.cyclePeriodNs)
        || matched.value(QStringLiteral("link_speed_mbps")).toInteger(-1)
               != qint64(topology.linkSpeedMbps)) {
        return false;
    }

    const QJsonArray slaves = matched.value(QStringLiteral("slaves")).toArray();
    if (slaves.size() != topology.slaves.size())
        return false;
    for (qsizetype slaveIndex = 0; slaveIndex < topology.slaves.size(); ++slaveIndex) {
        const RuntimePackageCompilerTopologySlaveEvidence &typed
            = topology.slaves.at(slaveIndex);
        const QJsonObject observed = slaves.at(slaveIndex).toObject();
        const QJsonObject identity = observed.value(QStringLiteral("identity")).toObject();
        static const QSet<QString> identityKeys{
            QStringLiteral("product_code"),
            QStringLiteral("revision"),
            QStringLiteral("vendor_id"),
        };
        if (!objectHasExactKeys(identity, identityKeys)
            || observed.value(QStringLiteral("position")).toInteger(-1) != typed.position
            || observed.value(QStringLiteral("station_address")).toInteger(-1)
                   != typed.stationAddress
            || observed.value(QStringLiteral("alias")).toInteger(-1) != typed.alias
            || observed.value(QStringLiteral("serial")).toInteger(-1) != typed.serialNumber
            || identity.value(QStringLiteral("vendor_id")).toInteger(-1)
                   != typed.identity.vendorId
            || identity.value(QStringLiteral("product_code")).toInteger(-1)
                   != typed.identity.productCode
            || identity.value(QStringLiteral("revision")).toInteger(-1)
                   != typed.identity.revisionNumber) {
            return false;
        }

        const QJsonArray modules = observed.value(QStringLiteral("modules")).toArray();
        if (modules.size() != typed.moduleAssignments.size())
            return false;
        for (qsizetype moduleIndex = 0;
             moduleIndex < typed.moduleAssignments.size();
             ++moduleIndex) {
            const QJsonObject module = modules.at(moduleIndex).toObject();
            const DeviceModuleAssignment &assignment
                = typed.moduleAssignments.at(moduleIndex);
            if (module.value(QStringLiteral("slot")).toInteger(-1) != assignment.slot
                || module.value(QStringLiteral("module_ident")).toInteger(-1)
                       != assignment.moduleIdent) {
                return false;
            }
        }
    }
    return true;
}

bool canonicalTargetProfileMatchesTyped(
    const RuntimePackageCompilerSignedTargetProfileEvidence &target)
{
    const QJsonObject profile = canonicalObject(target.canonicalProfile);
    static const QSet<QString> profileKeys{
        QStringLiteral("adapter_bundle_sha256"),
        QStringLiteral("capability"),
        QStringLiteral("capability_descriptor_sha256"),
        QStringLiteral("controller_features_sha256"),
        QStringLiteral("cpu1_abi_version"),
        QStringLiteral("cycle_periods_ns"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
        QStringLiteral("fpga_abi_version"),
        QStringLiteral("limits"),
        QStringLiteral("manifest_versions"),
        QStringLiteral("policy_revision"),
        QStringLiteral("profile_id"),
        QStringLiteral("signing_key_id"),
    };
    return objectHasExactKeys(profile, profileKeys)
           && profile.value(QStringLiteral("format")).toString()
                  == QStringLiteral("ethercat-target-capability-profile-v1")
           && profile.value(QStringLiteral("format_version")).toInt() == 1
           && profile.value(QStringLiteral("profile_id")).toString() == target.profileId
           && jsonUnsignedIntegerMatches(
               target.canonicalProfile,
               QByteArray("policy_revision"),
               target.policyRevision)
           && jsonUnsignedIntegerMatches(
               target.canonicalProfile,
               QByteArray("cpu1_abi_version"),
               target.cpu1AbiVersion)
           && jsonUnsignedIntegerMatches(
               target.canonicalProfile,
               QByteArray("fpga_abi_version"),
               target.fpgaAbiVersion)
           && jsonSha256Matches(
               profile,
               QStringLiteral("capability_descriptor_sha256"),
               target.capabilityDescriptorSha256)
           && jsonSha256Matches(
               profile,
               QStringLiteral("controller_features_sha256"),
               target.controllerFeaturesSha256)
           && jsonSha256Matches(
               profile,
               QStringLiteral("adapter_bundle_sha256"),
               target.adapterBundleSha256)
           && jsonSha256Matches(
               profile, QStringLiteral("signing_key_id"), target.signingKeyIdSha256)
           && !profile.value(QStringLiteral("manifest_versions")).toArray().isEmpty()
           && profile.value(QStringLiteral("manifest_versions")).toArray().contains(2)
           && !profile.value(QStringLiteral("cycle_periods_ns")).toArray().isEmpty()
           && !profile.value(QStringLiteral("limits")).toObject().isEmpty()
           && !profile.value(QStringLiteral("capability")).toObject().isEmpty();
}

bool adapterBundleBindsDeviceSources(
    const RuntimePackageCompilerSourceArtifact &bundleArtifact,
    const QList<RuntimePackageCompilerDeviceSourceEvidence> &deviceSources)
{
    const RuntimePackageCompilerCanonicalJson bundleJson(
        bundleArtifact.exactBytes, bundleArtifact.sha256);
    if (!bundleJson.isValid())
        return false;

    const QJsonObject bundle = canonicalObject(bundleJson);
    static const QSet<QString> bundleKeys{
        QStringLiteral("bundle_id"),
        QStringLiteral("entries"),
        QStringLiteral("format"),
        QStringLiteral("format_version"),
    };
    if (!objectHasExactKeys(bundle, bundleKeys)
        || bundle.value(QStringLiteral("format")).toString()
               != QStringLiteral("ethercat-lower-adapter-bundle-v1")
        || bundle.value(QStringLiteral("format_version")).toInt() != 1
        || !isStableId(bundle.value(QStringLiteral("bundle_id")).toString())) {
        return false;
    }

    const QJsonArray entries = bundle.value(QStringLiteral("entries")).toArray();
    if (entries.isEmpty() || entries.size() > 256)
        return false;

    QHash<QString, QJsonObject> entriesByAdapter;
    QSet<QString> relativePaths;
    QString previousAdapterId;
    QString previousAdapterVersion;
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        static const QSet<QString> entryKeys{
            QStringLiteral("adapter_id"),
            QStringLiteral("adapter_version"),
            QStringLiteral("bytes"),
            QStringLiteral("canonical_sha256"),
            QStringLiteral("file_sha256"),
            QStringLiteral("relative_path"),
        };
        const QString adapterId = entry.value(QStringLiteral("adapter_id")).toString();
        const QString adapterVersion
            = entry.value(QStringLiteral("adapter_version")).toString();
        const QString relativePath = entry.value(QStringLiteral("relative_path")).toString();
        const qint64 byteCount = entry.value(QStringLiteral("bytes")).toInteger(-1);
        const RuntimePackageCompilerSha256 fileSha256{
            QByteArray::fromHex(
                entry.value(QStringLiteral("file_sha256")).toString().toLatin1())};
        const RuntimePackageCompilerSha256 canonicalSha256{
            QByteArray::fromHex(
                entry.value(QStringLiteral("canonical_sha256")).toString().toLatin1())};
        const QString adapterKey = adapterId + QChar::Null + adapterVersion;
        if (!objectHasExactKeys(entry, entryKeys) || !isStableId(adapterId)
            || !isCanonicalText(adapterVersion, 64) || !isSafeRelativePath(relativePath)
            || byteCount < 1 || byteCount > 1024 * 1024 || !fileSha256.isValid()
            || !canonicalSha256.isValid()
            || entry.value(QStringLiteral("file_sha256")).toString()
                   != QString::fromLatin1(fileSha256.value().toHex())
            || entry.value(QStringLiteral("canonical_sha256")).toString()
                   != QString::fromLatin1(canonicalSha256.value().toHex())
            || relativePaths.contains(relativePath) || entriesByAdapter.contains(adapterKey)
            || (!previousAdapterId.isEmpty()
                && (previousAdapterId > adapterId
                    || (previousAdapterId == adapterId
                        && previousAdapterVersion >= adapterVersion)))) {
            return false;
        }
        relativePaths.insert(relativePath);
        entriesByAdapter.insert(adapterKey, entry);
        previousAdapterId = adapterId;
        previousAdapterVersion = adapterVersion;
    }

    for (const RuntimePackageCompilerDeviceSourceEvidence &source : deviceSources) {
        const QJsonObject entry
            = entriesByAdapter.value(source.adapterId + QChar::Null + source.adapterVersion);
        if (entry.isEmpty()
            || entry.value(QStringLiteral("relative_path")).toString()
                   != source.adapterSourceFile.relativePath
            || entry.value(QStringLiteral("bytes")).toInteger(-1)
                   != source.adapterSourceFile.exactBytes.size()
            || !jsonSha256Matches(
                entry, QStringLiteral("file_sha256"), source.adapterSourceFile.sha256)
            || !jsonSha256Matches(
                entry,
                QStringLiteral("canonical_sha256"),
                source.adapterCanonicalSha256)) {
            return false;
        }
    }
    return true;
}

bool resultCommonFieldsMatch(
    const RuntimePackageCompilerResultEnvelope &envelope,
    const QString &format,
    const QString &status)
{
    const QJsonObject object = canonicalObject(envelope.canonicalResult);
    return object.value(QStringLiteral("format")).toString() == format
           && object.value(QStringLiteral("status")).toString() == status
           && object.value(QStringLiteral("operation_id")).toString()
                  == envelope.operationId.value()
           && jsonUnsignedIntegerMatches(
               envelope.canonicalResult, QByteArray("configuration_id"), envelope.configurationId);
}

} // namespace

RuntimePackageCompilerOperationId::RuntimePackageCompilerOperationId(QString value)
    : m_value(std::move(value))
{}

const QString &RuntimePackageCompilerOperationId::value() const
{
    return m_value;
}

bool RuntimePackageCompilerOperationId::isValid() const
{
    return isOperationId(m_value);
}

RuntimePackageCompilerSha256::RuntimePackageCompilerSha256(QByteArray value)
    : m_value(std::move(value))
{}

const QByteArray &RuntimePackageCompilerSha256::value() const
{
    return m_value;
}

bool RuntimePackageCompilerSha256::isValid() const
{
    return isValidSha256(m_value);
}

RuntimePackageCompilerCanonicalJson::RuntimePackageCompilerCanonicalJson(
    QByteArray exactBytes, RuntimePackageCompilerSha256 sha256)
    : m_exactBytes(std::move(exactBytes))
    , m_sha256(std::move(sha256))
{}

RuntimePackageCompilerCanonicalJson RuntimePackageCompilerCanonicalJson::fromExactBytes(
    QByteArray exactBytes)
{
    const QByteArray sha256 = QCryptographicHash::hash(exactBytes, QCryptographicHash::Sha256);
    return RuntimePackageCompilerCanonicalJson(
        std::move(exactBytes), RuntimePackageCompilerSha256(sha256));
}

const QByteArray &RuntimePackageCompilerCanonicalJson::exactBytes() const
{
    return m_exactBytes;
}

const RuntimePackageCompilerSha256 &RuntimePackageCompilerCanonicalJson::sha256() const
{
    return m_sha256;
}

bool RuntimePackageCompilerCanonicalJson::isValid() const
{
    if (!sha256Matches(m_exactBytes, m_sha256) || !m_exactBytes.endsWith('\n')
        || m_exactBytes.count('\n') != 1) {
        return false;
    }
    if (!std::all_of(m_exactBytes.cbegin(), m_exactBytes.cend(), [](char byte) {
            return quint8(byte) <= 0x7f;
        })) {
        return false;
    }
    return CanonicalJsonParser(QByteArrayView(m_exactBytes.constData(), m_exactBytes.size() - 1))
        .parseRootObject();
}

bool RuntimePackageCompilerContractIdentity::isValid() const
{
    return isCanonicalText(contractId, 128) && contractVersion != 0 && schemaBundleSha256.isValid();
}

bool RuntimePackageCompilerSourceArtifact::isValid() const
{
    return sourceArtifactKindIsKnown(kind) && isSafeRelativePath(relativePath)
           && exactBytes.size() <= 16 * 1024 * 1024 && sha256Matches(exactBytes, sha256);
}

bool RuntimePackageCompilerSourceArtifacts::isValid() const
{
    const QList<const RuntimePackageCompilerSourceArtifact *> artifacts{
        &topologyEvidence,
        &targetProfile,
        &targetProfileSignature,
        &productionPublicKey,
        &adapterBundle,
        &policyTemplate,
        &controllerFeatures,
        &runtimeSource,
    };
    QSet<QString> paths;
    for (const RuntimePackageCompilerSourceArtifact *artifact : artifacts) {
        if (!artifact->isValid() || paths.contains(artifact->relativePath))
            return false;
        paths.insert(artifact->relativePath);
    }

    return topologyEvidence.kind == RuntimePackageCompilerSourceArtifactKind::TopologyEvidence
           && targetProfile.kind == RuntimePackageCompilerSourceArtifactKind::TargetProfile
           && targetProfileSignature.kind
                  == RuntimePackageCompilerSourceArtifactKind::TargetProfileSignature
           && productionPublicKey.kind
                  == RuntimePackageCompilerSourceArtifactKind::ProductionPublicKey
           && adapterBundle.kind == RuntimePackageCompilerSourceArtifactKind::AdapterBundle
           && policyTemplate.kind == RuntimePackageCompilerSourceArtifactKind::PolicyTemplate
           && controllerFeatures.kind
                  == RuntimePackageCompilerSourceArtifactKind::ControllerFeatures
           && runtimeSource.kind == RuntimePackageCompilerSourceArtifactKind::RuntimeSource;
}

bool RuntimePackageCompilerPdoEntry::isValid() const
{
    return isStableId(fieldId) && bitLength != 0 && isCanonicalText(dataType, 32);
}

bool RuntimePackageCompilerPdoMapping::isValid() const
{
    return isStableId(id) && pdoDirectionIsKnown(direction) && syncManager <= 15
           && !entries.isEmpty() && entries.size() <= 1024
           && std::all_of(entries.cbegin(), entries.cend(), [](const auto &entry) {
                  return entry.isValid();
              });
}

bool RuntimePackageCompilerStartupSdo::isValid() const
{
    if (!isStableId(id) || !startupStageIsKnown(stage) || valueBytes == 0 || valueBytes > 8
        || timeoutNs == 0 || !startupFailureActionIsKnown(failureAction)) {
        return false;
    }
    const int valueBits = int(valueBytes) * 8;
    if (std::holds_alternative<quint64>(value)) {
        const quint64 unsignedValue = std::get<quint64>(value);
        return valueBits == 64 || unsignedValue < (quint64(1) << valueBits);
    }
    const qint64 signedValue = std::get<qint64>(value);
    if (valueBits == 64)
        return true;
    const qint64 minimum = -(qint64(1) << (valueBits - 1));
    const qint64 maximum = (qint64(1) << (valueBits - 1)) - 1;
    return signedValue >= minimum && signedValue <= maximum;
}

bool RuntimePackageCompilerDcProjection::isValid() const
{
    if (enabled) {
        return signedDcProfileId && isStableId(*signedDcProfileId) && mode
               && isCanonicalText(*mode, 96) && assignActivate != 0 && sync0CycleNs != 0;
    }
    return !signedDcProfileId && !mode && assignActivate == 0 && sync0CycleNs == 0
           && sync0ShiftNs == 0 && sync1CycleNs == 0 && sync1ShiftNs == 0 && !referenceClock;
}

bool RuntimePackageCompilerManualEnvelope::isValid() const
{
    return maximumTtlCycles != 0 && refreshCycles != 0 && maximumHoldCycles != 0
           && refreshCycles <= maximumHoldCycles && manualRecoveryActionIsKnown(timeoutAction)
           && manualRecoveryActionIsKnown(releaseAction)
           && manualRecoveryActionIsKnown(failureAction);
}

bool RuntimePackageCompilerDeviceProjection::isValid() const
{
    if (projectSlaveNodeId.isNull() || !isStableId(slaveNodeId) || !isStableId(projectDeviceId)
        || position < 0 || position > 65535 || stationAddress == 0 || identity.vendorId == 0
        || identity.productCode == 0 || !esiSha256.isValid() || !isStableId(targetProfileId)
        || !isStableId(adapterId) || !isCanonicalText(adapterVersion, 64)
        || !adapterSha256.isValid() || !isStableId(pdoProfileId) || pdoMappings.isEmpty()
        || pdoMappings.size() > 64 || startupSdos.size() > 4096 || moduleAssignments.size() > 256
        || semanticBindingIds.isEmpty() || !componentBindingKeysAreValid(componentBindingIds)
        || !stableValues(componentBindingIds) || !stableValues(semanticBindingIds)
        || !stableValues(semanticActionBindingIds) || !symbolModeIsKnown(symbolMode)
        || !dc.isValid() || !manualEnvelope.isValid()) {
        return false;
    }
    if (signedDcProfileId != dc.signedDcProfileId)
        return false;
    if (!std::all_of(
            pdoMappings.cbegin(),
            pdoMappings.cend(),
            [](const auto &mapping) { return mapping.isValid(); })
        || !std::all_of(startupSdos.cbegin(), startupSdos.cend(), [](const auto &sdo) {
               return sdo.isValid();
           })) {
        return false;
    }
    quint16 previousSequence = 0;
    bool firstSdo = true;
    for (const RuntimePackageCompilerStartupSdo &sdo : startupSdos) {
        if (!firstSdo && sdo.sequence <= previousSequence)
            return false;
        previousSequence = sdo.sequence;
        firstSdo = false;
    }
    QSet<quint16> moduleSlots;
    for (const DeviceModuleAssignment &module : moduleAssignments) {
        if (module.slot < 0 || module.slot > 65535 || moduleSlots.contains(quint16(module.slot))) {
            return false;
        }
        moduleSlots.insert(quint16(module.slot));
    }
    return std::all_of(symbols.cbegin(), symbols.cend(), [](const QString &symbol) {
        return isCanonicalText(symbol, 128);
    });
}

bool RuntimePackageCompilerProjectProjection::isValid() const
{
    if (projectNodeId.isNull() || masterProjectNodeId.isNull() || !isStableId(projectId)
        || !isStableId(masterNodeId) || documentRevision == 0
        || (timingMode != MasterTimingMode::FreeRun
            && timingMode != MasterTimingMode::DistributedClocks)
        || cyclePeriodNs < 1000 || linkSpeedMbps != 100 || devices.isEmpty() || devices.size() > 256
        || !uiMetadata.isValid()) {
        return false;
    }
    QSet<NodeId> projectSlaveNodeIds;
    QSet<QString> slaveNodeIds;
    QSet<QString> projectDeviceIds;
    QSet<int> positions;
    QSet<quint16> stationAddresses;
    int previousPosition = -1;
    for (const RuntimePackageCompilerDeviceProjection &device : devices) {
        if (!device.isValid() || projectSlaveNodeIds.contains(device.projectSlaveNodeId)
            || slaveNodeIds.contains(device.slaveNodeId)
            || projectDeviceIds.contains(device.projectDeviceId)
            || positions.contains(device.position)
            || stationAddresses.contains(device.stationAddress)
            || device.position <= previousPosition) {
            return false;
        }
        projectSlaveNodeIds.insert(device.projectSlaveNodeId);
        slaveNodeIds.insert(device.slaveNodeId);
        projectDeviceIds.insert(device.projectDeviceId);
        positions.insert(device.position);
        stationAddresses.insert(device.stationAddress);
        previousPosition = device.position;
    }
    return true;
}

bool RuntimePackageCompilerDeviceSourceEvidence::isValid() const
{
    const bool dcDecisionIsExact = explicitNoDc
                                       ? !signedDcProfileId.has_value()
                                       : signedDcProfileId && isStableId(*signedDcProfileId);
    const bool upperDcBridgeIsExact
        = projectSignedDcProfileId.isEmpty()
              ? explicitNoDc && !signedDcProfileId.has_value()
              : !explicitNoDc && isStableId(projectSignedDcProfileId)
                    && signedDcProfileId
                    && *signedDcProfileId == projectSignedDcProfileId;
    const RuntimePackageCompilerSha256 controllerAdapterSha256{
        projectControllerAdapterTarget.adapterSha256};
    const RuntimePackageCompilerSha256 controllerEsiSha256{
        projectControllerAdapterTarget.esiSha256};
    return !projectSlaveNodeId.isNull() && originalEsi.isValid()
           && originalEsi.kind == RuntimePackageCompilerSourceArtifactKind::OriginalEsi
           && adapterSourceFile.isValid()
           && adapterSourceFile.kind == RuntimePackageCompilerSourceArtifactKind::AdapterSourceFile
           && projectAdapterContractVersion == DeviceAdapterContractVersion::V3
           && isStableId(projectAdapterId.value) && isCanonicalText(projectAdapterVersion, 64)
           && projectAdapterContentSha256.isValid() && isStableId(projectPdoProfileId)
           && isStableId(projectControllerAdapterTarget.adapterId)
           && isCanonicalText(projectControllerAdapterTarget.adapterVersion, 64)
           && controllerAdapterSha256.isValid() && controllerEsiSha256.isValid()
           && isStableId(projectSignedPdoProfileId) && isStableId(adapterId)
           && isCanonicalText(adapterVersion, 64) && adapterCanonicalSha256.isValid()
           && isStableId(pdoProfileId) && dcDecisionIsExact
           && projectControllerAdapterTarget.adapterId == adapterId
           && projectControllerAdapterTarget.adapterVersion == adapterVersion
           && projectControllerAdapterTarget.adapterSha256 == adapterCanonicalSha256.value()
           && projectControllerAdapterTarget.esiSha256 == originalEsi.sha256.value()
           && projectSignedPdoProfileId == pdoProfileId && upperDcBridgeIsExact;
}

bool RuntimePackageCompilerTopologySlaveEvidence::isValid() const
{
    return !projectSlaveNodeId.isNull() && position >= 0 && stationAddress != 0
           && identity.vendorId != 0 && identity.productCode != 0;
}

bool RuntimePackageCompilerFreshTopologyEvidence::isValid() const
{
    if (scope.projectId.isNull() || scope.masterId.isNull() || sessionGeneration == 0
        || sessionId == 0 || !isStableId(evidenceId) || captureBootId == 0 || captureSequence == 0
        || capturedAtNs == 0 || expiresAtNs <= capturedAtNs || cyclePeriodNs < 1000
        || linkSpeedMbps != 100 || slaves.isEmpty() || !canonicalEvidence.isValid()) {
        return false;
    }

    QSet<NodeId> deviceIds;
    QSet<int> positions;
    QSet<quint16> stationAddresses;
    int previousPosition = -1;
    for (const RuntimePackageCompilerTopologySlaveEvidence &slave : slaves) {
        if (!slave.isValid() || deviceIds.contains(slave.projectSlaveNodeId)
            || positions.contains(slave.position) || stationAddresses.contains(slave.stationAddress)
            || slave.position <= previousPosition) {
            return false;
        }
        deviceIds.insert(slave.projectSlaveNodeId);
        positions.insert(slave.position);
        stationAddresses.insert(slave.stationAddress);
        previousPosition = slave.position;
    }
    return canonicalTopologyMatchesTyped(*this);
}

bool RuntimePackageCompilerFreshTopologyEvidence::isFreshAt(quint64 compileTimeNs) const
{
    return isValid() && compileTimeNs >= capturedAtNs && compileTimeNs <= expiresAtNs;
}

bool RuntimePackageCompilerSignedTargetProfileEvidence::isValid() const
{
    return isStableId(profileId) && policyRevision != 0 && canonicalProfile.isValid()
           && capabilityDescriptorSha256.isValid() && controllerFeaturesSha256.isValid()
           && adapterBundleSha256.isValid() && cpu1AbiVersion != 0 && fpgaAbiVersion != 0
           && isNonzeroBytes(signature, 64) && signingKeyIdSha256.isValid() && productionSigned
           && canonicalTargetProfileMatchesTyped(*this);
}

RuntimePackageCompilerProjectSnapshotEvidence::RuntimePackageCompilerProjectSnapshotEvidence(
    const RuntimePackageActivationProjectCapture &capture)
    : m_validCapture(capture.isValid())
    , m_snapshot(capture.snapshot())
    , m_serializedProjectSha256(
          QCryptographicHash::hash(capture.serializedProject(), QCryptographicHash::Sha256))
    , m_documentRevisionNumber(capture.documentRevisionNumber())
    , m_documentRevision(capture.documentRevision())
    , m_originalBinding(capture.originalBinding())
{}

const ProjectSnapshot &RuntimePackageCompilerProjectSnapshotEvidence::snapshot() const
{
    return m_snapshot;
}

const RuntimePackageCompilerSha256 &
RuntimePackageCompilerProjectSnapshotEvidence::serializedProjectSha256() const
{
    return m_serializedProjectSha256;
}

quint64 RuntimePackageCompilerProjectSnapshotEvidence::documentRevisionNumber() const
{
    return m_documentRevisionNumber;
}

const RuntimePackageActivationDocumentRevisionToken &
RuntimePackageCompilerProjectSnapshotEvidence::documentRevision() const
{
    return m_documentRevision;
}

const RuntimePackageActivationOriginalBindingToken &
RuntimePackageCompilerProjectSnapshotEvidence::originalBinding() const
{
    return m_originalBinding;
}

bool RuntimePackageCompilerProjectSnapshotEvidence::isValid() const
{
    return m_validCapture && m_snapshot.valid && !m_snapshot.id.isNull()
           && m_serializedProjectSha256.isValid() && m_documentRevisionNumber != 0
           && m_documentRevision.isValid() && m_originalBinding.isValid();
}

bool RuntimePackageCompilerDiagnostic::isValid() const
{
    static const QRegularExpression codePattern(QStringLiteral("^ECOMP-[A-Z0-9]+(?:-[A-Z0-9]+)*$"));
    if (!diagnosticCategoryIsTransportable(category) || !diagnosticSeverityIsKnown(severity)
        || !diagnosticStageIsKnown(stage) || !isCanonicalText(code, 96)
        || !codePattern.match(code).hasMatch() || !isCanonicalText(path, 512)
        || !isCanonicalText(message, 1024) || !canonicalJson.isValid()) {
        return false;
    }

    QString severityName;
    switch (severity) {
    case RuntimePackageCompilerDiagnosticSeverity::Information:
        severityName = QStringLiteral("info");
        break;
    case RuntimePackageCompilerDiagnosticSeverity::Warning:
        severityName = QStringLiteral("warning");
        break;
    case RuntimePackageCompilerDiagnosticSeverity::Error:
        severityName = QStringLiteral("error");
        break;
    }
    const QJsonObject object = QJsonDocument::fromJson(canonicalJson.exactBytes()).object();
    static const QSet<QString> allowedKeys{
        QStringLiteral("format"),
        QStringLiteral("severity"),
        QStringLiteral("stage"),
        QStringLiteral("code"),
        QStringLiteral("path"),
        QStringLiteral("message"),
        QStringLiteral("retryable"),
        QStringLiteral("details"),
    };
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowedKeys.contains(it.key()))
            return false;
    }
    return object.value(QStringLiteral("format")).toString()
               == QStringLiteral("ethercat-ide-compiler-diagnostic-v1")
           && object.value(QStringLiteral("severity")).toString() == severityName
           && object.value(QStringLiteral("stage")).toString() == stage
           && object.value(QStringLiteral("code")).toString() == code
           && object.value(QStringLiteral("path")).toString() == path
           && object.value(QStringLiteral("message")).toString() == message
           && object.value(QStringLiteral("retryable")).isBool()
           && object.value(QStringLiteral("retryable")).toBool() == retryable
           && (!object.contains(QStringLiteral("details"))
               || object.value(QStringLiteral("details")).isObject());
}

bool RuntimePackageCompilerResultEnvelope::isValid() const
{
    if (!commandIsKnown(command) || !statusIsTransportable(status)
        || !isCanonicalText(backendStatus, 128) || !operationId.isValid()
        || !requestSha256.isValid() || !canonicalResult.isValid()
        || !backendStatusMatches(command, status, backendStatus)) {
        return false;
    }
    if (status == RuntimePackageCompilerResultStatus::Succeeded
        && (command == RuntimePackageCompilerCommand::Compile
            || command == RuntimePackageCompilerCommand::Finalize)
        && configurationId == 0) {
        return false;
    }
    if (!std::all_of(diagnostics.cbegin(), diagnostics.cend(), [](const auto &diagnostic) {
            return diagnostic.isValid();
        })) {
        return false;
    }

    const bool hasError = resultHasErrorDiagnostic(*this);
    if (status == RuntimePackageCompilerResultStatus::Succeeded)
        return !hasError;
    if (status == RuntimePackageCompilerResultStatus::DomainFailed
        || status == RuntimePackageCompilerResultStatus::Canceled) {
        const QJsonObject object = canonicalObject(canonicalResult);
        static const QSet<QString> failureKeys{
            QStringLiteral("diagnostics"),
            QStringLiteral("status"),
        };
        const QJsonValue canonicalDiagnosticsValue = object.value(QStringLiteral("diagnostics"));
        if (!canonicalDiagnosticsValue.isArray())
            return false;
        if (!objectHasExactKeys(object, failureKeys)
            || object.value(QStringLiteral("status")).toString() != QStringLiteral("fail")
            || canonicalDiagnosticsValue.toArray().size() != diagnostics.size()) {
            return false;
        }
        QByteArray expectedDiagnostics("[");
        for (qsizetype index = 0; index < diagnostics.size(); ++index) {
            if (index != 0)
                expectedDiagnostics += ',';
            QByteArray diagnosticBytes = diagnostics.at(index).canonicalJson.exactBytes();
            diagnosticBytes.chop(1);
            expectedDiagnostics += diagnosticBytes;
        }
        expectedDiagnostics += ']';
        const QByteArray &resultBytes = canonicalResult.exactBytes();
        CanonicalJsonParser parser(
            QByteArrayView(resultBytes.constData(), resultBytes.size() - 1));
        const std::optional<QByteArray> canonicalDiagnostics
            = parser.rootCanonicalValue(QByteArrayView("diagnostics"));
        if (!canonicalDiagnostics || *canonicalDiagnostics != expectedDiagnostics)
            return false;
    }
    if (status == RuntimePackageCompilerResultStatus::DomainFailed)
        return hasError;
    return true;
}

bool RuntimePackageCompilerResultEnvelope::isSuccess() const
{
    // Unknown backend statuses remain transportable for diagnostics and
    // recovery, but are never promoted to success.
    return isValid() && status == RuntimePackageCompilerResultStatus::Succeeded;
}

bool RuntimePackageCompilerCompileRequest::hasValidReservationInputs() const
{
    return operationId.isValid() && isStableId(intentId) && configurationId != 0
           && buildTimestampNs != 0 && compileTimeNs != 0 && manifestFormatVersion == 2
           && contractIdentity.isValid() && projectSnapshotEvidence.isValid()
           && projectProjection.isValid() && sourceArtifactReferencesAreValid(sourceArtifacts);
}

bool RuntimePackageCompilerCompileRequest::isValid() const
{
    if (!hasValidReservationInputs() || !topologyEvidence.isFreshAt(compileTimeNs)
        || !sourceArtifacts.isValid() || deviceSourceEvidence.isEmpty()
        || !targetProfile.isValid()) {
        return false;
    }
    if (sourceArtifacts.topologyEvidence.exactBytes
            != topologyEvidence.canonicalEvidence.exactBytes()
        || sourceArtifacts.topologyEvidence.sha256 != topologyEvidence.canonicalEvidence.sha256()
        || sourceArtifacts.targetProfile.exactBytes != targetProfile.canonicalProfile.exactBytes()
        || sourceArtifacts.targetProfile.sha256 != targetProfile.canonicalProfile.sha256()
        || sourceArtifacts.targetProfileSignature.exactBytes != targetProfile.signature
        || !isNonzeroBytes(sourceArtifacts.productionPublicKey.exactBytes, 32)
        || sourceArtifacts.productionPublicKey.sha256 != targetProfile.signingKeyIdSha256
        || sourceArtifacts.adapterBundle.sha256 != targetProfile.adapterBundleSha256
        || sourceArtifacts.controllerFeatures.sha256 != targetProfile.controllerFeaturesSha256) {
        return false;
    }

    const ProjectSnapshot &project = projectSnapshotEvidence.snapshot();
    if (projectProjection.projectNodeId != project.id
        || projectProjection.masterProjectNodeId != topologyEvidence.scope.masterId
        || projectProjection.documentRevision != projectSnapshotEvidence.documentRevisionNumber()
        || projectProjection.timingMode != project.masterConfiguration.timingMode
        || projectProjection.cyclePeriodNs != quint64(project.masterConfiguration.cyclePeriodNs)
        || projectProjection.linkSpeedMbps != topologyEvidence.linkSpeedMbps
        || topologyEvidence.scope.projectId != project.id
        || !projectContainsMaster(project, topologyEvidence.scope.masterId)
        || project.masterConfiguration.cyclePeriodNs != topologyEvidence.cyclePeriodNs) {
        return false;
    }

    QList<OfflineSlaveConfiguration> masterSlaves;
    for (const OfflineSlaveConfiguration &slave : project.slaves) {
        if (slave.masterId == topologyEvidence.scope.masterId)
            masterSlaves.append(slave);
    }
    if (masterSlaves.isEmpty() || masterSlaves.size() != topologyEvidence.slaves.size()
        || masterSlaves.size() != deviceSourceEvidence.size()
        || masterSlaves.size() != projectProjection.devices.size()) {
        return false;
    }

    QSet<NodeId> sourceDeviceIds;
    for (const RuntimePackageCompilerDeviceSourceEvidence &source : deviceSourceEvidence) {
        if (!source.isValid() || sourceDeviceIds.contains(source.projectSlaveNodeId))
            return false;
        sourceDeviceIds.insert(source.projectSlaveNodeId);
    }
    if (!adapterBundleBindsDeviceSources(
            sourceArtifacts.adapterBundle, deviceSourceEvidence)) {
        return false;
    }

    for (qsizetype slaveIndex = 0; slaveIndex < masterSlaves.size(); ++slaveIndex) {
        const OfflineSlaveConfiguration &slave = masterSlaves.at(slaveIndex);
        const RuntimePackageCompilerDeviceProjection &projection = projectProjection.devices.at(
            slaveIndex);
        if (slave.id.isNull() || slave.position < 0 || slave.stationAddress == 0
            || !RuntimePackageCompilerSha256(slave.esiSha256).isValid()
            || slave.adapterSelection.adapterId.value.isEmpty()
            || slave.adapterSelection.adapterVersion.isEmpty()
            || !RuntimePackageCompilerSha256(slave.adapterSelection.adapterContentSha256).isValid()
            || slave.adapterSelection.processDataProfileId.isEmpty()) {
            return false;
        }

        if (projection.projectSlaveNodeId != slave.id
            || projection.targetProfileId != targetProfile.profileId
            || projection.position != slave.position
            || projection.stationAddress != slave.stationAddress || projection.alias != slave.alias
            || projection.identity != slave.identity
            || projection.serialNumber != slave.serialNumber
            || projection.esiSha256.value() != slave.esiSha256
            || projection.moduleAssignments != slave.adapterSelection.moduleAssignments
            || projection.manualEnvelope.enabled != slave.manualControlEnvelope.enabled
            || !pdoProjectionMatches(projection.pdoMappings, slave.processData)
            || !startupProjectionMatches(projection.startupSdos, slave.startup)
            || !dcProjectionMatches(projection.dc, slave.dc)) {
            return false;
        }

        const RuntimePackageCompilerTopologySlaveEvidence *topology
            = &topologyEvidence.slaves.at(slaveIndex);
        if (topology->projectSlaveNodeId != slave.id || topology->position != slave.position
            || topology->stationAddress != slave.stationAddress || topology->alias != slave.alias
            || topology->identity != slave.identity || topology->serialNumber != slave.serialNumber
            || topology->moduleAssignments != slave.adapterSelection.moduleAssignments) {
            return false;
        }

        const RuntimePackageCompilerDeviceSourceEvidence *source
            = findDeviceSourceEvidence(deviceSourceEvidence, slave.id);
        if (!source || source->originalEsi.sha256.value() != slave.esiSha256
            || source->projectAdapterId != slave.adapterSelection.adapterId
            || source->projectAdapterVersion != slave.adapterSelection.adapterVersion
            || source->projectAdapterContentSha256.value()
                   != slave.adapterSelection.adapterContentSha256
            || source->projectPdoProfileId != slave.adapterSelection.processDataProfileId
            || source->adapterId != projection.adapterId
            || source->adapterVersion != projection.adapterVersion
            || source->adapterCanonicalSha256 != projection.adapterSha256
            || source->pdoProfileId != projection.pdoProfileId
            || source->signedDcProfileId != projection.signedDcProfileId
            || source->explicitNoDc == slave.dc.enabled) {
            return false;
        }
    }
    return true;
}

bool RuntimePackageCompilerFinalizeRequest::isValid() const
{
    return operationId.isValid() && configurationId != 0 && contractIdentity.isValid()
           && compileRequestSha256.isValid() && signRequestSha256.isValid()
           && manifestSha256.isValid() && signingKeyIdSha256.isValid() && signingPolicyRevision != 0
           && detachedSigningResponse.isValid()
           && detachedSigningResponseMatches(
               detachedSigningResponse,
               operationId,
               signRequestSha256,
               manifestSha256,
               signingKeyIdSha256,
               signingPolicyRevision);
}

bool RuntimePackageCompilerQueryRequest::isValid() const
{
    return operationId.isValid() && contractIdentity.isValid() && compileRequestSha256.isValid();
}

bool RuntimePackageCompilerVerifyRequest::isValid() const
{
    return operationId.isValid() && contractIdentity.isValid()
           && sha256Matches(packageBytes, packageSha256);
}

bool RuntimePackageCompilerCompileResult::isValid() const
{
    if (!envelope.isValid() || envelope.command != RuntimePackageCompilerCommand::Compile)
        return false;
    if (envelope.isSuccess()) {
        if (!(intentSha256 && intentSha256->isValid() && compiledProjectSha256
              && compiledProjectSha256->isValid() && compileReportSha256
              && compileReportSha256->isValid() && effectiveProjectCompanionSha256
              && effectiveProjectCompanionSha256->isValid() && signRequest && signRequest->isValid()
              && manifestSha256 && manifestSha256->isValid() && targetProfileSha256
              && targetProfileSha256->isValid() && adapterBundleSha256
              && adapterBundleSha256->isValid() && isCanonicalText(outputDirectory, 4096))) {
            return false;
        }
        const QJsonObject object = canonicalObject(envelope.canonicalResult);
        static const QSet<QString> compileResultKeys{
            QStringLiteral("adapter_bundle_sha256"),
            QStringLiteral("compile_report_sha256"),
            QStringLiteral("compiled_project_sha256"),
            QStringLiteral("configuration_id"),
            QStringLiteral("effective_project_companion_sha256"),
            QStringLiteral("format"),
            QStringLiteral("format_version"),
            QStringLiteral("intent_sha256"),
            QStringLiteral("manifest_sha256"),
            QStringLiteral("operation_id"),
            QStringLiteral("output_dir"),
            QStringLiteral("sign_request_sha256"),
            QStringLiteral("status"),
            QStringLiteral("target_profile_sha256"),
        };
        return objectHasExactKeys(object, compileResultKeys)
               && resultCommonFieldsMatch(
                   envelope,
                   QStringLiteral("ethercat-ide-project-compiler-result-v1"),
                   QStringLiteral("awaiting_signature"))
               && object.value(QStringLiteral("format_version")).toInt() == 1
               && jsonSha256Matches(object, QStringLiteral("intent_sha256"), *intentSha256)
               && jsonSha256Matches(
                   object, QStringLiteral("compiled_project_sha256"), *compiledProjectSha256)
               && jsonSha256Matches(
                   object, QStringLiteral("compile_report_sha256"), *compileReportSha256)
               && jsonSha256Matches(
                   object,
                   QStringLiteral("effective_project_companion_sha256"),
                   *effectiveProjectCompanionSha256)
               && jsonSha256Matches(
                   object, QStringLiteral("sign_request_sha256"), signRequest->sha256())
               && jsonSha256Matches(object, QStringLiteral("manifest_sha256"), *manifestSha256)
               && jsonSha256Matches(
                   object, QStringLiteral("target_profile_sha256"), *targetProfileSha256)
               && jsonSha256Matches(
                   object, QStringLiteral("adapter_bundle_sha256"), *adapterBundleSha256)
               && object.value(QStringLiteral("output_dir")).toString() == outputDirectory
               && detachedSigningRequestMatches(
                   *signRequest,
                   envelope.operationId,
                   *manifestSha256,
                   *intentSha256,
                   *targetProfileSha256,
                   *effectiveProjectCompanionSha256);
    }
    return !intentSha256 && !compiledProjectSha256 && !compileReportSha256
           && !effectiveProjectCompanionSha256 && !signRequest && !manifestSha256
           && !targetProfileSha256 && !adapterBundleSha256 && outputDirectory.isEmpty();
}

bool RuntimePackageCompilerCompileResult::isSuccess() const
{
    return isValid() && envelope.isSuccess();
}

bool RuntimePackageCompilerFinalizeResult::isValid() const
{
    if (!envelope.isValid() || envelope.command != RuntimePackageCompilerCommand::Finalize)
        return false;
    if (envelope.isSuccess()) {
        if (!(isCanonicalText(packagePath, 4096) && packageSha256
              && sha256Matches(packageBytes, *packageSha256) && manifestSha256
              && manifestSha256->isValid() && signingReceiptSha256
              && signingReceiptSha256->isValid())) {
            return false;
        }
        const QJsonObject object = canonicalObject(envelope.canonicalResult);
        static const QSet<QString> finalizeResultKeys{
            QStringLiteral("configuration_id"),
            QStringLiteral("format"),
            QStringLiteral("format_version"),
            QStringLiteral("manifest_sha256"),
            QStringLiteral("operation_id"),
            QStringLiteral("package_bytes"),
            QStringLiteral("package_path"),
            QStringLiteral("package_sha256"),
            QStringLiteral("signing_receipt_sha256"),
            QStringLiteral("status"),
        };
        return objectHasExactKeys(object, finalizeResultKeys)
               && resultCommonFieldsMatch(
                   envelope,
                   QStringLiteral("ethercat-ide-project-compiler-result-v1"),
                   QStringLiteral("complete"))
               && object.value(QStringLiteral("format_version")).toInt() == 1
               && object.value(QStringLiteral("package_path")).toString() == packagePath
               && jsonUnsignedIntegerMatches(
                   envelope.canonicalResult,
                   QByteArray("package_bytes"),
                   quint64(packageBytes.size()))
               && jsonSha256Matches(object, QStringLiteral("package_sha256"), *packageSha256)
               && jsonSha256Matches(object, QStringLiteral("manifest_sha256"), *manifestSha256)
               && jsonSha256Matches(
                   object, QStringLiteral("signing_receipt_sha256"), *signingReceiptSha256);
    }
    return packagePath.isEmpty() && packageBytes.isEmpty() && !packageSha256 && !manifestSha256
           && !signingReceiptSha256;
}

bool RuntimePackageCompilerFinalizeResult::isSuccess() const
{
    return isValid() && envelope.isSuccess();
}

bool RuntimePackageCompilerQueryResult::isValid() const
{
    if (!envelope.isValid() || envelope.command != RuntimePackageCompilerCommand::Query)
        return false;
    if (envelope.isSuccess()) {
        if ((compilerRecord && !compilerRecord->isValid())
            || (signerResponse && !signerResponse->isValid())
            || (!compilerRecord && !signerResponse)) {
            return false;
        }
        const QJsonObject object = canonicalObject(envelope.canonicalResult);
        static const QSet<QString> allowedKeys{
            QStringLiteral("compiler"),
            QStringLiteral("format"),
            QStringLiteral("operation_id"),
            QStringLiteral("signer_response"),
        };
        bool hasOnlyAllowedKeys = object.size() == allowedKeys.size();
        for (auto it = object.constBegin(); hasOnlyAllowedKeys && it != object.constEnd(); ++it)
            hasOnlyAllowedKeys = allowedKeys.contains(it.key());
        if (!hasOnlyAllowedKeys || object.value(QStringLiteral("format")).toString()
                != QStringLiteral("ethercat-ide-compiler-operation-state-v1")
            || object.value(QStringLiteral("operation_id")).toString()
                   != envelope.operationId.value()) {
            return false;
        }
        const QByteArray &resultBytes = envelope.canonicalResult.exactBytes();
        CanonicalJsonParser parser(
            QByteArrayView(resultBytes.constData(), resultBytes.size() - 1));
        const std::optional<QByteArray> compilerValue
            = parser.rootCanonicalValue(QByteArrayView("compiler"));
        const std::optional<QByteArray> signerValue
            = parser.rootCanonicalValue(QByteArrayView("signer_response"));
        if (!compilerValue || !signerValue)
            return false;
        const auto matchesRecord = [](const QByteArray &canonicalValue,
                                      const std::optional<
                                          RuntimePackageCompilerCanonicalJson> &record) {
            if (!record)
                return canonicalValue == QByteArrayView("null");
            QByteArray expected = record->exactBytes();
            expected.chop(1);
            return canonicalValue == expected && canonicalValue.startsWith('{')
                   && canonicalValue.endsWith('}');
        };
        return matchesRecord(*compilerValue, compilerRecord)
               && matchesRecord(*signerValue, signerResponse);
    }
    return !compilerRecord && !signerResponse;
}

bool RuntimePackageCompilerQueryResult::isSuccess() const
{
    return isValid() && envelope.isSuccess();
}

bool RuntimePackageCompilerQueryResult::hasRecoveredResult() const
{
    return isValid() && (compilerRecord || signerResponse);
}

bool RuntimePackageCompilerQueryResult::hasCompilerRecord() const
{
    return isValid() && compilerRecord.has_value();
}

bool RuntimePackageCompilerQueryResult::hasSignerResponse() const
{
    return isValid() && signerResponse.has_value();
}

bool RuntimePackageCompilerVerifyResult::isValid() const
{
    if (!envelope.isValid() || envelope.command != RuntimePackageCompilerCommand::Verify
        || (!packageSha256.value().isEmpty() && !packageSha256.isValid())) {
        return false;
    }
    if (envelope.isSuccess()) {
        if (!(packageSha256.isValid() && envelope.configurationId != 0 && trusted
              && manifestFormatVersion == 2
              && intentSha256.isValid() && effectiveProjectCompanionSha256.isValid()
              && targetProfileSha256.isValid() && adapterBundleSha256.isValid()
              && topologyEvidenceSha256.isValid())) {
            return false;
        }
        const QJsonObject object = canonicalObject(envelope.canonicalResult);
        static const QSet<QString> verifyResultKeys{
            QStringLiteral("adapter_bundle_sha256"),
            QStringLiteral("configuration_id"),
            QStringLiteral("effective_project_companion_sha256"),
            QStringLiteral("format"),
            QStringLiteral("intent_sha256"),
            QStringLiteral("manifest_format_version"),
            QStringLiteral("package_sha256"),
            QStringLiteral("status"),
            QStringLiteral("target_profile_sha256"),
            QStringLiteral("topology_evidence_sha256"),
        };
        return objectHasExactKeys(object, verifyResultKeys)
               && object.value(QStringLiteral("format")).toString()
                   == QStringLiteral("ethercat-ide-project-compiler-verification-v1")
               && object.value(QStringLiteral("status")).toString() == QStringLiteral("pass")
               && jsonUnsignedIntegerMatches(
                   envelope.canonicalResult,
                   QByteArray("configuration_id"),
                   envelope.configurationId)
               && object.value(QStringLiteral("manifest_format_version")).toInt()
                      == int(manifestFormatVersion)
               && jsonSha256Matches(object, QStringLiteral("package_sha256"), packageSha256)
               && jsonSha256Matches(object, QStringLiteral("intent_sha256"), intentSha256)
               && jsonSha256Matches(
                   object,
                   QStringLiteral("effective_project_companion_sha256"),
                   effectiveProjectCompanionSha256)
               && jsonSha256Matches(
                   object, QStringLiteral("target_profile_sha256"), targetProfileSha256)
               && jsonSha256Matches(
                   object, QStringLiteral("adapter_bundle_sha256"), adapterBundleSha256)
               && jsonSha256Matches(
                   object, QStringLiteral("topology_evidence_sha256"), topologyEvidenceSha256);
    }
    return !trusted && manifestFormatVersion == 0 && intentSha256.value().isEmpty()
           && effectiveProjectCompanionSha256.value().isEmpty()
           && targetProfileSha256.value().isEmpty() && adapterBundleSha256.value().isEmpty()
           && topologyEvidenceSha256.value().isEmpty();
}

bool RuntimePackageCompilerVerifyResult::isSuccess() const
{
    return isValid() && envelope.isSuccess() && trusted;
}

bool RuntimePackageCompilerActivationProof::isValid() const
{
    if (!isStableId(compilerProviderId) || !contractIdentity.isValid()
        || !compileRequest.isValid() || !compileResult.isSuccess()
        || !finalizeRequest.isValid() || !finalizeRequestSha256.isValid()
        || !finalizeResult.isSuccess() || !verifyRequest.isValid()
        || !verifyRequestSha256.isValid() || !verifyResult.isSuccess()) {
        return false;
    }

    if (compileRequest.contractIdentity != contractIdentity
        || finalizeRequest.contractIdentity != contractIdentity
        || verifyRequest.contractIdentity != contractIdentity
        || compileResult.envelope.operationId != compileRequest.operationId
        || compileResult.envelope.configurationId != compileRequest.configurationId
        || finalizeRequest.operationId != compileRequest.operationId
        || finalizeRequest.configurationId != compileRequest.configurationId
        || finalizeRequest.compileRequestSha256 != compileResult.envelope.requestSha256
        || finalizeRequest.signRequestSha256 != compileResult.signRequest->sha256()
        || finalizeRequest.manifestSha256 != *compileResult.manifestSha256
        || finalizeRequest.signingKeyIdSha256
               != compileRequest.targetProfile.signingKeyIdSha256
        || finalizeRequest.signingPolicyRevision
               != compileRequest.targetProfile.policyRevision
        || finalizeResult.envelope.operationId != finalizeRequest.operationId
        || finalizeResult.envelope.configurationId != finalizeRequest.configurationId
        || finalizeResult.envelope.requestSha256
               != finalizeRequest.compileRequestSha256
        || *finalizeResult.manifestSha256 != finalizeRequest.manifestSha256
        || verifyRequest.packageBytes != finalizeResult.packageBytes
        || verifyRequest.packageSha256 != *finalizeResult.packageSha256
        || verifyResult.envelope.operationId != verifyRequest.operationId
        || verifyResult.envelope.configurationId != compileRequest.configurationId
        || verifyResult.envelope.requestSha256 != verifyRequest.packageSha256
        || verifyResult.packageSha256 != verifyRequest.packageSha256
        || verifyResult.manifestFormatVersion != compileRequest.manifestFormatVersion
        || verifyResult.intentSha256 != *compileResult.intentSha256
        || verifyResult.effectiveProjectCompanionSha256
               != *compileResult.effectiveProjectCompanionSha256
        || verifyResult.targetProfileSha256 != *compileResult.targetProfileSha256
        || verifyResult.adapterBundleSha256 != *compileResult.adapterBundleSha256
        || verifyResult.topologyEvidenceSha256
               != compileRequest.topologyEvidence.canonicalEvidence.sha256()
        || *compileResult.targetProfileSha256
               != compileRequest.targetProfile.canonicalProfile.sha256()
        || *compileResult.adapterBundleSha256
               != compileRequest.sourceArtifacts.adapterBundle.sha256
        || *compileResult.adapterBundleSha256
               != compileRequest.targetProfile.adapterBundleSha256
        || verifyResult.topologyEvidenceSha256
               != compileRequest.sourceArtifacts.topologyEvidence.sha256
        || !sha256Matches(compiledProjectSource, *compileResult.compiledProjectSha256)
        || !sha256Matches(
            effectiveProjectCompanion,
            *compileResult.effectiveProjectCompanionSha256)) {
        return false;
    }

    const QJsonObject signingRequest = canonicalObject(*compileResult.signRequest);
    if (!jsonSha256Matches(
            signingRequest,
            QStringLiteral("signing_key_id"),
            compileRequest.targetProfile.signingKeyIdSha256)
        || !jsonSha256Matches(
            signingRequest,
            QStringLiteral("signing_key_id"),
            finalizeRequest.signingKeyIdSha256)
        || !jsonUnsignedIntegerMatches(
            *compileResult.signRequest,
            QByteArray("policy_revision"),
            compileRequest.targetProfile.policyRevision)
        || !jsonUnsignedIntegerMatches(
            *compileResult.signRequest,
            QByteArray("policy_revision"),
            finalizeRequest.signingPolicyRevision)) {
        return false;
    }

    const QJsonObject signingResponse = canonicalObject(finalizeRequest.detachedSigningResponse);
    return jsonSha256Matches(
        signingResponse,
        QStringLiteral("receipt_sha256"),
        *finalizeResult.signingReceiptSha256);
}

RuntimePackageCompilerJobResult::RuntimePackageCompilerJobResult(
    RuntimePackageCompilerCompileResult result)
    : m_value(std::move(result))
{}

RuntimePackageCompilerJobResult::RuntimePackageCompilerJobResult(
    RuntimePackageCompilerFinalizeResult result)
    : m_value(std::move(result))
{}

RuntimePackageCompilerJobResult::RuntimePackageCompilerJobResult(
    RuntimePackageCompilerQueryResult result)
    : m_value(std::move(result))
{}

RuntimePackageCompilerJobResult::RuntimePackageCompilerJobResult(
    RuntimePackageCompilerVerifyResult result)
    : m_value(std::move(result))
{}

RuntimePackageCompilerCommand RuntimePackageCompilerJobResult::command() const
{
    return std::visit(
        [](const auto &result) -> RuntimePackageCompilerCommand {
            using Result = std::decay_t<decltype(result)>;
            if constexpr (std::is_same_v<Result, std::monostate>)
                return RuntimePackageCompilerCommand::Unknown;
            else
                return result.envelope.command;
        },
        m_value);
}

const RuntimePackageCompilerJobResult::Value &RuntimePackageCompilerJobResult::value() const
{
    return m_value;
}

bool RuntimePackageCompilerJobResult::isValid() const
{
    return std::visit(
        [](const auto &result) {
            using Result = std::decay_t<decltype(result)>;
            if constexpr (std::is_same_v<Result, std::monostate>)
                return false;
            else
                return result.isValid();
        },
        m_value);
}

bool RuntimePackageCompilerJobResult::isSuccess() const
{
    return std::visit(
        [](const auto &result) {
            using Result = std::decay_t<decltype(result)>;
            if constexpr (std::is_same_v<Result, std::monostate>)
                return false;
            else
                return result.isSuccess();
        },
        m_value);
}

} // namespace EtherCAT::Data
