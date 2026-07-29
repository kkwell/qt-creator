// Copyright (C) 2026 Kvell

#pragma once

#include "deviceadapter.h"
#include "ethercatdata_global.h"
#include "nodeid.h"

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>

namespace EtherCAT::Data {

// Project selections retain only the immutable adapter inputs needed to reproduce a compilation.
// Runtime resource IDs, Process Image offsets, and controller epochs are deliberately excluded.
struct ETHERCATDATA_EXPORT DeviceAdapterProjectSelection
{
    DeviceAdapterId adapterId;
    QString adapterVersion;
    QByteArray adapterContentSha256;
    QString processDataProfileId;
    QList<DeviceModuleAssignment> moduleAssignments;

    friend bool operator==(
        const DeviceAdapterProjectSelection &, const DeviceAdapterProjectSelection &) = default;
};

// Compiler-produced explicit identity. Consumers must not infer this identity from a slave's
// position, display name, or vendor identity.
struct ETHERCATDATA_EXPORT SemanticProjectDeviceBinding
{
    NodeId slaveId;
    QString projectDeviceId;

    friend bool operator==(
        const SemanticProjectDeviceBinding &, const SemanticProjectDeviceBinding &)
        = default;
};

// This is an immutable compiler-produced reference. The project layer validates the digest shape
// but does not implement a second project-configuration hashing algorithm.
struct ETHERCATDATA_EXPORT SemanticBindingArtifactReference
{
    QString artifactId;
    QByteArray artifactSha256;
    QByteArray projectConfigurationSha256;
    QList<SemanticProjectDeviceBinding> projectDeviceBindings;

    friend bool operator==(
        const SemanticBindingArtifactReference &, const SemanticBindingArtifactReference &)
        = default;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::DeviceAdapterProjectSelection)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticProjectDeviceBinding)
Q_DECLARE_METATYPE(EtherCAT::Data::SemanticBindingArtifactReference)
