#include "socket_daemon.hpp"
#include <iostream>
#include <csignal>
#include <chrono>
#include <thread>

static std::atomic<bool> g_shutdown_requested{false};

static void SignalHandler(int signum) {
    (void)signum;
    g_shutdown_requested = true;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    std::signal(SIGPIPE, SIG_IGN); // Prevent crash if client disconnects before write()

    std::string config_path = "model.yaml";
    if (argc > 1) {
        config_path = argv[1];
    }

    razor::SocketConfig cfg;
    cfg.config_path = config_path;

    std::cout << "========================================================\n";
    std::cout << "          RazorAI Model Router Daemon                   \n";
    std::cout << "========================================================\n";

    razor::SocketDaemon daemon(cfg);

    const auto& model_cfg = daemon.GetModelConfig();
    std::cout << "[Model Config] Loaded configuration from: '" << config_path << "'\n";
    std::cout << "[Model Config] Total models defined: " << model_cfg.models.size() << "\n";

    for (const auto& m : model_cfg.models) {
        std::cout << "  - Model '" << m.name << "' (" << m.provider << "/" << m.model << ") -> Roles: [";
        for (size_t i = 0; i < m.roles.size(); ++i) {
            std::cout << m.roles[i] << (i + 1 < m.roles.size() ? ", " : "");
        }
        std::cout << "]\n";
    }

    if (!daemon.Start()) {
        std::cerr << "[Error] Failed to start Router Socket Daemon!\n";
        return 1;
    }

    const auto* session_mgr = daemon.GetSessionManager();
    std::cout << "[Socket Daemon] Unix Domain Socket: " << daemon.GetSessionManager()->GetBaseSessionsDir() << "/../router.sock\n";
    std::cout << "[Socket Daemon] TCP Socket: 127.0.0.1:9090\n";
    std::cout << "[Session Store] Base Sessions Directory: " << session_mgr->GetBaseSessionsDir() << "/\n";
    std::cout << "[Status] Router Daemon is listening for incoming socket connections...\n";
    std::cout << "Press Ctrl+C to terminate daemon.\n";

    while (!g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\nShutting down RazorAI Model Router Daemon...\n";
    daemon.Stop();
    std::cout << "Router Daemon stopped cleanly.\n";

    return 0;
}
