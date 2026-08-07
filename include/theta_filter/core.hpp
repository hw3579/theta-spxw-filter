#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace theta_filter {

struct Target {
    std::string symbol;
    std::string expiration;
};

struct ForwardedEvent {
    std::string receipt_time_utc;
    std::string event_type;
    std::string symbol;
    std::string expiration;
    std::optional<long long> sequence;
    nlohmann::json raw;

    nlohmann::json ToJson() const;
};

bool IsCompactDate(const std::string& value);
std::optional<std::string> NormalizeExpiration(const nlohmann::json& value);
std::optional<ForwardedEvent> FilterFpseEvent(
    const nlohmann::json& event,
    const Target& target,
    const std::string& receipt_time_utc);
std::string UtcNowIso8601();
std::string ToUpperAscii(std::string value);

class BoundedEventRing {
  public:
    explicit BoundedEventRing(std::size_t capacity);

    void Push(ForwardedEvent event);
    std::vector<ForwardedEvent> Latest(std::size_t limit) const;
    std::size_t size() const;
    std::size_t capacity() const;

  private:
    std::size_t capacity_;
    std::deque<ForwardedEvent> events_;
};

}  // namespace theta_filter
