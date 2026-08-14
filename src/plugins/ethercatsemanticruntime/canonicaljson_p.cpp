// Copyright (C) 2026 Embed Labs

#include "canonicaljson_p.h"

#include <QString>

#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace EtherCAT::SemanticRuntime::Internal {

namespace {

constexpr std::size_t maximumJsonDepth = 64;

Utils::ResultError invalidJson(const QString &detail)
{
    return Utils::ResultError(QString::fromLatin1("Invalid JSON: %1").arg(detail));
}

Utils::ResultError invalidCanonicalJson(const QString &detail)
{
    return Utils::ResultError(QString::fromLatin1("Invalid canonical JSON: %1").arg(detail));
}

class StrictJsonSax
{
public:
    using binary_t = StrictJson::binary_t;
    using number_float_t = StrictJson::number_float_t;
    using number_integer_t = StrictJson::number_integer_t;
    using number_unsigned_t = StrictJson::number_unsigned_t;
    using string_t = StrictJson::string_t;

    bool null() { return appendValue(nullptr); }

    bool boolean(bool value) { return appendValue(value); }

    bool number_integer(number_integer_t value) { return appendValue(value); }

    bool number_unsigned(number_unsigned_t value) { return appendValue(value); }

    bool number_float(number_float_t, const string_t &)
    {
        return fail(QString::fromLatin1("floating-point values are not permitted"));
    }

    bool string(string_t &value) { return appendValue(std::move(value)); }

    bool binary(binary_t &) { return fail(QString::fromLatin1("binary values are not permitted")); }

    bool start_object(std::size_t) { return startContainer(ContainerKind::Object); }

    bool key(string_t &value)
    {
        if (m_frames.empty() || m_frames.back().kind != ContainerKind::Object)
            return fail(QString::fromLatin1("an object key appeared outside an object"));

        Frame &frame = m_frames.back();
        if (frame.hasPendingKey)
            return fail(QString::fromLatin1("an object key has no preceding value"));

        auto [iterator, inserted] = frame.objectKeys.insert(value);
        Q_UNUSED(iterator)
        if (!inserted)
            return fail(QString::fromLatin1("duplicate object keys are not permitted"));

        frame.pendingKey = std::move(value);
        frame.hasPendingKey = true;
        return true;
    }

    bool end_object() { return endContainer(ContainerKind::Object); }

    bool start_array(std::size_t) { return startContainer(ContainerKind::Array); }

    bool end_array() { return endContainer(ContainerKind::Array); }

    bool parse_error(
        std::size_t position, const std::string &, const nlohmann::detail::exception &exception)
    {
        if (m_error.isEmpty()) {
            m_error = QString::fromLatin1("parse error at byte %1: %2")
                          .arg(qulonglong(position))
                          .arg(QString::fromUtf8(exception.what()));
        }
        return false;
    }

    QString error() const { return m_error; }

    std::optional<StrictJson> takeResult() { return std::move(m_result); }

private:
    enum class ContainerKind {
        Array,
        Object,
    };

    struct Frame
    {
        ContainerKind kind = ContainerKind::Array;
        StrictJson value;
        std::set<std::string> objectKeys;
        std::string pendingKey;
        bool hasPendingKey = false;
    };

    bool fail(const QString &error)
    {
        if (m_error.isEmpty())
            m_error = error;
        return false;
    }

    bool startContainer(ContainerKind kind)
    {
        if (m_frames.size() >= maximumJsonDepth) {
            return fail(
                QString::fromLatin1("container nesting exceeds the maximum depth of %1")
                    .arg(maximumJsonDepth));
        }

        Frame frame;
        frame.kind = kind;
        frame.value = kind == ContainerKind::Object ? StrictJson::object() : StrictJson::array();
        m_frames.push_back(std::move(frame));
        return true;
    }

    bool endContainer(ContainerKind kind)
    {
        if (m_frames.empty() || m_frames.back().kind != kind)
            return fail(QString::fromLatin1("container boundaries are inconsistent"));
        if (m_frames.back().hasPendingKey)
            return fail(QString::fromLatin1("an object key is missing its value"));

        StrictJson value = std::move(m_frames.back().value);
        m_frames.pop_back();
        return appendValue(std::move(value));
    }

    template<typename Value>
    bool appendValue(Value &&value)
    {
        if (m_frames.empty()) {
            if (m_result)
                return fail(QString::fromLatin1("multiple root values are not permitted"));
            m_result.emplace(std::forward<Value>(value));
            return true;
        }

        Frame &frame = m_frames.back();
        if (frame.kind == ContainerKind::Array) {
            frame.value.get_ref<StrictJson::array_t &>().emplace_back(std::forward<Value>(value));
            return true;
        }

        if (!frame.hasPendingKey)
            return fail(QString::fromLatin1("an object value has no key"));

        frame.value.get_ref<StrictJson::object_t &>()
            .emplace(std::move(frame.pendingKey), StrictJson(std::forward<Value>(value)));
        frame.hasPendingKey = false;
        return true;
    }

    std::vector<Frame> m_frames;
    std::optional<StrictJson> m_result;
    QString m_error;
};

bool validateSerializableJson(const StrictJson &value, std::size_t depth, QString *error)
{
    if (value.is_number_float()) {
        *error = QString::fromLatin1("floating-point values are not permitted");
        return false;
    }
    if (value.is_binary()) {
        *error = QString::fromLatin1("binary values are not permitted");
        return false;
    }
    if (value.is_discarded()) {
        *error = QString::fromLatin1("discarded values are not permitted");
        return false;
    }

    if (!value.is_array() && !value.is_object())
        return true;
    if (depth >= maximumJsonDepth) {
        *error = QString::fromLatin1("container nesting exceeds the maximum depth of %1")
                     .arg(maximumJsonDepth);
        return false;
    }

    if (value.is_array()) {
        for (const StrictJson &item : value) {
            if (!validateSerializableJson(item, depth + 1, error))
                return false;
        }
        return true;
    }

    for (auto iterator = value.cbegin(); iterator != value.cend(); ++iterator) {
        if (!validateSerializableJson(iterator.value(), depth + 1, error))
            return false;
    }
    return true;
}

Utils::Result<StrictJson> parseStrictJsonImpl(QByteArrayView input)
{
    try {
        StrictJsonSax sax;
        const bool parsed = StrictJson::sax_parse(
            input.data(),
            input.data() + input.size(),
            &sax,
            StrictJson::input_format_t::json,
            true,
            false);
        if (!parsed)
            return invalidJson(
                sax.error().isEmpty() ? QString::fromLatin1("parsing failed") : sax.error());

        std::optional<StrictJson> result = sax.takeResult();
        if (!result)
            return invalidJson(QString::fromLatin1("the document has no root value"));
        return std::move(*result);
    } catch (const std::exception &exception) {
        return invalidJson(
            QString::fromLatin1("parsing failed: %1").arg(QString::fromUtf8(exception.what())));
    } catch (...) {
        return invalidJson(QString::fromLatin1("parsing failed with an unknown error"));
    }
}

} // namespace

Utils::Result<StrictJson> parseStrictJson(QByteArrayView input, qsizetype maximumInputBytes)
{
    if (maximumInputBytes <= 0)
        return invalidJson(QString::fromLatin1("the input size limit is invalid"));
    if (input.isEmpty())
        return invalidJson(QString::fromLatin1("the document is empty"));
    if (input.size() > maximumInputBytes)
        return invalidJson(QString::fromLatin1("the document exceeds the configured size limit"));

    return parseStrictJsonImpl(input);
}

Utils::Result<StrictJson> parseCanonicalJson(QByteArrayView input, qsizetype maximumInputBytes)
{
    if (maximumInputBytes <= 0)
        return invalidCanonicalJson(QString::fromLatin1("the input size limit is invalid"));
    if (input.isEmpty())
        return invalidCanonicalJson(QString::fromLatin1("the document is empty"));
    if (input.size() > maximumInputBytes) {
        return invalidCanonicalJson(
            QString::fromLatin1("the document exceeds the configured size limit"));
    }

    if (input.size() >= 3 && quint8(input[0]) == 0xef && quint8(input[1]) == 0xbb
        && quint8(input[2]) == 0xbf) {
        return invalidCanonicalJson(QString::fromLatin1("a UTF-8 BOM is not permitted"));
    }

    for (char byte : input) {
        if (quint8(byte) >= 0x80)
            return invalidCanonicalJson(
                QString::fromLatin1("the document must contain ASCII bytes"));
        if (byte == '\r')
            return invalidCanonicalJson(QString::fromLatin1("carriage returns are not permitted"));
    }

    if (input.back() != '\n') {
        return invalidCanonicalJson(
            QString::fromLatin1("the document must end with exactly one line feed"));
    }
    if (input.size() >= 2 && input[input.size() - 2] == '\n') {
        return invalidCanonicalJson(
            QString::fromLatin1("the document must end with exactly one line feed"));
    }

    Utils::Result<StrictJson> parsed = parseStrictJsonImpl(input);
    if (!parsed)
        return invalidCanonicalJson(parsed.error());

    Utils::Result<QByteArray> serialized = serializeCanonicalJson(*parsed, maximumInputBytes);
    if (!serialized)
        return invalidCanonicalJson(serialized.error());
    if (*serialized != input) {
        return invalidCanonicalJson(
            QString::fromLatin1(
                "the bytes do not match sorted compact ASCII JSON with one terminal line feed"));
    }

    return std::move(*parsed);
}

Utils::Result<QByteArray> serializeCanonicalJson(
    const StrictJson &value, qsizetype maximumOutputBytes)
{
    if (maximumOutputBytes <= 0)
        return invalidCanonicalJson(QString::fromLatin1("the output size limit is invalid"));

    try {
        QString validationError;
        if (!validateSerializableJson(value, 0, &validationError))
            return invalidCanonicalJson(validationError);

        const std::string serialized
            = value.dump(-1, ' ', true, StrictJson::error_handler_t::strict);
        const std::size_t maximumBytes = static_cast<std::size_t>(maximumOutputBytes);
        if (serialized.size() >= maximumBytes) {
            return invalidCanonicalJson(
                QString::fromLatin1("the serialized document exceeds the configured size limit"));
        }
        if (serialized.size() > static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
            return invalidCanonicalJson(QString::fromLatin1("the serialized document is too large"));
        }

        QByteArray result(serialized.data(), qsizetype(serialized.size()));
        result.append('\n');
        return result;
    } catch (const std::exception &exception) {
        return invalidCanonicalJson(
            QString::fromLatin1("serialization failed: %1").arg(QString::fromUtf8(exception.what())));
    } catch (...) {
        return invalidCanonicalJson(
            QString::fromLatin1("serialization failed with an unknown error"));
    }
}

} // namespace EtherCAT::SemanticRuntime::Internal
