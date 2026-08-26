#include "skill_manager.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <algorithm>
#include <regex>

namespace fs = std::filesystem;

namespace razor {

void SkillManager::ParseFrontmatter(const std::string& skill_file_path, SkillInfo& info) {
    std::ifstream file(skill_file_path);
    if (!file.is_open()) return;

    std::string line;
    bool in_frontmatter = false;
    std::string frontmatter_text;

    if (std::getline(file, line)) {
        // Trim line
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line == "---") {
            in_frontmatter = true;
            while (std::getline(file, line)) {
                std::string trimmed = line;
                trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
                trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
                if (trimmed == "---") {
                    break;
                }
                
                if (trimmed.rfind("name:", 0) == 0) {
                    info.name = trimmed.substr(5);
                    info.name.erase(0, info.name.find_first_not_of(" \"'\t"));
                    info.name.erase(info.name.find_last_not_of(" \"'\t") + 1);
                } else if (trimmed.rfind("description:", 0) == 0) {
                    info.description = trimmed.substr(12);
                    info.description.erase(0, info.description.find_first_not_of(" \"'\t"));
                    info.description.erase(info.description.find_last_not_of(" \"'\t") + 1);
                }
            }
        }
    }

    if (info.name.empty()) {
        fs::path p(skill_file_path);
        info.name = p.parent_path().filename().string();
    }
}

std::vector<SkillInfo> SkillManager::DiscoverSkills(bool force_refresh) {
    if (scanned_ && !force_refresh) {
        return cached_skills_;
    }

    cached_skills_.clear();
    std::vector<std::string> search_roots;

    const char* home = std::getenv("HOME");
    if (home) {
        search_roots.push_back(std::string(home) + "/.gemini/config/skills");
        search_roots.push_back(std::string(home) + "/.gemini/config/plugins");
        search_roots.push_back(std::string(home) + "/.gemini/antigravity-ide/builtin/skills");
    }
    search_roots.push_back(".agents/skills");
    search_roots.push_back("assets/skills");
    search_roots.push_back("assets/plugins");

    for (const auto& root_str : search_roots) {
        fs::path root_path(root_str);
        std::error_code ec;
        if (!fs::exists(root_path, ec)) continue;

        try {
            for (const auto& entry : fs::recursive_directory_iterator(root_path, fs::directory_options::skip_permission_denied, ec)) {
                if (ec) break;
                if (entry.is_regular_file()) {
                    std::string fname = entry.path().filename().string();
                    if (fname == "SKILL.md" || fname == "skill.md") {
                        SkillInfo info;
                        info.path = entry.path().string();
                        
                        // Extract plugin name if within a plugins directory
                        std::string full_path = entry.path().string();
                        size_t plug_idx = full_path.find("/plugins/");
                        if (plug_idx != std::string::npos) {
                            size_t start = plug_idx + 9;
                            size_t end = full_path.find('/', start);
                            if (end != std::string::npos) {
                                info.plugin_or_source = full_path.substr(start, end - start);
                            }
                        } else if (full_path.find("/builtin/") != std::string::npos) {
                            info.plugin_or_source = "builtin";
                        } else if (full_path.find(".agents") != std::string::npos) {
                            info.plugin_or_source = "workspace";
                        } else {
                            info.plugin_or_source = "global";
                        }

                        ParseFrontmatter(info.path, info);
                        
                        // Deduplicate by name
                        auto it = std::find_if(cached_skills_.begin(), cached_skills_.end(), [&](const SkillInfo& s) {
                            return s.name == info.name;
                        });
                        if (it == cached_skills_.end()) {
                            cached_skills_.push_back(info);
                        }
                    }
                }
            }
        } catch (...) {}
    }

    std::sort(cached_skills_.begin(), cached_skills_.end(), [](const SkillInfo& a, const SkillInfo& b) {
        return a.name < b.name;
    });

    scanned_ = true;
    return cached_skills_;
}

std::vector<std::string> SkillManager::GetSkillNames() {
    DiscoverSkills();
    std::vector<std::string> names;
    for (const auto& s : cached_skills_) {
        names.push_back(s.name);
    }
    return names;
}

std::vector<SkillInfo> SkillManager::SearchSkills(const std::string& query) {
    DiscoverSkills();
    if (query.empty()) return cached_skills_;

    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);

    std::vector<SkillInfo> results;
    for (const auto& s : cached_skills_) {
        std::string n = s.name;
        std::string d = s.description;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        std::transform(d.begin(), d.end(), d.begin(), ::tolower);

        if (n.find(q) != std::string::npos || d.find(q) != std::string::npos || s.plugin_or_source.find(q) != std::string::npos) {
            results.push_back(s);
        }
    }
    return results;
}

std::string SkillManager::FormatSkillsList(const std::string& filter) {
    auto skills = SearchSkills(filter);
    if (skills.empty()) {
        return "No skills found matching '" + filter + "'.";
    }

    std::ostringstream ss;
    ss << "Available Skills (" << skills.size() << " total):\n\n";
    for (size_t i = 0; i < skills.size(); ++i) {
        const auto& s = skills[i];
        ss << " " << std::setw(2) << (i + 1) << ". **" << s.name << "** `[" << s.plugin_or_source << "]`\n";
        if (!s.description.empty()) {
            std::string desc = s.description;
            if (desc.size() > 110) desc = desc.substr(0, 107) + "...";
            ss << "     " << desc << "\n";
        }
    }
    ss << "\nUse `/skill <name>` to view detailed skill instructions.";
    return ss.str();
}

std::string SkillManager::GetSkillContent(const std::string& skill_name) {
    DiscoverSkills();
    for (const auto& s : cached_skills_) {
        if (s.name == skill_name) {
            std::ifstream file(s.path);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                return buffer.str();
            }
        }
    }
    return "Error: Skill '" + skill_name + "' not found.";
}

const SkillInfo* SkillManager::GetSkill(const std::string& skill_name) {
    DiscoverSkills();
    for (const auto& s : cached_skills_) {
        if (s.name == skill_name) {
            return &s;
        }
    }
    return nullptr;
}

std::string SkillManager::FormatSkillsForSystemPrompt() {
    auto skills = DiscoverSkills();
    if (skills.empty()) return "";

    std::ostringstream ss;
    ss << "AVAILABLE SPECIALIZED SKILLS DIRECTORY:\n"
       << "- Skills Policy: Only activate and use a skill when the user's task specifically matches that domain or the user explicitly requests/activates it (e.g. via '[S: <skill>]' or 'Skill:<path>').\n"
       << "- Mandatory Skill Reading: When you activate a skill, you MUST read its full instructions using 'read_file' on its Path before executing actions.\n\n";

    for (const auto& s : skills) {
        ss << "• Skill: `" << s.name << "` [" << s.plugin_or_source << "]\n";
        ss << "  Path: `" << s.path << "`\n";
        if (!s.description.empty()) {
            ss << "  Description: " << s.description << "\n";
        }
    }
    return ss.str();
}

} // namespace razor
