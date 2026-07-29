// Copyright (C) 2026 Embed Labs

#pragma once

#include <utils/result.h>

#include <json/json.hpp>

#include <QByteArray>
#include <QByteArrayView>

namespace EtherCAT::SemanticRuntime::Internal {

using StrictJson = nlohmann::json;

Utils::Result<StrictJson> parseStrictJson(QByteArrayView input, qsizetype maximumInputBytes);

Utils::Result<StrictJson> parseCanonicalJson(QByteArrayView input, qsizetype maximumInputBytes);

Utils::Result<QByteArray> serializeCanonicalJson(
    const StrictJson &value, qsizetype maximumOutputBytes);

} // namespace EtherCAT::SemanticRuntime::Internal
