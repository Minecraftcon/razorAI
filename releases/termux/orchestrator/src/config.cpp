#include "config.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <unordered_set>
#include <filesystem>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace razor {

static const std::vector<std::string> kWildcardTools = {
    "run_command",
    "run",
    "manage_task",
    "task_management",
    "term_manage",
    "list_dir",
    "list_directory",
    "list_files",
    "ls",
    "read_file",
    "read_files",
    "write_file",
    "write_files",
    "replace_file_content",
    "replace_in_file",
    "web_search",
    "search_web",
    "fetch_url",
};

static std::string Trim(const std::string& str) {
    auto first = str.find_first_not_of(" \t\r\n\"'");
    if (first == std::string::npos) return "";
    auto last = str.find_last_not_of(" \t\r\n\"'");
    return str.substr(first, (last - first + 1));
}


static void ResolveDefaultEndpoint(ModelEntry& model) {
    if (!model.endpoint.empty()) return;
    
    std::string p = model.provider;
    std::transform(p.begin(), p.end(), p.begin(), ::tolower);
    
    if (p == "mistral") model.endpoint = "https://api.mistral.ai/v1/chat/completions";
    else if (p == "gemini") model.endpoint = "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions";
    else if (p == "openai") model.endpoint = "https://api.openai.com/v1/chat/completions";
    else if (p == "nvidia" || p == "nim") model.endpoint = "https://integrate.api.nvidia.com/v1/chat/completions";
    else if (p == "grok" || p == "xai") model.endpoint = "https://api.x.ai/v1/chat/completions";
    else if (p == "anthropic") model.endpoint = "https://api.anthropic.com/v1/messages"; 
}

static std::vector<std::string> ExpandTools(const std::vector<std::string>& tools) {
    std::vector<std::string> expanded;
    std::unordered_set<std::string> seen;

    auto add_tool = [&](const std::string& tool) {
        if (tool.empty() || seen.count(tool)) return;
        seen.insert(tool);
        expanded.push_back(tool);
    };

    for (const auto& tool : tools) {
        if (tool == "all") {
            for (const auto& wildcard_tool : kWildcardTools) {
                add_tool(wildcard_tool);
            }
        } else {
            add_tool(tool);
        }
    }

    return expanded;
}

std::string Config::ExpandEnvVar(const std::string& value) {
    std::string val = Trim(value);
    if (val.size() > 3 && val.substr(0, 2) == "${" && val.back() == '}') {
        std::string var_name = val.substr(2, val.size() - 3);
        const char* env_val = std::getenv(var_name.c_str());
        if (env_val) {
            return std::string(env_val);
        }
        return ""; // Environment variable not set
    }
    return val;
}

std::string Config::GetDefaultConfigPath() {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/.razor/model.yaml";
    }
    return "model.yaml";
}

std::string Config::ResolveConfigPath(const std::string& requested_path) {
    if (!requested_path.empty()) {
        std::ifstream f(requested_path);
        if (f.is_open()) return requested_path;
    }

    const char* razor_home = std::getenv("RAZOR_HOME");
    const char* home = std::getenv("HOME");
    std::vector<std::string> search_candidates;

    if (razor_home) {
        search_candidates.push_back(std::string(razor_home) + "/model.yaml");
        search_candidates.push_back(std::string(razor_home) + "/config.yaml");
    }
    if (home) {
        search_candidates.push_back(std::string(home) + "/.razor/model.yaml");
        search_candidates.push_back(std::string(home) + "/.razor/config.yaml");
    }
    search_candidates.push_back("model.yaml");
    search_candidates.push_back("config.yaml");

    for (const auto& candidate : search_candidates) {
        std::ifstream f(candidate);
        if (f.is_open()) {
            return candidate;
        }
    }

    // Default fallback
    if (home) {
        return std::string(home) + "/.razor/model.yaml";
    }
    return "model.yaml";
}

Config Config::LoadConfig(const std::string& path) {
    Config cfg;
    std::string target_path = ResolveConfigPath(path);

    std::ifstream file(target_path);
    if (!file.is_open()) {
        return cfg;
    }


    std::string line;
    std::string current_section;
    ModelEntry current_model;
    EmbeddingEntry current_embedding;
    bool in_model_block = false;
    bool in_emb_block = false;
    std::string current_model_list = "roles";

    while (std::getline(file, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        if (trimmed.find("version:") == 0) {
            continue;
        }

        if (trimmed.find("globalSysprompt:") == 0) {
            current_section = "globalSysprompt";
            continue;
        }

        if (trimmed.find("models:") == 0) {
            current_section = "models";
            continue;
        }

        if (trimmed.find("embeddings:") == 0) {
            if (in_model_block && (!current_model.name.empty() || !current_model.provider.empty())) {
                ResolveDefaultEndpoint(current_model);
                current_model.tools = ExpandTools(current_model.tools);
                cfg.models.push_back(current_model);
                current_model = ModelEntry();
                in_model_block = false;
            }
            current_section = "embeddings";
            continue;
        }

        if (current_section == "globalSysprompt" && (line.find("- ") != std::string::npos)) {
            auto pos = line.find("- ");
            cfg.global_sysprompt.push_back(Trim(line.substr(pos + 2)));
            continue;
        }

        if (current_section == "models") {
            if (line.find("- name:") != std::string::npos || line.find("- provider:") != std::string::npos) {
                if (in_model_block && (!current_model.name.empty() || !current_model.provider.empty())) {
                    ResolveDefaultEndpoint(current_model);
                    current_model.tools = ExpandTools(current_model.tools);
                    cfg.models.push_back(current_model);
                    current_model = ModelEntry();
                }
                in_model_block = true;
                current_model_list = "roles"; // reset list state
            }

            auto dash_pos = line.find("- ");
            auto colon = line.find(':');

            if (colon != std::string::npos) {
                std::string key = Trim(line.substr(0, colon));
                if (!key.empty() && key[0] == '-') key = Trim(key.substr(1));
                std::string val = Trim(line.substr(colon + 1));

                if (key == "name") current_model.name = val;
                else if (key == "model") current_model.model = val;
                else if (key == "provider") current_model.provider = val;
                else if (key == "apiKey") current_model.api_key = ExpandEnvVar(val);
                else if (key == "endpoint") current_model.endpoint = val;
                else if (key == "tools") current_model_list = "tools";
            } else if (dash_pos != std::string::npos && in_model_block) {
                std::string list_val = Trim(line.substr(dash_pos + 2));
                if (!list_val.empty() && current_model_list == "tools") {
                    current_model.tools.push_back(list_val);
                }
            }
            continue;
        }

        if (current_section == "embeddings") {
            if (line.find("- provider:") != std::string::npos) {
                if (in_emb_block && !current_embedding.provider.empty()) {
                    cfg.embeddings.push_back(current_embedding);
                    current_embedding = EmbeddingEntry();
                }
                in_emb_block = true;
            }

            auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = Trim(line.substr(0, colon));
                if (!key.empty() && key[0] == '-') key = Trim(key.substr(1));
                std::string val = Trim(line.substr(colon + 1));

                if (key == "provider") current_embedding.provider = val;
                else if (key == "model") current_embedding.model = val;
                else if (key == "apiKey") current_embedding.api_key = ExpandEnvVar(val);
            }
            continue;
        }

        // Standard key: val fallback
        auto colon = line.find(':');
        if (colon != std::string::npos && current_section.empty()) {
            std::string key = Trim(line.substr(0, colon));
            std::string val = Trim(line.substr(colon + 1));
            if (key == "user_name" || key == "UserName") cfg.user_name = val;
            else if (key == "timeout_seconds" || key == "TimeoutSeconds") {
                try { cfg.timeout_seconds = std::stoi(val); } catch (...) {}
            }
        }
    }

    if (in_model_block && (!current_model.name.empty() || !current_model.provider.empty())) {
        ResolveDefaultEndpoint(current_model);
        current_model.tools = ExpandTools(current_model.tools);
        cfg.models.push_back(current_model);
    }
    if (in_emb_block && !current_embedding.provider.empty()) {
        cfg.embeddings.push_back(current_embedding);
    }

    return cfg;
}

} // namespace razor
