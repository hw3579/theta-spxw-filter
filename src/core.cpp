#include "theta_filter/core.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace theta_filter {
namespace {

std::optional<long long> FindSequence(const nlohmann::json& event, const std::string& type) {
    const std::string lower_type = ToUpperAscii(type);
    const char* payload_key = lower_type == "TRADE" ? "trade" : "quote";
    if (event.contains(payload_key) && event.at(payload_key).is_object()) {
        const auto& payload = event.at(payload_key);
        if (payload.contains("sequence") && payload.at("sequence").is_number_integer()) {
            return payload.at("sequence").get<long long>();
        }
    }
    if (event.contains("sequence") && event.at("sequence").is_number_integer()) {
        return event.at("sequence").get<long long>();
    }
    return std::nullopt;
}

std::string DigitsOnly(const std::string& value) {
    std::string digits;
    digits.reserve(value.size());
    for (const unsigned char character : value) {
        if (std::isdigit(character) != 0) {
            digits.push_back(static_cast<char>(character));
        }
    }
    return digits;
}

}  // namespace

std::string ToUpperAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

bool IsCompactDate(const std::string& value) {
    return value.size() == 8 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isdigit(character) != 0;
    });
}

std::optional<std::string> NormalizeExpiration(const nlohmann::json& value) {
    std::string normalized;
    if (value.is_number_integer()) {
        normalized = std::to_string(value.get<long long>());
    } else if (value.is_string()) {
        normalized = DigitsOnly(value.get<std::string>());
    } else {
        return std::nullopt;
    }
    if (!IsCompactDate(normalized)) {
        return std::nullopt;
    }
    return normalized;
}

std::string UtcNowIso8601() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t raw_time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&raw_time, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
           << std::setw(3) << std::setfill('0') << milliseconds.count() << 'Z';
    return output.str();
}

nlohmann::json ForwardedEvent::ToJson() const {
    nlohmann::json output{
        {"receipt_time_utc", receipt_time_utc},
        {"event_type", event_type},
        {"symbol", symbol},
        {"expiration", expiration},
        {"raw", raw},
    };
    if (sequence.has_value()) {
        output["sequence"] = *sequence;
    }
    return output;
}

std::optional<ForwardedEvent> FilterFpseEvent(
    const nlohmann::json& event,
    const Target& target,
    const std::string& receipt_time_utc) {
    if (!event.is_object() || !event.contains("header") || !event.at("header").is_object() ||
        !event.contains("contract") || !event.at("contract").is_object()) {
        return std::nullopt;
    }
    const auto& header = event.at("header");
    const auto& contract = event.at("contract");
    if (!header.contains("type") || !header.at("type").is_string()) {
        return std::nullopt;
    }
    const std::string type = ToUpperAscii(header.at("type").get<std::string>());
    if (type != "TRADE" && type != "QUOTE") {
        return std::nullopt;
    }

    const nlohmann::json* symbol_value = nullptr;
    if (contract.contains("root")) {
        symbol_value = &contract.at("root");
    } else if (contract.contains("symbol")) {
        symbol_value = &contract.at("symbol");
    }
    if (symbol_value == nullptr || !symbol_value->is_string()) {
        return std::nullopt;
    }
    if (!contract.contains("expiration")) {
        return std::nullopt;
    }
    const std::optional<std::string> expiration = NormalizeExpiration(contract.at("expiration"));
    if (!expiration.has_value() || ToUpperAscii(symbol_value->get<std::string>()) != ToUpperAscii(target.symbol) ||
        *expiration != target.expiration) {
        return std::nullopt;
    }

    return ForwardedEvent{
        .receipt_time_utc = receipt_time_utc,
        .event_type = type,
        .symbol = ToUpperAscii(symbol_value->get<std::string>()),
        .expiration = *expiration,
        .sequence = FindSequence(event, type),
        .raw = event,
    };
}

BoundedEventRing::BoundedEventRing(const std::size_t capacity) : capacity_(capacity) {}

void BoundedEventRing::Push(ForwardedEvent event) {
    if (capacity_ == 0) {
        return;
    }
    while (events_.size() >= capacity_) {
        events_.pop_front();
    }
    events_.push_back(std::move(event));
}

std::vector<ForwardedEvent> BoundedEventRing::Latest(const std::size_t limit) const {
    const std::size_t selected = std::min(limit, events_.size());
    const auto begin = events_.end() - static_cast<std::ptrdiff_t>(selected);
    return {begin, events_.end()};
}

std::size_t BoundedEventRing::size() const {
    return events_.size();
}

std::size_t BoundedEventRing::capacity() const {
    return capacity_;
}

}  // namespace theta_filter
