// Copyright (C) 2026 Embed Labs

#pragma once

#include <QByteArrayView>

namespace EtherCAT::SemanticRuntime::Internal {

bool verifyEd25519DetachedSignature(
    QByteArrayView publicKey, QByteArrayView signature, QByteArrayView message);

} // namespace EtherCAT::SemanticRuntime::Internal
