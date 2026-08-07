#include "theta_filter/core.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

void Require(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

nlohmann::json Trade(const std::string& root, const nlohmann::json& expiration, const long long sequence) {
    return nlohmann::json{
        {"header", {{"type", "TRADE"}, {"status", "CONNECTED"}}},
        {"contract", {{"root", root}, {"expiration", expiration}, {"strike", 7800000}, {"right", "C"}}},
        {"trade", {{"sequence", sequence}, {"price", 1.25}, {"size", 3}}},
    };
}

}  // namespace

int main() {
    try {
        using theta_filter::BoundedEventRing;
        using theta_filter::FilterFpseEvent;
        using theta_filter::IsCompactDate;
        using theta_filter::NormalizeExpiration;
        using theta_filter::Target;

        Require(IsCompactDate("20260807"), "compact date rejected");
        Require(!IsCompactDate("2026-08-07"), "formatted date accepted as compact");
        Require(NormalizeExpiration("2026-08-07").value() == "20260807", "string expiration normalization failed");
        Require(NormalizeExpiration(20260807).value() == "20260807", "numeric expiration normalization failed");
        Require(!NormalizeExpiration("bad-date").has_value(), "invalid expiration accepted");

        const Target target{.symbol = "SPXW", .expiration = "20260807"};
        const auto target_event = FilterFpseEvent(Trade("spxw", "2026-08-07", 42), target, "2026-08-07T13:30:00.000Z");
        Require(target_event.has_value(), "target trade was filtered out");
        Require(target_event->sequence.has_value() && *target_event->sequence == 42, "sequence missing");
        Require(target_event->ToJson().at("raw").at("contract").at("root") == "spxw", "raw event not retained");
        Require(!FilterFpseEvent(Trade("SPXW", 20260808, 43), target, "now").has_value(), "other expiry forwarded");
        Require(!FilterFpseEvent(Trade("SPY", 20260807, 44), target, "now").has_value(), "other symbol forwarded");
        Require(!FilterFpseEvent(nlohmann::json{{"header", {{"type", "STATUS"}}}}, target, "now").has_value(), "status forwarded");
        Require(!FilterFpseEvent(nlohmann::json::parse("{\"broken\":true}"), target, "now").has_value(), "malformed event forwarded");

        BoundedEventRing ring{2};
        ring.Push(*target_event);
        ring.Push(*FilterFpseEvent(Trade("SPXW", 20260807, 45), target, "later"));
        ring.Push(*FilterFpseEvent(Trade("SPXW", 20260807, 46), target, "latest"));
        Require(ring.size() == 2, "ring capacity was not applied");
        const auto latest = ring.Latest(100);
        Require(latest.size() == 2 && latest.front().sequence == 45 && latest.back().sequence == 46, "ring order incorrect");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
