#include "razor_ui.hpp"
#include "task_manager.hpp"
#include "file_inspector.hpp"
#include "web_search.hpp"
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

std::string SendToDaemonSync(const std::string& prompt, razor::RazorUI* ui = nullptr, int model_index = 0, int timeout_secs = 60, const std::string& session_id = "") {
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
                if (j.contains("status") && (j["status"] == "stream_chunk" || j["status"] == "reasoning_chunk")) {
                    if (j["status"] == "stream_chunk") {
                        if (ui) ui->AppendStreamToken(j["token"].get<std::string>());
                    } else if (j["status"] == "reasoning_chunk") {
                        if (ui) ui->AppendReasoningToken(j["token"].get<std::string>());
                    }
                } else if (j.contains("status") && j["status"] == "handover") {
                    if (ui) ui->UpdateModelName(j["model"].get<std::string>());
                } else if (j.contains("status") && j["status"] == "error") {
                    if (ui && j.contains("session_id")) ui->SetSessionId(j["session_id"].get<std::string>());
                    close(sock);
                    if (j.contains("response")) return j["response"].get<std::string>();
                    return "";
                } else if (j.contains("status") && j["status"] == "success") {
                    if (ui && j.contains("session_id")) ui->SetSessionId(j["session_id"].get<std::string>());
                    close(sock);
                    if (j.contains("response")) return j["response"].get<std::string>();
                    return "";
                } else if (!j.contains("status")) {
                    // Just return if status is missing, likely final response
                    close(sock);
                    return line;
                } else {
                    close(sock);
                    return "";
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

void ProcessAndExecute(razor::RazorUI& ui, const std::string& api_response, int retries = 0) {
    bool is_tool_call = false;
    std::string inner_response = api_response; // Default to api_response directly
    
    try {
        json wrapper = json::parse(api_response);
        
        if (wrapper.is_object()) {
            if (wrapper.contains("session_id") && wrapper["session_id"].is_string()) {
                ui.SetSessionId(wrapper["session_id"].get<std::string>());
            }
            
            if (wrapper.contains("response") && wrapper["response"].is_string()) {
                inner_response = wrapper["response"].get<std::string>();
            }
        }
    } catch (...) {
        // Not a JSON wrapper, just use api_response directly as inner_response
    }
        
        json parsed_resp;
        try {
            parsed_resp = json::parse(inner_response);
        } catch (...) {
            // Check for markdown json block
            size_t start = inner_response.find("```json");
            if (start == std::string::npos) start = inner_response.find("```");
            if (start != std::string::npos) {
                size_t json_start = inner_response.find_first_of("[{", start);
                size_t end = inner_response.find("```", json_start);
                if (json_start != std::string::npos && end != std::string::npos) {
                    try {
                        parsed_resp = json::parse(inner_response.substr(json_start, end - json_start));
                    } catch (...) {}
                }
            }
        }

        std::string leading_text = "";
        json tools_array = json::array();

        if (parsed_resp.is_object()) {
            if (parsed_resp.contains("content") && parsed_resp["content"].is_string() && !parsed_resp["content"].get<std::string>().empty()) {
                leading_text = parsed_resp["content"].get<std::string>();
            }
            if (parsed_resp.contains("tool_calls") && parsed_resp["tool_calls"].is_array()) {
                tools_array = parsed_resp["tool_calls"];
            } else if (parsed_resp.contains("tool") && parsed_resp.contains("args")) {
                tools_array.push_back(parsed_resp);
            }
        } else if (parsed_resp.is_array()) {
            tools_array = parsed_resp;
        }
        
        if (!tools_array.empty() && tools_array[0].is_object() && tools_array[0].contains("tool")) {
            is_tool_call = true;
            
            // Print the model's text/thought first before displaying tool executions!
            if (!leading_text.empty()) {
                ui.ProvideResponse(leading_text);
            } else {
                // If there's no explicitly separated leading text (e.g., fallback markdown JSON), 
                // preserve the entire streamed text so it doesn't get wiped by the tool execution tag.
                ui.ProvideResponse(inner_response);
            }

            json results_array = json::array();
            bool should_continue_any = false;
            
            for (auto& tc : tools_array) {
                if (tc.contains("tool") && tc.contains("args")) {
                    try {
                        std::string tool = tc["tool"].get<std::string>();
                        std::string tool_call_id = "";
                        if (tc.contains("tool_call_id")) {
                            tool_call_id = tc["tool_call_id"].get<std::string>();
                        }
                        
                        if (tool == "run_command" || tool == "run") {
                            if (tc.contains("args") && tc.at("args").contains("command") && tc.at("args").at("command").is_string()) {
                                std::string cmd = tc["args"]["command"].get<std::string>();
                                int work_time = 5;
                                if (tc.at("args").contains("work_time")) {
                                    if (tc.at("args")["work_time"].is_number()) work_time = tc["args"]["work_time"].get<int>();
                                    else if (tc.at("args")["work_time"].is_string()) work_time = std::stoi(tc["args"]["work_time"].get<std::string>());
                                }
                                std::string task_name = "";
                                if (tc.at("args").contains("name") && tc.at("args")["name"].is_string()) {
                                    task_name = tc["args"]["name"].get<std::string>();
                                }
                                
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
                                result_payload["prompt"] = res.output.empty() ? "(Command executed successfully with exit code " + std::to_string(res.exit_code) + " and no output)" : res.output;
                                
                                results_array.push_back(result_payload);
                                should_continue_any = true;
                            }
                        } else if (tool == "manage_task" || tool == "task_management" || tool == "term_manage") {
                            if (tc.contains("args") && tc.at("args").is_object()) {
                                std::string action = "view";
                                if (tc.at("args").contains("action") && tc.at("args")["action"].is_string()) action = tc["args"]["action"].get<std::string>();
                                
                                std::string task_id = "";
                                if (tc.at("args").contains("task_id") && tc.at("args")["task_id"].is_string()) task_id = tc["args"]["task_id"].get<std::string>();
                                
                                std::string input = "";
                                if (tc.at("args").contains("keycode") && tc.at("args")["keycode"].is_string()) input = tc["args"]["keycode"].get<std::string>();
                                else if (tc.at("args").contains("input") && tc.at("args")["input"].is_string()) input = tc["args"]["input"].get<std::string>();
                                
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
                                    tag = "[TOOL_EXECUTION:manage_task_send|STATUS:" + std::string(ok ? "0" : "1") + "|NAME:" + readable_name + "|ID:" + task_id + "]";
                                } else {
                                    output = "Error: Invalid action.";
                                    tag = "[TOOL_EXECUTION_ERROR:manage_task] Invalid action: " + action;
                                }
                                ui.ProvideResponse(tag);
                                
                                json result_payload;
                                result_payload["tool_result"] = true;
                                result_payload["tool_call_id"] = tool_call_id;
                                result_payload["name"] = tool;
                                result_payload["prompt"] = output.empty() ? "(Success with no output)" : output;
                                
                                results_array.push_back(result_payload);
                                should_continue_any = true;
                            }
                        } else if (tool == "read_file" || tool == "read_files") {
                            if (tc.contains("args") && tc.at("args").contains("file_path") && tc.at("args")["file_path"].is_string()) {
                                std::string file_path = tc["args"]["file_path"].get<std::string>();
                                std::string tag = "[TOOL_EXECUTION:read_file|STATUS:0] " + file_path;
                                ui.ProvideResponse(tag);
                                
                                std::string output = "Error: File not found.";
                                std::ifstream f(file_path);
                                if (f.good()) {
                                    std::stringstream buffer;
                                    buffer << f.rdbuf();
                                    output = buffer.str();
                                    if (output.empty()) output = "(File is empty)";
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
                            if (tc.contains("args") && tc.at("args").contains("file_path") && tc.at("args")["file_path"].is_string() && tc.at("args").contains("content") && tc.at("args")["content"].is_string()) {
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
                        } else if (tool == "list_dir" || tool == "list_directory" || tool == "list_files" || tool == "ls") {
                            if (tc.contains("args") && (tc.at("args").contains("dir_path") || tc.at("args").contains("path"))) {
                                std::string dir_path = ".";
                                if (tc.at("args").contains("path") && tc.at("args")["path"].is_string()) dir_path = tc["args"]["path"].get<std::string>();
                                else if (tc.at("args").contains("dir_path") && tc.at("args")["dir_path"].is_string()) dir_path = tc["args"]["dir_path"].get<std::string>();
                                
                                std::string tag = "[TOOL_EXECUTION:list_dir|STATUS:0] " + dir_path;
                                ui.ProvideResponse(tag);
                                
                                std::string output = "Error: Directory not found or permission denied.";
                                std::string cmd = "ls -la \"" + dir_path + "\" 2>&1";
                                FILE* pipe = popen(cmd.c_str(), "r");
                                if (pipe) {
                                    char buffer[128];
                                    output = "";
                                    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                                        output += buffer;
                                    }
                                    pclose(pipe);
                                    if (output.empty()) output = "(Directory is empty)";
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
                            if (tc.contains("args") && tc.at("args").contains("file_path") && tc.at("args")["file_path"].is_string() &&
                                tc.at("args").contains("search_string") && tc.at("args")["search_string"].is_string() &&
                                tc.at("args").contains("replace_string") && tc.at("args")["replace_string"].is_string()) {
                                
                                std::string file_path = tc["args"]["file_path"].get<std::string>();
                                std::string search = tc["args"]["search_string"].get<std::string>();
                                std::string replace = tc["args"]["replace_string"].get<std::string>();
                                std::string tag = "[TOOL_EXECUTION:replace_file_content|STATUS:0] " + file_path;
                                ui.ProvideResponse(tag);
                                
                                std::string output = "Error: File not found.";
                                std::ifstream in_file(file_path);
                                if (in_file.good()) {
                                    std::stringstream buffer;
                                    buffer << in_file.rdbuf();
                                    std::string content = buffer.str();
                                    in_file.close();
                                    
                                    size_t pos = content.find(search);
                                    if (pos != std::string::npos) {
                                        content.replace(pos, search.length(), replace);
                                        std::ofstream out_file(file_path);
                                        if (out_file.good()) {
                                            out_file << content;
                                            out_file.close();
                                            output = "Replacement successful.";
                                        } else {
                                            output = "Error: Could not write to file.";
                                        }
                                    } else {
                                        output = "Error: Search string not found in file.";
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
                        } else if (tool == "web_search") {
                            if (tc.contains("args") && tc.at("args").contains("query") && tc.at("args")["query"].is_string()) {
                                std::string query = tc["args"]["query"].get<std::string>();
                                std::string tag = "[TOOL_EXECUTION:web_search|STATUS:0] " + query;
                                ui.ProvideResponse(tag);
                                
                                std::string output = "Error: Web search failed.";
                                std::string cmd = "curl -s \"https://html.duckduckgo.com/html/?q=" + query + "\" | grep -oE 'class=\"result__snippet[^>]*>.*' | sed 's/<[^>]*>//g' | head -n 5";
                                FILE* pipe = popen(cmd.c_str(), "r");
                                if (pipe) {
                                    char buffer[128];
                                    output = "";
                                    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                                        output += buffer;
                                    }
                                    pclose(pipe);
                                    if (output.empty()) output = "No results found.";
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
                    } catch (...) {
                        // Catch type errors safely, let should_continue_any remain false!
                    }
                }
            }
            
            int current_model_idx = ui.GetSelectedModelIndex();
            std::string current_model_name = "Primary Model";
            auto model_names = ui.GetAvailableModels();
            if (current_model_idx >= 0 && current_model_idx < (int)model_names.size()) {
                current_model_name = model_names[current_model_idx];
            }
            
            if (should_continue_any && !results_array.empty()) {
                ui.StartThinking(current_model_name);
                std::string next_resp = SendToDaemonSync(results_array.dump(), &ui, current_model_idx, 0, ui.GetSessionId());
                ProcessAndExecute(ui, next_resp, retries);
            } else {
                std::string fallback_tool = tools_array[0].value("tool", "unknown");
                std::string fallback_id = tools_array[0].value("tool_call_id", "");
                
                std::string expected_schema = "Check the tools schema.";
                if (fallback_tool == "run_command" || fallback_tool == "run") {
                    expected_schema = "Expected arguments: {\"command\": \"<string>\", \"work_time\": <optional int>, \"name\": \"<optional string>\"}";
                } else if (fallback_tool == "manage_task" || fallback_tool == "task_management" || fallback_tool == "term_manage") {
                    expected_schema = "Expected arguments: {\"action\": \"view\"|\"kill\"|\"send_keycode\", \"task_id\": \"<string>\", \"keycode\": \"<optional string>\"}";
                } else if (fallback_tool == "read_file" || fallback_tool == "read_files") {
                    expected_schema = "Expected arguments: {\"file_path\": \"<absolute path>\"}";
                } else if (fallback_tool == "write_file" || fallback_tool == "write_files") {
                    expected_schema = "Expected arguments: {\"file_path\": \"<absolute path>\", \"content\": \"<string>\"}";
                } else if (fallback_tool == "list_dir" || fallback_tool == "list_directory" || fallback_tool == "list_files" || fallback_tool == "ls") {
                    expected_schema = "Expected arguments: {\"dir_path\": \"<absolute path>\"}";
                } else if (fallback_tool == "replace_file_content" || fallback_tool == "replace_in_file") {
                    expected_schema = "Expected arguments: {\"file_path\": \"<absolute path>\", \"search_string\": \"<exact text>\", \"replace_string\": \"<new text>\"}";
                } else if (fallback_tool == "web_search") {
                    expected_schema = "Expected arguments: {\"query\": \"<search query>\"}";
                }
                
                std::string tag = "[TOOL_EXECUTION_ERROR] Tool '" + fallback_tool + "' called with missing or invalid arguments.";
                ui.ProvideResponse(tag);
                
                std::string next_resp;
                if (retries >= 3) {
                    ui.ProvideResponse("[SYSTEM ABORT] Model failed to output a valid tool format after multiple attempts.");
                    return;
                }
                
                if (!fallback_id.empty()) {
                    json result_payload;
                    result_payload["tool_result"] = true;
                    result_payload["tool_call_id"] = fallback_id;
                    result_payload["name"] = fallback_tool;
                    result_payload["prompt"] = "Error: Tool '" + fallback_tool + "' failed to execute because required arguments were missing or invalid. " + expected_schema;
                    
                    ui.StartThinking(current_model_name);
                    next_resp = SendToDaemonSync(json::array({result_payload}).dump(), &ui, current_model_idx, 0, ui.GetSessionId());
                } else {
                    ui.StartThinking(current_model_name);
                    std::string reprompt = "[SYSTEM NOTE]: You attempted to call tool '" + fallback_tool + "' but the format was completely invalid (missing tool_call_id). Please output a valid JSON array of tool calls instead.";
                    next_resp = SendToDaemonSync(reprompt, &ui, current_model_idx, 0, ui.GetSessionId());
                }
                
                ProcessAndExecute(ui, next_resp, retries + 1);
            }
    }
    
    if (!is_tool_call) {
        int current_model_idx = ui.GetSelectedModelIndex();
        std::string current_model_name = "Primary Model";
        auto model_names = ui.GetAvailableModels();
        if (current_model_idx >= 0 && current_model_idx < (int)model_names.size()) {
            current_model_name = model_names[current_model_idx];
        }
        
        std::string trimmed_resp = inner_response;
        trimmed_resp.erase(0, trimmed_resp.find_first_not_of(" \t\r\n`'\""));
        trimmed_resp.erase(trimmed_resp.find_last_not_of(" \t\r\n`'\"") + 1);
        
        if (trimmed_resp == "run_command" || trimmed_resp == "write_file" || trimmed_resp == "read_file" || trimmed_resp == "list_dir" || trimmed_resp == "manage_task" || trimmed_resp == "replace_file_content" || trimmed_resp == "web_search") {
            if (retries < 3) {
                ui.StartThinking(current_model_name);
                std::string reprompt = "[SYSTEM NOTE]: You outputted the raw tool name '" + trimmed_resp + "' as text. Please output a valid JSON array of tool calls instead.";
                std::string next_resp = SendToDaemonSync(reprompt, &ui, current_model_idx, 0, ui.GetSessionId());
                ProcessAndExecute(ui, next_resp, retries + 1);
                return;
            }
        }
        
        if (trimmed_resp.empty()) {
            if (retries < 2) {
                ui.StartThinking(current_model_name);
                std::string reprompt = "[SYSTEM NOTE]: You outputted reasoning tokens but failed to output a final text answer or a valid JSON tool call array. Please continue by outputting a JSON tool array or a final answer.";
                std::string next_resp = SendToDaemonSync(reprompt, &ui, current_model_idx, 0, ui.GetSessionId());
                ProcessAndExecute(ui, next_resp, retries + 1);
                return;
            }
            ui.ProvideResponse("");
            return;
        }

        ui.ProvideResponse(inner_response);
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

    // Run python script to sync modelinfo
    std::system("python3 scripts/update_modelinfo.py >/dev/null 2>&1");
    
    razor::RazorUI ui;
    ui.SetUserName(cfg.user_name.empty() ? "Shado" : cfg.user_name);
    
    // Load limits
    std::map<std::string, int> limits;
    std::string home_dir = getenv("HOME") ? getenv("HOME") : "";
    if (!home_dir.empty()) {
        std::ifstream info_file(home_dir + "/.razor/modelinfo.json");
        if (info_file.is_open()) {
            try {
                nlohmann::json info_json;
                info_file >> info_json;
                for (auto& el : info_json.items()) {
                    limits[el.key()] = el.value().value("max_ctx", 32768);
                }
            } catch (...) {}
        }
    }
    ui.SetModelContextLimits(limits);

    std::vector<std::string> model_names;
    std::vector<razor::RazorUI::ModelEntryInfo> model_details;
    for (const auto& m : cfg.models) {
        if (!m.name.empty()) {
            model_names.push_back(m.name);
            razor::RazorUI::ModelEntryInfo info;
            info.name = m.name;
            info.provider = m.provider;
            info.model = m.model;
            model_details.push_back(info);
        }
    }
    if (model_names.empty()) model_names.push_back("Default Model");
    
    ui.SetModelDetails(model_details);
    
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
