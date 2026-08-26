#include "config.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace razor {

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
    else if (p == "grok" || p == "xai") model.endpoint = "https://api.x.ai/v1/chat/completions";
    else if (p == "anthropic") model.endpoint = "https://api.anthropic.com/v1/messages"; 
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

const ModelEntry* Config::GetModelForRole(const std::string& role) const {
    for (const auto& m : models) {
        for (const auto& r : m.roles) {
            if (r == role) {
                return &m;
            }
        }
    }
    return nullptr;
}

Config Config::LoadConfig(const std::string& path) {
    Config cfg;
    
    // Try provided path first, fallback to model.yaml or config.yaml
    std::string target_path = path;
    std::ifstream check_file(target_path);
    if (!check_file.is_open() && target_path != "model.yaml") {
        std::ifstream model_file("model.yaml");
        if (model_file.is_open()) {
            target_path = "model.yaml";
        }
    }

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
                else if (key == "roles") current_model_list = "roles";
                else if (key == "tools") current_model_list = "tools";
            } else if (dash_pos != std::string::npos && in_model_block) {
                std::string list_val = Trim(line.substr(dash_pos + 2));
                if (!list_val.empty()) {
                    if (current_model_list == "tools") {
                        current_model.tools.push_back(list_val);
                    } else {
                        current_model.roles.push_back(list_val);
                    }
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
                    cfg.models.push_back(current_model);
    }
    if (in_emb_block && !current_embedding.provider.empty()) {
        cfg.embeddings.push_back(current_embedding);
    }

    // --- Load Role YAMLs from ~/.razor/roles/ (Placeholder plugin directory) ---
    const char* home = std::getenv("HOME");
    if (home) {
        fs::path roles_dir = fs::path(home) / ".razor" / "roles";
        if (fs::exists(roles_dir) && fs::is_directory(roles_dir)) {
            for (const auto& entry : fs::directory_iterator(roles_dir)) {
                if (entry.path().extension() == ".yaml" || entry.path().extension() == ".yml") {
                    try {
                        YAML::Node node = YAML::LoadFile(entry.path().string());
                        if (node.IsMap()) {
                            RoleConfig rc;
                            if (node["roleName"]) {
                                rc.role_name = node["roleName"].as<std::string>();
                            } else if (node["name"]) {
                                rc.role_name = node["name"].as<std::string>();
                            } else {
                                rc.role_name = entry.path().stem().string();
                            }
                            
                            if (node["description"]) {
                                rc.description = node["description"].as<std::string>();
                            }

                            if (node["sysPrompt"]) {
                                if (node["sysPrompt"].IsSequence()) {
                                    for (const auto& sp : node["sysPrompt"]) {
                                        rc.sys_prompt.push_back(sp.as<std::string>());
                                    }
                                } else if (node["sysPrompt"].IsScalar()) {
                                    rc.sys_prompt.push_back(node["sysPrompt"].as<std::string>());
                                }
                            }
                            
                            if (node["tools"] && node["tools"].IsSequence()) {
                                for (const auto& tool : node["tools"]) {
                                    rc.tools.push_back(tool.as<std::string>());
                                }
                            }

                            cfg.roles[rc.role_name] = rc;
                        }
                    } catch (const YAML::Exception& e) {
                        std::cerr << "[Razor Config] Failed to parse role YAML: " << entry.path() << " - " << e.what() << "\n";
                    }
                }
            }
        }
    }

    return cfg;
}

} // namespace razor
