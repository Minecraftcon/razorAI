#include "socket_daemon.hpp"
#include "task_manager.hpp"
#include "skill_manager.hpp"
#include "web_search.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <chrono>
#include <shared_mutex>
#include <algorithm>
#include "http_client.hpp"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace razor {



static std::string EscapeJson(const std::string& str) {
    std::ostringstream ss;
    for (char c : str) {
        switch (c) {
            case '"': ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default: ss << c; break;
        }
    }
    return ss.str();
}

std::string SocketDaemon::GetDefaultSocketPath() {
    const char* home = std::getenv("HOME");
    std::string path;
    if (home) {
        path = std::string(home) + "/.razor/router.sock";
    } else {
        path = "/tmp/razor_router.sock";
    }
    return path;
}

void SocketDaemon::LogMessage(const std::string& severity, const std::string& message) {
    const char* home = std::getenv("HOME");
    std::string path = home ? std::string(home) + "/.razor/daemon.log" : "/tmp/daemon.log";
    std::ofstream ofs(path, std::ios::app);
    if (!ofs) return;

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    ofs << "[" << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S") << "] "
        << "[" << severity << "] " << message << "\n";
}

SocketDaemon::SocketDaemon(const SocketConfig& config)
    : socket_config_(config),
      router_engine_(std::make_unique<RouterEngine>()),
      session_manager_(std::make_unique<SessionManager>()) {
    if (socket_config_.socket_path.empty()) {
        socket_config_.socket_path = GetDefaultSocketPath();
    }
    model_config_ = Config::LoadConfig(socket_config_.config_path);
}

SocketDaemon::~SocketDaemon() {
    Stop();
}

bool SocketDaemon::Start() {
    if (is_running_) return true;

    // Reload model configuration on start
    model_config_ = Config::LoadConfig(socket_config_.config_path);

    is_running_ = true;

    // Start Unix socket listener thread
    unix_listener_thread_ = std::thread(&SocketDaemon::ListenUnixSocket, this);

    // Start TCP socket listener thread
    tcp_listener_thread_ = std::thread(&SocketDaemon::ListenTcpSocket, this);

    // Start Config watcher thread
    config_watcher_thread_ = std::thread(&SocketDaemon::WatchConfig, this);

    LogMessage("SUCCESS", "Daemon started successfully.");

    return true;
}

void SocketDaemon::Stop() {
    if (!is_running_) return;

    is_running_ = false;

    if (unix_socket_fd_ >= 0) {
        close(unix_socket_fd_);
        unix_socket_fd_ = -1;
    }
    if (tcp_socket_fd_ >= 0) {
        close(tcp_socket_fd_);
        tcp_socket_fd_ = -1;
    }

    if (unix_listener_thread_.joinable()) {
        unix_listener_thread_.join();
    }
    if (tcp_listener_thread_.joinable()) {
        tcp_listener_thread_.join();
    }
    if (config_watcher_thread_.joinable()) {
        config_watcher_thread_.join();
    }

    LogMessage("INFO", "Daemon stopped.");

    // Clean up socket file
    unlink(socket_config_.socket_path.c_str());
}

std::string SocketDaemon::ProcessRequestJson(const std::string& request_json, int client_fd) {
    std::string prompt = "";
    std::string session_id = "";
    std::string requested_role = "";
    int model_index = 0;
    
    bool is_tool_result = false;
    bool is_tool_result_array = false;
    std::string tool_call_id;
    
    try {
        json req = json::parse(request_json);
        if (req.contains("prompt") && req["prompt"].is_string()) prompt = req["prompt"].get<std::string>();
        if (req.contains("session_id") && req["session_id"].is_string()) session_id = req["session_id"].get<std::string>();
        if (req.contains("role") && req["role"].is_string()) requested_role = req["role"].get<std::string>();
        if (req.contains("model_index") && req["model_index"].is_number()) model_index = req["model_index"].get<int>();

        if (!prompt.empty()) {
            json p_json = json::parse(prompt);
            if (p_json.is_array() && !p_json.empty()) {
                if (p_json[0].contains("tool_result") && p_json[0]["tool_result"].is_boolean() && p_json[0]["tool_result"].get<bool>()) {
                    is_tool_result_array = true;
                    is_tool_result = true; // Still flag as tool result for cache bypass
                }
            } else if (p_json.is_object()) {
                if (p_json.contains("tool_result") && p_json["tool_result"].is_boolean() && p_json["tool_result"].get<bool>()) {
                    is_tool_result = true;
                    if (p_json.contains("tool_call_id") && p_json["tool_call_id"].is_string()) {
                        tool_call_id = p_json["tool_call_id"].get<std::string>();
                    }
                }
            }
        }
    } catch (...) {}

    std::string original_prompt = prompt; // Keep original unmodified wrapper for chat.jsonl

    // Create session if session_id is empty or doesn't exist
    SessionInfo s_info;
    if (session_id.empty() || !session_manager_->SessionExists(session_id)) {
        s_info = session_manager_->CreateSession(session_id);
        LogMessage("INFO", "Created new session: " + s_info.session_id);
    } else {
        s_info = session_manager_->GetSessionInfo(session_id);
    }

    RouteResult route_res = router_engine_->RoutePrompt(prompt);
    if (is_tool_result) {
        route_res.cache_hit = false; // Never use semantic cache on tool results!
    }

    std::string model_name, model_id, provider, api_key, endpoint;
    bool api_key_set = false;
    std::vector<std::string> sys_prompts;
    const ModelEntry* selected_model_ptr = nullptr;
    ModelEntry model_copy;
    
    std::string exec_policy = "direct";
    std::string handover = "";
    std::string handover_success = "";
    int loopback = 0;
    bool multi_model = false;
    json tools_json = json::array();
    json api_tools_array = json::array();

    {
        std::shared_lock<std::shared_mutex> lock(config_mutex_);
        const ModelEntry* selected_model = nullptr;
        if (model_index >= 0 && model_index < (int)model_config_.models.size()) {
            selected_model = &model_config_.models[model_index];
        } else if (!model_config_.models.empty()) {
            selected_model = &model_config_.models[0];
        }

        if (selected_model) {
            model_copy = *selected_model;
            selected_model_ptr = &model_copy;
        }

        model_name = selected_model ? selected_model->name : "default";
        model_id = selected_model ? selected_model->model : "default-model";
        provider = selected_model ? selected_model->provider : "custom";
        api_key = selected_model ? selected_model->api_key : "";
        endpoint = selected_model ? selected_model->endpoint : "";
        api_key_set = !api_key.empty();
        
        sys_prompts = model_config_.global_sysprompt;
        
        // If an active skill was invoked via "Skill:<path>", automatically load and inject it!
        if (prompt.rfind("Skill:", 0) == 0) {
            size_t space_idx = prompt.find(' ', 6);
            std::string skill_path = (space_idx != std::string::npos) ? prompt.substr(6, space_idx - 6) : prompt.substr(6);
            
            std::ifstream sk_file(skill_path);
            if (sk_file.is_open()) {
                std::stringstream sk_buf;
                sk_buf << sk_file.rdbuf();
                std::string sk_content = sk_buf.str();
                
                sys_prompts.push_back(
                    "ACTIVE SKILL DIRECTIVE (" + skill_path + "):\n"
                    "The user has activated a specialized skill for this request.\n\n"
                    "```markdown\n" + sk_content + "\n```\n\n"
                    "CRITICAL: Follow all guidelines, workflows, best practices, and scripts specified in this skill to accomplish the user's request."
                );
            }
        }

        sys_prompts.push_back(
            "IDENTITY & WORKFLOW:\n"
            "You are Razor, an intelligent pair-programming engineer working inside an interactive terminal environment. "
            "You have access to tools for listing directories, inspecting files, editing code, running shell commands, and managing tasks.\n\n"

            "SESSION & SCRATCH DIRECTORY:\n"
            "- Active Session ID: " + s_info.session_id + "\n"
            "- Session Scratch Path: " + s_info.scratch_dir + "\n"
            "- Purpose: Use this scratch directory for temporary scratch scripts (e.g. one-off Python or shell test scripts), "
            "intermediate calculation dumps, and debug data to avoid polluting the user's workspace repository.\n\n"

            "EXECUTION MODEL (Reason → Act → Observe):\n"
            "1. REASON: Before taking any action, briefly explain WHAT you need to do and WHY.\n"
            "2. ACT: Call the appropriate tool(s). Batch independent actions into a single turn.\n"
            "3. OBSERVE: After receiving tool results, analyze the output and conclude or proceed to the next step.\n\n"

            "PROBLEM SOLVING & RESILIENCE (Autonomous Execution & Proactive Retries):\n"
            "- STOP COMPLAINING & BAN PASSIVE QUESTIONS:\n"
            "  * NEVER stop mid-task to ask rhetorical permission questions (e.g. 'Would you like me to check logs?', 'Should I retry navigation?', 'Would you like me to open a new tab?').\n"
            "  * NEVER complain about CLI flags or warnings. If a command/tool emits warnings or fails, SILENTLY ADAPT, FIX ARGUMENTS, AND RETRY IMMEDIATELY.\n"
            "  * Take the next logical step autonomously. You are an empowered autonomous engineer — execute, inspect results, and keep progressing until the goal is accomplished.\n"
            "- UNIVERSAL TENACITY & RETRIES (Applies to EVERYTHING, not just coding):\n"
            "  * If a command/tool is not found or fails: Do NOT immediately give up. Search for the binary in './build', './bin', './scripts', 'tools/', '~/.local/bin', '~/.cargo/bin', or node_modules. Use 'which', 'find', or 'whereis'. Check if it can be invoked via an alternative tool or package runner. Only conclude it is missing after thorough exploration.\n"
            "  * If a file/symbol/config is missing: Do NOT report missing on first try. Search using 'find_files', 'grep', and 'list_dir' with flexible globs and case-insensitive matching across subdirectories.\n"
            "  * If a search or lookup returns empty: Reformulate queries, test synonyms, adjust keywords, and inspect local README/docs.\n"
            "  * If a command/build fails: Read the exact error trace, form a new hypothesis, try different flags or combos, and test again.\n"
            "- NEVER GUESS: Do not guess file contents, bug causes, or API signatures. Track down the code to its source, follow callers and callees, and verify actual filesystem state.\n"
            "- BROWSER AUTOMATION & CHROME PROTOCOL (CDP):\n"
            "  * Note: Chrome DevTools target endpoints (`/devtools/page/<id>`) are WebSockets (`ws://`), not HTTP POST endpoints.\n"
            "  * To evaluate JS or automate a live browser: Write a short Python/Node runner script in your session scratch path using `websockets`, `playwright`, or `selenium`, and run it with 'run_command'.\n"
            "- SILENT OR EMPTY COMMAND OUTPUTS:\n"
            "  * If a command completes with no output, evaluate why. If data was expected, re-run with verbose/debug flags, test alternative approaches, or inspect logs in your scratch directory rather than ending the turn with an empty response.\n"
            "- VERIFY THOROUGHLY: After making changes, always run build checks or tests to prove the fix works rather than assuming success.\n\n"

            "TOOL STRATEGY:\n"
            "- WEB SEARCH & SCRAPING: Use 'web_search' with 'search' (to query the live web for real-time information) or 'fetch' (to scrape/fetch clean text/markdown from specific URLs via TinyFish).\n"
            "- DIRECTORIES: Use 'list_dir' to view directories with detailed file types, sizes, permissions, and timestamps.\n"
            "- EDITING: Use 'replace_file_content' to make targeted code modifications, or 'write_file' for new files.\n"
            "- COMMANDS & BACKGROUND PROCESSES:\n"
            "  * Always give commands a short descriptive 'name' (e.g. 'dev_server', 'unit_tests', 'daemon_proc').\n"
            "  * For long-running processes (e.g. dev servers, daemons, watchers, log tails, long builds), specify a short 'work_time' (1-2 seconds).\n"
            "  * NEVER append '&', 'nohup', 'disown', 'screen', or 'tmux' to commands. The built-in Task Manager automatically manages and tracks background execution with PTY logging.\n"
            "- BACKGROUND TASKS MANAGEMENT:\n"
            "  * Use 'manage_task' with action='view' to check status/logs, 'send_keycode' to send stdin input, and 'kill' to terminate background jobs.\n"
            "- SKILLS POLICY & EXECUTION:\n"
            "  * You have access to the specialized skills catalog.\n"
            "  * USE POLICY: Only use a skill when the user's task specifically matches that domain or the user explicitly activates/requests the skill (e.g. via '[S: <name>]' or 'Skill:<path>').\n"
            "  * MANDATORY READING: When activating or using a skill, you MUST read the full skill instructions using 'read_file' on its Path before taking action, then strictly adhere to the skill's procedures, scripts, and rules.\n"
        );

        // Inject Skills Directory with paths and descriptions
        std::string skills_directory = SkillManager::Instance().FormatSkillsForSystemPrompt();
        if (!skills_directory.empty()) {
            sys_prompts.push_back(skills_directory);
        }

        std::vector<std::string> combined_tools;
        if (selected_model) {
            combined_tools = selected_model->tools;
        }

        if (!combined_tools.empty()) {
            std::vector<std::string> unimplemented_tools;
            for (const auto& t : combined_tools) {
                tools_json.push_back(t);
                
                json tool_def = {
                    {"type", "function"},
                    {"function", {
                        {"name", t},
                        {"description", "Execute the " + t + " tool."}
                    }}
                };
                if (t == "run_command" || t == "run") {
                    tool_def["function"]["parameters"] = {
                        {"type", "object"},
                        {"properties", {
                            {"command", {
                                {"type", "string"},
                                {"description", "The shell command to execute. NEVER append '&', 'nohup', or 'disown' — use the 'work_time' parameter to background long-running tasks."}
                            }},
                            {"work_time", {
                                {"type", "integer"},
                                {"description", "Number of seconds to wait synchronously before backgrounding the command (e.g. 1 or 2 for servers, watchers, and background daemons). Default: 5"}
                            }},
                            {"name", {
                                {"type", "string"},
                                {"description", "Short, concise descriptive name for the task (e.g. 'dev_server', 'test_runner')"}
                            }}
                        }},
                        {"required", json::array({"command"})}
                    };
                } else if (t == "manage_task" || t == "task_management" || t == "term_manage") {
                    tool_def["function"]["parameters"] = {
                        {"type", "object"},
                        {"properties", {
                            {"action", {
                                {"type", "string"},
                                {"enum", json::array({"view", "kill", "send_keycode"})},
                                {"description", "Action to perform: 'view' (inspect task status & logs), 'kill' (terminate background task), 'send_keycode' (send input string to stdin)"}
                            }},
                            {"task_id", {
                                {"type", "string"},
                                {"description", "The Task ID (e.g. 'task-1')"}
                            }},
                            {"keycode", {
                                {"type", "string"},
                                {"description", "Input string or characters to send to the task's stdin (when action is 'send_keycode')"}
                            }},
                            {"input", {
                                {"type", "string"},
                                {"description", "Alternative name for input/keycode string to send to stdin"}
                            }}
                        }},
                        {"required", json::array({"action", "task_id"})}
                    };
                } else if (t == "replace_file_content" || t == "replace_in_file") {
                    tool_def["function"]["parameters"] = {
                        {"type", "object"},
                        {"properties", {
                            {"file_path", {{"type", "string"}}},
                            {"target", {{"type", "string"}}},
                            {"replacement", {{"type", "string"}}}
                        }},
                        {"required", json::array({"file_path", "target", "replacement"})}
                    };
                } else if (t == "write_file" || t == "write_files") {
                    tool_def["function"]["parameters"] = {
                        {"type", "object"},
                        {"properties", {
                            {"file_path", {{"type", "string"}}},
                            {"content", {{"type", "string"}}}
                        }},
                        {"required", json::array({"file_path", "content"})}
                    };
                } else if (t == "read_file" || t == "read_files") {
                    tool_def["function"]["parameters"] = {
                        {"type", "object"},
                        {"properties", {
                            {"file_path", {{"type", "string"}}}
                        }},
                        {"required", json::array({"file_path"})}
                    };
                } else if (t == "list_dir" || t == "list_directory" || t == "list_files" || t == "ls") {
                    tool_def["function"]["parameters"] = {
                        {"type", "object"},
                        {"properties", {
                            {"path", {
                                {"type", "string"},
                                {"description", "The directory path to inspect and list (default: '.' for current directory)"}
                            }},
                            {"dir_path", {
                                {"type", "string"},
                                {"description", "Alternative parameter for directory path to list"}
                            }}
                        }}
                    };
                } else if (t == "web_search" || t == "search_web" || t == "fetch_url") {
                    tool_def["function"]["description"] = "Search the live web for real-time information or fetch and extract clean markdown content from specific web URLs using TinyFish.";
                    tool_def["function"]["parameters"] = {
                        {"type", "object"},
                        {"properties", {
                            {"search", {
                                {"type", "string"},
                                {"description", "Web search query string to look up up-to-date live information on the internet."}
                            }},
                            {"fetch", {
                                {"type", "string"},
                                {"description", "URL of a web page to fetch, scrape, and extract clean text/markdown content from."}
                            }}
                        }}
                    };
                } else {
                    unimplemented_tools.push_back(t);
                    tool_def["function"]["parameters"] = {
                        {"type", "object"},
                        {"properties", json::object()}
                    };
                }
                api_tools_array.push_back(tool_def);
            }
            
            if (!unimplemented_tools.empty()) {
                std::string forbid_prompt = "CRITICAL: The following tools are currently UNIMPLEMENTED and WILL FAIL if you try to use them: ";
                for (size_t i = 0; i < unimplemented_tools.size(); ++i) {
                    forbid_prompt += unimplemented_tools[i];
                    if (i < unimplemented_tools.size() - 1) forbid_prompt += ", ";
                }
                forbid_prompt += ". DO NOT use these tools under any circumstances. You DO HAVE working tools: 'web_search' (with search and fetch), 'run_command' (with work_time and name), 'manage_task' (with view, kill, send_keycode), 'list_dir', 'read_file', 'write_file', and 'replace_file_content'. Use these working tools instead!";
                sys_prompts.push_back(forbid_prompt);
            }
        }
    }

    // Inject active [Tasks:] context if any tasks exist for this session
    std::string task_table = TaskManager::Instance().FormatTaskTable(s_info.session_id);
    if (!task_table.empty()) {
        sys_prompts.push_back(
            "CURRENT ACTIVE BACKGROUND TASKS:\n" + task_table +
            "\nUse 'manage_task' (action='view', 'send_keycode', or 'kill') to monitor or interact with any background task."
        );
    }
    std::string tool_call_id_val = tool_call_id;
    std::string tool_name = "";
    if (is_tool_result) {
        try {
            json upj = json::parse(prompt);
            if (upj.contains("tool_call_id")) {
                tool_call_id_val = upj["tool_call_id"].get<std::string>();
            }
            if (upj.contains("name")) {
                tool_name = upj["name"].get<std::string>();
            }
            if (upj.contains("prompt")) {
                prompt = upj["prompt"].get<std::string>(); // This strips the JSON for the current API call, but we must save original_prompt to history
            }
        } catch (...) {}
    }

    LogMessage("INFO", "Routing request to model: " + model_name + " for session: " + s_info.session_id);

    std::string response_text;
    if (route_res.cache_hit && !route_res.cached_response.empty()) {
        LogMessage("SUCCESS", "Cache hit for prompt.");
        response_text = route_res.cached_response;
    } else {
        json payload;
        payload["model"] = model_id;
        payload["messages"] = json::array();
        
        for (const auto& sys_line : sys_prompts) {
            if (!sys_line.empty()) {
                payload["messages"].push_back({{"role", "system"}, {"content", sys_line}});
            }
        }
        
        std::vector<json> history = session_manager_->GetHistory(s_info.session_id);
        for (const auto& msg : history) {
            payload["messages"].push_back(msg);
        }
        
        if (is_tool_result_array) {
            try {
                json p_json = json::parse(prompt);
                for (const auto& tr : p_json) {
                    json tool_msg;
                    tool_msg["role"] = "tool";
                    if (tr.contains("tool_call_id") && tr["tool_call_id"].is_string()) {
                        tool_msg["tool_call_id"] = tr["tool_call_id"].get<std::string>();
                    }
                    if (tr.contains("name") && tr["name"].is_string()) {
                        tool_msg["name"] = tr["name"].get<std::string>();
                    }
                    if (tr.contains("prompt") && tr["prompt"].is_string()) {
                        std::string p_str = tr["prompt"].get<std::string>();
                        tool_msg["content"] = p_str.empty() ? "(Tool executed successfully with no output)" : p_str;
                    } else {
                        tool_msg["content"] = "(Tool executed successfully with no output)";
                    }
                    payload["messages"].push_back(tool_msg);
                }
            } catch (...) {}
        } else if (is_tool_result) {
            json tool_msg;
            tool_msg["role"] = "tool";
            tool_msg["tool_call_id"] = tool_call_id;
            try {
                json p_json = json::parse(prompt);
                if (p_json.contains("name") && p_json["name"].is_string()) {
                    tool_msg["name"] = p_json["name"].get<std::string>();
                }
                if (p_json.contains("prompt") && p_json["prompt"].is_string()) {
                    std::string p_str = p_json["prompt"].get<std::string>();
                    tool_msg["content"] = p_str.empty() ? "(Tool executed successfully with no output)" : p_str;
                } else {
                    tool_msg["content"] = "(Tool executed successfully with no output)";
                }
            } catch (...) {
                tool_msg["content"] = prompt.empty() ? "(Tool executed successfully with no output)" : prompt;
            }
            payload["messages"].push_back(tool_msg);
        } else {
            payload["messages"].push_back({
                {"role", "user"},
                {"content", prompt}
            });
        }

        if (!api_tools_array.empty()) {
            payload["tools"] = api_tools_array;
            payload["tool_choice"] = "auto";
        }

        std::string payload_str = payload.dump();
        LogMessage("INFO", "Payload Sent to API: " + payload_str);
        
        std::string api_response = HttpClient::Post(endpoint, api_key, payload_str, 60);
        
        if (api_response.empty()) {
            response_text = "[Router Output] API call failed: No response received from " + model_name + " endpoint (" + endpoint + "). Please verify your network and API key.";
        } else {
            try {
                json resp_json = json::parse(api_response);
                if (resp_json.contains("choices") && !resp_json["choices"].empty()) {
                    auto& message = resp_json["choices"][0]["message"];
                    if (message.contains("tool_calls") && !message["tool_calls"].empty()) {
                        // Extract tool calls and normalize
                        json normalized_calls = json::array();
                        for (auto& tc : message["tool_calls"]) {
                            json norm;
                            norm["tool"] = tc["function"]["name"];
                            norm["tool_call_id"] = tc["id"];
                            norm["args"] = json::parse(tc["function"]["arguments"].get<std::string>());
                            normalized_calls.push_back(norm);
                        }
                        response_text = normalized_calls.dump();
                    } else if (message.contains("content") && !message["content"].is_null()) {
                        response_text = message["content"].get<std::string>();
                    } else {
                        response_text = "[Router Output] API returned empty content.";
                    }
                } else {
                    response_text = "[Router Output] API call failed: " + api_response;
                }
            } catch (...) {
                response_text = "[Router Output] JSON Parsing failed. Raw: " + api_response;
            }
        }
        
        if (response_text.find("[Router Output]") != std::string::npos) {
            LogMessage("ERROR", "API call failed for model: " + model_id);
        } else {
            if (!is_tool_result) {
                router_engine_->UpdateCacheResponse(route_res.cache_key, route_res.embedding, route_res.category, response_text);
            }
            LogMessage("SUCCESS", "API response generated.");
        }
    }

    session_manager_->AppendChatMessage(
        s_info.session_id,
        original_prompt,
        route_res.category,
        "default",
        selected_model_ptr,
        response_text,
        route_res.cache_hit
    );

    json json_resp;
    json_resp["status"] = "success";
    json_resp["session_id"] = s_info.session_id;
    json_resp["category"] = route_res.category;
    json_resp["execution_policy"] = exec_policy;
    json_resp["handover"] = handover;
    json_resp["handover_success"] = handover_success;
    json_resp["loopback_limit"] = loopback;
    json_resp["multi_model"] = multi_model;
    json_resp["tools"] = tools_json;
    json_resp["model_name"] = model_name;
    json_resp["model_id"] = model_id;
    json_resp["provider"] = provider;
    json_resp["endpoint"] = endpoint;
    json_resp["api_key_set"] = api_key_set;
    json_resp["cache_hit"] = route_res.cache_hit;
    json_resp["confidence"] = route_res.confidence;
    json_resp["session_dir"] = s_info.session_dir;
    json_resp["chat_jsonl"] = s_info.chat_jsonl_path;
    json_resp["cache_dir"] = s_info.cache_dir;
    json_resp["scratch_dir"] = s_info.scratch_dir;
    json_resp["response"] = response_text;

    return json_resp.dump() + "\n";
}

void SocketDaemon::HandleClientConnection(int client_fd) {
    LogMessage("INFO", "Accepted new client connection.");
    
    // Read until EOF so we handle large payloads (e.g. tool output > 4096 bytes)
    std::string request_str;
    char buffer[8192];
    ssize_t bytes_read;
    while ((bytes_read = read(client_fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        request_str += buffer;
    }
    
    if (!request_str.empty()) {
        std::string response_json = ProcessRequestJson(request_str, client_fd);
        write(client_fd, response_json.c_str(), response_json.size());
        // Write newline as delimiter
        write(client_fd, "\n", 1);
    }
    close(client_fd);
}

void SocketDaemon::ListenUnixSocket() {
    std::string path = socket_config_.socket_path;
    unlink(path.c_str());

    // Ensure parent directory exists (~/.razor/)
    auto last_slash = path.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string parent = path.substr(0, last_slash);
        std::string cmd = "mkdir -p \"" + parent + "\"";
        int ret = system(cmd.c_str());
        (void)ret;
    }

    unix_socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (unix_socket_fd_ < 0) return;

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(unix_socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(unix_socket_fd_);
        unix_socket_fd_ = -1;
        return;
    }

    if (listen(unix_socket_fd_, 5) < 0) {
        close(unix_socket_fd_);
        unix_socket_fd_ = -1;
        return;
    }

    while (is_running_) {
        struct pollfd pfd;
        pfd.fd = unix_socket_fd_;
        pfd.events = POLLIN;
        int poll_res = poll(&pfd, 1, 200); // 200ms timeout check
        if (poll_res > 0 && (pfd.revents & POLLIN)) {
            int client_fd = accept(unix_socket_fd_, nullptr, nullptr);
            if (client_fd >= 0) {
                HandleClientConnection(client_fd);
            }
        }
    }
}

void SocketDaemon::ListenTcpSocket() {
    tcp_socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket_fd_ < 0) return;

    int opt = 1;
    setsockopt(tcp_socket_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(socket_config_.tcp_port);

    if (bind(tcp_socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(tcp_socket_fd_);
        tcp_socket_fd_ = -1;
        return;
    }

    if (listen(tcp_socket_fd_, 5) < 0) {
        close(tcp_socket_fd_);
        tcp_socket_fd_ = -1;
        return;
    }

    while (is_running_) {
        struct pollfd pfd;
        pfd.fd = tcp_socket_fd_;
        pfd.events = POLLIN;
        int poll_res = poll(&pfd, 1, 200); // 200ms timeout check
        if (poll_res > 0 && (pfd.revents & POLLIN)) {
            int client_fd = accept(tcp_socket_fd_, nullptr, nullptr);
            if (client_fd >= 0) {
                HandleClientConnection(client_fd);
            }
        }
    }
}

void SocketDaemon::WatchConfig() {
    int fd = inotify_init();
    if (fd < 0) {
        LogMessage("ERROR", "inotify_init failed!");
        return;
    }

    int wd = inotify_add_watch(fd, socket_config_.config_path.c_str(), IN_MODIFY);
    if (wd < 0) {
        LogMessage("ERROR", "inotify_add_watch failed for: " + socket_config_.config_path);
        close(fd);
        return;
    }

    LogMessage("INFO", "Inotify watcher started for config: " + socket_config_.config_path);

    char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    
    while (is_running_) {
        struct pollfd pfd = { fd, POLLIN, 0 };
        int ret = poll(&pfd, 1, 1000); // 1-second timeout
        if (ret > 0) {
            ssize_t len = read(fd, buffer, sizeof(buffer));
            if (len > 0) {
                // Wait briefly in case the editor is still writing to the file
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                // Read through all events in buffer
                bool readd_watch = false;
                for (char* ptr = buffer; ptr < buffer + len; ) {
                    struct inotify_event* event = (struct inotify_event*)ptr;
                    if (event->mask & IN_IGNORED) {
                        readd_watch = true; // File was replaced/deleted
                    }
                    ptr += sizeof(struct inotify_event) + event->len;
                }

                LogMessage("EVENT", "Config file modification detected!");
                try {
                    Config new_config = Config::LoadConfig(socket_config_.config_path);
                    
                    std::unique_lock<std::shared_mutex> lock(config_mutex_);
                    model_config_ = std::move(new_config);
                    
                    LogMessage("SUCCESS", "Technical reload successful: model.yaml hot-swapped in memory.");
                } catch (const std::exception& e) {
                    LogMessage("ERROR", std::string("Failed to hot-reload config: ") + e.what());
                }

                if (readd_watch) {
                    // Re-add the watch since the old inode was destroyed
                    wd = inotify_add_watch(fd, socket_config_.config_path.c_str(), IN_MODIFY);
                }
            }
        }
    }
    
    inotify_rm_watch(fd, wd);
    close(fd);
}

} // namespace razor
