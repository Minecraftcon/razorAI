#include "session_manager.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <random>

namespace razor {

static std::string EscapeJsonString(const std::string& input) {
    std::ostringstream ss;
    for (char c : input) {
        switch (c) {
            case '"': ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    ss << c;
                }
                break;
        }
    }
    return ss.str();
}

std::string SessionManager::GetDefaultSessionsDir() {
    const char* home = std::getenv("HOME");
    std::string base_dir;
    if (home) {
        base_dir = std::string(home) + "/.razor/sessions";
    } else {
        base_dir = "/tmp/razor_sessions";
    }
    return base_dir;
}

std::string SessionManager::GetCurrentTimestampStr() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
    return ss.str();
}

bool SessionManager::EnsureDirectoryExists(const std::string& path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) {
        // Directory does not exist, create recursively
        std::string command = "mkdir -p \"" + path + "\"";
        int ret = system(command.c_str());
        return (ret == 0);
    }
    return (info.st_mode & S_IFDIR) != 0;
}

SessionManager::SessionManager(const std::string& base_sessions_dir) {
    if (base_sessions_dir.empty()) {
        base_sessions_dir_ = GetDefaultSessionsDir();
    } else {
        base_sessions_dir_ = base_sessions_dir;
    }
    EnsureDirectoryExists(base_sessions_dir_);
}

SessionInfo SessionManager::CreateSession(const std::string& custom_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string session_id = custom_id;
    if (session_id.empty()) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<uint32_t> dist(1000, 9999);
        session_id = "session_" + GetCurrentTimestampStr() + "_" + std::to_string(dist(gen));
    }

    SessionInfo info;
    info.session_id = session_id;
    info.session_dir = base_sessions_dir_ + "/" + session_id;
    info.chat_jsonl_path = info.session_dir + "/chat.jsonl";
    info.cache_dir = info.session_dir + "/cache";
    info.scratch_dir = info.session_dir + "/scratch";
    info.metadata_path = info.session_dir + "/metadata.json";
    info.created_at = GetCurrentTimestampStr();

    EnsureDirectoryExists(info.session_dir);
    EnsureDirectoryExists(info.cache_dir);
    EnsureDirectoryExists(info.scratch_dir);

    // Initialize metadata.json if it doesn't exist
    struct stat st;
    if (stat(info.metadata_path.c_str(), &st) != 0) {
        std::ofstream meta_file(info.metadata_path);
        if (meta_file.is_open()) {
            meta_file << "{\n";
            meta_file << "  \"session_id\": \"" << EscapeJsonString(info.session_id) << "\",\n";
            meta_file << "  \"created_at\": \"" << EscapeJsonString(info.created_at) << "\",\n";
            meta_file << "  \"status\": \"active\"\n";
            meta_file << "}\n";
        }
    }

    // Touch chat.jsonl
    if (stat(info.chat_jsonl_path.c_str(), &st) != 0) {
        std::ofstream chat_file(info.chat_jsonl_path);
    }

    return info;
}

SessionInfo SessionManager::GetSessionInfo(const std::string& session_id) const {
    SessionInfo info;
    info.session_id = session_id;
    info.session_dir = base_sessions_dir_ + "/" + session_id;
    info.chat_jsonl_path = info.session_dir + "/chat.jsonl";
    info.cache_dir = info.session_dir + "/cache";
    info.scratch_dir = info.session_dir + "/scratch";
    info.metadata_path = info.session_dir + "/metadata.json";
    return info;
}

bool SessionManager::SessionExists(const std::string& session_id) const {
    if (session_id.empty()) return false;
    std::string s_dir = base_sessions_dir_ + "/" + session_id;
    struct stat st;
    return (stat(s_dir.c_str(), &st) == 0 && (st.st_mode & S_IFDIR));
}


std::vector<nlohmann::json> SessionManager::GetHistory(const std::string& session_id) const {
    std::vector<nlohmann::json> history;
    std::string chat_file_path = base_sessions_dir_ + "/" + session_id + "/chat.jsonl";
    
    std::ifstream file(chat_file_path);
    if (!file.is_open()) return history;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        try {
            nlohmann::json j = nlohmann::json::parse(line);
            if (!j.contains("response") || !j.contains("user_prompt")) continue;

            std::string resp = j["response"].get<std::string>();
            // If the turn ended in an API failure or corrupt raw tool name, skip this entire broken turn so history stays clean!
            if (resp.empty() || resp.find("[Router Output]") != std::string::npos ||
                resp == "run_command" || resp == "write_file" || resp == "read_file" ||
                resp == "list_dir" || resp == "manage_task" || resp == "replace_file_content" || resp == "web_search") {
                continue;
            }

            // Build assistant message from response
            bool is_tc = false;
            nlohmann::json assistant_msg;
            try {
                nlohmann::json rj = nlohmann::json::parse(resp);
                if (rj.is_array() && !rj.empty() && rj[0].contains("tool")) {
                    is_tc = true;
                    assistant_msg["role"] = "assistant";
                    assistant_msg["content"] = "";
                    assistant_msg["tool_calls"] = nlohmann::json::array();
                    for (auto& tc : rj) {
                        nlohmann::json t;
                        t["id"] = tc.value("tool_call_id", "");
                        t["type"] = "function";
                        t["function"]["name"] = tc.value("tool", "");
                        t["function"]["arguments"] = tc.contains("args") ? tc["args"].dump() : "{}";
                        assistant_msg["tool_calls"].push_back(t);
                    }
                }
            } catch (...) {}

            if (!is_tc) {
                assistant_msg["role"] = "assistant";
                assistant_msg["content"] = resp;
            }

            // Build user / tool message from user_prompt
            std::string up = j["user_prompt"].get<std::string>();
            std::vector<nlohmann::json> prompt_msgs;
            try {
                nlohmann::json upj = nlohmann::json::parse(up);
                if (upj.is_array() && !upj.empty() && upj[0].contains("tool_result") && upj[0]["tool_result"].get<bool>()) {
                    for (const auto& tr : upj) {
                        nlohmann::json msg;
                        msg["role"] = "tool";
                        if (tr.contains("tool_call_id")) msg["tool_call_id"] = tr["tool_call_id"];
                        if (tr.contains("name")) msg["name"] = tr["name"];
                        if (tr.contains("prompt")) {
                            std::string p_str = tr["prompt"].get<std::string>();
                            msg["content"] = p_str.empty() ? "(Tool executed successfully with no output)" : p_str;
                        } else {
                            msg["content"] = "(Tool executed successfully with no output)";
                        }
                        prompt_msgs.push_back(msg);
                    }
                } else if (upj.is_object() && upj.contains("tool_result") && upj["tool_result"].get<bool>()) {
                    nlohmann::json msg;
                    msg["role"] = "tool";
                    if (upj.contains("tool_call_id")) msg["tool_call_id"] = upj["tool_call_id"];
                    if (upj.contains("name")) msg["name"] = upj["name"];
                    if (upj.contains("prompt")) {
                        std::string p_str = upj["prompt"].get<std::string>();
                        msg["content"] = p_str.empty() ? "(Tool executed successfully with no output)" : p_str;
                    } else {
                        msg["content"] = "(Tool executed successfully with no output)";
                    }
                    prompt_msgs.push_back(msg);
                }
            } catch (...) {}

            if (prompt_msgs.empty()) {
                nlohmann::json msg;
                msg["role"] = "user";
                msg["content"] = up;
                prompt_msgs.push_back(msg);
            }

            // Push prompt msgs, then assistant msg
            for (auto& pm : prompt_msgs) {
                history.push_back(pm);
            }
            history.push_back(assistant_msg);

        } catch (...) {}
    }
    return history;
}

bool SessionManager::AppendChatMessage(const std::string& session_id,
                                        const std::string& user_prompt,
                                        const std::string& category,
                                        const std::string& assigned_role,
                                        const ModelEntry* selected_model,
                                        const std::string& response,
                                        bool cache_hit) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!SessionExists(session_id)) {
        CreateSession(session_id);
    }

    std::string chat_file_path = base_sessions_dir_ + "/" + session_id + "/chat.jsonl";
    std::ofstream chat_file(chat_file_path, std::ios::app);
    if (!chat_file.is_open()) {
        return false;
    }

    auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::string m_name = selected_model ? selected_model->name : "none";
    std::string m_id = selected_model ? selected_model->model : "none";
    std::string m_provider = selected_model ? selected_model->provider : "none";

    chat_file << "{"
              << "\"timestamp\":" << now_ts << ","
              << "\"user_prompt\":\"" << EscapeJsonString(user_prompt) << "\","
              << "\"category\":\"" << EscapeJsonString(category) << "\","
              << "\"assigned_role\":\"" << EscapeJsonString(assigned_role) << "\","
              << "\"model_name\":\"" << EscapeJsonString(m_name) << "\","
              << "\"model_id\":\"" << EscapeJsonString(m_id) << "\","
              << "\"provider\":\"" << EscapeJsonString(m_provider) << "\","
              << "\"cache_hit\":" << (cache_hit ? "true" : "false") << ","
              << "\"response\":\"" << EscapeJsonString(response) << "\""
              << "}\n";

    return true;
}

} // namespace razor
