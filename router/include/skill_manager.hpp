#pragma once

#include <string>
#include <vector>
#include <map>

namespace razor {

struct SkillInfo {
    std::string name;
    std::string description;
    std::string path;
    std::string plugin_or_source;
};

class SkillManager {
public:
    static SkillManager& Instance() {
        static SkillManager instance;
        return instance;
    }

    // Discovers skills from ~/.gemini/config/skills, ~/.gemini/config/plugins, and .agents/skills
    std::vector<SkillInfo> DiscoverSkills(bool force_refresh = false);

    // Returns a list of all skill names for autocomplete
    std::vector<std::string> GetSkillNames();

    // Formatted table/list of skills
    std::string FormatSkillsList(const std::string& filter = "");

    // Get the full markdown content of a specific skill
    std::string GetSkillContent(const std::string& skill_name);

    // Search skills matching query
    std::vector<SkillInfo> SearchSkills(const std::string& query);

private:
    SkillManager() = default;
    void ParseFrontmatter(const std::string& skill_file_path, SkillInfo& info);
    
    std::vector<SkillInfo> cached_skills_;
    bool scanned_{false};
};

} // namespace razor
