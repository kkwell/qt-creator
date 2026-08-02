// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatdata/runtimepackagecompiler.h>

#include <utils/filepath.h>
#include <utils/result.h>

namespace EtherCAT::ProjectCompiler {

struct CompilerInputProvisionedDevice
{
    Data::NodeId projectSlaveNodeId;
    QString slaveNodeId;
    QString projectDeviceId;
    quint16 expectedAlias = 0;
    QList<Data::DeviceModuleAssignment> expectedModuleAssignments;
    QMap<QString, QString> componentBindingIds;
    QMap<QString, QString> semanticBindingIds;
    QMap<QString, QString> semanticActionBindingIds;
    Data::RuntimePackageCompilerSymbolMode symbolMode
        = Data::RuntimePackageCompilerSymbolMode::Unknown;
    QMap<QString, QString> symbols;
    Data::RuntimePackageCompilerManualEnvelope manualEnvelope;
    Data::RuntimePackageCompilerSourceArtifact adapterSourceFile;

    bool isValid() const;

    friend bool operator==(
        const CompilerInputProvisionedDevice &, const CompilerInputProvisionedDevice &)
        = default;
};

// Exact administrator-provisioned, public compiler inputs. It deliberately
// has no private-key field and retains immutable file bytes rather than paths
// as compiler truth. validateCurrent() detects replacement before every build.
class CompilerInputProvisioningProfile
{
public:
    static Utils::Result<CompilerInputProvisioningProfile> load(
        const Utils::FilePath &profileFile);

    Utils::Result<> validateCurrent() const;

    Utils::FilePath profileFile() const;
    Data::RuntimePackageCompilerContractIdentity contractIdentity() const;
    QString projectId() const;
    QString masterNodeId() const;
    quint64 topologyTtlNs() const;
    Data::RuntimePackageCompilerCanonicalJson uiMetadata() const;
    Data::RuntimePackageCompilerSourceArtifacts sourceArtifacts() const;
    Data::RuntimePackageCompilerSignedTargetProfileEvidence targetProfile() const;
    const QList<CompilerInputProvisionedDevice> &devices() const;

private:
    Utils::FilePath m_profileFile;
    QByteArray m_exactProfileBytes;
    Data::RuntimePackageCompilerContractIdentity m_contractIdentity;
    QString m_projectId;
    QString m_masterNodeId;
    quint64 m_topologyTtlNs = 0;
    Data::RuntimePackageCompilerCanonicalJson m_uiMetadata;
    Data::RuntimePackageCompilerSourceArtifacts m_sourceArtifacts;
    Data::RuntimePackageCompilerSignedTargetProfileEvidence m_targetProfile;
    QList<CompilerInputProvisionedDevice> m_devices;
};

} // namespace EtherCAT::ProjectCompiler
