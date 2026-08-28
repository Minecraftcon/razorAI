#ifndef RAZOR_SOCKET_DAEMON_HPP
#define RAZOR_SOCKET_DAEMON_HPP

#include "router.hpp"
#include "config.hpp"
#include "session_manager.hpp"
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <shared_mutex>

namespace razor {

struct SocketConfig {
    std::string socket_path = "";  // Defaults to ~/.razor/router.sock
    int tcp_port = 9090;           // TCP port
    std::string config_path = "";  // Defaults to ~/.razor/model.yaml or ./model.yaml
};


class SocketDaemon {
public:
    explicit SocketDaemon(const SocketConfig& config = SocketConfig());
    ~SocketDaemon();

    bool Start();
    void Stop();
    bool IsRunning() const { return is_running_; }
    void ClientHandler(int client_fd);
    std::string ProcessRequestJson(const std::string& request_json, int client_fd = -1);
    SessionManager* GetSessionManager() { return session_manager_.get(); }
    const Config& GetModelConfig() const { return model_config_; }

private:
    SocketConfig socket_config_;
    Config model_config_;
    mutable std::shared_mutex config_mutex_;
    std::unique_ptr<RouterEngine> router_engine_;
    std::unique_ptr<SessionManager> session_manager_;

    std::atomic<bool> is_running_{false};
    int unix_socket_fd_ = -1;
    int tcp_socket_fd_ = -1;

    std::thread unix_listener_thread_;
    std::thread tcp_listener_thread_;
    std::thread config_watcher_thread_;

    void ListenUnixSocket();
    void ListenTcpSocket();
    void WatchConfig();
    void LogMessage(const std::string& severity, const std::string& message);
    void HandleClientConnection(int client_fd);

    static std::string GetDefaultSocketPath();
};

} // namespace razor

#endif // RAZOR_SOCKET_DAEMON_HPP
