#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/system/error_code.hpp>

#include <openssl/ssl.h>

#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>

namespace cts {

    namespace data {

        /* Client to connect to Binance WebSocket feed */
        template<
        typename Resolver,
        typename WebSocketStream
        >
        class BinanceWebSocketClient {
        private:
            std::string url_{};
            std::string endpoint_{};
            std::string port_{};

            Resolver resolver_;
            WebSocketStream ws_;

            boost::beast::flat_buffer rBuffer_{};

            bool closed_{ true };

            // Callback functions
            std::function<void(boost::system::error_code)> onConnect_{ nullptr };
            std::function<void(boost::system::error_code,
                std::string&&)> onMessage_{ nullptr };
            std::function<void(boost::system::error_code)> onDisconnect_{ nullptr };

        public:
            /* 
            Binance WebSocket client constructor
            Does not initiate the connection

            \param url : Server url
            \param endpoint : Server endpoint to connect to
            \param port : Port on the server
            \param ioc : The io_context object. Users calls ioc.run()

            */
            BinanceWebSocketClient(
                std::string& url,
                std::string& endpoint,
                std::string& port,
                boost::asio::io_context& ioc
            );

            /*Destructor*/
            ~BinanceWebSocketClient();

            /*
            Connect to the server

            param\ onConnect : Called when connection fails or succeeds
            param\ onMessage : Called when a message (rvalue) is successfully received.
            param\ onDisconnect : Called when connection is closed
            */
            void Connect(
                std::function<void (boost::system::error_code)> onConnect = nullptr,
                std::function<void (boost::system::error_code, std::string&&)> onMessage = nullptr,
                std::function<void (boost::system::error_code)> onDisconnect = nullptr
            );

            /*
            Send a message to WebSocket server

            param\ message : The message to send
            param\ onSend : Called when the message is send successfully or failed
            */
            void Send(
                std::string& message,
                std::function<void (boost::system::error_code)> onSend = nullptr
            );

            /*
            Close the WebSocket connection

            param\ onClose : Called when connection is closed successfully or not
            */
            void Close(
                std::function<void (boost::system::error_code)> onClose = nullptr
            );
        }


    } // namespace data

} // namespace cts