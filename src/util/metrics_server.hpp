#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "logger.hpp"

namespace fused {

class MetricsServer {
public:
    using MetricsCallback = std::function<std::string()>;

    MetricsServer(uint16_t port, MetricsCallback callback)
        : port_(port), callback_(callback) {
        start_server();
    }

    ~MetricsServer() {
        stop_server();
    }

private:
    uint16_t port_;
    MetricsCallback callback_;
    std::thread server_thread_;
    std::atomic<bool> running_{true};
    int server_sock_{-1};

    void start_server() {
        server_thread_ = std::thread([this]() {
            server_sock_ = socket(AF_INET, SOCK_STREAM, 0);
            if (server_sock_ < 0) {
                LOG_ERROR("Failed to create metrics server socket");
                return;
            }

            // Allow socket reuse
            int opt = 1;
            if (setsockopt(server_sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
                LOG_ERROR("Failed to set metrics server socket options");
                close(server_sock_);
                return;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(port_);

            if (bind(server_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
                LOG_ERROR("Failed to bind metrics server socket");
                close(server_sock_);
                return;
            }

            if (listen(server_sock_, 5) < 0) {
                LOG_ERROR("Failed to listen on metrics server socket");
                close(server_sock_);
                return;
            }

            LOG_INFO("Metrics server listening on port {}", port_);

            while (running_) {
                handle_connection();
            }
        });
    }

    void stop_server() {
        running_ = false;
        if (server_sock_ >= 0) {
            close(server_sock_);
            server_sock_ = -1;
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    void handle_connection() {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_sock_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if (client_sock < 0) {
            LOG_ERROR("Failed to accept metrics client connection");
            return;
        }

        // Send HTTP response with metrics
        std::string metrics = callback_();
        std::string response = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Length: " + std::to_string(metrics.length()) + "\r\n"
            "\r\n" + metrics;

        write(client_sock, response.c_str(), response.length());
        close(client_sock);
    }
};

} // namespace fuse_t 