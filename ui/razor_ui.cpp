#include "razor_ui.hpp"
#include "skill_manager.hpp"
#include "task_manager.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/color.hpp>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <iostream>

#include <unordered_set>
#include <algorithm>
#include <cctype>

namespace razor {

using namespace ftxui;

static Color HSLToRGB(float h, float s, float l) {
    h = std::fmod(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = (1.0f - std::abs(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = l - c / 2.0f;
    float r = 0, g = 0, b = 0;
    if (h < 60)       { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else              { r = c; g = 0; b = x; }
    uint8_t R = static_cast<uint8_t>((r + m) * 255.0f);
    uint8_t G = static_cast<uint8_t>((g + m) * 255.0f);
    uint8_t B = static_cast<uint8_t>((b + m) * 255.0f);
    return Color::RGB(R, G, B);
}

// Helper to extract UTF-8 characters from a string
static std::vector<std::string> SplitUTF8(const std::string& str) {
    std::vector<std::string> chars;
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = str[i];
        size_t len = 1;
        if ((c & 0x80) == 0)        len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        
        if (i + len <= str.size()) {
            chars.push_back(str.substr(i, len));
        } else {
            chars.push_back(str.substr(i));
            break;
        }
        i += len;
    }
    return chars;
}

static const std::vector<std::string> BRAILLE_SPINNER = {
    "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"
};


static Element ParseInline(const std::string& line) {
    Elements hflow_elements;
    Elements current_word_elements;
    std::string current_chunk = "";
    
    bool is_bold = false;
    bool is_italic = false;
    bool is_code = false;
    bool is_strike = false;
    bool is_highlight = false;
    
    bool has_custom_fg = false;
    Color custom_fg = Color::Default;
    bool has_custom_bg = false;
    Color custom_bg = Color::Default;
    
    auto flush_chunk = [&]() {
        if (!current_chunk.empty()) {
            auto el = text(current_chunk);
            if (is_bold) el = el | bold;
            if (is_italic) el = el | dim; // FTXUI dim for italic
            if (is_strike) el = el | strikethrough;
            
            if (is_highlight) {
                el = el | color(Color::Black) | bgcolor(Color::Yellow);
            } else if (is_code) {
                el = el | color(Color::RGB(255, 165, 0)); // Orange for inline code
            } else {
                if (has_custom_fg) {
                    el = el | color(custom_fg);
                } else if (is_bold) {
                    el = el | color(Color::White);
                } else {
                    el = el | color(Color::GrayLight);
                }
                if (has_custom_bg) {
                    el = el | bgcolor(custom_bg);
                }
            }
            
            current_word_elements.push_back(el);
            current_chunk = "";
        }
    };

    for (size_t i = 0; i < line.size(); ++i) {
        if (i + 1 < line.size() && line[i] == '\x1b' && line[i+1] == '[') {
            flush_chunk();
            size_t m_pos = line.find('m', i + 2);
            if (m_pos != std::string::npos) {
                std::string code_str = line.substr(i + 2, m_pos - (i + 2));
                std::vector<int> codes;
                size_t s = 0;
                while (s < code_str.size()) {
                    size_t e = code_str.find(';', s);
                    if (e == std::string::npos) e = code_str.size();
                    try { codes.push_back(std::stoi(code_str.substr(s, e - s))); } catch(...) {}
                    s = e + 1;
                }
                
                if (codes.empty()) {
                    has_custom_fg = false; has_custom_bg = false; is_bold = false; is_italic = false; is_strike = false;
                }
                
                for (size_t c = 0; c < codes.size(); ++c) {
                    int code = codes[c];
                    if (code == 0) {
                        has_custom_fg = false; has_custom_bg = false; is_bold = false; is_italic = false; is_strike = false;
                    } else if (code == 1) {
                        is_bold = true;
                    } else if (code == 9) {
                        is_strike = true;
                    } else if (code >= 30 && code <= 37) {
                        has_custom_fg = true;
                        Color::Palette16 p[] = {Color::Black, Color::Red, Color::Green, Color::Yellow, Color::Blue, Color::Magenta, Color::Cyan, Color::White};
                        custom_fg = Color(p[code - 30]);
                    } else if (code == 39) {
                        has_custom_fg = false;
                    } else if (code >= 40 && code <= 47) {
                        has_custom_bg = true;
                        Color::Palette16 p[] = {Color::Black, Color::Red, Color::Green, Color::Yellow, Color::Blue, Color::Magenta, Color::Cyan, Color::White};
                        custom_bg = Color(p[code - 40]);
                    } else if (code == 49) {
                        has_custom_bg = false;
                    } else if (code == 38 && c + 2 < codes.size() && codes[c+1] == 5) {
                        has_custom_fg = true;
                        custom_fg = Color(static_cast<Color::Palette256>(codes[c+2]));
                        c += 2;
                    } else if (code == 48 && c + 2 < codes.size() && codes[c+1] == 5) {
                        has_custom_bg = true;
                        custom_bg = Color(static_cast<Color::Palette256>(codes[c+2]));
                        c += 2;
                    } else if (code == 38 && c + 4 < codes.size() && codes[c+1] == 2) {
                        has_custom_fg = true;
                        custom_fg = Color::RGB(codes[c+2], codes[c+3], codes[c+4]);
                        c += 4;
                    } else if (code == 48 && c + 4 < codes.size() && codes[c+1] == 2) {
                        has_custom_bg = true;
                        custom_bg = Color::RGB(codes[c+2], codes[c+3], codes[c+4]);
                        c += 4;
                    }
                }
                i = m_pos;
                continue;
            }
        }
        
        if (line[i] == ' ') {
            flush_chunk();
            if (!current_word_elements.empty()) {
                hflow_elements.push_back(hbox(std::move(current_word_elements)));
                current_word_elements.clear();
            }
            hflow_elements.push_back(text(" "));
        } else if (i + 2 < line.size() && line[i] == '*' && line[i+1] == '*' && line[i+2] == '*') {
            flush_chunk();
            is_bold = !is_bold;
            is_italic = !is_italic;
            i += 2;
        } else if (i + 1 < line.size() && line[i] == '*' && line[i+1] == '*') {
            flush_chunk();
            is_bold = !is_bold;
            i++;
        } else if (line[i] == '*') {
            flush_chunk();
            is_italic = !is_italic;
        } else if (i + 1 < line.size() && line[i] == '~' && line[i+1] == '~') {
            flush_chunk();
            is_strike = !is_strike;
            i++;
        } else if (i + 1 < line.size() && line[i] == '=' && line[i+1] == '=') {
            flush_chunk();
            is_highlight = !is_highlight;
            i++;
        } else if (line[i] == '`') {
            flush_chunk();
            is_code = !is_code;
        } else {
            current_chunk += line[i];
        }
    }
    flush_chunk();
    if (!current_word_elements.empty()) {
        hflow_elements.push_back(hbox(std::move(current_word_elements)));
    }
    
    if (hflow_elements.empty()) return text("");
    return hflow(std::move(hflow_elements));
}


static Element SyntaxHighlight(const std::string& line, std::string lang) {
    std::transform(lang.begin(), lang.end(), lang.begin(), ::tolower);
    // Trim
    lang.erase(0, lang.find_first_not_of(" \t\r\n"));
    lang.erase(lang.find_last_not_of(" \t\r\n") + 1);

    if (lang.empty()) {
        return text(line) | color(Color::CyanLight);
    }

    std::unordered_set<std::string> keywords = {
        "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern",
        "float", "for", "goto", "if", "int", "long", "register", "return", "short", "signed", "sizeof", "static",
        "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while",
        "class", "namespace", "template", "typename", "public", "private", "protected", "virtual", "override", "new", "delete",
        "def", "import", "from", "as", "pass", "yield", "True", "False", "None", "elif", "except", "finally", "try", "with",
        "lambda", "nonlocal", "global", "assert", "del", "async", "await", "in", "is", "and", "or", "not",
        "func", "var", "let", "const", "type", "interface", "package", "chan", "defer", "go", "select", "fallthrough",
        "string", "bool", "uint", "uint8", "uint16", "uint32", "uint64", "int8", "int16", "int32", "int64", "float32", "float64",
        "fn", "match", "mut", "impl", "trait", "pub", "use", "mod", "loop", "where", "ref", "move",
        "function", "export", "typeof", "instanceof", "extends", "implements", "null", "undefined",
        "boolean", "number", "any", "require", "module",
        "echo", "set", "export", "alias", "source", "read", "local", "shift", "then", "fi", "esac", "done"
    };

    std::unordered_set<std::string> builtins = {
        "print", "len", "range", "open", "str", "int", "float", "list", "dict", "set", "tuple",
        "console", "Math", "String", "Number", "Array", "Object", "Promise", "Error", "Map", "Set",
        "fmt", "os", "io", "math", "strings", "make", "append", "panic", "recover",
        "std", "cout", "cin", "cerr", "endl", "vector", "string", "map", "set", "list", "shared_ptr", "unique_ptr"
    };

    Elements row;
    std::string current_token = "";
    int state = 0; // 0: normal, 1: word, 2: string ", 3: string ', 4: comment

    auto flush_token = [&]() {
        if (current_token.empty()) return;
        if (state == 1) {
            if (keywords.count(current_token)) {
                row.push_back(text(current_token) | color(Color::RGB(198, 120, 221)) | bold); // Purple for keywords
            } else if (builtins.count(current_token)) {
                row.push_back(text(current_token) | color(Color::RGB(97, 175, 239))); // Blue for builtins
            } else if (!current_token.empty() && isdigit(current_token[0])) {
                row.push_back(text(current_token) | color(Color::RGB(209, 154, 102))); // Orange for numbers
            } else {
                row.push_back(text(current_token) | color(Color::RGB(224, 108, 117))); // Reddish-White for identifiers
            }
        } else if (state == 2 || state == 3) {
            row.push_back(text(current_token) | color(Color::RGB(152, 195, 121))); // Green for strings
        } else if (state == 4) {
            row.push_back(text(current_token) | color(Color::RGB(92, 99, 112)) | dim); // Grey for comments
        } else {
            row.push_back(text(current_token) | color(Color::RGB(171, 178, 191))); // Light grey for punctuation
        }
        current_token = "";
    };

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        
        if (state == 4) {
            current_token += c;
            continue;
        }

        if (state == 2) {
            current_token += c;
            if (c == '"' && (i == 0 || line[i-1] != '\\')) {
                flush_token();
                state = 0;
            }
            continue;
        }
        
        if (state == 3) {
            current_token += c;
            if (c == '\'' && (i == 0 || line[i-1] != '\\')) {
                flush_token();
                state = 0;
            }
            continue;
        }

        // Check for comments
        bool is_comment_start = false;
        if ((lang == "python" || lang == "sh" || lang == "ruby" || lang == "cmake" || lang == "bash") && c == '#') {
            is_comment_start = true;
        } else if ((lang == "c" || lang == "cpp" || lang == "c++" || lang == "c#" || lang == "go" || lang == "rust" || lang == "java" || lang == "javascript" || lang == "typescript" || lang == "kotlin") && c == '/' && i+1 < line.size() && line[i+1] == '/') {
            is_comment_start = true;
        } else if (lang == "lua" && c == '-' && i+1 < line.size() && line[i+1] == '-') {
            is_comment_start = true;
        }

        if (is_comment_start) {
            flush_token();
            state = 4;
            current_token += c;
            continue;
        }

        if (c == '"') {
            flush_token();
            state = 2;
            current_token += c;
        } else if (c == '\'') {
            flush_token();
            state = 3;
            current_token += c;
        } else if (isalnum(c) || c == '_') {
            if (state != 1) {
                flush_token();
                state = 1;
            }
            current_token += c;
        } else {
            flush_token();
            state = 0;
            current_token += c;
            flush_token();
        }
    }
    flush_token();

    if (row.empty()) {
        return text("");
    }
    return hbox(std::move(row));
}

static Elements RenderMarkdown(const std::string& raw_text) {
    auto truncate = [](const std::string& s, size_t max_len = 50) {
        if (s.length() > max_len) return s.substr(0, max_len - 3) + "...";
        return s;
    };

    std::vector<std::string> lines;
    size_t start = 0;
    while (start < raw_text.size()) {
        size_t end = raw_text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(raw_text.substr(start));
            break;
        }
        lines.push_back(raw_text.substr(start, end - start));
        start = end + 1;
    }

    Elements vbox_lines;
    std::vector<std::vector<Element>> current_table;

    auto flush_table = [&]() {
        if (!current_table.empty()) {
            Table table(std::move(current_table));
            table.SelectAll().Border(LIGHT);
            table.SelectAll().Separator(LIGHT);
            table.SelectRow(0).DecorateCells(color(Color::White) | bold);
            table.SelectRow(0).BorderBottom(DOUBLE);
            vbox_lines.push_back(table.Render());
            current_table.clear();
        }
    };

    int pending_empty_lines = 0;
    bool in_code_block = false;
    std::string code_block_content = "";
    std::string code_block_language = "";
    for (const auto& line : lines) {
        if (line.substr(0, 3) == "```") {
            flush_table();
            if (in_code_block) {
                Elements code_lines;
                std::string current_line = "";
                for (char c : code_block_content) {
                    if (c == '\n') {
                        code_lines.push_back(SyntaxHighlight(current_line, code_block_language));
                        current_line = "";
                    } else {
                        current_line += c;
                    }
                }
                if (!current_line.empty()) {
                    code_lines.push_back(SyntaxHighlight(current_line, code_block_language));
                }
                if (code_lines.empty()) code_lines.push_back(text(""));
                
                vbox_lines.push_back(
                    vbox(std::move(code_lines)) | border
                );
                code_block_content = "";
                code_block_language = "";
            } else {
                code_block_language = line.substr(3);
            }
            in_code_block = !in_code_block;
            continue;
        }

        if (in_code_block) {
            code_block_content += line + "\n";
            continue;
        }

        if (line.empty()) {
            flush_table();
            pending_empty_lines++;
            continue;
        }
        
        // Tool lines
        auto safe_extract_arg = [](const std::string& l, size_t end_bracket) {
            if (end_bracket + 1 < l.size()) {
                size_t start_idx = (l[end_bracket+1] == ' ') ? end_bracket + 2 : end_bracket + 1;
                if (start_idx < l.size()) {
                    return l.substr(start_idx);
                }
            }
            return std::string("");
        };

        if (line.rfind("[TOOL_EXECUTION:run_command|STATUS:", 0) == 0) {
            flush_table();
            size_t bar_pos = line.find('|');
            size_t end_bracket = line.find(']');
            if (bar_pos != std::string::npos && end_bracket != std::string::npos && end_bracket > bar_pos + 8) {
                std::string status_str = line.substr(bar_pos + 8, end_bracket - (bar_pos + 8));
                std::string cmd = safe_extract_arg(line, end_bracket);
                int status = 0;
                try { status = std::stoi(status_str); } catch (...) {}
                Color circle_color = (status == 0) ? Color::Green : Color::Red;
                auto el = hbox({
                    text("● ") | color(circle_color),
                    text("RunCommand") | bold | color(Color::GrayLight),
                    text("(") | color(Color::GrayDark),
                    text(truncate(cmd)) | color(Color::White),
                    text(")") | color(Color::GrayDark),
                });
                vbox_lines.push_back(el);
                pending_empty_lines = 0;
                continue;
            }
        }
        if (line.rfind("[TOOL_EXECUTION:run_command|BACKGROUND:", 0) == 0) {
            flush_table();
            size_t bar_pos = line.find('|');
            size_t end_bracket = line.find(']');
            if (bar_pos != std::string::npos && end_bracket != std::string::npos) {
                std::string task_id = line.substr(bar_pos + 12, end_bracket - (bar_pos + 12));
                std::string cmd = safe_extract_arg(line, end_bracket);
                auto el = hbox({
                    text("● ") | color(Color::Yellow),
                    text("RunCommand") | bold | color(Color::GrayLight),
                    text("[BKG: " + task_id + "](") | color(Color::YellowLight),
                    text(truncate(cmd)) | color(Color::White),
                    text(")") | color(Color::GrayDark),
                });
                vbox_lines.push_back(el);
                pending_empty_lines = 0;
                continue;
            }
        }
        if (line.rfind("[TOOL_EXECUTION:manage_task", 0) == 0) {
            flush_table();
            size_t end_bracket = line.find(']');
            std::string tag_content = line.substr(16, end_bracket - 16); // e.g. manage_task_view|NAME:foo|ID:task-1
            
            std::string name = "";
            std::string id = "";
            size_t name_pos = tag_content.find("|NAME:");
            size_t id_pos = tag_content.find("|ID:");
            if (name_pos != std::string::npos) {
                size_t next_bar = tag_content.find('|', name_pos + 6);
                if (next_bar != std::string::npos) {
                    name = tag_content.substr(name_pos + 6, next_bar - (name_pos + 6));
                } else {
                    name = tag_content.substr(name_pos + 6);
                }
            }
            if (id_pos != std::string::npos) {
                id = tag_content.substr(id_pos + 4);
            }

            if (line.rfind("[TOOL_EXECUTION:manage_task_view", 0) == 0) {
                auto el = hbox({
                    text("● ") | color(Color::Green),
                    text("Checked ") | bold | color(Color::GrayLight),
                    text(name.empty() ? id : name) | bold | color(Color::White),
                    text(" "),
                    text(id) | color(Color::CyanLight),
                });
                vbox_lines.push_back(el);
            } else if (line.rfind("[TOOL_EXECUTION:manage_task_kill", 0) == 0) {
                auto el = hbox({
                    text("● ") | color(Color::Red),
                    text("Killed ") | bold | color(Color::GrayLight),
                    text(name.empty() ? id : name) | bold | color(Color::White),
                    text(" "),
                    text(id) | color(Color::RedLight),
                });
                vbox_lines.push_back(el);
            } else if (line.rfind("[TOOL_EXECUTION:manage_task_send_keycode", 0) == 0) {
                auto el = hbox({
                    text("● ") | color(Color::CyanLight),
                    text("ManageTask ") | bold | color(Color::GrayLight),
                    text(name.empty() ? id : name) | bold | color(Color::White),
                    text(" "),
                    text(id) | color(Color::CyanLight),
                });
                vbox_lines.push_back(el);
            } else {
                auto el = hbox({
                    text("● ") | color(Color::CyanLight),
                    text("ManageTask") | bold | color(Color::GrayLight),
                    text(" ("),
                    text(name.empty() ? id : (name + " " + id)) | color(Color::White),
                    text(")"),
                });
                vbox_lines.push_back(el);
            }
            pending_empty_lines = 0;
            continue;
        }
        if (line.rfind("[TOOL_EXECUTION:list_dir|STATUS:", 0) == 0) {
            flush_table();
            size_t end_bracket = line.find(']');
            if (end_bracket != std::string::npos) {
                std::string path = safe_extract_arg(line, end_bracket);
                auto el = hbox({
                    text("● ") | color(Color::Green),
                    text("ListDir") | bold | color(Color::GrayLight),
                    text("(") | color(Color::GrayDark),
                    text(truncate(path)) | color(Color::White),
                    text(")") | color(Color::GrayDark),
                });
                vbox_lines.push_back(el);
                pending_empty_lines = 0;
                continue;
            }
        }
        if (line.rfind("[TOOL_EXECUTION:read_file|STATUS:", 0) == 0) {
            flush_table();
            size_t end_bracket = line.find(']');
            if (end_bracket != std::string::npos) {
                std::string path = safe_extract_arg(line, end_bracket);
                auto el = hbox({
                    text("● ") | color(Color::Green),
                    text("ReadFile") | bold | color(Color::GrayLight),
                    text("(") | color(Color::GrayDark),
                    text(truncate(path)) | color(Color::White),
                    text(")") | color(Color::GrayDark),
                });
                vbox_lines.push_back(el);
                pending_empty_lines = 0;
                continue;
            }
        }
        if (line.rfind("[TOOL_EXECUTION:write_file|STATUS:", 0) == 0) {
            flush_table();
            size_t end_bracket = line.find(']');
            if (end_bracket != std::string::npos) {
                std::string path = safe_extract_arg(line, end_bracket);
                auto el = hbox({
                    text("● ") | color(Color::Green),
                    text("WriteFile") | bold | color(Color::GrayLight),
                    text("(") | color(Color::GrayDark),
                    text(truncate(path)) | color(Color::White),
                    text(")") | color(Color::GrayDark),
                });
                vbox_lines.push_back(el);
                pending_empty_lines = 0;
                continue;
            }
        }
        if (line.rfind("[TOOL_EXECUTION:replace_file_content|STATUS:", 0) == 0) {
            flush_table();
            size_t end_bracket = line.find(']');
            if (end_bracket != std::string::npos) {
                std::string path = line.substr(end_bracket + 2);
                auto el = hbox({
                    text("● ") | color(Color::Green),
                    text("ReplaceFile") | bold | color(Color::GrayLight),
                    text("(") | color(Color::GrayDark),
                    text(truncate(path)) | color(Color::White),
                    text(")") | color(Color::GrayDark),
                });
                vbox_lines.push_back(el);
                pending_empty_lines = 0;
                continue;
            }
        }
        if (line.rfind("[TOOL_EXECUTION_ERROR]", 0) == 0) {
            flush_table();
            std::string msg_text = line.substr(22);
            auto el = hbox({
                text("● ") | color(Color::Red),
                text("ToolError") | bold | color(Color::GrayLight),
                text("(") | color(Color::GrayDark),
                text(truncate(msg_text)) | color(Color::Red),
                text(")") | color(Color::GrayDark),
            });
            vbox_lines.push_back(el);
            pending_empty_lines = 0;
            continue;
        }
        
        while (pending_empty_lines > 0) {
            vbox_lines.push_back(text(""));
            pending_empty_lines--;
        }

        size_t pipe_pos = line.find('|');
        if (pipe_pos != std::string::npos && (line.front() == '|' || line.find(" | ") != std::string::npos)) {
            bool is_sep = true;
            for (char c : line) {
                if (c != '|' && c != '-' && c != ':' && c != ' ') {
                    is_sep = false;
                    break;
                }
            }
            if (is_sep && line.find('-') != std::string::npos) continue; 
            
            std::vector<Element> row_elements;
            size_t s = (line.front() == '|') ? 1 : 0;
            while (s < line.size()) {
                size_t e = line.find('|', s);
                std::string cell_text;
                if (e == std::string::npos) {
                    cell_text = line.substr(s);
                    s = line.size();
                } else {
                    cell_text = line.substr(s, e - s);
                    s = e + 1;
                }
                
                size_t first = cell_text.find_first_not_of(' ');
                if (first != std::string::npos) {
                    size_t last = cell_text.find_last_not_of(' ');
                    cell_text = cell_text.substr(first, last - first + 1);
                } else {
                    cell_text = "";
                }
                
                if (e != std::string::npos || !cell_text.empty() || row_elements.empty()) {
                    row_elements.push_back(ParseInline(cell_text));
                }
            }
            if (!row_elements.empty()) current_table.push_back(std::move(row_elements));
            continue;
        }

        flush_table();

        std::string content = line;

        // Blockquotes
        int quote_level = 0;
        while (content.size() >= 2 && content.substr(0, 2) == "> ") {
            quote_level++;
            content = content.substr(2);
        }

        // GitHub Alerts
        if (quote_level > 0 || content.front() == '[') {
            std::string alert_type = "";
            if (content.rfind("[!NOTE]", 0) == 0) alert_type = "NOTE";
            else if (content.rfind("[!TIP]", 0) == 0) alert_type = "TIP";
            else if (content.rfind("[!IMPORTANT]", 0) == 0) alert_type = "IMPORTANT";
            else if (content.rfind("[!WARNING]", 0) == 0) alert_type = "WARNING";
            else if (content.rfind("[!CAUTION]", 0) == 0) alert_type = "CAUTION";
            
            if (!alert_type.empty()) {
                content = content.substr(alert_type.length() + 3); // Strip [!TYPE]
                Color alert_col = Color::GrayLight;
                if (alert_type == "NOTE") alert_col = Color::RGB(88, 166, 255);
                else if (alert_type == "TIP") alert_col = Color::RGB(63, 185, 80);
                else if (alert_type == "IMPORTANT") alert_col = Color::RGB(163, 113, 247);
                else if (alert_type == "WARNING") alert_col = Color::RGB(210, 153, 34);
                else if (alert_type == "CAUTION") alert_col = Color::RGB(248, 81, 73);

                auto alert_icon = text(" [" + alert_type + "] ") | bold | color(alert_col);
                auto el = vbox({
                    alert_icon,
                    ParseInline(content)
                }) | borderLight | color(alert_col);
                vbox_lines.push_back(el);
                continue;
            }
        }

        // Headers
        if (content.front() == '#') {
            size_t h_level = 0;
            while (h_level < content.size() && content[h_level] == '#') {
                h_level++;
            }
            if (h_level < content.size() && content[h_level] == ' ') {
                std::string title = content.substr(h_level + 1);
                auto el = ParseInline(title) | bold;
                if (h_level == 1) el = el | color(Color::CyanLight);
                else if (h_level == 2) el = el | color(Color::Cyan);
                else if (h_level == 3) el = el | color(Color::RGB(100, 200, 255));
                else el = el | color(Color::White);
                
                vbox_lines.push_back(el);
                continue;
            }
        }

        // Lists
        bool is_list = false;
        std::string list_prefix = "";
        if (content.rfind("- ", 0) == 0 || content.rfind("* ", 0) == 0) {
            is_list = true;
            list_prefix = " • ";
            content = content.substr(2);
        } else if (content.size() > 2 && isdigit(content[0]) && content[1] == '.' && content[2] == ' ') {
            is_list = true;
            list_prefix = content.substr(0, 3);
            content = content.substr(3);
        }
        
        // Task list
        if (content.rfind("[ ] ", 0) == 0) {
            list_prefix += "☐ ";
            content = content.substr(4);
        } else if (content.rfind("[x] ", 0) == 0 || content.rfind("[X] ", 0) == 0) {
            list_prefix += "☑ ";
            content = content.substr(4);
        } else if (content.rfind("[/] ", 0) == 0) {
            list_prefix += "☒ ";
            content = content.substr(4);
        }

        Element parsed_line = ParseInline(content);
        if (is_list || !list_prefix.empty()) {
            parsed_line = hbox(text(list_prefix) | color(Color::GrayLight), parsed_line);
        }
        
        if (quote_level > 0) {
            Elements quotes;
            for (int q = 0; q < quote_level; ++q) {
                quotes.push_back(text("┃ ") | color(Color::GrayDark));
            }
            quotes.push_back(parsed_line);
            vbox_lines.push_back(hbox(std::move(quotes)));
        } else {
            vbox_lines.push_back(parsed_line);
        }
    }
    
    if (in_code_block) {
        Elements code_lines;
        std::string current_line = "";
        for (char c : code_block_content) {
            if (c == '\n') {
                code_lines.push_back(SyntaxHighlight(current_line, code_block_language));
                current_line = "";
            } else {
                current_line += c;
            }
        }
        if (!current_line.empty()) {
            code_lines.push_back(SyntaxHighlight(current_line, code_block_language));
        }
        if (code_lines.empty()) code_lines.push_back(text(""));
        
        vbox_lines.push_back(
            vbox(std::move(code_lines)) | border
        );
    }
    
    flush_table();
    return vbox_lines;
}

RazorUI::RazorUI() : prompt_value_(""), user_name_(""), is_running_(false), spinner_frame_(0), auto_scroll_(true), scroll_index_(0), selected_command_index_(0) {
    last_keypress_ = std::chrono::steady_clock::now();
    last_scroll_ = std::chrono::steady_clock::now() - std::chrono::hours(1);
}

RazorUI::~RazorUI() {
    is_running_ = false;
    if (animation_thread_.joinable()) {
        animation_thread_.join();
    }
}

void RazorUI::SetSubmitCallback(PromptCallback callback) {
    submit_callback_ = callback;
}

void RazorUI::SetUserName(const std::string& user_name) {
    user_name_ = user_name;
}

bool RazorUI::IsModelThinking() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(history_mutex_));
    return !history_.empty() && history_.back().is_loading;
}

void RazorUI::ProvideResponse(const std::string& response) {
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        for (auto& msg : history_) {
            if (msg.is_loading) {
                msg.is_loading = false;
                msg.response = response;
                break;
            }
        }
    }

    // Auto-dispatch queued steer if model has finished current response
    DispatchQueuedSteer();
}

void RazorUI::DispatchQueuedSteer() {
    std::string to_dispatch = "";
    {
        std::lock_guard<std::mutex> lock(steer_mutex_);
        if (!queued_steer_prompt_.empty()) {
            to_dispatch = queued_steer_prompt_;
            queued_steer_prompt_.clear();
        }
    }

    if (!to_dispatch.empty() && submit_callback_) {
        {
            std::lock_guard<std::mutex> lock(history_mutex_);
            ChatMessage msg;
            msg.prompt = to_dispatch;
            msg.is_loading = true;
            msg.streamed_length = 0;
            msg.start_time = std::chrono::steady_clock::now();
            history_.push_back(msg);
        }
        submit_callback_(to_dispatch);
    }
}

void RazorUI::StartThinking(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    if (!history_.empty() && history_.back().is_loading) {
        history_.back().model_name = model_name;
        history_.back().start_time = std::chrono::steady_clock::now();
        return;
    }
    ChatMessage msg;
    msg.prompt = ""; // empty prompt for autonomous loops
    msg.is_loading = true;
    msg.streamed_length = 0;
    msg.start_time = std::chrono::steady_clock::now();
    msg.model_name = model_name;
    history_.push_back(msg);
}

void RazorUI::UpdateModelName(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    for (auto& msg : history_) {
        if (msg.is_loading) {
            msg.model_name = model_name;
            msg.start_time = std::chrono::steady_clock::now(); // Reset timer on fallback
            return;
        }
    }
}

void RazorUI::Run() {
    auto get_autocomplete_state = [&]() -> std::vector<std::string> {
        std::vector<std::string> current_matches;
        if (prompt_value_.empty() || prompt_value_[0] != '/') return current_matches;
        
        if (prompt_value_ == "/model" || prompt_value_ == "/models" || prompt_value_ == "/model " || prompt_value_ == "/models ") {
            return available_models_;
        } else if (prompt_value_ == "/roles" || prompt_value_ == "/role" || prompt_value_ == "/role " || prompt_value_ == "/roles ") {
            return available_roles_;
        } else if (prompt_value_ == "/skill" || prompt_value_ == "/skills" || prompt_value_ == "/skill " || prompt_value_ == "/skills ") {
            return SkillManager::Instance().GetSkillNames();
        } else if (prompt_value_.rfind("/model ", 0) == 0) {
            std::string term = prompt_value_.substr(7);
            std::string term_lower = term;
            std::transform(term_lower.begin(), term_lower.end(), term_lower.begin(), ::tolower);
            for (const auto& m : available_models_) {
                std::string m_lower = m;
                std::transform(m_lower.begin(), m_lower.end(), m_lower.begin(), ::tolower);
                if (term.empty() || m_lower.find(term_lower) != std::string::npos) {
                    current_matches.push_back(m);
                }
            }
        } else if (prompt_value_.rfind("/role ", 0) == 0) {
            std::string term = prompt_value_.substr(6);
            std::string term_lower = term;
            std::transform(term_lower.begin(), term_lower.end(), term_lower.begin(), ::tolower);
            for (const auto& r : available_roles_) {
                std::string r_lower = r;
                std::transform(r_lower.begin(), r_lower.end(), r_lower.begin(), ::tolower);
                if (term.empty() || r_lower.find(term_lower) != std::string::npos) {
                    current_matches.push_back(r);
                }
            }
        } else if (prompt_value_.rfind("/skill ", 0) == 0) {
            std::string term = prompt_value_.substr(7);
            auto skills = SkillManager::Instance().SearchSkills(term);
            for (const auto& s : skills) {
                current_matches.push_back(s.name);
            }
        } else if (prompt_value_.find(' ') == std::string::npos) {
            std::string search_term = prompt_value_.substr(1);
            std::vector<std::string> all_commands = {"models", "model", "roles", "role", "skills", "skill", "tasks", "session", "clear", "help"};
            for (const auto& cmd : all_commands) {
                if (search_term.empty() || cmd.find(search_term) == 0) {
                    current_matches.push_back(cmd);
                }
            }
        }
        return current_matches;
    };

    auto screen = ScreenInteractive::Fullscreen();
    is_running_ = true;
    last_keypress_ = std::chrono::steady_clock::now();

    // Animation thread to force redraw
    animation_thread_ = std::thread([&]() {
        while (is_running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            spinner_frame_++;
            
            // Advance streaming effect
            {
                std::lock_guard<std::mutex> lock(history_mutex_);
                for (auto& msg : history_) {
                    if (!msg.is_loading && msg.streamed_length < msg.response.size()) {
                        // Stream characters over time
                        msg.streamed_length = std::min(msg.response.size(), msg.streamed_length + 5);
                    }
                }
            }
            screen.PostEvent(Event::Custom);
        }
    });

    InputOption input_option;
    input_option.multiline = true;
    input_option.on_enter = [&]() {}; // Handled by CatchEvent below
    
    input_option.transform = [](InputState state) {
        if (state.is_placeholder) {
            state.element = state.element | color(Color::GrayDark);
        }
        return state.element;
    };

    auto base_input_component = Input(&prompt_value_, "Type your prompt... (Paste preserves newlines, Alt+Enter for manual newline)", input_option);
    
    auto last_event_time_ = std::chrono::steady_clock::now();
    auto last_char_time_ = std::chrono::steady_clock::now();
    
    auto input_component = CatchEvent(base_input_component, [&](Event event) {
        auto now = std::chrono::steady_clock::now();
        
        if (event != Event::Custom) {
            last_keypress_ = now;
        }

        bool is_paste = std::chrono::duration_cast<std::chrono::microseconds>(now - last_event_time_).count() < 2000; // 2ms
        last_event_time_ = now;

        // === PASTE BUFFERING ===
        // Track char-to-char timing (unaffected by Custom animation events)
        bool char_is_rapid = false;
        if (event.is_character() || event == Event::Return) {
            char_is_rapid = std::chrono::duration_cast<std::chrono::microseconds>(now - last_char_time_).count() < 5000; // 5ms
            last_char_time_ = now;
            if (char_is_rapid) {
                rapid_char_count_++;
            } else {
                rapid_char_count_ = 0;
            }
        }

        // Finalize paste on timeout (Custom events arrive every ~30ms)
        if (event == Event::Custom && is_pasting_.load()) {
            auto since_last_char = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_char_time_).count();
            if (since_last_char > 150) {
                is_pasting_ = false;
                rapid_char_count_ = 0;
                paste_line_count_ = (int)std::count(pasted_buffer_.begin(), pasted_buffer_.end(), '\n');
                if (paste_line_count_ == 0) paste_line_count_ = 1;
            }
        }

        // Buffer chars during active paste or when threshold hit
        if ((event.is_character() || event == Event::Return) && (is_pasting_.load() || (char_is_rapid && rapid_char_count_ >= 15))) {
            if (!is_pasting_.load()) {
                // Entering paste mode — grab chars that already leaked into prompt_value_
                is_pasting_ = true;
                pasted_buffer_ = prompt_value_;
                prompt_value_.clear();
            }
            if (event.is_character()) {
                pasted_buffer_ += event.character();
            } else {
                pasted_buffer_ += "\n";
            }
            return true; // consume — don't let FTXUI Input re-render per char
        }
        // === END PASTE BUFFERING ===

        // Ctrl+S: Immediate mid-flight steer
        if (event == Event::Character('\x13') || event == Event::Special("\x13")) {
            std::string to_steer = "";
            {
                std::lock_guard<std::mutex> lock(steer_mutex_);
                if (!queued_steer_prompt_.empty()) {
                    to_steer = queued_steer_prompt_;
                    queued_steer_prompt_.clear();
                }
            }
            if (to_steer.empty() && !prompt_value_.empty()) {
                to_steer = prompt_value_;
                prompt_value_.clear();
            }

            if (!to_steer.empty()) {
                {
                    std::lock_guard<std::mutex> lock(history_mutex_);
                    ChatMessage msg;
                    msg.prompt = "[STEER] " + to_steer;
                    msg.is_loading = true;
                    msg.streamed_length = 0;
                    msg.start_time = std::chrono::steady_clock::now();
                    history_.push_back(msg);
                }
                if (submit_callback_) {
                    submit_callback_("[USER STEER]: " + to_steer);
                }
                last_keypress_ = std::chrono::steady_clock::now();
                return true;
            }
        }

        if (event == Event::Tab) {
            auto current_matches = get_autocomplete_state();
            if (!current_matches.empty() && selected_command_index_.load() < (int)current_matches.size()) {
                std::string match = current_matches[selected_command_index_.load()];
                if (prompt_value_.rfind("/model", 0) == 0) {
                    prompt_value_ = "/model " + match;
                } else if (prompt_value_.rfind("/role", 0) == 0) {
                    prompt_value_ = "/role " + match;
                } else if (prompt_value_.rfind("/skill", 0) == 0) {
                    prompt_value_ = "/skill " + match;
                } else {
                    prompt_value_ = "/" + match + " ";
                }
                return true;
            }
        }

        if (event == Event::Return || event == Event::Character(' ')) {
            if (event == Event::Return && is_paste) {
                return false; // Let base_input_component insert a newline
            }

            if (event == Event::Character(' ')) {
                return false; // Let base_input_component handle normal space
            }

            if (!prompt_value_.empty() || !pasted_buffer_.empty()) {
                // Combine pasted content + typed afterword
                std::string full_prompt;
                if (!pasted_buffer_.empty()) {
                    full_prompt = pasted_buffer_;
                    if (!prompt_value_.empty()) {
                        full_prompt += "\n" + prompt_value_;
                    }
                    pasted_buffer_.clear();
                    paste_line_count_ = 0;
                } else {
                    full_prompt = prompt_value_;
                }
                prompt_value_.clear();

                // Intercept and process local slash commands
                if (full_prompt.rfind("/", 0) == 0) {
                    std::string cmd = full_prompt.substr(1);
                    std::string arg = "";
                    size_t space_pos = cmd.find(' ');
                    if (space_pos != std::string::npos) {
                        arg = cmd.substr(space_pos + 1);
                        cmd = cmd.substr(0, space_pos);
                    }
                    arg.erase(0, arg.find_first_not_of(" \t\r\n"));
                    arg.erase(arg.find_last_not_of(" \t\r\n") + 1);

                    if (cmd == "clear") {
                        std::lock_guard<std::mutex> lock(history_mutex_);
                        history_.clear();
                        last_keypress_ = std::chrono::steady_clock::now();
                        return true;
                    }

                    if (cmd == "models" || cmd == "model") {
                        if (arg.empty()) {
                            model_menu_selected_ = selected_model_idx_.load();
                            show_model_picker_ = true;
                            last_keypress_ = std::chrono::steady_clock::now();
                            return true;
                        }
                    }

                    std::string local_response = "";
                    if (cmd == "help") {
                        local_response = 
                            "### Razor Slash Commands\n\n"
                            "| Command | Description |\n"
                            "| :--- | :--- |\n"
                            "| `/models`, `/model` | Open interactive model switcher (arrow keys + Enter) |\n"
                            "| `/model [name|idx]` | Switch directly to model by name or index |\n"
                            "| `/roles`, `/role [name]` | List available agent roles or inspect role |\n"
                            "| `/skills [filter]` | Discover and list global and workspace skills |\n"
                            "| `/skill <name>` | View detailed instructions for a specific skill |\n"
                            "| `/tasks` | View active and background task processes table |\n"
                            "| `/session` | View current session metadata and statistics |\n"
                            "| `/clear` | Clear terminal chat history |\n"
                            "| `/help` | Show this command cheat-sheet |\n";
                    } else if (cmd == "models" || cmd == "model") {
                        int idx = -1;
                        try {
                            idx = std::stoi(arg) - 1;
                        } catch (...) {}

                        if (idx >= 0 && idx < (int)available_models_.size()) {
                            selected_model_idx_ = idx;
                            local_response = "Switched active model to: **" + available_models_[idx] + "**";
                        } else {
                            auto it = std::find_if(available_models_.begin(), available_models_.end(), [&](const std::string& m) {
                                std::string m_lower = m;
                                std::string arg_lower = arg;
                                std::transform(m_lower.begin(), m_lower.end(), m_lower.begin(), ::tolower);
                                std::transform(arg_lower.begin(), arg_lower.end(), arg_lower.begin(), ::tolower);
                                return m_lower.find(arg_lower) != std::string::npos;
                            });
                            if (it != available_models_.end()) {
                                selected_model_idx_ = std::distance(available_models_.begin(), it);
                                local_response = "Switched active model to: **" + *it + "**";
                            } else {
                                local_response = "Error: Model '" + arg + "' not found in configuration.";
                            }
                        }
                    } else if (cmd == "roles" || cmd == "role") {
                        if (arg.empty()) {
                            std::ostringstream ss;
                            ss << "### Available Agent Roles\n\n";
                            for (size_t i = 0; i < available_roles_.size(); ++i) {
                                ss << " " << (i + 1) << ". **" << available_roles_[i] << "**\n";
                            }
                            ss << "\n*Use `/role <name>` to select or inspect.*";
                            local_response = ss.str();
                        } else {
                            local_response = "Selected role: **" + arg + "** (prompts will prioritize role specialization)";
                        }
                    } else if (cmd == "skills") {
                        local_response = SkillManager::Instance().FormatSkillsList(arg);
                    } else if (cmd == "skill") {
                        if (arg.empty()) {
                            local_response = SkillManager::Instance().FormatSkillsList("");
                        } else {
                            local_response = SkillManager::Instance().GetSkillContent(arg);
                        }
                    } else if (cmd == "tasks") {
                        std::string table = TaskManager::Instance().FormatTaskTable(current_session_id_);
                        if (table.empty()) {
                            local_response = "No active background tasks for current session.";
                        } else {
                            local_response = "```\n" + table + "\n```";
                        }
                    } else if (cmd == "session") {
                        std::ostringstream ss;
                        ss << "### Session Information\n\n"
                           << "- **Session ID**: `" << current_session_id_ << "`\n"
                           << "- **Session Storage**: `~/.razor/sessions/" << current_session_id_ << "/`\n"
                           << "- **Message Count**: " << history_.size() << "\n"
                           << "- **Active Model**: " << (selected_model_idx_.load() < (int)available_models_.size() ? available_models_[selected_model_idx_.load()] : "Default") << "\n";
                        local_response = ss.str();
                    }

                    if (!local_response.empty()) {
                        std::lock_guard<std::mutex> lock(history_mutex_);
                        ChatMessage msg;
                        msg.prompt = full_prompt;
                        msg.is_loading = false;
                        msg.response = local_response;
                        msg.streamed_length = local_response.size();
                        msg.model_name = "Razor System";
                        history_.push_back(msg);
                        last_keypress_ = std::chrono::steady_clock::now();
                        return true;
                    }
                }

                if (IsModelThinking()) {
                    // Model is currently working: queue it above the input bar!
                    {
                        std::lock_guard<std::mutex> lock(steer_mutex_);
                        queued_steer_prompt_ = full_prompt;
                    }
                    last_keypress_ = std::chrono::steady_clock::now();
                    return true;
                } else {
                    {
                        std::lock_guard<std::mutex> lock(history_mutex_);
                        ChatMessage msg;
                        msg.prompt = full_prompt;
                        msg.is_loading = true;
                        msg.streamed_length = 0;
                        msg.start_time = std::chrono::steady_clock::now();
                        history_.push_back(msg);
                    }
                    if (submit_callback_) {
                        submit_callback_(full_prompt);
                    }
                    last_keypress_ = std::chrono::steady_clock::now();
                    return true;
                }
            }
            return true; // Consume event so it doesn't insert a newline
        }
        
        // Manual Alt+Enter or Esc+Enter for newline
        if (event == Event::Special({27, 13}) || event == Event::Special({27, 10})) {
            prompt_value_ += "\n";
            return true;
        }
        
        return false;
    });
    
    MenuOption menu_option;
    menu_option.on_enter = [&]() {
        selected_model_idx_ = model_menu_selected_;
        show_model_picker_ = false;
        std::lock_guard<std::mutex> lock(history_mutex_);
        ChatMessage msg;
        msg.prompt = "";
        msg.response = "[System] Switched active model to " + available_models_[selected_model_idx_.load()];
        history_.push_back(msg);
    };
    
    auto model_picker = Menu(&available_models_, &model_menu_selected_, menu_option);
    model_picker = CatchEvent(model_picker, [&](Event event) {
        if (event == Event::Escape) {
            show_model_picker_ = false;
            return true;
        }
        return false;
    });

    int tab_index = 0;
    auto container = Container::Tab({input_component, model_picker}, &tab_index);

    auto renderer = Renderer(container, [&] {
        tab_index = show_model_picker_.load() ? 1 : 0;
        // 1. Thin horizontal bright blue line across the top of the terminal
        Element top_line = separatorLight() | color(Color::Blue);

        // 2. Cyberpunk Full-Spectrum TrueColor Rainbow ASCII Logo
        std::vector<std::string> logo_lines = {
            "██████╗  █████╗ ███████╗ ██████╗ ██████╗ ",
            "██╔══██╗██╔══██╗╚══███╔╝██╔═══██╗██╔══██╗",
            "██████╔╝███████║  ███╔╝ ██║   ██║██████╔╝",
            "██╔══██╗██╔══██║ ███╔╝  ██║   ██║██╔══██╗",
            "██║  ██║██║  ██║███████╗╚██████╔╝██║  ██║",
            "╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝"
        };

        Elements logo_rows;
        for (size_t row_idx = 0; row_idx < logo_lines.size(); ++row_idx) {
            auto chars = SplitUTF8(logo_lines[row_idx]);
            Elements row_chars;
            for (size_t col_idx = 0; col_idx < chars.size(); ++col_idx) {
                // Diagonal 24-bit TrueColor full spectrum rainbow hue progression across characters, animated over time
                float hue = col_idx * 7.5f + row_idx * 15.0f + spinner_frame_ * 2.5f;
                // Keep hue in 0-360 range for HSLToRGB
                hue = std::fmod(hue, 360.0f);
                if (hue < 0) hue += 360.0f;
                row_chars.push_back(
                    text(chars[col_idx]) | bold | color(HSLToRGB(hue, 1.0f, 0.60f))
                );
            }

            // Append "○ User: {user_name}" on row 2 (row_idx == 1)
            if (row_idx == 1 && !user_name_.empty()) {
                row_chars.push_back(text("     ○ User: ") | bold | color(Color::White));
                row_chars.push_back(text(user_name_) | bold | color(Color::Cyan));
            }

            logo_rows.push_back(hbox(std::move(row_chars)));
        }

        // Single coherent logo block indented slightly to the right
        Element header = hbox(text("    "), vbox(std::move(logo_rows)));

        Elements chat_elements;
        chat_elements.push_back(top_line);
        chat_elements.push_back(header);
        chat_elements.push_back(text("")); // Spacing below logo
        
        {
            std::lock_guard<std::mutex> lock(history_mutex_);
            for (const auto& msg : history_) {
                // User prompt: bright blue "> " prefix, no separators
                if (!msg.prompt.empty()) {
                    chat_elements.push_back(text(""));
                    std::vector<std::string> prompt_lines;
                    std::stringstream ss(msg.prompt);
                    std::string item;
                    while (std::getline(ss, item, '\n')) {
                        prompt_lines.push_back(item);
                    }
                    // Handle case where string ends with \n or is empty
                    if (prompt_lines.empty()) prompt_lines.push_back("");

                    Elements prompt_ui;
                    for (size_t i = 0; i < prompt_lines.size(); ++i) {
                        prompt_ui.push_back(
                            hbox(
                                text(i == 0 ? "> " : "  ") | bold | color(Color::RGB(75, 184, 252)),
                                text(prompt_lines[i]) | bold | color(Color::RGB(75, 184, 252))
                            )
                        );
                    }
                    chat_elements.push_back(vbox(std::move(prompt_ui)));
                    chat_elements.push_back(text(""));
                }

                if (msg.is_loading) {
                    // Braille thinking spinner
                    int frame = (spinner_frame_ / 2) % BRAILLE_SPINNER.size();
                    
                    // Smooth continuous sub-character sweep on "Thinking ..." every ~1 sec
                    std::string t_str = "Thinking ...";
                    Elements thinking_chars;
                    
                    // Use a float position that smoothly increments to slide the glow sub-character
                    float sweep_pos = std::fmod(spinner_frame_ * 0.35f, (float)t_str.size() + 4.0f) - 2.0f;
                    
                    for (size_t i = 0; i < t_str.size(); ++i) {
                        float dist = std::abs((float)i - sweep_pos);
                        
                        // Gaussian-like falloff for glow, radius of 3 characters
                        float intensity = std::max(0.0f, 1.0f - (dist / 2.5f));
                        // Ease in-out interpolation for buttery smoothness
                        intensity = intensity * intensity * (3.0f - 2.0f * intensity);
                        
                        uint8_t r, g, b;
                        if (intensity < 0.6f) {
                            // Dark Gray (60,60,60) blending into Neon Blue (0, 191, 255)
                            float t = intensity / 0.6f;
                            r = 60 * (1.0f - t) + 0 * t;
                            g = 60 * (1.0f - t) + 191 * t;
                            b = 60 * (1.0f - t) + 255 * t;
                        } else {
                            // Neon Blue (0, 191, 255) peaking into Pure White (255, 255, 255)
                            float t = (intensity - 0.6f) / 0.4f;
                            r = 0 * (1.0f - t) + 255 * t;
                            g = 191 * (1.0f - t) + 255 * t;
                            b = 255 * (1.0f - t) + 255 * t;
                        }
                        
                        thinking_chars.push_back(text(std::string(1, t_str[i])) | color(Color::RGB(r, g, b)));
                    }
                    
                    auto now = std::chrono::steady_clock::now();
                    std::chrono::duration<double> elapsed = now - msg.start_time;
                    char buf[32];
                    snprintf(buf, sizeof(buf), "(%.1fs)", elapsed.count());
                    std::string timer_str(buf);
                    
                    std::string m_name = msg.model_name.empty() ? "" : " " + msg.model_name;
                    
                    chat_elements.push_back(
                        hbox(
                            text("  "),
                            text(BRAILLE_SPINNER[frame] + " ") | color(Color::BlueLight),
                            hbox(std::move(thinking_chars)),
                            text(" " + timer_str + m_name) | color(Color::GrayDark)
                        )
                    );
                } else if (!msg.response.empty()) {
                    // Streaming response with Markdown
                    Elements md_lines = RenderMarkdown(msg.response.substr(0, msg.streamed_length));
                    for (auto& md_line : md_lines) {
                        // Indent by 2 spaces
                        chat_elements.push_back(hbox(text("  "), std::move(md_line)));
                    }
                    chat_elements.push_back(text(""));
                }
            }
        }

        // Input Area
        auto now = std::chrono::steady_clock::now();
        auto duration_idle = std::chrono::duration_cast<std::chrono::seconds>(now - last_keypress_.load());
        auto duration_scroll = std::chrono::duration_cast<std::chrono::seconds>(now - last_scroll_.load());
        
        Element border_top = separatorLight() | color(Color::GrayDark);
        Element border_bottom = separatorLight() | color(Color::GrayDark);
        Element prompt_symbol = text("> ") | bold | color(Color::White);
        Element input_row;
        if (is_pasting_.load()) {
            // Active paste — show live counter
            int live_count = (int)std::count(pasted_buffer_.begin(), pasted_buffer_.end(), '\n') + 1;
            input_row = hbox(
                prompt_symbol,
                text("[pasting ") | color(Color::YellowLight),
                text(std::to_string(live_count)) | bold | color(Color::YellowLight),
                text(" lines...]") | color(Color::YellowLight)
            );
        } else if (!pasted_buffer_.empty()) {
            // Paste finalized — show indicator + editable input after
            input_row = hbox(
                prompt_symbol,
                text("[pasted ") | color(Color::BlueLight),
                text(std::to_string(paste_line_count_)) | bold | color(Color::CyanLight),
                text(" lines] ") | color(Color::BlueLight),
                input_component->Render() | flex
            );
        } else {
            input_row = hbox(
                prompt_symbol,
                input_component->Render() | flex
            );
        }
        
        // Autocomplete dropdown
        Elements matching_elements;
        std::vector<std::string> current_matches = get_autocomplete_state();
        
        if (selected_command_index_.load() >= (int)current_matches.size()) {
            selected_command_index_ = std::max(0, (int)current_matches.size() - 1);
        }
        
        if (!current_matches.empty()) {
            if (prompt_value_.rfind("/model", 0) == 0) {
                matching_elements.push_back(text("Models (select or type):") | bold | color(Color::YellowLight));
                for (size_t i = 0; i < current_matches.size(); ++i) {
                    bool is_selected = (i == (size_t)selected_command_index_.load());
                    auto col = is_selected ? Color::YellowLight : Color::Yellow;
                    auto prefix = is_selected ? "  > " : "    ";
                    bool is_active = (i == (size_t)selected_model_idx_.load());
                    std::string label = prefix + current_matches[i] + (is_active ? " [Active]" : "");
                    matching_elements.push_back(text(label) | color(col));
                }
            } else if (prompt_value_.rfind("/role", 0) == 0) {
                matching_elements.push_back(text("Roles (select or type):") | bold | color(Color::GreenLight));
                for (size_t i = 0; i < current_matches.size(); ++i) {
                    bool is_selected = (i == (size_t)selected_command_index_.load());
                    auto col = is_selected ? Color::GreenLight : Color::Green;
                    auto prefix = is_selected ? "  > " : "    ";
                    matching_elements.push_back(text(prefix + current_matches[i]) | color(col));
                }
            } else if (prompt_value_.rfind("/skill", 0) == 0) {
                matching_elements.push_back(text("Skills (select or type):") | bold | color(Color::MagentaLight));
                size_t limit = std::min(current_matches.size(), (size_t)10);
                for (size_t i = 0; i < limit; ++i) {
                    bool is_selected = (i == (size_t)selected_command_index_.load());
                    auto col = is_selected ? Color::MagentaLight : Color::Magenta;
                    auto prefix = is_selected ? "  > " : "    ";
                    matching_elements.push_back(text(prefix + current_matches[i]) | color(col));
                }
                if (current_matches.size() > limit) {
                    matching_elements.push_back(text("    ... (" + std::to_string(current_matches.size() - limit) + " more skills)") | color(Color::GrayDark));
                }
            } else {
                matching_elements.push_back(text("Slash Commands:") | bold | color(Color::CyanLight));
                for (size_t i = 0; i < current_matches.size(); ++i) {
                    bool is_selected = (i == (size_t)selected_command_index_.load());
                    auto col = is_selected ? Color::CyanLight : Color::Cyan;
                    auto prefix = is_selected ? "  > /" : "    /";
                    matching_elements.push_back(text(prefix + current_matches[i]) | color(col));
                }
            }
        }
        
        std::string current_steer = "";
        {
            std::lock_guard<std::mutex> lock(steer_mutex_);
            current_steer = queued_steer_prompt_;
        }
        
        Element steer_banner = text("");
        if (!current_steer.empty()) {
            steer_banner = hbox({
                text(" Ctrl+S (send) ") | bold | color(Color::Black) | bgcolor(Color::Yellow),
                text(" Queued: ") | bold | color(Color::YellowLight),
                text(current_steer) | color(Color::White) | flex,
            }) | bgcolor(Color::RGB(30, 30, 30));
        }

        Element input_area;
        bool has_text = !prompt_value_.empty();
        if (has_text || !current_steer.empty() || (duration_idle.count() < 10 && duration_scroll.count() >= 3)) {
            Elements input_rows;
            input_rows.push_back(border_top);
            if (!current_steer.empty()) {
                input_rows.push_back(steer_banner);
                input_rows.push_back(separatorLight() | color(Color::GrayDark));
            }
            input_rows.push_back(input_row);
            input_rows.push_back(border_bottom);
            if (!matching_elements.empty()) {
                input_rows.push_back(vbox(std::move(matching_elements)));
            }
            input_area = vbox(std::move(input_rows));
        } else {
            input_area = text(""); // Hidden
        }

        if (auto_scroll_.load()) {
            scroll_index_ = std::max(0, (int)chat_elements.size() - 1);
        } else {
            scroll_index_ = std::min(std::max(0, scroll_index_.load()), (int)chat_elements.size() - 1);
        }

        if (scroll_index_ >= 0 && scroll_index_ < chat_elements.size()) {
            chat_elements[scroll_index_] = chat_elements[scroll_index_] | focus;
        }

        Element main_view = vbox(
            vbox(std::move(chat_elements)) | vscroll_indicator | yframe | flex,
            input_area
        );
        
        if (show_model_picker_.load()) {
            Elements model_items;
            for (size_t i = 0; i < available_models_.size(); ++i) {
                bool is_highlighted = ((int)i == model_menu_selected_);
                bool is_active = ((int)i == selected_model_idx_.load());
                
                std::string prefix = is_highlighted ? "  > " : "    ";
                std::string label = prefix + std::to_string(i + 1) + ". " + available_models_[i];
                if (is_active) {
                    label += "  [ACTIVE]";
                }
                
                Element item = text(label);
                if (is_highlighted) {
                    item = item | bold | color(Color::Black) | bgcolor(Color::Yellow);
                } else if (is_active) {
                    item = item | bold | color(Color::GreenLight);
                } else {
                    item = item | color(Color::White);
                }
                model_items.push_back(item);
            }

            Element picker_box = vbox({
                text(" SELECT ACTIVE MODEL ") | bold | color(Color::Black) | bgcolor(Color::Yellow) | center,
                separatorLight() | color(Color::GrayDark),
                vbox(std::move(model_items)),
                separatorLight() | color(Color::GrayDark),
                text("Up/Down to navigate  •  Enter to select  •  Esc to cancel") | color(Color::GrayLight) | center
            }) | borderRounded | bgcolor(Color::RGB(18, 18, 18)) | size(WIDTH, GREATER_THAN, 64);

            Element picker_view = picker_box | clear_under | center;
            return dbox({main_view, picker_view});
        }
        return main_view;
    });

    auto main_component = CatchEvent(renderer, [&](Event event) {
        if (show_model_picker_.load()) {
            if (event == Event::Escape || event == Event::Character('q') || event == Event::Character('Q')) {
                show_model_picker_ = false;
                last_keypress_ = std::chrono::steady_clock::now();
                return true;
            }
            if (event == Event::ArrowUp) {
                model_menu_selected_ = std::max(0, model_menu_selected_ - 1);
                last_keypress_ = std::chrono::steady_clock::now();
                return true;
            }
            if (event == Event::ArrowDown) {
                model_menu_selected_ = std::min((int)available_models_.size() - 1, model_menu_selected_ + 1);
                last_keypress_ = std::chrono::steady_clock::now();
                return true;
            }
            if (event == Event::Return) {
                selected_model_idx_ = model_menu_selected_;
                show_model_picker_ = false;
                std::string chosen_model = (selected_model_idx_.load() < (int)available_models_.size()) 
                    ? available_models_[selected_model_idx_.load()] : "Default";
                {
                    std::lock_guard<std::mutex> lock(history_mutex_);
                    ChatMessage msg;
                    msg.prompt = "/model " + chosen_model;
                    msg.response = "Switched active model to: **" + chosen_model + "**";
                    msg.model_name = "Razor System";
                    msg.is_loading = false;
                    msg.streamed_length = msg.response.size();
                    history_.push_back(msg);
                }
                last_keypress_ = std::chrono::steady_clock::now();
                return true;
            }
            return true;
        }

        if (event == Event::Character('\x13') || event == Event::Special("\x13")) {
            std::string to_steer = "";
            {
                std::lock_guard<std::mutex> lock(steer_mutex_);
                if (!queued_steer_prompt_.empty()) {
                    to_steer = queued_steer_prompt_;
                    queued_steer_prompt_.clear();
                }
            }
            if (to_steer.empty() && !prompt_value_.empty()) {
                to_steer = prompt_value_;
                prompt_value_.clear();
            }

            if (!to_steer.empty()) {
                {
                    std::lock_guard<std::mutex> lock(history_mutex_);
                    ChatMessage msg;
                    msg.prompt = "[STEER] " + to_steer;
                    msg.is_loading = true;
                    msg.streamed_length = 0;
                    msg.start_time = std::chrono::steady_clock::now();
                    history_.push_back(msg);
                }
                if (submit_callback_) {
                    submit_callback_("[USER STEER]: " + to_steer);
                }
                last_keypress_ = std::chrono::steady_clock::now();
                return true;
            }
        }

        if (event == Event::Escape || event == Event::Character('\x03')) {
            if (event == Event::Escape && !pasted_buffer_.empty()) {
                pasted_buffer_.clear();
                paste_line_count_ = 0;
                prompt_value_.clear();
                return true;
            }
            if (event == Event::Escape && !get_autocomplete_state().empty()) {
                prompt_value_.clear();
                return true;
            }
            is_running_ = false;
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::ArrowUp) {
            selected_command_index_ = std::max(0, selected_command_index_.load() - 1);
            return true;
        }
        if (event == Event::ArrowDown) {
            selected_command_index_ = selected_command_index_.load() + 1;
            return true;
        }
        if (event.is_mouse()) {
            if (event.mouse().button == Mouse::WheelUp) {
                last_scroll_ = std::chrono::steady_clock::now();
                auto_scroll_ = false;
                scroll_index_ = std::max(0, scroll_index_.load() - 3);
            } else if (event.mouse().button == Mouse::WheelDown) {
                last_scroll_ = std::chrono::steady_clock::now();
                scroll_index_ = scroll_index_.load() + 3;
            } else {
                last_keypress_ = std::chrono::steady_clock::now();
            }
        } else if (event.is_character() || event == Event::Backspace || event == Event::Return) {
            last_keypress_ = std::chrono::steady_clock::now();
            last_scroll_ = std::chrono::steady_clock::now() - std::chrono::hours(1);
            auto_scroll_ = true;
        }
        return false;
    });

    screen.Loop(main_component);
    
    is_running_ = false;
    if (animation_thread_.joinable()) {
        animation_thread_.join();
    }
}

} // namespace razor

