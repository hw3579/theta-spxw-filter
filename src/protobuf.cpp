#include "theta_filter/protobuf.hpp"

#include <chrono>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string_view>

namespace theta_filter {
namespace {

using Json = nlohmann::json;

const Json* ObjectMember(const Json& object, const std::string_view name) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto found = object.find(std::string{name});
    if (found == object.end() || !found->is_object()) {
        return nullptr;
    }
    return &found.value();
}

const Json* Payload(const ForwardedEvent& event) {
    if (!event.raw.is_object()) {
        return nullptr;
    }
    const std::string key = event.event_type == "QUOTE" ? "quote" : "trade";
    const auto found = event.raw.find(key);
    if (found == event.raw.end() || !found->is_object()) {
        return nullptr;
    }
    return &found.value();
}

std::optional<std::uint64_t> ParseUnsignedText(const std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::uint64_t> ReadUnsigned(
    const Json* object,
    const std::initializer_list<std::string_view> names) {
    if (object == nullptr || !object->is_object()) {
        return std::nullopt;
    }
    for (const std::string_view name : names) {
        const auto found = object->find(std::string{name});
        if (found == object->end()) {
            continue;
        }
        const Json& value = found.value();
        if (value.is_number_unsigned()) {
            return value.get<std::uint64_t>();
        }
        if (value.is_number_integer()) {
            const auto signed_value = value.get<std::int64_t>();
            if (signed_value >= 0) {
                return static_cast<std::uint64_t>(signed_value);
            }
            return std::nullopt;
        }
        if (value.is_string()) {
            return ParseUnsignedText(value.get_ref<const std::string&>());
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::int64_t> ReadSigned(
    const Json* object,
    const std::initializer_list<std::string_view> names) {
    if (object == nullptr || !object->is_object()) {
        return std::nullopt;
    }
    for (const std::string_view name : names) {
        const auto found = object->find(std::string{name});
        if (found == object->end()) {
            continue;
        }
        const Json& value = found.value();
        if (value.is_number_integer()) {
            return value.get<std::int64_t>();
        }
        if (value.is_number_unsigned()) {
            const auto unsigned_value = value.get<std::uint64_t>();
            if (unsigned_value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                return static_cast<std::int64_t>(unsigned_value);
            }
            return std::nullopt;
        }
        if (value.is_string()) {
            std::int64_t result = 0;
            const std::string& text = value.get_ref<const std::string&>();
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
            if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()) {
                return result;
            }
            return std::nullopt;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<double> ReadDouble(
    const Json* object,
    const std::initializer_list<std::string_view> names) {
    if (object == nullptr || !object->is_object()) {
        return std::nullopt;
    }
    for (const std::string_view name : names) {
        const auto found = object->find(std::string{name});
        if (found == object->end()) {
            continue;
        }
        const Json& value = found.value();
        if (value.is_number()) {
            return value.get<double>();
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::string> ReadString(
    const Json* object,
    const std::initializer_list<std::string_view> names) {
    if (object == nullptr || !object->is_object()) {
        return std::nullopt;
    }
    for (const std::string_view name : names) {
        const auto found = object->find(std::string{name});
        if (found == object->end()) {
            continue;
        }
        if (found->is_string()) {
            return found->get<std::string>();
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> ReadUnsignedPayloadOrRaw(
    const Json* payload,
    const Json& raw,
    const std::initializer_list<std::string_view> names) {
    if (const auto value = ReadUnsigned(payload, names); value.has_value()) {
        return value;
    }
    return ReadUnsigned(&raw, names);
}

std::optional<std::int64_t> ReadSignedPayloadOrRaw(
    const Json* payload,
    const Json& raw,
    const std::initializer_list<std::string_view> names) {
    if (const auto value = ReadSigned(payload, names); value.has_value()) {
        return value;
    }
    return ReadSigned(&raw, names);
}

std::optional<double> ReadDoublePayloadOrRaw(
    const Json* payload,
    const Json& raw,
    const std::initializer_list<std::string_view> names) {
    if (const auto value = ReadDouble(payload, names); value.has_value()) {
        return value;
    }
    return ReadDouble(&raw, names);
}

std::optional<std::uint32_t> ReadUint32PayloadOrRaw(
    const Json* payload,
    const Json& raw,
    const std::initializer_list<std::string_view> names) {
    const auto value = ReadUnsignedPayloadOrRaw(payload, raw, names);
    if (!value || *value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

void AppendVarint(std::string& output, std::uint64_t value) {
    while (value >= 0x80) {
        output.push_back(static_cast<char>((value & 0x7fU) | 0x80U));
        value >>= 7;
    }
    output.push_back(static_cast<char>(value));
}

void AppendTag(std::string& output, const std::uint32_t field_number, const std::uint32_t wire_type) {
    AppendVarint(output, (static_cast<std::uint64_t>(field_number) << 3) | wire_type);
}

void AppendUint32(std::string& output, const std::uint32_t field_number, const std::uint32_t value) {
    if (value == 0) {
        return;
    }
    AppendTag(output, field_number, 0);
    AppendVarint(output, value);
}

void AppendInt64(std::string& output, const std::uint32_t field_number, const std::int64_t value) {
    if (value == 0) {
        return;
    }
    AppendTag(output, field_number, 0);
    AppendVarint(output, static_cast<std::uint64_t>(value));
}

void AppendDouble(std::string& output, const std::uint32_t field_number, const double value) {
    if (value == 0.0) {
        return;
    }
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    AppendTag(output, field_number, 1);
    for (unsigned shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<char>((bits >> shift) & 0xffU));
    }
}

void AppendString(std::string& output, const std::uint32_t field_number, const std::string_view value) {
    if (value.empty()) {
        return;
    }
    AppendTag(output, field_number, 2);
    AppendVarint(output, value.size());
    output.append(value.data(), value.size());
}

void AppendMessage(std::string& output, const std::uint32_t field_number, const std::string& value) {
    AppendTag(output, field_number, 2);
    AppendVarint(output, value.size());
    output.append(value);
}

std::optional<int> ParseDigits(const std::string_view text, const std::size_t offset, const std::size_t count) {
    if (offset + count > text.size()) {
        return std::nullopt;
    }
    int result = 0;
    for (std::size_t index = offset; index < offset + count; ++index) {
        const unsigned char character = static_cast<unsigned char>(text[index]);
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        result = result * 10 + static_cast<int>(character - '0');
    }
    return result;
}

std::optional<std::uint64_t> ParseReceiptTimeUnixMs(const std::string_view text) {
    if (text.size() != 24 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':' || text[19] != '.' || text[23] != 'Z') {
        return std::nullopt;
    }
    const auto year = ParseDigits(text, 0, 4);
    const auto month = ParseDigits(text, 5, 2);
    const auto day = ParseDigits(text, 8, 2);
    const auto hour = ParseDigits(text, 11, 2);
    const auto minute = ParseDigits(text, 14, 2);
    const auto second = ParseDigits(text, 17, 2);
    const auto millisecond = ParseDigits(text, 20, 3);
    if (!year || !month || !day || !hour || !minute || !second || !millisecond ||
        *month < 1 || *month > 12 || *day < 1 || *day > 31 || *hour > 23 || *minute > 59 ||
        *second > 59 || *millisecond > 999) {
        return std::nullopt;
    }

    const std::chrono::year_month_day date{
        std::chrono::year{*year},
        std::chrono::month{static_cast<unsigned>(*month)},
        std::chrono::day{static_cast<unsigned>(*day)}};
    if (!date.ok()) {
        return std::nullopt;
    }
    const auto day_time = std::chrono::sys_days{date} + std::chrono::hours{*hour} +
        std::chrono::minutes{*minute} + std::chrono::seconds{*second} +
        std::chrono::milliseconds{*millisecond};
    const auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(day_time.time_since_epoch()).count();
    if (epoch_ms < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(epoch_ms);
}

std::uint32_t ReadExpiration(const ForwardedEvent& event) {
    const auto expiration = ParseUnsignedText(event.expiration);
    if (!expiration || *expiration > std::numeric_limits<std::uint32_t>::max()) {
        return 0;
    }
    return static_cast<std::uint32_t>(*expiration);
}

std::string EncodeIdentity(const ForwardedEvent& event) {
    std::string output;
    if (const auto receipt = ParseReceiptTimeUnixMs(event.receipt_time_utc); receipt.has_value()) {
        if (*receipt != 0) {
            AppendTag(output, 1, 0);
            AppendVarint(output, *receipt);
        }
    }
    AppendString(output, 2, event.symbol);
    AppendUint32(output, 3, ReadExpiration(event));

    const Json* contract = ObjectMember(event.raw, "contract");
    if (const auto right = ReadString(contract, {"right"}); right.has_value()) {
        AppendString(output, 4, *right);
    }
    if (const auto strike = ReadSigned(contract, {"strike"}); strike.has_value()) {
        AppendInt64(output, 5, *strike);
    }
    return output;
}

std::string EncodeTrade(const ForwardedEvent& event) {
    std::string output;
    const Json* payload = Payload(event);
    const Json& raw = event.raw;
    if (const auto sequence = ReadSignedPayloadOrRaw(payload, raw, {"sequence"}); sequence.has_value()) {
        AppendInt64(output, 1, *sequence);
    } else if (event.sequence.has_value()) {
        AppendInt64(output, 1, *event.sequence);
    }
    if (const auto date = ReadUint32PayloadOrRaw(payload, raw, {"date", "source_date"}); date.has_value()) {
        AppendUint32(output, 2, *date);
    }
    if (const auto ms_of_day = ReadUint32PayloadOrRaw(payload, raw, {"ms_of_day", "source_ms_of_day"}); ms_of_day.has_value()) {
        AppendUint32(output, 3, *ms_of_day);
    }
    if (const auto price = ReadDoublePayloadOrRaw(payload, raw, {"price"}); price.has_value()) {
        AppendDouble(output, 4, *price);
    }
    if (const auto size = ReadUint32PayloadOrRaw(payload, raw, {"size"}); size.has_value()) {
        AppendUint32(output, 5, *size);
    }
    if (const auto condition = ReadUint32PayloadOrRaw(payload, raw, {"condition"}); condition.has_value()) {
        AppendUint32(output, 6, *condition);
    }
    if (const auto exchange = ReadUint32PayloadOrRaw(payload, raw, {"exchange"}); exchange.has_value()) {
        AppendUint32(output, 7, *exchange);
    }
    return output;
}

std::string EncodeQuote(const ForwardedEvent& event) {
    std::string output;
    const Json* payload = Payload(event);
    const Json& raw = event.raw;
    if (const auto date = ReadUint32PayloadOrRaw(payload, raw, {"date", "source_date"}); date.has_value()) {
        AppendUint32(output, 1, *date);
    }
    if (const auto ms_of_day = ReadUint32PayloadOrRaw(payload, raw, {"ms_of_day", "source_ms_of_day"}); ms_of_day.has_value()) {
        AppendUint32(output, 2, *ms_of_day);
    }
    if (const auto bid = ReadDoublePayloadOrRaw(payload, raw, {"bid"}); bid.has_value()) {
        AppendDouble(output, 3, *bid);
    }
    if (const auto ask = ReadDoublePayloadOrRaw(payload, raw, {"ask"}); ask.has_value()) {
        AppendDouble(output, 4, *ask);
    }
    if (const auto bid_size = ReadUint32PayloadOrRaw(payload, raw, {"bid_size"}); bid_size.has_value()) {
        AppendUint32(output, 5, *bid_size);
    }
    if (const auto ask_size = ReadUint32PayloadOrRaw(payload, raw, {"ask_size"}); ask_size.has_value()) {
        AppendUint32(output, 6, *ask_size);
    }
    if (const auto bid_condition = ReadUint32PayloadOrRaw(payload, raw, {"bid_condition"}); bid_condition.has_value()) {
        AppendUint32(output, 7, *bid_condition);
    }
    if (const auto ask_condition = ReadUint32PayloadOrRaw(payload, raw, {"ask_condition"}); ask_condition.has_value()) {
        AppendUint32(output, 8, *ask_condition);
    }
    if (const auto bid_exchange = ReadUint32PayloadOrRaw(payload, raw, {"bid_exchange"}); bid_exchange.has_value()) {
        AppendUint32(output, 9, *bid_exchange);
    }
    if (const auto ask_exchange = ReadUint32PayloadOrRaw(payload, raw, {"ask_exchange"}); ask_exchange.has_value()) {
        AppendUint32(output, 10, *ask_exchange);
    }
    return output;
}

std::string EncodeEvent(const ForwardedEvent& event) {
    std::string output;
    AppendMessage(output, 1, EncodeIdentity(event));
    if (event.event_type == "QUOTE") {
        AppendMessage(output, 3, EncodeQuote(event));
    } else {
        AppendMessage(output, 2, EncodeTrade(event));
    }
    return output;
}

}  // namespace

std::string SerializeEventBatchProtobuf(const std::vector<ForwardedEvent>& events) {
    std::string output;
    AppendUint32(output, 1, 1);
    for (const ForwardedEvent& event : events) {
        AppendMessage(output, 2, EncodeEvent(event));
    }
    return output;
}

}  // namespace theta_filter
