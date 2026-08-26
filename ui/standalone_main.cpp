#include "razor_ui.hpp"
#include "task_manager.hpp"
#include "file_inspector.hpp"
#include <poll.h>
#include <chrono>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <regex>
#include <cstdio>
#include <sys/wait.h>
#include <fstream>
#include <sstream>
#include <iomanip>

static std::string EscapeJsonClient(const std::string& input) {
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

std::string ExecuteCommandAndCapture(const std::string& cmd, int& exit_code) {
    std::array<char, 256> buffer;
    std::string result;
    std::string full_cmd = cmd + " 2>&1";
    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) {
        exit_code = -1;
        return "popen() failed!";
    }
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    int status = pclose(pipe);
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else {
        exit_code = -1;
    }
    return result;
}

std::string SendToDaemonSync(const std::string& prompt, razor::RazorUI* ui = nullptr, int model_index = 0, int timeout_secs = 10, const std::string& session_id = "") {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return "{\"response\": \"Failed to create socket.\"}";
    
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    const char* home = std::getenv("HOME");
    std::string path = std::string(home) + "/.razor/router.sock";
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return "{\"response\": \"Failed to connect to daemon socket. Is razor_router_daemon running?\"}";
    }
    
    json req_json;
    req_json["prompt"] = prompt;
    if (model_index > 0) {
        req_json["model_index"] = model_index;
    }
    if (!session_id.empty()) {
        req_json["session_id"] = session_id;
    }
    
    std::string req = req_json.dump();
    
    write(sock, req.c_str(), req.size());
    // Signal EOF to the server so its read loop terminates
    shutdown(sock, SHUT_WR);
    
    struct timeval tv;
    tv.tv_sec = timeout_secs; // 0 means block indefinitely
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    
    char buffer[8192];
    std::string full_response;
    int bytes;
    
    while ((bytes = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes] = '\0';
        full_response += buffer;
        
        size_t nl_pos;
        while ((nl_pos = full_response.find('\n')) != std::string::npos) {
            std::string line = full_response.substr(0, nl_pos);
            full_response = full_response.substr(nl_pos + 1);
            
            try {
                json j = json::parse(line);
                if (j.contains("status") && j["status"] == "handover") {
                    if (ui) ui->UpdateModelName(j["model"].get<std::string>());
                } else {
                    close(sock);
                    return line;
                }
            } catch (...) {
                // Ignore parsing errors of partial lines
            }
        }
    }
    close(sock);
    
    if (full_response.empty()) return "{\"response\": \"Empty response from daemon.\"}";
    return full_response;
}

void ProcessAndExecute(razor::RazorUI& ui, std::string api_response) {
    bool is_tool_call = false;
    std::string inner_response = "";
    
    try {
        json wrapper = json::parse(api_response);
        
        if (wrapper.contains("session_id") && wrapper["session_id"].is_string()) {
            ui.SetSessionId(wrapper["session_id"].get<std::string>());
        }
        
        if (wrapper.contains("response") && wrapper["response"].is_string()) {
            inner_response = wrapper["response"].get<std::string>();
        } else {
            inner_response = api_response;
        }
        
        json parsed_resp = json::parse(inner_response);
        if (parsed_resp.is_array()) {
            is_tool_call = true;
            
            json results_array = json::array();
            bool should_continue_any = false;
            
            for (auto& tc : parsed_resp) {
                if (tc.contains("tool") && tc.contains("args")) {
                    std::string tool = tc["tool"].get<std::string>();
                    std::string tool_call_id = "";
                    if (tc.contains("tool_call_id")) {
                        tool_call_id = tc["tool_call_id"].get<std::string>();
                    }
                    
                    if (tool == "run_command" || tool == "run") {
                        if (tc.contains("args") && tc["args"].contains("command")) {
                            std::string cmd = tc["args"]["command"].get<std::string>();
                            int work_time = tc["args"].value("work_time", 5);
                            std::string task_name = tc["args"].value("name", "");
                            
                            auto res = razor::TaskManager::Instance().LaunchTask(ui.GetSessionId(), cmd, task_name, work_time);
                            
                            std::string tag;
                            if (res.is_background) {
                                tag = "[TOOL_EXECUTION:run_command|BACKGROUND:" + res.task_id + "] " + cmd;
                            } else {
                                tag = "[TOOL_EXECUTION:run_command|STATUS:" + std::to_string(res.exit_code) + "] " + cmd;
                            }
                            ui.ProvideResponse(tag);
                            
                            json result_payload;
                            result_payload["tool_result"] = true;
                            result_payload["tool_call_id"] = tool_call_id;
                            result_payload["name"] = tool;
                            result_payload["prompt"] = res.output;
                            
                            results_array.push_back(result_payload);
                            should_continue_any = true;
                        }
                    } else if (tool == "manage_task" || tool == "task_management" || tool == "term_manage") {
                        if (tc.contains("args")) {
                            std::string action = tc["args"].value("action", "view");
                            std::string task_id = tc["args"].value("task_id", "");
                            std::string input = tc["args"].value("keycode", tc["args"].value("input", ""));
                            std::string readable_name = razor::TaskManager::Instance().GetTaskName(task_id);
                            
                            std::string output;
                            std::string tag;
                            
                            if (action == "view") {
                                output = razor::TaskManager::Instance().ViewTask(task_id);
                                tag = "[TOOL_EXECUTION:manage_task_view|NAME:" + readable_name + "|ID:" + task_id + "]";
                            } else if (action == "kill") {
                                std::string msg;
                                bool ok = razor::TaskManager::Instance().KillTask(task_id, msg);
                                output = msg;
                                tag = "[TOOL_EXECUTION:manage_task_kill|STATUS:" + std::string(ok ? "0" : "1") + "|NAME:" + readable_name + "|ID:" + task_id + "]";
                            } else if (action == "send_keycode") {
                                std::string msg;
                                bool ok = razor::TaskManager::Instance().SendKeycode(task_id, input, msg);
                                output = msg;
                                tag = "[TOOL_EXECUTION:manage_task_send_keycode|STATUS:" + std::string(ok ? "0" : "1") + "|NAME:" + readable_name + "|ID:" + task_id + "]";
                            } else {
                                output = "Error: Unknown action '" + action + "'. Valid actions are 'view', 'kill', 'send_keycode'.";
                                tag = "[TOOL_EXECUTION_ERROR] Unknown action: " + action;
                            }
                            
                            ui.ProvideResponse(tag);
                            
                            json result_payload;
                            result_payload["tool_result"] = true;
                            result_payload["tool_call_id"] = tool_call_id;
                            result_payload["name"] = tool;
                            result_payload["prompt"] = output;
                            
                            results_array.push_back(result_payload);
                            should_continue_any = true;
                        }
                    } else if (tool == "list_dir" || tool == "list_directory" || tool == "list_files" || tool == "ls") {
                        std::string dir_path = ".";
                        if (tc.contains("args")) {
                            if (tc["args"].contains("path") && tc["args"]["path"].is_string()) {
                                dir_path = tc["args"]["path"].get<std::string>();
                            } else if (tc["args"].contains("dir_path") && tc["args"]["dir_path"].is_string()) {
                                dir_path = tc["args"]["dir_path"].get<std::string>();
                            }
                        }
                        std::string tag = "[TOOL_EXECUTION:list_dir|STATUS:0] " + dir_path;
                        ui.ProvideResponse(tag);
                        
                        auto list_res = razor::FileInspector::ListDirectory(dir_path);
                        std::string output;
                        if (!list_res.error_message.empty()) {
                            output = "Error listing directory: " + list_res.error_message;
                        } else {
                            output = list_res.formatted_table;
                        }
                        
                        json result_payload;
                        result_payload["tool_result"] = true;
                        result_payload["tool_call_id"] = tool_call_id;
                        result_payload["name"] = tool;
                        result_payload["prompt"] = output;
                        
                        results_array.push_back(result_payload);
                        should_continue_any = true;
                    } else if (tool == "read_file" || tool == "read_files") {
                        if (tc.contains("args") && tc["args"].contains("file_path")) {
                            std::string file_path = tc["args"]["file_path"].get<std::string>();
                            std::string tag = "[TOOL_EXECUTION:read_file|STATUS:0] " + file_path;
                            ui.ProvideResponse(tag);
                            
                            std::string output = "Error: File not found.";
                            std::ifstream f(file_path);
                            if (f.good()) {
                                std::stringstream buffer;
                                buffer << f.rdbuf();
                                output = buffer.str();
                            }
                            f.close();
                            
                            json result_payload;
                            result_payload["tool_result"] = true;
                            result_payload["tool_call_id"] = tool_call_id;
                            result_payload["name"] = tool;
                            result_payload["prompt"] = output;
                            
                            results_array.push_back(result_payload);
                            should_continue_any = true;
                        }
                    } else if (tool == "write_file" || tool == "write_files") {
                        if (tc.contains("args") && tc["args"].contains("file_path") && tc["args"].contains("content")) {
                            std::string file_path = tc["args"]["file_path"].get<std::string>();
                            std::string content = tc["args"]["content"].get<std::string>();
                            std::string tag = "[TOOL_EXECUTION:write_file|STATUS:0] " + file_path;
                            ui.ProvideResponse(tag);
                            
                            std::ofstream f(file_path);
                            std::string output;
                            if (f.good()) {
                                f << content;
                                f.close();
                                output = "File written successfully.";
                            } else {
                                output = "Error: Failed to write to file.";
                            }
                            
                            json result_payload;
                            result_payload["tool_result"] = true;
                            result_payload["tool_call_id"] = tool_call_id;
                            result_payload["name"] = tool;
                            result_payload["prompt"] = output;
                            
                            results_array.push_back(result_payload);
                            should_continue_any = true;
                        }
                    } else if (tool == "replace_file_content" || tool == "replace_in_file") {
                        if (tc.contains("args") && tc["args"].contains("file_path") && tc["args"].contains("target") && tc["args"].contains("replacement")) {
                            std::string file_path = tc["args"]["file_path"].get<std::string>();
                            std::string target = tc["args"]["target"].get<std::string>();
                            std::string replacement = tc["args"]["replacement"].get<std::string>();
                            std::string tag = "[TOOL_EXECUTION:replace_file_content|STATUS:0] " + file_path;
                            ui.ProvideResponse(tag);
                            
                            std::string output = "Error: File not found.";
                            std::ifstream f(file_path);
                            if (f.good()) {
                                std::stringstream buffer;
                                buffer << f.rdbuf();
                                std::string content = buffer.str();
                                f.close();
                                
                                size_t pos = content.find(target);
                                if (pos != std::string::npos) {
                                    content.replace(pos, target.length(), replacement);
                                    std::ofstream out_f(file_path);
                                    out_f << content;
                                    out_f.close();
                                    output = "Replacement successful.";
                                } else {
                                    output = "Error: Target string not found in file.";
                                }
                            }
                            
                            json result_payload;
                            result_payload["tool_result"] = true;
                            result_payload["tool_call_id"] = tool_call_id;
                            result_payload["name"] = tool;
                            result_payload["prompt"] = output;
                            
                            results_array.push_back(result_payload);
                            should_continue_any = true;
                        }
                    } else {
                        std::string tag = "[TOOL_EXECUTION_ERROR] Unsupported tool requested: " + tool;
                        ui.ProvideResponse(tag);
                        
                        json result_payload;
                        result_payload["tool_result"] = true;
                        result_payload["tool_call_id"] = tool_call_id;
                        result_payload["name"] = tool;
                        result_payload["prompt"] = "Error: Tool '" + tool + "' is not implemented in the standalone UI. Please use run_command instead.";
                        
                        results_array.push_back(result_payload);
                        should_continue_any = true;
                    }
                }
            }
            
            if (should_continue_any && !results_array.empty()) {
                ui.StartThinking("Primary Model");
                std::string next_resp = SendToDaemonSync(results_array.dump(), &ui, 0, 0, ui.GetSessionId());
                ProcessAndExecute(ui, next_resp);
            }
        }
    } catch (...) {
        // Not a JSON array, just ignore
    }

    if (!is_tool_call) {
        if (!inner_response.empty()) {
            ui.ProvideResponse(inner_response);
        } else {
            ui.ProvideResponse(api_response);
        }
    }
}

#include "socket_daemon.hpp"
#include "config.hpp"

int main() {
    std::string config_path = "model.yaml";
    razor::Config cfg = razor::Config::LoadConfig(config_path);

    razor::SocketConfig daemon_cfg;
    daemon_cfg.config_path = config_path;
    
    // Fuse MCP server / Socket daemon directly into UI process
    razor::SocketDaemon daemon(daemon_cfg);
    daemon.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Allow daemon socket initialization

    razor::RazorUI ui;
    ui.SetUserName(cfg.user_name.empty() ? "Shado" : cfg.user_name);
    
    std::vector<std::string> model_names;
    for (const auto& m : cfg.models) {
        if (!m.name.empty()) {
            model_names.push_back(m.name);
        }
    }
    if (model_names.empty()) model_names.push_back("Default Model");
    ui.SetAvailableModels(model_names);
    
    ui.SetSubmitCallback([&ui, model_names](const std::string& prompt) {
        std::thread([prompt, &ui, model_names]() {
            int selected_idx = ui.GetSelectedModelIndex();
            std::string m_name = "Primary Model";
            if (selected_idx >= 0 && selected_idx < (int)model_names.size()) {
                m_name = model_names[selected_idx];
            }
            ui.StartThinking(m_name);
            std::string resp = SendToDaemonSync(prompt, &ui, selected_idx, 120, ui.GetSessionId());
            ProcessAndExecute(ui, resp);
        }).detach();
    });
    
    ui.Run();

    daemon.Stop();
    return 0;
}
