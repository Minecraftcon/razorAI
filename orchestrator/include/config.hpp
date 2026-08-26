#ifndef RAZOR_CONFIG_HPP
#define RAZOR_CONFIG_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <map>

namespace razor {

struct ModelEntry {
    std::string name;
    std::string model;
    std::string provider;
    std::string api_key;
    std::string endpoint;
    std::vector<std::string> roles;
    std::vector<std::string> tools;
};

struct EmbeddingEntry {
    std::string provider;
    std::string model;
    std::string api_key;
};

struct RoleConfig {
    std::string role_name;
    std::string description;
    std::vector<std::string> sys_prompt;
    std::vector<std::string> tools;
};

struct Config {
    std::string user_name = "Shado";
    int timeout_seconds = 300;
    std::string agent_executable = "";
    std::vector<std::string> global_sysprompt;
    std::vector<ModelEntry> models;
    std::vector<EmbeddingEntry> embeddings;
    std::map<std::string, RoleConfig> roles;

    // Helper to find model entry assigned to a specific role
    const ModelEntry* GetModelForRole(const std::string& role) const;
    static std::string ExpandEnvVar(const std::string& value);

    static Config LoadConfig(const std::string& path);
};

} // namespace razor

#endif // RAZOR_CONFIG_HPP
