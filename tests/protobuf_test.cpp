#include "theta_filter/protobuf.hpp"

#include "theta_events.pb.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

void Require(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

nlohmann::json TradeEvent() {
    return nlohmann::json{
        {"header", {{"status", "CONNECTED"}, {"type", "TRADE"}}},
        {"contract", {{"root", "SPXW"}, {"expiration", 20260807}, {"strike", 7800000}, {"right", "C"}}},
        {"trade", {
            {"date", 20260807},
            {"ms_of_day", 34200001},
            {"sequence", -563040482},
            {"size", 5},
            {"condition", 18},
            {"price", 1.06},
            {"exchange", 65},
        }},
    };
}

nlohmann::json QuoteEvent() {
    return nlohmann::json{
        {"header", {{"status", "CONNECTED"}, {"type", "QUOTE"}}},
        {"contract", {{"root", "SPXW"}, {"expiration", 20260807}, {"strike", 7800000}, {"right", "P"}}},
        {"quote", {
            {"date", 20260807},
            {"ms_of_day", 34200025},
            {"bid", 110.2},
            {"ask", 110.5},
            {"bid_size", 7},
            {"ask_size", 9},
            {"bid_condition", 50},
            {"ask_condition", 51},
            {"bid_exchange", 5},
            {"ask_exchange", 6},
        }},
    };
}

}  // namespace

int main() {
    try {
        const theta_filter::Target target{.symbol = "SPXW", .expiration = "20260807"};
        const auto trade = theta_filter::FilterFpseEvent(
            TradeEvent(), target, "2026-08-07T13:30:00.123Z");
        const auto quote = theta_filter::FilterFpseEvent(
            QuoteEvent(), target, "2026-08-07T13:30:00.124Z");
        Require(trade.has_value(), "trade fixture was filtered out");
        Require(quote.has_value(), "quote fixture was filtered out");

        const std::string wire = theta_filter::SerializeEventBatchProtobuf({*trade, *quote});
        theta_filter::v1::EventBatch batch;
        Require(batch.ParseFromString(wire), "official protobuf parser rejected sender wire format");
        Require(batch.schema_version() == 1, "schema version mismatch");
        Require(batch.events_size() == 2, "event count mismatch");

        const auto& parsed_trade = batch.events(0);
        Require(parsed_trade.has_identity(), "trade identity missing");
        Require(parsed_trade.identity().receipt_time_unix_ms() == 1786109400123ULL, "trade receipt timestamp mismatch");
        Require(parsed_trade.identity().symbol() == "SPXW", "trade symbol mismatch");
        Require(parsed_trade.identity().expiration() == 20260807, "trade expiration mismatch");
        Require(parsed_trade.identity().right() == "C", "trade right mismatch");
        Require(parsed_trade.identity().strike_milli() == 7800000, "trade strike mismatch");
        Require(parsed_trade.has_trade() && !parsed_trade.has_quote(), "trade oneof mismatch");
        Require(parsed_trade.trade().sequence() == -563040482, "trade sequence mismatch");
        Require(parsed_trade.trade().source_date() == 20260807, "trade source date mismatch");
        Require(parsed_trade.trade().source_ms_of_day() == 34200001, "trade source time mismatch");
        Require(std::abs(parsed_trade.trade().price() - 1.06) < 1e-12, "trade price mismatch");
        Require(parsed_trade.trade().size() == 5, "trade size mismatch");
        Require(parsed_trade.trade().condition() == 18, "trade condition mismatch");
        Require(parsed_trade.trade().exchange() == 65, "trade exchange mismatch");

        const auto& parsed_quote = batch.events(1);
        Require(parsed_quote.identity().receipt_time_unix_ms() == 1786109400124ULL, "quote receipt timestamp mismatch");
        Require(parsed_quote.identity().right() == "P", "quote right mismatch");
        Require(parsed_quote.has_quote() && !parsed_quote.has_trade(), "quote oneof mismatch");
        Require(parsed_quote.quote().source_date() == 20260807, "quote source date mismatch");
        Require(parsed_quote.quote().source_ms_of_day() == 34200025, "quote source time mismatch");
        Require(std::abs(parsed_quote.quote().bid() - 110.2) < 1e-12, "quote bid mismatch");
        Require(std::abs(parsed_quote.quote().ask() - 110.5) < 1e-12, "quote ask mismatch");
        Require(parsed_quote.quote().bid_size() == 7, "quote bid size mismatch");
        Require(parsed_quote.quote().ask_size() == 9, "quote ask size mismatch");
        Require(parsed_quote.quote().bid_condition() == 50, "quote bid condition mismatch");
        Require(parsed_quote.quote().ask_condition() == 51, "quote ask condition mismatch");
        Require(parsed_quote.quote().bid_exchange() == 5, "quote bid exchange mismatch");
        Require(parsed_quote.quote().ask_exchange() == 6, "quote ask exchange mismatch");

        theta_filter::v1::EventBatch expected;
        expected.set_schema_version(1);
        auto* expected_trade = expected.add_events();
        expected_trade->mutable_identity()->set_receipt_time_unix_ms(1786109400123ULL);
        expected_trade->mutable_identity()->set_symbol("SPXW");
        expected_trade->mutable_identity()->set_expiration(20260807);
        expected_trade->mutable_identity()->set_right("C");
        expected_trade->mutable_identity()->set_strike_milli(7800000);
        expected_trade->mutable_trade()->set_sequence(-563040482);
        expected_trade->mutable_trade()->set_source_date(20260807);
        expected_trade->mutable_trade()->set_source_ms_of_day(34200001);
        expected_trade->mutable_trade()->set_price(1.06);
        expected_trade->mutable_trade()->set_size(5);
        expected_trade->mutable_trade()->set_condition(18);
        expected_trade->mutable_trade()->set_exchange(65);
        auto* expected_quote = expected.add_events();
        expected_quote->mutable_identity()->set_receipt_time_unix_ms(1786109400124ULL);
        expected_quote->mutable_identity()->set_symbol("SPXW");
        expected_quote->mutable_identity()->set_expiration(20260807);
        expected_quote->mutable_identity()->set_right("P");
        expected_quote->mutable_identity()->set_strike_milli(7800000);
        expected_quote->mutable_quote()->set_source_date(20260807);
        expected_quote->mutable_quote()->set_source_ms_of_day(34200025);
        expected_quote->mutable_quote()->set_bid(110.2);
        expected_quote->mutable_quote()->set_ask(110.5);
        expected_quote->mutable_quote()->set_bid_size(7);
        expected_quote->mutable_quote()->set_ask_size(9);
        expected_quote->mutable_quote()->set_bid_condition(50);
        expected_quote->mutable_quote()->set_ask_condition(51);
        expected_quote->mutable_quote()->set_bid_exchange(5);
        expected_quote->mutable_quote()->set_ask_exchange(6);
        Require(wire == expected.SerializeAsString(), "sender wire differs from official protobuf serialization");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
