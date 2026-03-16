#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "macros.hpp"
#include "logger.hpp"
#include "OrderBook.hpp"
#include "TradeRingBuffer.hpp"
#include "LatencyLogger.hpp"

namespace cts {
namespace data {

namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;
namespace ssl = boost::asio::ssl;
namespace asio = boost::asio;
namespace http = boost::beast::http;

// Concrete type aliases
using tcp_resolver = asio::ip::tcp::resolver;
using tcp_stream = beast::tcp_stream;
using ssl_stream = beast::ssl_stream<tcp_stream>;
using ws_stream = websocket::stream<ssl_stream>;

class BinanceWebSocketClient {
public:
    BinanceWebSocketClient(
        OrderBook&      order_book,
        TradeRingBuffer& trade_buffer,
        LatencyLogger&  latency_logger
    )
        : order_book_(order_book),
        trade_buffer_(trade_buffer),
        latency_logger_(latency_logger),
        ssl_context_(ssl::context::tlsv12_client),
        ws_(asio::make_strand(io_context_), ssl_context_),
        resolver_(asio::make_strand(io_context_))
    {
        // Load system root certificates for TLS verification
        ssl_context_.set_default_verify_paths();
        ssl_context_.set_verify_mode(ssl::verify_peer);
    }

    // Non-copyable, non-movable — owns io_context and thread
    BinanceWebSocketClient(const BinanceWebSocketClient&) = delete;
    BinanceWebSocketClient& operator=(const BinanceWebSocketClient&) = delete;
    BinanceWebSocketClient(BinanceWebSocketClient&&) = delete;
    BinanceWebSocketClient& operator=(BinanceWebSocketClient&&) = delete;

    // Launches IO thread — returns immediately
    auto start() -> void {
        running_.store(true, std::memory_order_release);
        io_thread_ = std::thread([this]{ run(); });
    }

    // Signals shutdown and joins IO thread
    auto stop() -> void {
        running_.store(false, std::memory_order_release);
        io_context_.stop();
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
    }

    ~BinanceWebSocketClient() {
        if (running_.load(std::memory_order_acquire)) {
            stop();
        }
    }

private:
    // Entry point for IO thread
    auto run() -> void {
        // Fetch REST snapshot first, buffer WebSocket events
        // until snapshot is applied (handled in on_depth_event)
        resolver_.async_resolve(
            WS_HOST, WS_PORT,
            [this](beast::error_code ec,
                   tcp_resolver::results_type results) {
                on_resolve(ec, results);
            }
        );

        // Fetch snapshot on same thread before running io_context
        // so buffering starts before any WebSocket messages arrive
        fetch_snapshot();

        io_context_.run();
    }

    // REST snapshot fetch
    auto fetch_snapshot() -> void {
        // Separate io_context for the blocking REST call
        asio::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();

        tcp_resolver resolver(ioc);
        ssl_stream stream(ioc, ctx);

        auto const results = resolver.resolve(REST_HOST, "443");
        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(ssl::stream_base::client);

        // Build HTTP GET request
        http::request<http::string_body> req{
            http::verb::get,
            REST_SNAPSHOT_ENDPOINT,
            11  // HTTP/1.1
        };
        req.set(http::field::host, REST_HOST);
        req.set(http::field::user_agent, "cts/1.0");

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        // Parse and apply snapshot to order book
        auto json = nlohmann::json::parse(res.body());
        apply_snapshot(json);

        // Graceful SSL shutdown
        beast::error_code ec;
        stream.shutdown(ec);
    }

    auto apply_snapshot(const nlohmann::json& json) -> void {
        const uint64_t last_update_id = json["lastUpdateId"].get<uint64_t>();

        std::vector<PriceLevel> bids, asks;

        for (const auto& entry : json["bids"]) {
            bids.push_back({
                std::stod(entry[0].get<std::string>()),
                std::stod(entry[1].get<std::string>())
            });
        }
        for (const auto& entry : json["asks"]) {
            asks.push_back({
                std::stod(entry[0].get<std::string>()),
                std::stod(entry[1].get<std::string>())
            });
        }

        order_book_.apply_snapshot(last_update_id, bids, asks);

        // Replay any buffered diff events that arrived before snapshot
        for (const auto& event : pre_snapshot_buffer_) {
            handle_depth_event(event);
        }
        pre_snapshot_buffer_.clear();
    }

    // Async WebSocket connection chain
    auto on_resolve(
        beast::error_code ec,
        tcp_resolver::results_type results
    ) -> void {
        if (ec) {
            log_error("on_resolve", ec);
            return;
        }

        beast::get_lowest_layer(ws_).expires_after(
            std::chrono::seconds(30)
        );
        beast::get_lowest_layer(ws_).async_connect(
            results,
            [this](beast::error_code ec,
                   tcp_resolver::results_type::endpoint_type) {
                on_connect(ec);
            }
        );
    }

    auto on_connect(beast::error_code ec) -> void {
        if (ec) {
            log_error("on_connect", ec);
            return;
        }

        // Disable timeout for WebSocket — Beast manages it via
        // websocket timeout options set below
        beast::get_lowest_layer(ws_).expires_never();

        ws_.next_layer().async_handshake(
            ssl::stream_base::client,
            [this](beast::error_code ec) {
                on_tls_handshake(ec);
            }
        );
    }

    auto on_tls_handshake(beast::error_code ec) -> void {
        if (ec) {
            log_error("on_tls_handshake", ec);
            return;
        }

        // Set SNI hostname for TLS — required by most servers
        SSL_set_tlsext_host_name(
            ws_.next_layer().native_handle(),
            WS_HOST
        );

        // Apply Beast's suggested WebSocket timeout settings
        ws_.set_option(
            websocket::stream_base::timeout::suggested(
                beast::role_type::client
            )
        );

        ws_.async_handshake(
            WS_HOST, WS_ENDPOINT,
            [this](beast::error_code ec) {
                on_ws_handshake(ec);
            }
        );
    }

    auto on_ws_handshake(beast::error_code ec) -> void {
        if (ec) {
            log_error("on_ws_handshake", ec);
            return;
        }

        ws_.text(true);
        listen_for_messages();
    }

    // Recursive async read loop
    auto listen_for_messages() -> void {
        ws_.async_read(
            read_buffer_,
            [this](beast::error_code ec, std::size_t bytes_transferred) {
                on_read(ec, bytes_transferred);
            }
        );
    }

    auto on_read(beast::error_code ec, std::size_t bytes_transferred) -> void {
        if (ec == websocket::error::closed) {
            return;  // clean close — do not recurse
        }

        if (ec) {
            log_error("on_read", ec);
            // Attempt reconnect after delay if still running
            if (running_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                // TODO: full reconnect logic in later iteration
            }
            return;
        }

        // Parse and route the message
        std::string message = beast::buffers_to_string(read_buffer_.data());
        read_buffer_.consume(bytes_transferred);
        on_message(message);

        // Recurse to read next message
        listen_for_messages();
    }

    // Message routing
    auto on_message(const std::string& message) -> void {
        auto json = nlohmann::json::parse(message, nullptr, false);
        if (json.is_discarded()) {
            return;  // malformed JSON — skip silently
        }

        // Combined stream wraps events in {"stream":..., "data":...}
        if (!json.contains("stream") || !json.contains("data")) {
            return;
        }

        const auto& stream = json["stream"].get<std::string>();
        const auto& data = json["data"];

        if (stream.find("depth") != std::string::npos) {
            handle_depth_event(data);
        } else if (stream.find("trade") != std::string::npos) {
            handle_trade_event(data);
        }
    }

    auto handle_depth_event(const nlohmann::json& event) -> void {
        // If snapshot not yet applied, buffer the event and return
        if (!order_book_.is_initialized()) {
            pre_snapshot_buffer_.push_back(event);
            return;
        }

        const uint64_t first_update_id = event["U"].get<uint64_t>();
        const uint64_t last_update_id = event["u"].get<uint64_t>();
        const int64_t  exchange_ts_ms = event["E"].get<int64_t>();

        std::vector<PriceLevel> bid_updates, ask_updates;

        for (const auto& entry : event["b"]) {
            bid_updates.push_back({
                std::stod(entry[0].get<std::string>()),
                std::stod(entry[1].get<std::string>())
            });
        }
        for (const auto& entry : event["a"]) {
            ask_updates.push_back({
                std::stod(entry[0].get<std::string>()),
                std::stod(entry[1].get<std::string>())
            });
        }

        order_book_.apply_diff(
            first_update_id, last_update_id,
            bid_updates, ask_updates
        );

        // Log latency — exchange timestamp to book update completion
        const int64_t book_updated_ns = Common::getSteadyNanos();
        latency_logger_.log(exchange_ts_ms, book_updated_ns);
    }

    auto handle_trade_event(const nlohmann::json& event) -> void {
        Trade trade{
            std::stod(event["p"].get<std::string>()),   // price
            std::stod(event["q"].get<std::string>()),   // quantity
            event["T"].get<int64_t>() * 1'000'000LL,   // trade time ms → ns
            event["m"].get<bool>()                      // is_buyer_maker
        };

        trade_buffer_.push(trade);
    }

    auto log_error(
        const std::string& where,
        beast::error_code  ec
    ) -> void {
        std::cerr << "[BinanceWebSocketClient] " << where
                  << ": " << ec.message() << "\n";
    }

    // Constants
    static constexpr const char* WS_HOST = "stream.binance.com";
    static constexpr const char* WS_PORT = "9443";
    static constexpr const char* WS_ENDPOINT =
        "/stream?streams=btcusdt@depth@100ms/btcusdt@trade";

    static constexpr const char* REST_HOST = "api.binance.com";
    static constexpr const char* REST_SNAPSHOT_ENDPOINT =
        "/api/v3/depth?symbol=BTCUSDT&limit=1000";

    // Members
    OrderBook& order_book_;
    TradeRingBuffer& trade_buffer_;
    LatencyLogger& latency_logger_;

    asio::io_context io_context_;

    // ssl_context_ must be declared before ws_ — destruction order
    // is reverse of declaration order. ws_ must be destroyed first.
    ssl::context ssl_context_;
    ws_stream ws_;
    tcp_resolver resolver_;

    beast::flat_buffer read_buffer_;
    std::vector<nlohmann::json> pre_snapshot_buffer_;

    std::atomic<bool> running_{false};
    std::thread io_thread_;
};

} // namespace data
} // namespace cts