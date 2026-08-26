#ifndef RAZOR_SESSION_MANAGER_HPP
#define RAZOR_SESSION_MANAGER_HPP

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "config.hpp"
#include <nlohmann/json.hpp>

namespace razor {

struct SessionInfo {
    std::string session_id;
    std::string session_dir;
    std::string chat_jsonl_path;
    std::string cache_dir;
    std::string scratch_dir;
    std::string metadata_path;
    std::string created_at;
};

class SessionManager {
public:
    explicit SessionManager(const std::string& base_sessions_dir = "");
    ~SessionManager() = default;

    SessionInfo CreateSession(const std::string& custom_id = "");
    SessionInfo GetSessionInfo(const std::string& session_id) const;
    bool SessionExists(const std::string& session_id) const;

    // Fetch message history for the session
    std::vector<nlohmann::json> GetHistory(const std::string& session_id) const;

    // Log chat interaction into session chat.jsonl
    bool AppendChatMessage(const std::string& session_id,
                           const std::string& user_prompt,
                           const std::string& category,
                           const std::string& assigned_role,
                           const ModelEntry* selected_model,
                           const std::string& response,
                           bool cache_hit);

    std::string GetBaseSessionsDir() const { return base_sessions_dir_; }

private:
    std::string base_sessions_dir_;
    mutable std::mutex mutex_;
    static std::string GetDefaultSessionsDir();
    static std::string GetCurrentTimestampStr();
    static bool EnsureDirectoryExists(const std::string& path);
};

} // namespace razor

#endif // RAZOR_SESSION_MANAGER_HPP
