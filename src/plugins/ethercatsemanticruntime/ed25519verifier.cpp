// Copyright (C) 2026 Embed Labs

#include "ed25519verifier.h"

#include <monocypher-ed25519.h>

#include <cstddef>
#include <cstdint>

namespace EtherCAT::SemanticRuntime::Internal {

bool verifyEd25519DetachedSignature(
    QByteArrayView publicKey, QByteArrayView signature, QByteArrayView message)
{
    constexpr qsizetype publicKeyBytes = 32;
    constexpr qsizetype signatureBytes = 64;

    if (publicKey.size() != publicKeyBytes || signature.size() != signatureBytes)
        return false;

    const auto *publicKeyData = reinterpret_cast<const std::uint8_t *>(publicKey.data());
    const auto *signatureData = reinterpret_cast<const std::uint8_t *>(signature.data());
    const auto *messageData = reinterpret_cast<const std::uint8_t *>(message.data());

    return crypto_ed25519_check(
               signatureData, publicKeyData, messageData, static_cast<std::size_t>(message.size()))
           == 0;
}

} // namespace EtherCAT::SemanticRuntime::Internal
