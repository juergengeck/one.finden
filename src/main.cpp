#include "fuse_server.hpp"
#include <iostream>
#include <string>
#include <signal.h>

namespace {
    std::unique_ptr<fused::FuseServer> g_server;

    void signal_handler(int signal) {
        if (g_server) {
            g_server->stop();
        }
        exit(signal);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <mount_point>" << std::endl;
        return 1;
    }

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Create and initialize server
    g_server = std::make_unique<fused::FuseServer>();
    
    if (!g_server->initialize(argv[1])) {
        std::cerr << "Failed to initialize server" << std::endl;
        return 1;
    }

    if (!g_server->start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }

    std::cout << "Server started. Press Ctrl+C to stop." << std::endl;

    // Wait for signals
    while (true) {
        pause();
    }

    return 0;
} 