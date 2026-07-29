// Copyright (C) 2026 Embed Labs

#pragma once

#include <utils/result.h>

#include <QByteArray>
#include <QByteArrayView>

namespace EtherCAT::SemanticRuntime::Internal {

constexpr qsizetype defaultMaximumEcpkgContainerBytes = 16 * 1024 * 1024;

struct EcpkgContainer
{
    QByteArray packageSha256;
    QByteArray manifestJson;
    QByteArray capabilityBin;
    QByteArray configurationEcfg;
    QByteArray runtimeErun;
    QByteArray compileReportJson;
    QByteArray manifestSignature;
};

Utils::Result<EcpkgContainer> parseCanonicalEcpkgContainer(
    QByteArrayView package, qsizetype maximumPackageBytes = defaultMaximumEcpkgContainerBytes);

} // namespace EtherCAT::SemanticRuntime::Internal
