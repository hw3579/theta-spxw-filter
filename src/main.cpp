#include "theta_filter/core.hpp"
#include "theta_filter/protobuf.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = net::ip::tcp;

namespace {

struct Options {
    std::string ws_url{"ws://172.18.0.2:25520/v1/events"};
    std::string listen_host{"127.0.0.1"};
    unsigned short listen_port{25521};
    std::string symbol{"SPXW"};
    std::string expiration;
    std::filesystem::path archive_dir{"archive"};
    bool archive_enabled{true};
    std::size_t ring_capacity{10000};
    int reconnect_ms{1000};
    int duration_seconds{0};
};

struct WsEndpoint {
    std::string host;
    std::string port;
    std::string target;
};

std::atomic_bool* g_stop = nullptr;

void HandleSignal(int) {
    if (g_stop != nullptr) {
        g_stop->store(true);
    }
}

void PrintUsage(const char* program) {
    std::cout
        << "Usage: " << program << " --expiration YYYYMMDD [options]\n"
        << "  --ws-url URL              Theta FPSS ws:// endpoint\n"
        << "                            default ws://172.18.0.2:25520/v1/events\n"
        << "  --listen-host HOST        loopback host, default 127.0.0.1\n"
        << "  --listen-port PORT        HTTP port, default 25521\n"
        << "  --symbol SYMBOL           option root, default SPXW\n"
        << "  --expiration YYYYMMDD     required target expiration\n"
        << "  --archive-dir PATH        append-only event directory, default ./archive\n"
        << "  --no-archive              disable append-only event archive\n"
        << "  --ring-capacity N         bounded retained event count, default 10000\n"
        << "  --reconnect-ms N          reconnect pause, default 1000\n"
        << "  --duration-seconds N      0 runs until signal, default 0\n";
}

unsigned long long ParseUnsigned(const std::string& text, const char* option) {
    unsigned long long result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::runtime_error(std::string("Invalid value for ") + option + ": " + text);
    }
    return result;
}

Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            PrintUsage(argv[0]);
            std::exit(0);
        }
        if (argument == "--no-archive") {
            options.archive_enabled = false;
            continue;
        }
        if (index + 1 >= argc) {
            throw std::runtime_error("Missing value for " + argument);
        }
        const std::string value = argv[++index];
        if (argument == "--ws-url") {
            options.ws_url = value;
        } else if (argument == "--listen-host") {
            options.listen_host = value;
        } else if (argument == "--listen-port") {
            const auto port = ParseUnsigned(value, "--listen-port");
            if (port == 0 || port > 65535) {
                throw std::runtime_error("--listen-port must be in 1..65535");
            }
            options.listen_port = static_cast<unsigned short>(port);
        } else if (argument == "--symbol") {
            options.symbol = theta_filter::ToUpperAscii(value);
        } else if (argument == "--expiration") {
            options.expiration = value;
        } else if (argument == "--archive-dir") {
            options.archive_dir = value;
        } else if (argument == "--ring-capacity") {
            const auto capacity = ParseUnsigned(value, "--ring-capacity");
            if (capacity == 0 || capacity > 1000000) {
                throw std::runtime_error("--ring-capacity must be in 1..1000000");
            }
            options.ring_capacity = static_cast<std::size_t>(capacity);
        } else if (argument == "--reconnect-ms") {
            const auto milliseconds = ParseUnsigned(value, "--reconnect-ms");
            if (milliseconds == 0 || milliseconds > 600000) {
                throw std::runtime_error("--reconnect-ms must be in 1..600000");
            }
            options.reconnect_ms = static_cast<int>(milliseconds);
        } else if (argument == "--duration-seconds") {
            const auto seconds = ParseUnsigned(value, "--duration-seconds");
            if (seconds > 86400) {
                throw std::runtime_error("--duration-seconds must be in 0..86400");
            }
            options.duration_seconds = static_cast<int>(seconds);
        } else {
            throw std::runtime_error("Unknown argument: " + argument);
        }
    }

    if (!theta_filter::IsCompactDate(options.expiration)) {
        throw std::runtime_error("--expiration is required and must use YYYYMMDD");
    }
    if (options.symbol.empty()) {
        throw std::runtime_error("--symbol must not be empty");
    }
    if (options.listen_host != "127.0.0.1" && options.listen_host != "::1") {
        throw std::runtime_error("--listen-host must be loopback (127.0.0.1 or ::1)");
    }
    if (options.archive_enabled && options.archive_dir.empty()) {
        throw std::runtime_error("--archive-dir must not be empty");
    }
    return options;
}

WsEndpoint ParseWsEndpoint(const std::string& url) {
    constexpr std::string_view prefix{"ws://"};
    if (!url.starts_with(prefix)) {
        throw std::runtime_error("--ws-url must use ws://; this minimal collector does not accept wss://");
    }
    const std::string remainder = url.substr(prefix.size());
    const std::size_t slash = remainder.find('/');
    const std::string authority = slash == std::string::npos ? remainder : remainder.substr(0, slash);
    const std::string target = slash == std::string::npos ? "/" : remainder.substr(slash);
    const std::size_t colon = authority.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon == authority.size() - 1) {
        throw std::runtime_error("--ws-url must include host:port");
    }
    const std::string port = authority.substr(colon + 1);
    const auto numeric_port = ParseUnsigned(port, "WebSocket port");
    if (numeric_port == 0 || numeric_port > 65535) {
        throw std::runtime_error("WebSocket port must be in 1..65535");
    }
    return WsEndpoint{.host = authority.substr(0, colon), .port = port, .target = target};
}

class SharedState {
  public:
    SharedState(theta_filter::Target target, const std::filesystem::path& archive_directory, const bool archive_enabled, const std::size_t capacity)
        : target_(std::move(target)), ring_(capacity) {
        if (!archive_enabled) {
            return;
        }
        std::error_code error;
        std::filesystem::create_directories(archive_directory, error);
        if (error) {
            throw std::runtime_error("Unable to create archive directory: " + error.message());
        }
        archive_.open(archive_directory / "events.ndjson", std::ios::app);
        if (!archive_) {
            throw std::runtime_error("Unable to open archive events.ndjson");
        }
    }

    void SetConnection(const bool connected) {
        std::lock_guard lock(mutex_);
        connected_ = connected;
        if (!connected) {
            subscribed_ = false;
        }
    }

    void SetError(std::string error) {
        std::lock_guard lock(mutex_);
        last_error_ = std::move(error);
    }

    void SetSubscribed(const bool subscribed) {
        std::lock_guard lock(mutex_);
        subscribed_ = subscribed;
    }

    void HandleEvent(const nlohmann::json& event) {
        const std::string receipt = theta_filter::UtcNowIso8601();
        std::string type;
        if (event.contains("header") && event.at("header").is_object() &&
            event.at("header").contains("type") && event.at("header").at("type").is_string()) {
            type = theta_filter::ToUpperAscii(event.at("header").at("type").get<std::string>());
        }

        {
            std::lock_guard lock(mutex_);
            ++received_;
            if (type == "STATUS") {
                connected_ = true;
            }
            if (type == "REQ_RESPONSE" && event.at("header").value("response", std::string{}) == "SUBSCRIBED") {
                subscribed_ = true;
            }
        }

        const auto forwarded = theta_filter::FilterFpseEvent(event, target_, receipt);
        if (!forwarded.has_value()) {
            if (type == "TRADE" || type == "QUOTE") {
                std::lock_guard lock(mutex_);
                ++filtered_;
            }
            return;
        }

        const nlohmann::json serialized = forwarded->ToJson();
        std::lock_guard lock(mutex_);
        ring_.Push(*forwarded);
        ++forwarded_;
        last_event_time_ = forwarded->receipt_time_utc;
        if (archive_.is_open()) {
            archive_ << serialized.dump() << '\n';
            archive_.flush();
            if (!archive_) {
                last_error_ = "Unable to append archive event";
                archive_.clear();
            }
        }
    }

    nlohmann::json Health() const {
        std::lock_guard lock(mutex_);
        nlohmann::json result{
            {"connected", connected_},
            {"subscribed", subscribed_},
            {"received", received_},
            {"forwarded", forwarded_},
            {"filtered", filtered_},
            {"target", {{"symbol", target_.symbol}, {"expiration", target_.expiration}}},
            {"ring_size", ring_.size()},
            {"ring_capacity", ring_.capacity()},
        };
        if (last_event_time_.empty()) {
            result["last_event_time"] = nullptr;
        } else {
            result["last_event_time"] = last_event_time_;
        }
        if (last_error_.empty()) {
            result["last_error"] = nullptr;
        } else {
            result["last_error"] = last_error_;
        }
        return result;
    }

    std::vector<theta_filter::ForwardedEvent> Recent(const std::size_t limit) const {
        std::lock_guard lock(mutex_);
        return ring_.Latest(limit);
    }

    std::size_t RingCapacity() const {
        std::lock_guard lock(mutex_);
        return ring_.capacity();
    }

    void Flush() {
        std::lock_guard lock(mutex_);
        if (archive_.is_open()) {
            archive_.flush();
        }
    }

  private:
    mutable std::mutex mutex_;
    theta_filter::Target target_;
    theta_filter::BoundedEventRing ring_;
    std::ofstream archive_;
    bool connected_{false};
    bool subscribed_{false};
    std::size_t received_{0};
    std::size_t forwarded_{0};
    std::size_t filtered_{0};
    std::string last_event_time_;
    std::string last_error_;
};

std::string EventsBody(const std::vector<theta_filter::ForwardedEvent>& events) {
    std::string body;
    for (const auto& event : events) {
        body += event.ToJson().dump();
        body.push_back('\n');
    }
    return body;
}

std::optional<std::size_t> ParseEventsLimit(const std::string& target, const std::size_t maximum) {
    const std::size_t question = target.find('?');
    if (question == std::string::npos) {
        return std::min<std::size_t>(100, maximum);
    }
    const std::string query = target.substr(question + 1);
    constexpr std::string_view prefix{"limit="};
    if (!query.starts_with(prefix)) {
        return std::nullopt;
    }
    const std::string value = query.substr(prefix.size());
    unsigned long long parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed == 0 || parsed > maximum) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(parsed);
}

http::response<http::string_body> MakeResponse(
    const http::status status,
    const unsigned version,
    const std::string& body,
    const std::string& content_type) {
    http::response<http::string_body> response{status, version};
    response.set(http::field::server, "theta-spxw-filter");
    response.set(http::field::content_type, content_type);
    response.body() = body;
    response.prepare_payload();
    return response;
}

void HandleHttpClient(tcp::socket socket, const std::shared_ptr<SharedState>& state) {
    beast::flat_buffer buffer;
    http::request<http::string_body> request;
    boost::system::error_code error;
    http::read(socket, buffer, request, error);
    if (error) {
        return;
    }

    http::response<http::string_body> response;
    const std::string target{request.target().data(), request.target().size()};
    if (request.method() != http::verb::get) {
        response = MakeResponse(http::status::method_not_allowed, request.version(), "GET only\n", "text/plain");
    } else if (target == "/healthz") {
        response = MakeResponse(http::status::ok, request.version(), state->Health().dump() + "\n", "application/json");
    } else if (target == "/events" || target.starts_with("/events?")) {
        const auto limit = ParseEventsLimit(target, state->RingCapacity());
        if (!limit.has_value()) {
            response = MakeResponse(http::status::bad_request, request.version(), "invalid limit\n", "text/plain");
        } else {
            response = MakeResponse(
                http::status::ok,
                request.version(),
                EventsBody(state->Recent(*limit)),
                "application/x-ndjson");
        }
    } else if (target == "/events.pb" || target.starts_with("/events.pb?")) {
        const auto limit = ParseEventsLimit(target, state->RingCapacity());
        if (!limit.has_value()) {
            response = MakeResponse(http::status::bad_request, request.version(), "invalid limit\n", "text/plain");
        } else {
            response = MakeResponse(
                http::status::ok,
                request.version(),
                theta_filter::SerializeEventBatchProtobuf(state->Recent(*limit)),
                "application/x-protobuf");
        }
    } else {
        response = MakeResponse(http::status::not_found, request.version(), "not found\n", "text/plain");
    }
    http::write(socket, response, error);
    socket.shutdown(tcp::socket::shutdown_send, error);
}

void RunHttpServer(const Options& options, const std::shared_ptr<SharedState>& state, std::atomic_bool& stop) {
    try {
        net::io_context context{1};
        const auto address = net::ip::make_address(options.listen_host);
        tcp::acceptor acceptor{context};
        acceptor.open(address.is_v6() ? tcp::v6() : tcp::v4());
        acceptor.set_option(net::socket_base::reuse_address(true));
        acceptor.bind({address, options.listen_port});
        acceptor.listen(net::socket_base::max_listen_connections);
        acceptor.non_blocking(true);
        while (!stop.load()) {
            tcp::socket socket{context};
            boost::system::error_code error;
            acceptor.accept(socket, error);
            if (error == net::error::would_block || error == net::error::try_again) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                continue;
            }
            if (error) {
                state->SetError("HTTP accept: " + error.message());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            HandleHttpClient(std::move(socket), state);
        }
    } catch (const std::exception& error) {
        state->SetError(std::string("HTTP server: ") + error.what());
        stop.store(true);
    }
}

void RunWebSocketClient(const Options& options, const WsEndpoint& endpoint, const std::shared_ptr<SharedState>& state, std::atomic_bool& stop) {
    const nlohmann::json subscribe{
        {"msg_type", "STREAM_BULK"},
        {"sec_type", "OPTION"},
        {"req_type", "TRADE"},
        {"add", true},
        {"id", 0},
    };

    while (!stop.load()) {
        try {
            net::io_context context{1};
            tcp::resolver resolver{context};
            websocket::stream<beast::tcp_stream> stream{context};
            const auto endpoints = resolver.resolve(endpoint.host, endpoint.port);
            beast::get_lowest_layer(stream).connect(endpoints);
            stream.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
            stream.handshake(endpoint.host + ":" + endpoint.port, endpoint.target);
            state->SetConnection(true);
            stream.write(net::buffer(subscribe.dump()));
            stream.read_message_max(4 * 1024 * 1024);

            while (!stop.load()) {
                beast::flat_buffer buffer;
                boost::system::error_code error;
                stream.read(buffer, error);
                if (error == websocket::error::closed) {
                    break;
                }
                if (error) {
                    throw boost::system::system_error(error);
                }
                const std::string payload = beast::buffers_to_string(buffer.data());
                try {
                    const nlohmann::json event = nlohmann::json::parse(payload);
                    state->HandleEvent(event);
                    const auto& header = event.value("header", nlohmann::json::object());
                    if (header.value("type", std::string{}) == "REQ_RESPONSE") {
                        state->SetSubscribed(true);
                    }
                } catch (const std::exception& parse_error) {
                    state->SetError(std::string("FPSS JSON: ") + parse_error.what());
                }
            }
        } catch (const std::exception& error) {
            state->SetError(std::string("FPSS: ") + error.what());
        }
        state->SetConnection(false);
        if (!stop.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.reconnect_ms));
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = ParseOptions(argc, argv);
        const WsEndpoint endpoint = ParseWsEndpoint(options.ws_url);
        std::atomic_bool stop{false};
        g_stop = &stop;
        std::signal(SIGINT, HandleSignal);
        std::signal(SIGTERM, HandleSignal);

        const auto state = std::make_shared<SharedState>(
            theta_filter::Target{.symbol = options.symbol, .expiration = options.expiration},
            options.archive_dir,
            options.archive_enabled,
            options.ring_capacity);
        std::thread http_thread{RunHttpServer, std::cref(options), state, std::ref(stop)};
        std::thread websocket_thread{RunWebSocketClient, std::cref(options), std::cref(endpoint), state, std::ref(stop)};

        std::cout << "theta-spxw-filter started target=" << options.symbol << ':' << options.expiration
                  << " http=" << options.listen_host << ':' << options.listen_port
                  << " ws=" << options.ws_url << '\n';
        if (options.duration_seconds > 0) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(options.duration_seconds);
            while (!stop.load() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            stop.store(true);
        } else {
            while (!stop.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        }

        websocket_thread.join();
        http_thread.join();
        state->Flush();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
