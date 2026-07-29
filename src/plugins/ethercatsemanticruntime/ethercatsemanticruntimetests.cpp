// Copyright (C) 2026 Embed Labs

#include "ethercatsemanticruntimetests.h"

#include "canonicaljson_p.h"
#include "ecfgconfiguration_p.h"
#include "ecpkgcontainer.h"
#include "ed25519verifier.h"
#include "semanticbindingartifact_p.h"
#include "semanticruntimeexecutor.h"
#include "signedecpkgmanifest_p.h"
#include "verifiedecpkgstore_p.h"

#include <extensionsystem/pluginmanager.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <array>
#include <future>
#include <limits>

namespace EtherCAT::SemanticRuntime::Internal {

class TestProjectService final : public Core::ProjectService
{
public:
    TestProjectService()
        : ProjectService("EtherCAT.SemanticRuntime.Tests.Project", "Semantic Runtime test projects")
    {
        setAvailable(true);
    }

    QList<Data::ProjectSnapshot> projects() const final { return m_projects; }

    std::optional<Data::ProjectSnapshot> project(const Data::NodeId &projectId) const final
    {
        const auto found = std::find_if(
            m_projects.cbegin(),
            m_projects.cend(),
            [&projectId](const Data::ProjectSnapshot &candidate) {
                return candidate.id == projectId;
            });
        if (found == m_projects.cend())
            return std::nullopt;
        return *found;
    }

    Data::NodeId activeProjectId() const final { return m_activeProjectId; }
    bool managesProject(const QObject *) const final { return false; }

    Utils::Result<> activateProject(const Data::NodeId &projectId) final
    {
        if (!project(projectId))
            return Utils::ResultError("Unknown test project");
        const Data::NodeId previous = m_activeProjectId;
        m_activeProjectId = projectId;
        emit activeProjectChanged(previous, m_activeProjectId);
        return Utils::ResultOk;
    }

    Utils::Result<> renameProject(const Data::NodeId &, const QString &) final
    {
        return unsupported();
    }

    Utils::Result<> saveProject(const Data::NodeId &) final { return unsupported(); }
    Utils::Result<> undoProject(const Data::NodeId &) final { return unsupported(); }
    Utils::Result<> redoProject(const Data::NodeId &) final { return unsupported(); }

    Utils::Result<> setMasterConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::MasterConfiguration &) final
    {
        return unsupported();
    }

    Utils::Result<> replaceOfflineSlaves(
        const Data::NodeId &,
        const Data::NodeId &,
        const QList<Data::OfflineSlaveConfiguration> &) final
    {
        return unsupported();
    }

    Utils::Result<> setProcessDataConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::ProcessDataConfiguration &) final
    {
        return unsupported();
    }

    Utils::Result<> setStartupConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::StartupConfiguration &) final
    {
        return unsupported();
    }

    Utils::Result<> setDcConfiguration(
        const Data::NodeId &, const Data::NodeId &, const Data::DcConfiguration &) final
    {
        return unsupported();
    }

    bool canUndoProject(const Data::NodeId &) const final { return false; }
    bool canRedoProject(const Data::NodeId &) const final { return false; }

    Utils::Result<> renameStructuralNode(
        const Data::NodeId &, const Data::NodeId &, const QString &) final
    {
        return unsupported();
    }

    Utils::Result<> setDeviceAdapterSelection(
        const Data::NodeId &,
        const Data::NodeId &,
        const QByteArray &,
        const Data::DeviceAdapterProjectSelection &) final
    {
        return unsupported();
    }

    Utils::Result<> setManualControlEnvelope(
        const Data::NodeId &, const Data::NodeId &, const Data::ManualControlEnvelope &) final
    {
        return unsupported();
    }

    Utils::Result<> setMasterBindingArtifact(
        const Data::NodeId &, const Data::SemanticBindingArtifactReference &) final
    {
        return unsupported();
    }

    void addProject(const Data::ProjectSnapshot &project)
    {
        m_projects.append(project);
        emit projectAdded(project);
    }

    void changeProject(const Data::ProjectSnapshot &project)
    {
        for (Data::ProjectSnapshot &candidate : m_projects) {
            if (candidate.id != project.id)
                continue;
            candidate = project;
            emit projectChanged(project);
            return;
        }
    }

    void removeProject(const Data::NodeId &projectId)
    {
        for (qsizetype index = 0; index < m_projects.size(); ++index) {
            if (m_projects.at(index).id != projectId)
                continue;
            emit projectAboutToBeRemoved(projectId);
            m_projects.removeAt(index);
            return;
        }
    }

private:
    static Utils::Result<> unsupported()
    {
        return Utils::ResultError("Test project mutation is not implemented");
    }

    QList<Data::ProjectSnapshot> m_projects;
    Data::NodeId m_activeProjectId;
};

class CountingControllerProvider final : public Core::ControllerConnectionProvider
{
public:
    explicit CountingControllerProvider(Utils::Id id)
        : ControllerConnectionProvider(id, QStringLiteral("Semantic Runtime counting controller"))
    {}

    QList<Data::ControllerConnectionProfile> connectionProfiles(
        const Data::ControllerConnectionScope &) const final
    {
        return {};
    }

    Utils::Result<> setConnectionProfileEndpoint(
        const Data::ControllerConnectionScope &, const Data::NodeId &, const QString &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    Data::ControllerConnectionSnapshot connectionSnapshot() const final
    {
        ++snapshotReads;
        return snapshot;
    }

    Utils::Result<> connectToController(const Data::ControllerConnectionRequest &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    Utils::Result<> disconnectFromController() final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    Utils::Result<> refreshController() final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    bool supportsControlCommand(Data::ControllerControlCommand) const final { return true; }

    Utils::Result<> executeControlCommand(const Data::ControllerControlRequest &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    bool supportsPackageDeployment() const final { return true; }

    Utils::Result<> deployPackage(const Data::ControllerPackageDeploymentRequest &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    Utils::Result<> cancelPackageDeployment(const QString &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    bool supportsRuntimeResources() const final { return runtimeResourcesSupported; }

    std::optional<Data::RuntimeResourceCatalog> runtimeResourceCatalog() const final
    {
        ++catalogReads;
        return catalog;
    }

    std::optional<Data::RuntimeResourceSnapshot> runtimeResourceSnapshot() const final
    {
        ++resourceSnapshotReads;
        return std::nullopt;
    }

    Utils::Result<> refreshRuntimeResources() final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    Utils::Result<> requestRuntimeResourceSnapshot(const Data::RuntimeResourceSnapshotRequest &) final
    {
        ++mutationCalls;
        return rejectedMutation();
    }

    void publishSnapshot(const Data::ControllerConnectionSnapshot &value)
    {
        snapshot = value;
        emit connectionSnapshotChanged();
    }

    void publishCatalog(const std::optional<Data::RuntimeResourceCatalog> &value)
    {
        catalog = value;
        emit runtimeResourceCatalogChanged();
    }

    void setRuntimeResourcesSupported(bool supported)
    {
        runtimeResourcesSupported = supported;
        emit runtimeResourceCatalogChanged();
    }

    mutable int snapshotReads = 0;
    mutable int catalogReads = 0;
    mutable int resourceSnapshotReads = 0;
    int mutationCalls = 0;
    bool runtimeResourcesSupported = true;
    Data::ControllerConnectionSnapshot snapshot;
    std::optional<Data::RuntimeResourceCatalog> catalog;

private:
    static Utils::Result<> rejectedMutation()
    {
        return Utils::ResultError("Counting controller must not be mutated");
    }
};

class RegisteredObject
{
public:
    explicit RegisteredObject(QObject *object)
        : m_object(object)
    {
        ExtensionSystem::PluginManager::addObject(m_object);
    }

    ~RegisteredObject() { remove(); }

    void remove()
    {
        if (!m_object)
            return;
        ExtensionSystem::PluginManager::removeObject(m_object);
        m_object = nullptr;
    }

private:
    QObject *m_object = nullptr;
};

static Data::ControllerConnectionScope projectScope(const Data::ProjectSnapshot &project)
{
    for (const Data::ProjectNodeSnapshot &node : project.nodes) {
        if (node.kind == Data::ProjectNodeKind::Master)
            return {project.id, node.id};
    }
    return {};
}

static Data::SemanticBindingArtifactReference bindingArtifact()
{
    return {
        QStringLiteral("binding/embed-labs/runtime/1"),
        QByteArray(32, '\x6b'),
        QByteArray(32, '\x7c'),
    };
}

static Data::ProjectSnapshot testProject(bool includeBinding = true, bool includeAdapters = false)
{
    Data::ProjectSnapshot project;
    project.id = Data::NodeId::create();
    project.name = QStringLiteral("Semantic Runtime Test");
    project.formatVersion = 4;
    project.createdBy = QStringLiteral("EtherCATSemanticRuntimeTests");
    project.valid = true;

    const Data::NodeId masterId = Data::NodeId::create();
    project.nodes = {
        {project.id, {}, Data::ProjectNodeKind::Project, project.name},
        {masterId, project.id, Data::ProjectNodeKind::Master, QStringLiteral("Master")},
    };
    if (includeBinding)
        project.masterBindingArtifact = bindingArtifact();

    if (includeAdapters) {
        Data::OfflineSlaveConfiguration xb6;
        xb6.id = Data::NodeId::create();
        xb6.masterId = masterId;
        xb6.position = 0;
        xb6.name = QStringLiteral("XB6-EC0002");
        xb6.esiSha256 = QByteArray(32, '\x11');
        xb6.adapterSelection = {
            Data::DeviceAdapterId{QStringLiteral("org.embedlabs.adapter.solidot.xb6-ec0002.rev1")},
            QStringLiteral("0.1.0"),
            QByteArray(32, '\x21'),
            QStringLiteral("do16-default"),
            {},
        };

        Data::OfflineSlaveConfiguration sv630n;
        sv630n.id = Data::NodeId::create();
        sv630n.masterId = masterId;
        sv630n.position = 1;
        sv630n.name = QStringLiteral("SV630N_1Axis_03716");
        sv630n.esiSha256 = QByteArray(32, '\x12');
        sv630n.adapterSelection = {
            Data::DeviceAdapterId{
                QStringLiteral("org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000")},
            QStringLiteral("1.4.0"),
            QByteArray(32, '\x22'),
            QStringLiteral("sync0-125us"),
            {},
        };
        project.slaves = {xb6, sv630n};
    }
    return project;
}

static Data::ControllerConnectionSnapshot connectedSnapshot(
    const Data::ControllerConnectionScope &scope, quint64 sessionGeneration)
{
    Data::ControllerConnectionSnapshot snapshot;
    snapshot.scope = scope;
    snapshot.state = Data::ControllerConnectionState::Connected;
    snapshot.sessionGeneration = sessionGeneration;
    snapshot.readOnly = false;
    return snapshot;
}

static Data::RuntimeResourceCatalogEpoch catalogEpoch(quint64 runtimeGeneration = 5)
{
    Data::RuntimeResourceCatalogEpoch epoch;
    epoch.controllerBootId = 0x1122334455667788;
    epoch.activePackageSlot = Data::ControllerSlot::A;
    epoch.activePackageGeneration = 3;
    epoch.configurationId = 813;
    epoch.topologyGeneration = 4;
    epoch.runtimeGeneration = runtimeGeneration;
    epoch.catalogRevision = 6;
    epoch.topologyIdentity = QByteArray(32, '\x33');
    return epoch;
}

static Data::RuntimeResourceCatalog resourceCatalog(
    const Data::ControllerConnectionScope &scope,
    quint64 sessionGeneration,
    const Data::RuntimeResourceCatalogEpoch &epoch = catalogEpoch())
{
    Data::RuntimeResourceCatalog catalog;
    catalog.scope = scope;
    catalog.sessionGeneration = sessionGeneration;
    catalog.epoch = epoch;

    Data::RuntimeResourceDescriptor descriptor;
    descriptor.id.value = QByteArray::fromHex("1000000000000001");
    descriptor.componentInstanceId.value = QByteArray::fromHex("2000000000000001");
    descriptor.consistencyGroupId.value = QByteArray::fromHex("00000001");
    descriptor.primitiveType = Data::RuntimeResourcePrimitiveType::UnsignedInteger;
    descriptor.bitWidth = 16;
    descriptor.direction = Data::RuntimeResourceDirection::Input;
    descriptor.access = Data::RuntimeResourceAccess::ReadOnly;
    catalog.resources = {descriptor};
    return catalog;
}

static QString detailFor(SemanticRuntimeContextIssue issue)
{
    return semanticRuntimeContextIssueDetail(issue);
}

static QByteArray fromHex(const char *hex)
{
    return QByteArray::fromHex(QByteArray(hex));
}

static QByteArray readTestData(const QString &relativePath)
{
    QFile file(
        QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR) + QLatin1Char('/')
        + relativePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void EtherCATSemanticRuntimeTests::testCanonicalJsonRoundTrip()
{
    const QByteArray canonical = "{\"array\":[true,false,null],\"control\":\"\\u0001\","
                                 "\"large\":18446744073709551615,\"negative\":-9223372036854775808,"
                                 "\"precise\":9007199254740993,\"unicode\":\"\\ud83d\\ude00\"}\n";

    const Utils::Result<StrictJson> parsed = parseCanonicalJson(canonical, canonical.size());
    QVERIFY_RESULT(parsed);
    QVERIFY(parsed->at("large").is_number_unsigned());
    QCOMPARE(parsed->at("large").get<quint64>(), std::numeric_limits<quint64>::max());
    QVERIFY(parsed->at("negative").is_number_integer());
    QCOMPARE(parsed->at("negative").get<qint64>(), std::numeric_limits<qint64>::min());
    QCOMPARE(parsed->at("precise").get<quint64>(), quint64(9007199254740993ULL));

    const Utils::Result<QByteArray> serialized = serializeCanonicalJson(*parsed, canonical.size());
    QVERIFY_RESULT(serialized);
    QCOMPARE(*serialized, canonical);

    const QByteArray pretty = "{\n  \"value\": 1\n}\n";
    const Utils::Result<StrictJson> strict = parseStrictJson(pretty, pretty.size());
    QVERIFY_RESULT(strict);
    QVERIFY(!parseCanonicalJson(pretty, pretty.size()));
}

void EtherCATSemanticRuntimeTests::testCanonicalJsonRejectsAmbiguity()
{
    const std::array<QByteArray, 17> invalidJson{
        QByteArray(),
        QByteArray("{\"a\":1,\"a\":2}"),
        QByteArray("{\"a\":1,\"\\u0061\":2}"),
        QByteArray("{\"outer\":{\"a\":1,\"a\":2}}"),
        QByteArray("{\"float\":1.0}"),
        QByteArray("{\"exponent\":1e2}"),
        QByteArray("{\"overflow\":18446744073709551616}"),
        QByteArray("{\"underflow\":-9223372036854775809}"),
        QByteArray("{\"unterminated\":"),
        QByteArray("{\"invalid\":\"\xc0\xaf\"}"),
        QByteArray("\xef\xbb\xbf{\"a\":1}\n"),
        QByteArray("{\"a\":1}\r\n"),
        QByteArray("{\"a\":1}"),
        QByteArray("{\"a\":1}\n\n"),
        QByteArray("{ \"a\":1}\n"),
        QByteArray("{\"b\":1,\"a\":2}\n"),
        QByteArray("{\"unicode\":\"\xf0\x9f\x98\x80\"}\n"),
    };

    for (const QByteArray &input : invalidJson)
        QVERIFY(!parseCanonicalJson(input, qMax<qsizetype>(input.size(), 1)));

    QByteArray tooDeep;
    for (int depth = 0; depth < 65; ++depth)
        tooDeep.append('[');
    tooDeep.append('0');
    for (int depth = 0; depth < 65; ++depth)
        tooDeep.append(']');
    QVERIFY(!parseStrictJson(tooDeep, tooDeep.size()));

    StrictJson floating = StrictJson::object();
    floating["value"] = 1.5;
    QVERIFY(!serializeCanonicalJson(floating, 128));

    const StrictJson valid = StrictJson::object({{"value", 1}});
    QVERIFY(!serializeCanonicalJson(valid, 11));
    const Utils::Result<QByteArray> exactLimit = serializeCanonicalJson(valid, 12);
    QVERIFY_RESULT(exactLimit);
    QCOMPARE(*exactLimit, QByteArray("{\"value\":1}\n"));
}

void EtherCATSemanticRuntimeTests::testCanonicalJsonTransferredManifests()
{
    const std::array<QString, 2> manifests{
        "testdata/api035-manifest.json",
        "testdata/api036-manifest.json",
    };
    for (const QString &path : manifests) {
        const QByteArray bytes = readTestData(path);
        QVERIFY2(!bytes.isEmpty(), qPrintable(path));
        const Utils::Result<StrictJson> parsed = parseCanonicalJson(bytes, 1024 * 1024);
        QVERIFY_RESULT(parsed);
        const Utils::Result<QByteArray> serialized = serializeCanonicalJson(*parsed, 1024 * 1024);
        QVERIFY_RESULT(serialized);
        QCOMPARE(*serialized, bytes);
    }
}

namespace {

constexpr std::array<const char *, 6> canonicalTestEntryNames{
    "manifest.json",
    "capability.bin",
    "configuration.ecfg",
    "runtime.erun",
    "compile_report.json",
    "manifest.sig",
};

struct CanonicalTestEcpkg
{
    QByteArray wire;
    std::array<qsizetype, 6> localOffsets;
    std::array<qsizetype, 6> dataOffsets;
    std::array<qsizetype, 6> centralOffsets;
    qsizetype centralOffset = 0;
    qsizetype eocdOffset = 0;
};

static void appendLe16(QByteArray &bytes, quint16 value)
{
    bytes.append(char(value & 0xff));
    bytes.append(char((value >> 8) & 0xff));
}

static void appendLe32(QByteArray &bytes, quint32 value)
{
    bytes.append(char(value & 0xff));
    bytes.append(char((value >> 8) & 0xff));
    bytes.append(char((value >> 16) & 0xff));
    bytes.append(char((value >> 24) & 0xff));
}

static void putLe16(QByteArray &bytes, qsizetype offset, quint16 value)
{
    bytes[offset] = char(value & 0xff);
    bytes[offset + 1] = char((value >> 8) & 0xff);
}

static void putLe32(QByteArray &bytes, qsizetype offset, quint32 value)
{
    bytes[offset] = char(value & 0xff);
    bytes[offset + 1] = char((value >> 8) & 0xff);
    bytes[offset + 2] = char((value >> 16) & 0xff);
    bytes[offset + 3] = char((value >> 24) & 0xff);
}

static quint32 testZipCrc32(QByteArrayView bytes)
{
    quint32 crc = std::numeric_limits<quint32>::max();
    for (char byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) {
            const quint32 lowBitMask = quint32(0) - (crc & 1);
            crc = (crc >> 1) ^ (0xedb88320 & lowBitMask);
        }
    }
    return ~crc;
}

static std::array<QByteArray, 6> canonicalTestPayloads()
{
    return {
        QByteArray("{\"format\":\"test\"}\n"),
        QByteArray::fromHex("01020304"),
        QByteArray::fromHex("454346470100"),
        QByteArray::fromHex("4552554e0100"),
        QByteArray("{\"result\":\"ok\"}\n"),
        QByteArray(64, char(0xa5)),
    };
}

static std::array<QByteArray, 6> canonicalTestNames()
{
    std::array<QByteArray, 6> names;
    for (std::size_t index = 0; index < names.size(); ++index)
        names[index] = canonicalTestEntryNames[index];
    return names;
}

static CanonicalTestEcpkg buildCanonicalTestEcpkg(
    const std::array<QByteArray, 6> &payloads = canonicalTestPayloads(),
    const std::array<QByteArray, 6> &names = canonicalTestNames())
{
    CanonicalTestEcpkg package;
    std::array<quint32, 6> crcs;

    for (std::size_t index = 0; index < payloads.size(); ++index) {
        const QByteArray &name = names[index];
        const QByteArray &payload = payloads[index];
        package.localOffsets[index] = package.wire.size();
        crcs[index] = testZipCrc32(payload);

        appendLe32(package.wire, 0x04034b50);
        appendLe16(package.wire, 20);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 33);
        appendLe32(package.wire, crcs[index]);
        appendLe32(package.wire, quint32(payload.size()));
        appendLe32(package.wire, quint32(payload.size()));
        appendLe16(package.wire, quint16(name.size()));
        appendLe16(package.wire, 0);
        package.wire.append(name);
        package.dataOffsets[index] = package.wire.size();
        package.wire.append(payload);
    }

    package.centralOffset = package.wire.size();
    for (std::size_t index = 0; index < payloads.size(); ++index) {
        const QByteArray &name = names[index];
        const QByteArray &payload = payloads[index];
        package.centralOffsets[index] = package.wire.size();

        appendLe32(package.wire, 0x02014b50);
        appendLe16(package.wire, 0x0314);
        appendLe16(package.wire, 20);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 33);
        appendLe32(package.wire, crcs[index]);
        appendLe32(package.wire, quint32(payload.size()));
        appendLe32(package.wire, quint32(payload.size()));
        appendLe16(package.wire, quint16(name.size()));
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 0);
        appendLe16(package.wire, 0);
        appendLe32(package.wire, 0x81a40000);
        appendLe32(package.wire, quint32(package.localOffsets[index]));
        package.wire.append(name);
    }

    package.eocdOffset = package.wire.size();
    appendLe32(package.wire, 0x06054b50);
    appendLe16(package.wire, 0);
    appendLe16(package.wire, 0);
    appendLe16(package.wire, 6);
    appendLe16(package.wire, 6);
    appendLe32(package.wire, quint32(package.eocdOffset - package.centralOffset));
    appendLe32(package.wire, quint32(package.centralOffset));
    appendLe16(package.wire, 0);
    return package;
}

static QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

static quint32 testReadLe32(QByteArrayView bytes, qsizetype offset)
{
    return quint32(quint8(bytes[offset])) | (quint32(quint8(bytes[offset + 1])) << 8)
           | (quint32(quint8(bytes[offset + 2])) << 16)
           | (quint32(quint8(bytes[offset + 3])) << 24);
}

static void putLe64(QByteArray &bytes, qsizetype offset, quint64 value)
{
    putLe32(bytes, offset, quint32(value));
    putLe32(bytes, offset + 4, quint32(value >> 32));
}

static quint32 testCrc32c(QByteArrayView bytes)
{
    quint32 crc = std::numeric_limits<quint32>::max();
    for (qsizetype index = 0; index < bytes.size(); ++index) {
        const quint8 value = index >= 116 && index < 120 ? 0 : quint8(bytes[index]);
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit) {
            const quint32 lowBitMask = quint32(0) - (crc & 1U);
            crc = (crc >> 1) ^ (0x82f63b78U & lowBitMask);
        }
    }
    return ~crc;
}

static void refreshEcfgEnvelope(QByteArray &configuration)
{
    const quint32 payloadOffset = testReadLe32(configuration, 108);
    const quint32 payloadBytes = testReadLe32(configuration, 112);
    const QByteArray payloadSha256 = QCryptographicHash::hash(
        QByteArrayView(configuration).sliced(payloadOffset, payloadBytes),
        QCryptographicHash::Sha256);
    configuration.replace(68, payloadSha256.size(), payloadSha256);
    putLe32(configuration, 116, testCrc32c(configuration));
}

static void refreshResourceTableSection(
    QByteArray &configuration, const EcfgSectionDescriptor &section)
{
    const qsizetype recordsOffset = section.offset + 80;
    const qsizetype recordsBytes = section.length - 80;
    const QByteArray recordsSha256 = QCryptographicHash::hash(
        QByteArrayView(configuration).sliced(recordsOffset, recordsBytes),
        QCryptographicHash::Sha256);
    configuration.replace(section.offset + 40, recordsSha256.size(), recordsSha256);
    quint64 catalogRevision = 0;
    for (int index = 0; index < 8; ++index)
        catalogRevision |= quint64(quint8(recordsSha256[index])) << (index * 8);
    putLe64(configuration, section.offset + 16, catalogRevision ? catalogRevision : 1);
}

static void refreshOutputPolicySection(
    QByteArray &configuration, const EcfgSectionDescriptor &section)
{
    const qsizetype recordsOffset = section.offset + 64;
    const qsizetype recordsBytes = section.length - 64;
    const QByteArray recordsSha256 = QCryptographicHash::hash(
        QByteArrayView(configuration).sliced(recordsOffset, recordsBytes),
        QCryptographicHash::Sha256);
    configuration.replace(section.offset + 24, recordsSha256.size(), recordsSha256);
}

} // namespace

void EtherCATSemanticRuntimeTests::testEcfgTransferredConfigurations()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff"));
    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");

    struct Fixture
    {
        QString directory;
        QString packageName;
        quint16 formatMinor = 0;
        QByteArray configurationSha256;
        quint64 catalogRevision = 0;
        qsizetype policyCount = 0;
    };
    const std::array<Fixture, 2> fixtures{
        Fixture{
            "api035",
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg",
            1,
            fromHex("3df29b74313a43678d751fb5646c7cac1f851f518c95bab64aee2c5edda9ac71"),
            0x0dea3816a0a0d7afULL,
            0,
        },
        Fixture{
            "api036",
            "three-slave-output-transaction-cfg3501.ecpkg",
            2,
            fromHex("0e7f51d93400224bc1dfc362359525815222e78c44e83bc6794922c26702c99d"),
            0xc84fe35be276b2a8ULL,
            1,
        },
    };

    bool foundFixture = false;
    for (const Fixture &fixture : fixtures) {
        const QDir directory(fixtureRoot.absoluteFilePath(fixture.directory));
        const QByteArray packageBytes
            = readFile(directory.absoluteFilePath(fixture.packageName));
        if (packageBytes.isEmpty())
            continue;
        foundFixture = true;

        const Utils::Result<EcpkgContainer> container
            = parseCanonicalEcpkgContainer(packageBytes);
        QVERIFY_RESULT(container);
        const Utils::Result<VerifiedSignedEcpkgManifest> manifest
            = verifySignedEcpkgManifest(
                *container, {{publicKey, EcpkgTrustClass::Production}});
        QVERIFY_RESULT(manifest);
        const Utils::Result<EcfgConfiguration> configuration
            = parseStrictEcfgConfiguration(container->configurationEcfg);
        QVERIFY_RESULT(configuration);

        QCOMPARE(configuration->formatMajor, quint16(1));
        QCOMPARE(configuration->formatMinor, fixture.formatMinor);
        QCOMPARE(configuration->configurationId, quint64(3501));
        QCOMPARE(configuration->configurationSha256, fixture.configurationSha256);
        QCOMPARE(configuration->configurationSha256, manifest->configuration.sha256);
        QCOMPARE(quint32(container->configurationEcfg.size()), manifest->configuration.bytes);
        QCOMPARE(configuration->processInputBits, quint32(480));
        QCOMPARE(configuration->processOutputBits, quint32(400));
        QCOMPARE(configuration->catalogRevision, fixture.catalogRevision);
        QCOMPARE(configuration->topologyIdentity, quint64(0x2ee7c79bc774840cULL));
        QCOMPARE(configuration->resources.size(), qsizetype(56));
        QCOMPARE(configuration->outputPolicies.size(), fixture.policyCount);
        QVERIFY(manifest->semanticBinding.has_value());
        QCOMPARE(
            configuration->resourceRecordsSha256,
            manifest->semanticBinding->resourceRecordsSha256);
        QCOMPARE(
            configuration->resourceTableSectionSha256,
            manifest->semanticBinding->resourceSectionSha256);
        QCOMPARE(
            configuration->catalogRevision, manifest->semanticBinding->catalogRevision);
        QCOMPARE(
            configuration->topologyIdentity, manifest->semanticBinding->topologyIdentity);

        if (fixture.policyCount == 1) {
            const EcfgOutputGroupPolicy &policy = configuration->outputPolicies.constFirst();
            QCOMPARE(policy.consistencyGroupId, quint32(13825));
            QCOMPARE(policy.flags, quint32(1));
            QCOMPARE(policy.recoveryPolicy, EcfgOutputRecoveryPolicy::ReturnTask);
            QCOMPARE(policy.maximumTtlCycles, quint32(1000));
            QCOMPARE(policy.resourceCount, quint32(2));
            QCOMPARE(policy.resourceIds.size(), qsizetype(2));
        }
    }

    if (!foundFixture)
        QSKIP("Transferred API-035/API-036 ECFG fixtures are not present");
}

void EtherCATSemanticRuntimeTests::testEcfgRejectsDeepMutations()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureDirectory(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff/api036"));
    const QByteArray original
        = readFile(fixtureDirectory.absoluteFilePath("configuration.ecfg"));
    if (original.isEmpty())
        QSKIP("Transferred API-036 ECFG fixture is not present");

    const Utils::Result<EcfgConfiguration> parsed = parseStrictEcfgConfiguration(original);
    QVERIFY_RESULT(parsed);
    QCOMPARE(parsed->sections.size(), qsizetype(8));

    QByteArray truncated = original;
    truncated.chop(1);
    QVERIFY(!parseStrictEcfgConfiguration(truncated));
    QVERIFY(!parseStrictEcfgConfiguration(original, original.size() - 1));

    QByteArray badCrc = original;
    badCrc[130] ^= 1;
    QVERIFY(!parseStrictEcfgConfiguration(badCrc));

    QByteArray reservedHeader = original;
    reservedHeader[120] = 1;
    refreshEcfgEnvelope(reservedHeader);
    QVERIFY(!parseStrictEcfgConfiguration(reservedHeader));

    QByteArray unknownSection = original;
    putLe16(unknownSection, 128 + 7 * 16, 9);
    refreshEcfgEnvelope(unknownSection);
    QVERIFY(!parseStrictEcfgConfiguration(unknownSection));

    const EcfgSectionDescriptor resourceSection = parsed->sections.at(6);
    QByteArray duplicateResource = original;
    const qsizetype firstResource = resourceSection.offset + 80;
    const qsizetype secondResource = firstResource + 64;
    duplicateResource.replace(
        secondResource, 8, QByteArrayView(duplicateResource).sliced(firstResource, 8));
    refreshResourceTableSection(duplicateResource, resourceSection);
    refreshEcfgEnvelope(duplicateResource);
    QVERIFY(!parseStrictEcfgConfiguration(duplicateResource));

    const EcfgSectionDescriptor policySection = parsed->sections.at(7);
    QByteArray reservedPolicy = original;
    reservedPolicy[policySection.offset + 64 + 20] = 1;
    refreshOutputPolicySection(reservedPolicy, policySection);
    refreshEcfgEnvelope(reservedPolicy);
    QVERIFY(!parseStrictEcfgConfiguration(reservedPolicy));

    QByteArray wrongGroupDigest = original;
    wrongGroupDigest[policySection.offset + 64 + 32] ^= 1;
    refreshOutputPolicySection(wrongGroupDigest, policySection);
    refreshEcfgEnvelope(wrongGroupDigest);
    QVERIFY(!parseStrictEcfgConfiguration(wrongGroupDigest));
}

void EtherCATSemanticRuntimeTests::testEcpkgContainerCanonicalMinimal()
{
    const std::array<QByteArray, 6> payloads = canonicalTestPayloads();
    const CanonicalTestEcpkg package = buildCanonicalTestEcpkg(payloads);

    const Utils::Result<EcpkgContainer> parsed = parseCanonicalEcpkgContainer(package.wire);
    QVERIFY_RESULT(parsed);
    QCOMPARE(
        parsed->packageSha256, QCryptographicHash::hash(package.wire, QCryptographicHash::Sha256));
    QCOMPARE(parsed->manifestJson, payloads[0]);
    QCOMPARE(parsed->capabilityBin, payloads[1]);
    QCOMPARE(parsed->configurationEcfg, payloads[2]);
    QCOMPARE(parsed->runtimeErun, payloads[3]);
    QCOMPARE(parsed->compileReportJson, payloads[4]);
    QCOMPARE(parsed->manifestSignature, payloads[5]);

    const Utils::Result<EcpkgContainer> explicitLimit
        = parseCanonicalEcpkgContainer(package.wire, package.wire.size());
    QVERIFY_RESULT(explicitLimit);
}

void EtherCATSemanticRuntimeTests::testEcpkgContainerRejectsMetadataMutations()
{
    const CanonicalTestEcpkg package = buildCanonicalTestEcpkg();

    const auto rejectMutation = [&package](const char *description, const auto &mutate) {
        QByteArray altered = package.wire;
        mutate(altered);
        const Utils::Result<EcpkgContainer> parsed = parseCanonicalEcpkgContainer(altered);
        QVERIFY2(!parsed, description);
    };

    const qsizetype local = package.localOffsets[0];
    rejectMutation("local magic", [local](QByteArray &bytes) { putLe32(bytes, local, 0x04034b51); });
    rejectMutation("local version needed", [local](QByteArray &bytes) {
        putLe16(bytes, local + 4, 21);
    });
    rejectMutation("local flags/data descriptor", [local](QByteArray &bytes) {
        putLe16(bytes, local + 6, 0x0008);
    });
    rejectMutation("local compression method", [local](QByteArray &bytes) {
        putLe16(bytes, local + 8, 8);
    });
    rejectMutation("local DOS time", [local](QByteArray &bytes) { putLe16(bytes, local + 10, 1); });
    rejectMutation("local DOS date", [local](QByteArray &bytes) { putLe16(bytes, local + 12, 34); });
    rejectMutation("local CRC", [local](QByteArray &bytes) { putLe32(bytes, local + 14, 1); });
    rejectMutation("local compressed size", [local](QByteArray &bytes) {
        putLe32(bytes, local + 18, 1);
    });
    rejectMutation("local uncompressed size", [local](QByteArray &bytes) {
        putLe32(bytes, local + 22, 1);
    });
    rejectMutation("local name length", [local](QByteArray &bytes) {
        putLe16(bytes, local + 26, 1);
    });
    rejectMutation("local extra length", [local](QByteArray &bytes) {
        putLe16(bytes, local + 28, 1);
    });
    rejectMutation("local name", [local](QByteArray &bytes) { bytes[local + 30] ^= 1; });

    const qsizetype central = package.centralOffsets[0];
    rejectMutation("central magic", [central](QByteArray &bytes) {
        putLe32(bytes, central, 0x02014b51);
    });
    rejectMutation("central version made by", [central](QByteArray &bytes) {
        putLe16(bytes, central + 4, 0x0014);
    });
    rejectMutation("central version needed", [central](QByteArray &bytes) {
        putLe16(bytes, central + 6, 21);
    });
    rejectMutation("central flags", [central](QByteArray &bytes) {
        putLe16(bytes, central + 8, 1);
    });
    rejectMutation("central compression method", [central](QByteArray &bytes) {
        putLe16(bytes, central + 10, 8);
    });
    rejectMutation("central DOS time", [central](QByteArray &bytes) {
        putLe16(bytes, central + 12, 1);
    });
    rejectMutation("central DOS date", [central](QByteArray &bytes) {
        putLe16(bytes, central + 14, 34);
    });
    rejectMutation("central CRC", [central](QByteArray &bytes) { putLe32(bytes, central + 16, 1); });
    rejectMutation("central compressed size", [central](QByteArray &bytes) {
        putLe32(bytes, central + 20, 1);
    });
    rejectMutation("central uncompressed size", [central](QByteArray &bytes) {
        putLe32(bytes, central + 24, 1);
    });
    rejectMutation("central name length", [central](QByteArray &bytes) {
        putLe16(bytes, central + 28, 1);
    });
    rejectMutation("central extra length", [central](QByteArray &bytes) {
        putLe16(bytes, central + 30, 1);
    });
    rejectMutation("central comment length", [central](QByteArray &bytes) {
        putLe16(bytes, central + 32, 1);
    });
    rejectMutation("central disk", [central](QByteArray &bytes) {
        putLe16(bytes, central + 34, 1);
    });
    rejectMutation("central internal attributes", [central](QByteArray &bytes) {
        putLe16(bytes, central + 36, 1);
    });
    rejectMutation("central external attributes", [central](QByteArray &bytes) {
        putLe32(bytes, central + 38, 0x81ed0000);
    });
    rejectMutation("central local offset", [central](QByteArray &bytes) {
        putLe32(bytes, central + 42, 1);
    });
    rejectMutation("central name", [central](QByteArray &bytes) { bytes[central + 46] ^= 1; });

    const qsizetype eocd = package.eocdOffset;
    rejectMutation("EOCD magic", [eocd](QByteArray &bytes) { putLe32(bytes, eocd, 0x06054b51); });
    rejectMutation("EOCD disk", [eocd](QByteArray &bytes) { putLe16(bytes, eocd + 4, 1); });
    rejectMutation("EOCD central disk", [eocd](QByteArray &bytes) { putLe16(bytes, eocd + 6, 1); });
    rejectMutation("EOCD disk entry count", [eocd](QByteArray &bytes) {
        putLe16(bytes, eocd + 8, 5);
    });
    rejectMutation("EOCD total entry count", [eocd](QByteArray &bytes) {
        putLe16(bytes, eocd + 10, 5);
    });
    rejectMutation("EOCD central size", [eocd](QByteArray &bytes) { putLe32(bytes, eocd + 12, 1); });
    rejectMutation("EOCD central offset", [eocd](QByteArray &bytes) {
        putLe32(bytes, eocd + 16, 1);
    });
    rejectMutation("EOCD archive comment", [eocd](QByteArray &bytes) {
        putLe16(bytes, eocd + 20, 1);
    });

    rejectMutation("payload CRC", [&package](QByteArray &bytes) {
        bytes[package.dataOffsets[0]] ^= 1;
    });
}

void EtherCATSemanticRuntimeTests::testEcpkgContainerRejectsLayoutsAndLimits()
{
    const CanonicalTestEcpkg package = buildCanonicalTestEcpkg();
    const auto reject = [](const QByteArray &wire, const char *description) {
        const Utils::Result<EcpkgContainer> parsed = parseCanonicalEcpkgContainer(wire);
        QVERIFY2(!parsed, description);
    };

    std::array<QByteArray, 6> reorderedNames = canonicalTestNames();
    std::swap(reorderedNames[0], reorderedNames[1]);
    reject(buildCanonicalTestEcpkg(canonicalTestPayloads(), reorderedNames).wire, "entry order");

    std::array<QByteArray, 6> duplicateNames = canonicalTestNames();
    duplicateNames[1] = duplicateNames[0];
    reject(buildCanonicalTestEcpkg(canonicalTestPayloads(), duplicateNames).wire, "duplicate entry");

    std::array<QByteArray, 6> unsafeNames = canonicalTestNames();
    unsafeNames[0] = "../manifest.json";
    reject(buildCanonicalTestEcpkg(canonicalTestPayloads(), unsafeNames).wire, "zip slip name");

    QByteArray prefix = package.wire;
    prefix.prepend('\0');
    for (std::size_t index = 0; index < package.centralOffsets.size(); ++index) {
        putLe32(
            prefix,
            package.centralOffsets[index] + 1 + 42,
            quint32(package.localOffsets[index] + 1));
    }
    putLe32(prefix, package.eocdOffset + 1 + 16, quint32(package.centralOffset + 1));
    reject(prefix, "leading bytes");

    QByteArray gap = package.wire;
    gap.insert(package.centralOffset, '\0');
    putLe32(gap, package.eocdOffset + 1 + 16, quint32(package.centralOffset + 1));
    reject(gap, "gap before central directory");

    QByteArray overlap = package.wire;
    putLe32(overlap, package.centralOffsets[1] + 42, quint32(package.localOffsets[0]));
    reject(overlap, "overlapping local entries");

    QByteArray trailing = package.wire;
    trailing.append('\0');
    reject(trailing, "trailing bytes");

    QByteArray zip64Size = package.wire;
    putLe32(zip64Size, package.centralOffsets[0] + 20, 0xffffffff);
    putLe32(zip64Size, package.centralOffsets[0] + 24, 0xffffffff);
    reject(zip64Size, "ZIP64 size marker");

    QByteArray zip64Offset = package.wire;
    putLe32(zip64Offset, package.centralOffsets[0] + 42, 0xffffffff);
    reject(zip64Offset, "ZIP64 offset marker");

    QByteArray zip64Count = package.wire;
    putLe16(zip64Count, package.eocdOffset + 8, 0xffff);
    putLe16(zip64Count, package.eocdOffset + 10, 0xffff);
    reject(zip64Count, "ZIP64 count marker");

    const std::array<qsizetype, 9> truncationPoints{
        0,
        1,
        21,
        package.localOffsets[0] + 29,
        package.dataOffsets[0],
        package.centralOffset,
        package.centralOffsets[0] + 45,
        package.eocdOffset,
        package.wire.size() - 1,
    };
    for (qsizetype bytes : truncationPoints)
        reject(package.wire.first(bytes), "truncated package");

    QVERIFY(!parseCanonicalEcpkgContainer(package.wire, 0));
    QVERIFY(!parseCanonicalEcpkgContainer(package.wire, -1));
    QVERIFY(!parseCanonicalEcpkgContainer(package.wire, package.wire.size() - 1));
    const QByteArray tooLarge(defaultMaximumEcpkgContainerBytes + 1, '\0');
    QVERIFY(!parseCanonicalEcpkgContainer(tooLarge));
    QVERIFY(!parseCanonicalEcpkgContainer(tooLarge, tooLarge.size()));

    const std::array<qsizetype, 5> entryLimits{
        1024 * 1024,
        2 * 1024 * 1024,
        2 * 1024 * 1024,
        2 * 1024 * 1024,
        8 * 1024 * 1024,
    };
    for (std::size_t index = 0; index < entryLimits.size(); ++index) {
        std::array<QByteArray, 6> payloads = canonicalTestPayloads();
        payloads[index] = QByteArray(entryLimits[index] + 1, char(index + 1));
        reject(buildCanonicalTestEcpkg(payloads).wire, "entry size limit");
    }

    std::array<QByteArray, 6> maximumPayloads = canonicalTestPayloads();
    for (std::size_t index = 0; index < entryLimits.size(); ++index)
        maximumPayloads[index] = QByteArray(entryLimits[index], char(index + 1));
    const Utils::Result<EcpkgContainer> maximumEntries = parseCanonicalEcpkgContainer(
        buildCanonicalTestEcpkg(maximumPayloads).wire);
    QVERIFY_RESULT(maximumEntries);

    std::array<QByteArray, 6> shortSignature = canonicalTestPayloads();
    shortSignature[5].chop(1);
    reject(buildCanonicalTestEcpkg(shortSignature).wire, "short signature");

    std::array<QByteArray, 6> longSignature = canonicalTestPayloads();
    longSignature[5].append('\0');
    reject(buildCanonicalTestEcpkg(longSignature).wire, "long signature");

    for (std::size_t index = 0; index < canonicalTestEntryNames.size(); ++index) {
        std::array<QByteArray, 6> payloads = canonicalTestPayloads();
        payloads[index].clear();
        reject(buildCanonicalTestEcpkg(payloads).wire, "empty entry");
    }
}

void EtherCATSemanticRuntimeTests::testEcpkgContainerTransferredPackages()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QString fixtureRoot = repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff");

    struct Fixture
    {
        QString directory;
        QString packageName;
        QByteArray packageSha256;
    };
    const std::array<Fixture, 2> fixtures{
        Fixture{
            "api035",
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg",
            QByteArray::fromHex("b41d1fe06960c94df6730ca36a5c7505c30f905f155c47f00228c5f5eaf13dee"),
        },
        Fixture{
            "api036",
            "three-slave-output-transaction-cfg3501.ecpkg",
            QByteArray::fromHex("40222de1f5156556117ade48922ea2e2ed246ba0a51a3caa871131803987980b"),
        },
    };

    bool foundFixture = false;
    for (const Fixture &fixture : fixtures) {
        const QDir directory(QDir(fixtureRoot).absoluteFilePath(fixture.directory));
        const QString packagePath = directory.absoluteFilePath(fixture.packageName);
        if (!QFileInfo::exists(packagePath))
            continue;
        foundFixture = true;

        const QByteArray package = readFile(packagePath);
        QVERIFY2(!package.isEmpty(), qPrintable(packagePath));
        const Utils::Result<EcpkgContainer> parsed = parseCanonicalEcpkgContainer(package);
        QVERIFY_RESULT(parsed);
        QCOMPARE(parsed->packageSha256, fixture.packageSha256);
        QCOMPARE(parsed->manifestJson, readFile(directory.absoluteFilePath("manifest.json")));
        QCOMPARE(
            parsed->configurationEcfg, readFile(directory.absoluteFilePath("configuration.ecfg")));
        QCOMPARE(
            parsed->compileReportJson, readFile(directory.absoluteFilePath("compile_report.json")));
        QCOMPARE(parsed->manifestSignature, readFile(directory.absoluteFilePath("manifest.sig")));
    }

    if (!foundFixture)
        QSKIP("Transferred API-035/API-036 ECPKG fixtures are not present");
}

void EtherCATSemanticRuntimeTests::testEd25519Rfc8032()
{
    const QByteArray publicKey = fromHex(
        "d75a980182b10ab7d54bfed3c964073a"
        "0ee172f3daa62325af021a68f707511a");
    const QByteArray signature = fromHex(
        "e5564300c360ac729086e2cc806e828a"
        "84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46b"
        "d25bf5f0595bbe24655141438e7a100b");

    QVERIFY(verifyEd25519DetachedSignature(publicKey, signature, QByteArrayView()));

    const QByteArray oneBytePublicKey = fromHex(
        "3d4017c3e843895a92b70aa74d1b7ebc"
        "9c982ccf2ec4968cc0cd55f12af4660c");
    const QByteArray oneByteSignature = fromHex(
        "92a009a9f0d4cab8720e820b5f642540"
        "a2b27b5416503f8fb3762223ebdb69da"
        "085ac1e43e15996e458f3613d0f11d8c"
        "387b2eaeb4302aeeb00d291612bb0c00");
    const QByteArray oneByteMessage = fromHex("72");

    QVERIFY(verifyEd25519DetachedSignature(oneBytePublicKey, oneByteSignature, oneByteMessage));
}

void EtherCATSemanticRuntimeTests::testEd25519RejectsInvalidInputs()
{
    const QByteArray publicKey = fromHex(
        "d75a980182b10ab7d54bfed3c964073a"
        "0ee172f3daa62325af021a68f707511a");
    const QByteArray signature = fromHex(
        "e5564300c360ac729086e2cc806e828a"
        "84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46b"
        "d25bf5f0595bbe24655141438e7a100b");

    QByteArray wrongPublicKey = publicKey;
    wrongPublicKey[0] ^= 1;
    QVERIFY(!verifyEd25519DetachedSignature(wrongPublicKey, signature, QByteArrayView()));

    QByteArray wrongSignature = signature;
    wrongSignature[0] ^= 1;
    QVERIFY(!verifyEd25519DetachedSignature(publicKey, wrongSignature, QByteArrayView()));
    QVERIFY(!verifyEd25519DetachedSignature(publicKey, signature, QByteArray("x")));

    QVERIFY(!verifyEd25519DetachedSignature(publicKey.first(31), signature, QByteArrayView()));
    QByteArray oversizedPublicKey = publicKey;
    oversizedPublicKey.append('\0');
    QVERIFY(!verifyEd25519DetachedSignature(oversizedPublicKey, signature, QByteArrayView()));
    QVERIFY(!verifyEd25519DetachedSignature(publicKey, signature.first(63), QByteArrayView()));
    QByteArray oversizedSignature = signature;
    oversizedSignature.append('\0');
    QVERIFY(!verifyEd25519DetachedSignature(publicKey, oversizedSignature, QByteArrayView()));

    const QByteArray nonCanonicalSignature = fromHex(
        "e5564300c360ac729086e2cc806e828a"
        "84877f1eb8e5d974d873e06522490155"
        "4c8c7872aa064e049dbb3013fbf29380"
        "d25bf5f0595bbe24655141438e7a101b");
    QVERIFY(!verifyEd25519DetachedSignature(publicKey, nonCanonicalSignature, QByteArrayView()));
}

void EtherCATSemanticRuntimeTests::testEd25519TransferredManifests()
{
    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const QByteArray api035Signature = fromHex(
        "847ec27182b17f0db3b77d8aba662df2"
        "032e06075fe57e1738490707de63e62e2"
        "faafd22d696f25e1d078e59033d12844"
        "27e0a0bf63e6e7be659060ea48bf509");
    const QByteArray api036Signature = fromHex(
        "f3e6c1506b144b0ac47e20c0a6d92ad"
        "bf2eb60a9245d7c851f7abd5d92d72808"
        "afd7b953fe43680ae0f574b47b14053a"
        "5020b4e37b1b1d14fcc325f5d021db0b");
    const QByteArray api035Manifest = readTestData("testdata/api035-manifest.json");
    const QByteArray api036Manifest = readTestData("testdata/api036-manifest.json");

    QCOMPARE(
        QCryptographicHash::hash(publicKey, QCryptographicHash::Sha256).toHex(),
        QByteArray("eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6"));
    QCOMPARE(
        QCryptographicHash::hash(api035Signature, QCryptographicHash::Sha256).toHex(),
        QByteArray("921c5ab1c0dfd1744c99dd84c4717ecb863d4c673059dbd042d9119054e3b708"));
    QCOMPARE(
        QCryptographicHash::hash(api036Signature, QCryptographicHash::Sha256).toHex(),
        QByteArray("dd25d7f2ebdecb7f97b541510f2e0ca29b368715e624408f4affa8698d09fee9"));
    QCOMPARE(api035Manifest.size(), 4327);
    QCOMPARE(
        QCryptographicHash::hash(api035Manifest, QCryptographicHash::Sha256).toHex(),
        QByteArray("f82dcf1bd1188b1eb4648396f946b8db5828ebf15d0c22fcc50fa2d14a6de4c0"));
    QVERIFY(verifyEd25519DetachedSignature(publicKey, api035Signature, api035Manifest));

    QCOMPARE(api036Manifest.size(), 4328);
    QCOMPARE(
        QCryptographicHash::hash(api036Manifest, QCryptographicHash::Sha256).toHex(),
        QByteArray("9eb3fcc112dfa5e52d286eb5e257a75f9e81d4fb0ace64ea4efdba9c2c680dcf"));
    QVERIFY(verifyEd25519DetachedSignature(publicKey, api036Signature, api036Manifest));

    QByteArray alteredManifest = api036Manifest;
    alteredManifest[0] ^= 1;
    QVERIFY(!verifyEd25519DetachedSignature(publicKey, api036Signature, alteredManifest));
}

void EtherCATSemanticRuntimeTests::testSignedEcpkgTransferredPackages()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff"));
    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const QList<EcpkgTrustedPublicKey> productionTrust{
        {publicKey, EcpkgTrustClass::Production},
    };

    struct Fixture
    {
        QString directory;
        QString packageName;
        QByteArray packageSha256;
        QByteArray manifestSha256;
        QByteArray mappingSha256;
        QByteArray projectSha256;
        quint64 catalogRevision = 0;
        quint64 topologyIdentity = 0;
    };
    const std::array<Fixture, 2> fixtures{
        Fixture{
            "api035",
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg",
            fromHex("b41d1fe06960c94df6730ca36a5c7505c30f905f155c47f00228c5f5eaf13dee"),
            fromHex("f82dcf1bd1188b1eb4648396f946b8db5828ebf15d0c22fcc50fa2d14a6de4c0"),
            fromHex("1b9b8d93222fb199a2b0f767e22a4ed53dd8898c09cc2260a1c1e929f551c64f"),
            fromHex("3f649afb59281629bea3729d6ac9a23086d612537b73327ad04d3131e69fb8ea"),
            0x0dea3816a0a0d7afULL,
            0x2ee7c79bc774840cULL,
        },
        Fixture{
            "api036",
            "three-slave-output-transaction-cfg3501.ecpkg",
            fromHex("40222de1f5156556117ade48922ea2e2ed246ba0a51a3caa871131803987980b"),
            fromHex("9eb3fcc112dfa5e52d286eb5e257a75f9e81d4fb0ace64ea4efdba9c2c680dcf"),
            fromHex("d4143cbbae9ac312181b12210d107db46957fb3b84a2d7d8082e929e96c26f2e"),
            fromHex("71aca9908f319acafb31fbf46b60c2e7c5e66e34dbeaf2804ab125d363384ac0"),
            0xc84fe35be276b2a8ULL,
            0x2ee7c79bc774840cULL,
        },
    };

    bool foundFixture = false;
    for (const Fixture &fixture : fixtures) {
        const QDir directory(fixtureRoot.absoluteFilePath(fixture.directory));
        const QString packagePath = directory.absoluteFilePath(fixture.packageName);
        if (!QFileInfo::exists(packagePath))
            continue;
        foundFixture = true;

        const QByteArray packageBytes = readFile(packagePath);
        const Utils::Result<EcpkgContainer> container
            = parseCanonicalEcpkgContainer(packageBytes);
        QVERIFY_RESULT(container);
        const Utils::Result<VerifiedSignedEcpkgManifest> verified
            = verifySignedEcpkgManifest(*container, productionTrust);
        QVERIFY_RESULT(verified);
        QCOMPARE(verified->trust, EcpkgTrustClass::Production);
        QCOMPARE(verified->configurationId, quint64(3501));
        QCOMPARE(verified->packageSha256, fixture.packageSha256);
        QCOMPARE(verified->manifestSha256, fixture.manifestSha256);
        QCOMPARE(
            verified->signingKeyIdSha256,
            fromHex("eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6"));
        QCOMPARE(verified->compiledProjectSource.sha256, fixture.projectSha256);
        QVERIFY(verified->semanticBinding.has_value());
        QCOMPARE(verified->semanticBinding->formatVersion, quint16(1));
        QCOMPARE(verified->semanticBinding->bindingCount, quint32(56));
        QCOMPARE(verified->semanticBinding->catalogRevision, fixture.catalogRevision);
        QCOMPARE(verified->semanticBinding->topologyIdentity, fixture.topologyIdentity);
        QCOMPARE(verified->semanticBinding->mappingSha256, fixture.mappingSha256);

        const QByteArray projectBytes
            = readFile(directory.absoluteFilePath("project.json"));
        QVERIFY_RESULT(verifyEcpkgCompiledProjectSource(*verified, projectBytes));
        QByteArray alteredProject = projectBytes;
        alteredProject[0] ^= 1;
        QVERIFY(!verifyEcpkgCompiledProjectSource(*verified, alteredProject));

        const QList<EcpkgTrustedPublicKey> wrongTrust{
            {publicKey, EcpkgTrustClass::Engineering},
        };
        QVERIFY(!verifySignedEcpkgManifest(*container, wrongTrust));

        QByteArray wrongKey = publicKey;
        wrongKey[0] ^= 1;
        QVERIFY(!verifySignedEcpkgManifest(
            *container, {{wrongKey, EcpkgTrustClass::Production}}));

        EcpkgContainer alteredSignature = *container;
        alteredSignature.manifestSignature[0] ^= 1;
        QVERIFY(!verifySignedEcpkgManifest(alteredSignature, productionTrust));

        EcpkgContainer alteredPayload = *container;
        alteredPayload.compileReportJson[0] ^= 1;
        QVERIFY(!verifySignedEcpkgManifest(alteredPayload, productionTrust));

        EcpkgContainer unknownField = *container;
        unknownField.manifestJson.replace(
            QByteArray("{\"compiler\":"),
            QByteArray("{\"additional\":0,\"compiler\":"));
        QVERIFY(!verifySignedEcpkgManifest(unknownField, productionTrust));
    }

    if (!foundFixture)
        QSKIP("Transferred API-035/API-036 ECPKG fixtures are not present");
}

void EtherCATSemanticRuntimeTests::testSemanticBindingTransferredPackages()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff"));
    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const QList<EcpkgTrustedPublicKey> trust{
        {publicKey, EcpkgTrustClass::Production},
    };

    struct Fixture
    {
        QString directory;
        QString packageName;
        QByteArray mappingSha256;
    };
    const std::array<Fixture, 2> fixtures{
        Fixture{
            "api035",
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg",
            fromHex("1b9b8d93222fb199a2b0f767e22a4ed53dd8898c09cc2260a1c1e929f551c64f"),
        },
        Fixture{
            "api036",
            "three-slave-output-transaction-cfg3501.ecpkg",
            fromHex("d4143cbbae9ac312181b12210d107db46957fb3b84a2d7d8082e929e96c26f2e"),
        },
    };

    bool foundFixture = false;
    for (const Fixture &fixture : fixtures) {
        const QDir directory(fixtureRoot.absoluteFilePath(fixture.directory));
        const QByteArray packageBytes
            = readFile(directory.absoluteFilePath(fixture.packageName));
        if (packageBytes.isEmpty())
            continue;
        foundFixture = true;

        const Utils::Result<EcpkgContainer> container
            = parseCanonicalEcpkgContainer(packageBytes);
        QVERIFY_RESULT(container);
        const Utils::Result<VerifiedSignedEcpkgManifest> manifest
            = verifySignedEcpkgManifest(*container, trust);
        QVERIFY_RESULT(manifest);
        const Utils::Result<EcfgConfiguration> configuration
            = parseStrictEcfgConfiguration(container->configurationEcfg);
        QVERIFY_RESULT(configuration);
        const Utils::Result<VerifiedSemanticBindingArtifact> artifact
            = verifySemanticBindingArtifact(*container, *manifest, *configuration);
        QVERIFY_RESULT(artifact);

        QCOMPARE(artifact->trust, EcpkgTrustClass::Production);
        QCOMPARE(artifact->artifactSha256, fixture.mappingSha256);
        QCOMPARE(
            artifact->canonicalArtifact,
            readFile(directory.absoluteFilePath("semantic-binding-v1.json")));
        QCOMPARE(artifact->bindings.size(), qsizetype(56));
        QCOMPARE(artifact->topologyInstances.size(), qsizetype(3));
        QCOMPARE(artifact->configurationId, quint64(3501));
        QCOMPARE(artifact->catalogRevision, configuration->catalogRevision);
        QCOMPARE(artifact->topologyIdentity, configuration->topologyIdentity);

        const VerifiedSemanticBinding *axis0Controlword
            = artifact->findBySemanticSignalId(
                u"embedlabs:fixture:axis0:command:controlword");
        QVERIFY(axis0Controlword);
        QCOMPARE(axis0Controlword->resourceId, quint64(0x0997885c279b0862ULL));
        QCOMPARE(axis0Controlword->componentInstanceId, quint64(0xcbe141c7c6c9c773ULL));
        QCOMPARE(artifact->findByResourceId(axis0Controlword->resourceId), axis0Controlword);
        QVERIFY(!artifact->findBySemanticSignalId(u"embedlabs:fixture:missing"));
        QVERIFY(!artifact->findByResourceId(0));
    }

    if (!foundFixture)
        QSKIP("Transferred API-035/API-036 ECPKG fixtures are not present");
}

void EtherCATSemanticRuntimeTests::testSemanticBindingRejectsMismatches()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureDirectory(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff/api035"));
    const QByteArray packageBytes = readFile(
        fixtureDirectory.absoluteFilePath(
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg"));
    if (packageBytes.isEmpty())
        QSKIP("Transferred API-035 ECPKG fixture is not present");

    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const Utils::Result<EcpkgContainer> container
        = parseCanonicalEcpkgContainer(packageBytes);
    QVERIFY_RESULT(container);
    const Utils::Result<VerifiedSignedEcpkgManifest> manifest
        = verifySignedEcpkgManifest(
            *container, {{publicKey, EcpkgTrustClass::Production}});
    QVERIFY_RESULT(manifest);
    const Utils::Result<EcfgConfiguration> configuration
        = parseStrictEcfgConfiguration(container->configurationEcfg);
    QVERIFY_RESULT(configuration);
    QVERIFY_RESULT(verifySemanticBindingArtifact(*container, *manifest, *configuration));

    EcpkgContainer wrongPackage = *container;
    wrongPackage.packageSha256[0] ^= 1;
    QVERIFY(!verifySemanticBindingArtifact(wrongPackage, *manifest, *configuration));

    VerifiedSignedEcpkgManifest wrongMapping = *manifest;
    wrongMapping.semanticBinding->mappingSha256[0] ^= 1;
    QVERIFY(!verifySemanticBindingArtifact(*container, wrongMapping, *configuration));

    EcfgConfiguration missingResource = *configuration;
    missingResource.resources.removeLast();
    QVERIFY(!verifySemanticBindingArtifact(*container, *manifest, missingResource));

    EcfgConfiguration wrongResourceType = *configuration;
    wrongResourceType.resources[0].bitWidth += 1;
    QVERIFY(!verifySemanticBindingArtifact(*container, *manifest, wrongResourceType));

    EcpkgContainer changedReport = *container;
    changedReport.compileReportJson[0] ^= 1;
    QVERIFY(!verifySemanticBindingArtifact(changedReport, *manifest, *configuration));

    const Utils::Result<StrictJson> parsedReport
        = parseStrictJson(container->compileReportJson, container->compileReportJson.size());
    QVERIFY_RESULT(parsedReport);

    const auto rebuildReportEvidence =
        [&container, &manifest](StrictJson report) {
            VerifiedSignedEcpkgManifest rebuiltManifest = *manifest;
            const Utils::Result<QByteArray> canonicalArtifact = serializeCanonicalJson(
                report.at("semantic_binding_manifest"),
                defaultMaximumSemanticArtifactBytes);
            if (!canonicalArtifact)
                return std::optional<std::pair<EcpkgContainer, VerifiedSignedEcpkgManifest>>();
            const QByteArray mappingSha256 = QCryptographicHash::hash(
                *canonicalArtifact, QCryptographicHash::Sha256);
            report["semantic_binding_manifest_sha256"]
                = mappingSha256.toHex().toStdString();
            std::string reportText = report.dump(2);
            reportText.push_back('\n');

            EcpkgContainer rebuiltContainer = *container;
            rebuiltContainer.compileReportJson = QByteArray(
                reportText.data(), qsizetype(reportText.size()));
            rebuiltManifest.compileReport.bytes
                = quint32(rebuiltContainer.compileReportJson.size());
            rebuiltManifest.compileReport.sha256 = QCryptographicHash::hash(
                rebuiltContainer.compileReportJson, QCryptographicHash::Sha256);
            rebuiltManifest.semanticBinding->mappingSha256 = mappingSha256;
            return std::optional(std::pair(
                std::move(rebuiltContainer), std::move(rebuiltManifest)));
        };

    StrictJson wrongSymbol = *parsedReport;
    wrongSymbol["semantic_symbols"][0]["data_type"] = "s16";
    const auto wrongSymbolEvidence = rebuildReportEvidence(std::move(wrongSymbol));
    QVERIFY(wrongSymbolEvidence);
    QVERIFY(!verifySemanticBindingArtifact(
        wrongSymbolEvidence->first, wrongSymbolEvidence->second, *configuration));

    StrictJson duplicateBinding = *parsedReport;
    duplicateBinding["semantic_binding_manifest"]["bindings"][1]["semantic_signal_id"]
        = duplicateBinding["semantic_binding_manifest"]["bindings"][0]["semantic_signal_id"];
    const auto duplicateEvidence = rebuildReportEvidence(std::move(duplicateBinding));
    QVERIFY(duplicateEvidence);
    QVERIFY(!verifySemanticBindingArtifact(
        duplicateEvidence->first, duplicateEvidence->second, *configuration));

    StrictJson unknownField = *parsedReport;
    unknownField["semantic_binding_manifest"]["unexpected"] = 0;
    const auto unknownEvidence = rebuildReportEvidence(std::move(unknownField));
    QVERIFY(unknownEvidence);
    QVERIFY(!verifySemanticBindingArtifact(
        unknownEvidence->first, unknownEvidence->second, *configuration));
}

void EtherCATSemanticRuntimeTests::testVerifiedEcpkgStore()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureRoot(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff"));
    const QDir api035(fixtureRoot.absoluteFilePath("api035"));
    const QDir api036(fixtureRoot.absoluteFilePath("api036"));
    const QByteArray package035 = readFile(
        api035.absoluteFilePath(
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg"));
    const QByteArray project035 = readFile(api035.absoluteFilePath("project.json"));
    const QByteArray package036 = readFile(
        api036.absoluteFilePath("three-slave-output-transaction-cfg3501.ecpkg"));
    const QByteArray project036 = readFile(api036.absoluteFilePath("project.json"));
    if (package035.isEmpty() || project035.isEmpty() || package036.isEmpty()
        || project036.isEmpty()) {
        QSKIP("Transferred API-035/API-036 ECPKG fixtures are not present");
    }

    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const QList<EcpkgTrustedPublicKey> trust{
        {publicKey, EcpkgTrustClass::Production},
    };
    const QByteArray package035Sha256
        = fromHex("b41d1fe06960c94df6730ca36a5c7505c30f905f155c47f00228c5f5eaf13dee");
    const QByteArray mapping035
        = fromHex("1b9b8d93222fb199a2b0f767e22a4ed53dd8898c09cc2260a1c1e929f551c64f");

    const Utils::Result<VerifiedEcpkgPackage> verified035
        = verifyProductionEcpkg(package035, trust, project035);
    QVERIFY_RESULT(verified035);
    QCOMPARE(verified035->manifest.packageSha256, package035Sha256);
    const Utils::Result<VerifiedEcpkgPackage> verified036
        = verifyProductionEcpkg(package036, trust, project036);
    QVERIFY_RESULT(verified036);
    QVERIFY(verified036->manifest.packageSha256 != verified035->manifest.packageSha256);

    QByteArray wrongKey = publicKey;
    wrongKey[0] ^= 1;
    QVERIFY(!verifyProductionEcpkg(
        package035, {{wrongKey, EcpkgTrustClass::Production}}, project035));
    QVERIFY(!verifyProductionEcpkg(
        package035, {{publicKey, EcpkgTrustClass::Engineering}}, project035));
    QByteArray wrongProject = project035;
    wrongProject[0] ^= 1;
    QVERIFY(!verifyProductionEcpkg(package035, trust, wrongProject));

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString storeRoot = QDir(temporary.path()).absoluteFilePath("packages");
    const Utils::Result<VerifiedEcpkgPackage> imported
        = importVerifiedEcpkg(storeRoot, package035, trust, project035);
    QVERIFY_RESULT(imported);
    QVERIFY(QFileInfo::exists(imported->storedFilePath));
    QVERIFY(imported->storedFilePath.endsWith(
        QString::fromLatin1(package035Sha256.toHex()) + ".ecpkg"));

    const Utils::Result<VerifiedEcpkgPackage> repeated
        = importVerifiedEcpkg(storeRoot, package035, trust, project035);
    QVERIFY_RESULT(repeated);
    QCOMPARE(repeated->storedFilePath, imported->storedFilePath);
    QCOMPARE(repeated->packageBytes, package035);

    const Utils::Result<VerifiedEcpkgPackage> loaded
        = loadVerifiedEcpkgByPackageSha256(
            storeRoot, package035Sha256, trust, project035);
    QVERIFY_RESULT(loaded);
    QCOMPARE(loaded->storedFilePath, imported->storedFilePath);
    const Utils::Result<VerifiedEcpkgPackage> found
        = findVerifiedEcpkgBySemanticMapping(
            storeRoot, mapping035, trust, project035);
    QVERIFY_RESULT(found);
    QCOMPARE(found->manifest.packageSha256, package035Sha256);

    QTemporaryDir concurrentTemporary;
    QVERIFY(concurrentTemporary.isValid());
    const QString concurrentRoot
        = QDir(concurrentTemporary.path()).absoluteFilePath("packages");
    const auto importConcurrent = [&] {
        const Utils::Result<VerifiedEcpkgPackage> result
            = importVerifiedEcpkg(concurrentRoot, package035, trust, project035);
        return result ? result->storedFilePath : QString();
    };
    std::future<QString> first
        = std::async(std::launch::async, importConcurrent);
    std::future<QString> second
        = std::async(std::launch::async, importConcurrent);
    const QString firstPath = first.get();
    const QString secondPath = second.get();
    QVERIFY(!firstPath.isEmpty());
    QCOMPARE(secondPath, firstPath);
    QCOMPARE(
        QDir(QDir(concurrentRoot).filePath("sha256"))
            .entryList({"*.ecpkg"}, QDir::Files)
            .size(),
        qsizetype(1));
}

void EtherCATSemanticRuntimeTests::testVerifiedEcpkgStoreRejectsTampering()
{
    const QDir sourceDir(QString::fromUtf8(ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR));
    const QDir repositoryRoot(sourceDir.absoluteFilePath("../../.."));
    const QDir fixtureDirectory(
        repositoryRoot.absoluteFilePath("build/vendor_api_036_handoff/api035"));
    const QByteArray package = readFile(
        fixtureDirectory.absoluteFilePath(
            "three-slave-xb6-sv630n-semantic-binding-cfg3501.ecpkg"));
    const QByteArray project = readFile(fixtureDirectory.absoluteFilePath("project.json"));
    if (package.isEmpty() || project.isEmpty())
        QSKIP("Transferred API-035 ECPKG fixture is not present");

    const QByteArray publicKey = fromHex(
        "bd51c2d7cb14eeabac5de51ca5feb5f3"
        "6d3d87986b77f19d056a7da4845f7063");
    const QList<EcpkgTrustedPublicKey> trust{
        {publicKey, EcpkgTrustClass::Production},
    };
    const QByteArray packageSha256
        = fromHex("b41d1fe06960c94df6730ca36a5c7505c30f905f155c47f00228c5f5eaf13dee");

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString storeRoot = QDir(temporary.path()).absoluteFilePath("packages");
    const Utils::Result<VerifiedEcpkgPackage> imported
        = importVerifiedEcpkg(storeRoot, package, trust, project);
    QVERIFY_RESULT(imported);

    QFile stored(imported->storedFilePath);
    QVERIFY(stored.open(QIODevice::ReadWrite));
    const QByteArray firstByte = stored.read(1);
    QCOMPARE(firstByte.size(), qsizetype(1));
    QVERIFY(stored.seek(0));
    QCOMPARE(stored.write(QByteArray(1, char(firstByte.at(0) ^ 1))), qint64(1));
    stored.close();
    QVERIFY(!loadVerifiedEcpkgByPackageSha256(
        storeRoot, packageSha256, trust, project));

    QTemporaryDir malformedTemporary;
    QVERIFY(malformedTemporary.isValid());
    const QString malformedRoot
        = QDir(malformedTemporary.path()).absoluteFilePath("packages");
    const Utils::Result<VerifiedEcpkgPackage> valid
        = importVerifiedEcpkg(malformedRoot, package, trust, project);
    QVERIFY_RESULT(valid);
    QFile unexpected(
        QDir(QDir(malformedRoot).filePath("sha256")).filePath("unexpected"));
    QVERIFY(unexpected.open(QIODevice::WriteOnly));
    QCOMPARE(unexpected.write("x"), qint64(1));
    unexpected.close();
    QVERIFY(!findVerifiedEcpkgBySemanticMapping(
        malformedRoot,
        fromHex("1b9b8d93222fb199a2b0f767e22a4ed53dd8898c09cc2260a1c1e929f551c64f"),
        trust,
        project));
}

void EtherCATSemanticRuntimeTests::testPublishesOneProductionService()
{
    QList<Core::SemanticRuntimeService *> services;
    for (QObject *object : ExtensionSystem::PluginManager::allObjects()) {
        if (auto *service = qobject_cast<Core::SemanticRuntimeService *>(object))
            services.append(service);
    }

    QCOMPARE(services.size(), 1);
    QCOMPARE(
        ExtensionSystem::PluginManager::getObject<Core::SemanticRuntimeService>(),
        services.constFirst());
    QVERIFY(qobject_cast<SemanticRuntimeExecutor *>(services.constFirst()));
}

void EtherCATSemanticRuntimeTests::testProjectAndProviderLifecycle()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    SemanticRuntimeExecutor executor(&projects, registry);
    QSignalSpy contextsSpy(&executor, &Core::SemanticRuntimeService::contextsChanged);
    QVERIFY(contextsSpy.isValid());
    QVERIFY(executor.contexts().isEmpty());

    const Data::ProjectSnapshot project = testProject();
    const Data::ControllerConnectionScope scope = projectScope(project);
    projects.addProject(project);
    QCOMPARE(executor.contexts().size(), 1);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));

    projects.setAvailable(false);
    QVERIFY(executor.contexts().isEmpty());
    projects.setAvailable(true);
    QCOMPARE(executor.contexts().size(), 1);

    CountingControllerProvider provider("EtherCAT.SemanticRuntime.Tests.Lifecycle");
    provider.publishSnapshot(connectedSnapshot(scope, 7));
    provider.publishCatalog(resourceCatalog(scope, 7));
    RegisteredObject registration(&provider);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));

    provider.setAvailable(true);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    registration.remove();
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));

    projects.removeProject(project.id);
    QVERIFY(executor.contexts().isEmpty());
    QVERIFY(contextsSpy.count() >= 6);
    QCOMPARE(provider.mutationCalls, 0);
}

void EtherCATSemanticRuntimeTests::testStrictProviderCardinality()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    const Data::ProjectSnapshot project = testProject();
    const Data::ControllerConnectionScope scope = projectScope(project);
    projects.addProject(project);
    SemanticRuntimeExecutor executor(&projects, registry);

    CountingControllerProvider first("EtherCAT.SemanticRuntime.Tests.Cardinality.First");
    first.publishSnapshot(connectedSnapshot(scope, 9));
    first.publishCatalog(resourceCatalog(scope, 9));
    first.setAvailable(true);
    RegisteredObject firstRegistration(&first);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    CountingControllerProvider second("EtherCAT.SemanticRuntime.Tests.Cardinality.Second");
    second.publishSnapshot(connectedSnapshot(scope, 10));
    second.publishCatalog(resourceCatalog(scope, 10));
    second.setAvailable(true);
    RegisteredObject secondRegistration(&second);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderAmbiguous));

    secondRegistration.remove();
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    RegisteredObject secondRegistrationAgain(&second);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderAmbiguous));

    second.setAvailable(false);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    first.setAvailable(false);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));
    QCOMPARE(first.mutationCalls, 0);
    QCOMPARE(second.mutationCalls, 0);
}

void EtherCATSemanticRuntimeTests::testScopeSessionAndEpochInvalidation()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    const Data::ProjectSnapshot project = testProject();
    const Data::ControllerConnectionScope scope = projectScope(project);
    projects.addProject(project);
    SemanticRuntimeExecutor executor(&projects, registry);

    CountingControllerProvider provider("EtherCAT.SemanticRuntime.Tests.Epoch");
    provider.publishSnapshot(connectedSnapshot(scope, 11));
    const Data::RuntimeResourceCatalogEpoch firstEpoch = catalogEpoch(5);
    provider.publishCatalog(resourceCatalog(scope, 11, firstEpoch));
    provider.setAvailable(true);
    RegisteredObject registration(&provider);

    QCOMPARE(executor.contexts().constFirst().sessionGeneration, quint64(11));
    QCOMPARE(executor.contexts().constFirst().epoch, firstEpoch);

    const Data::ControllerConnectionScope otherScope{
        Data::NodeId::create(),
        Data::NodeId::create(),
    };
    provider.publishSnapshot(connectedSnapshot(otherScope, 12));
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::ControllerProviderUnavailable));

    provider.publishSnapshot(connectedSnapshot(scope, 12));
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourceCatalogStale));
    QCOMPARE(executor.contexts().constFirst().epoch, Data::RuntimeResourceCatalogEpoch());

    const Data::RuntimeResourceCatalogEpoch secondEpoch = catalogEpoch(6);
    provider.publishCatalog(resourceCatalog(scope, 12, secondEpoch));
    QCOMPARE(executor.contexts().constFirst().sessionGeneration, quint64(12));
    QCOMPARE(executor.contexts().constFirst().epoch, secondEpoch);

    Data::RuntimeResourceCatalogEpoch incompleteEpoch = secondEpoch;
    incompleteEpoch.catalogRevision = 0;
    provider.publishCatalog(resourceCatalog(scope, 12, incompleteEpoch));
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourceEpochIncomplete));
    QCOMPARE(executor.contexts().constFirst().epoch, Data::RuntimeResourceCatalogEpoch());
    QCOMPARE(provider.mutationCalls, 0);
}

void EtherCATSemanticRuntimeTests::testCandidateAdaptersNeverWrite()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    const Data::ProjectSnapshot project = testProject(true, true);
    const Data::ControllerConnectionScope scope = projectScope(project);
    QCOMPARE(project.slaves.size(), 2);
    QCOMPARE(
        project.slaves.at(0).adapterSelection.adapterId.value,
        QString("org.embedlabs.adapter.solidot.xb6-ec0002.rev1"));
    QCOMPARE(
        project.slaves.at(1).adapterSelection.adapterId.value,
        QString("org.embedlabs.adapter.inovance.sv630n-1axis.rev00010000"));
    projects.addProject(project);
    SemanticRuntimeExecutor executor(&projects, registry);

    CountingControllerProvider provider("EtherCAT.SemanticRuntime.Tests.Candidates");
    provider.publishSnapshot(connectedSnapshot(scope, 14));
    provider.publishCatalog(resourceCatalog(scope, 14));
    provider.setAvailable(true);
    RegisteredObject registration(&provider);

    const Data::SemanticRuntimeContext context = executor.contexts().constFirst();
    QVERIFY(!context.complete);
    QCOMPARE(context.detail, detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));
    QVERIFY(context.signalStates.isEmpty());
    QVERIFY(context.actionStates.isEmpty());

    Data::SemanticOperationRequest request;
    request.operationId.value = QStringLiteral("operation/candidate-adapters/1");
    request.target.controllerId = context.controllerId;
    request.target.scope = scope;
    Data::SemanticRuntimeActor actor;
    actor.id = QStringLiteral("test-user");

    const Data::SemanticOperationRecord submitted = executor.submit(request, actor);
    QCOMPARE(submitted.state, Data::SemanticOperationState::Rejected);
    QVERIFY(!submitted.executionAttempted);

    Data::SemanticOperationApprovalRequest approval;
    approval.operationId = request.operationId;
    const Data::SemanticOperationRecord approved = executor.approve(approval, actor);
    QCOMPARE(approved.state, Data::SemanticOperationState::Rejected);
    QVERIFY(!approved.executionAttempted);
    QVERIFY(!executor.operation(request.operationId));
    QVERIFY(executor.audit(context.controllerId).isEmpty());
    QCOMPARE(provider.mutationCalls, 0);
}

void EtherCATSemanticRuntimeTests::testMissingCatalogAndProofFailClosed()
{
    auto *registry = ExtensionSystem::PluginManager::getObject<Core::ProviderRegistry>();
    QVERIFY(registry);

    TestProjectService projects;
    Data::ProjectSnapshot project = testProject();
    const Data::ControllerConnectionScope scope = projectScope(project);
    projects.addProject(project);
    SemanticRuntimeExecutor executor(&projects, registry);

    CountingControllerProvider provider("EtherCAT.SemanticRuntime.Tests.FailClosed");
    provider.publishSnapshot(connectedSnapshot(scope, 16));
    provider.setAvailable(true);
    RegisteredObject registration(&provider);

    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourceCatalogUnavailable));

    Data::RuntimeResourceCatalog emptyCatalog = resourceCatalog(scope, 16);
    emptyCatalog.resources.clear();
    provider.publishCatalog(emptyCatalog);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourceCatalogEmpty));

    provider.publishCatalog(resourceCatalog(scope, 16));
    const Data::SemanticRuntimeContext proofless = executor.contexts().constFirst();
    QVERIFY(!proofless.complete);
    QCOMPARE(proofless.bindingVerification.state, Data::SemanticBindingVerificationState::Unverified);
    QCOMPARE(
        proofless.detail, detailFor(SemanticRuntimeContextIssue::SemanticBindingProofUnavailable));

    project.masterBindingArtifact = {};
    projects.changeProject(project);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::BindingArtifactMissing));

    project.masterBindingArtifact = bindingArtifact();
    project.masterBindingArtifact.artifactSha256.chop(1);
    projects.changeProject(project);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::BindingArtifactInvalid));

    project.masterBindingArtifact = bindingArtifact();
    projects.changeProject(project);
    provider.setRuntimeResourcesSupported(false);
    QCOMPARE(
        executor.contexts().constFirst().detail,
        detailFor(SemanticRuntimeContextIssue::RuntimeResourcesUnsupported));
    QCOMPARE(provider.mutationCalls, 0);
}

} // namespace EtherCAT::SemanticRuntime::Internal
