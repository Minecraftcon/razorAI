#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace razor {

struct FileEntryInfo {
    std::string name;
    std::string path;
    uintmax_t size{0};
    std::string permissions;
    std::string modified_time;
    std::string file_type_description;
    bool is_directory{false};
    bool is_symlink{false};
    bool is_executable{false};
};

struct ListDirectoryResult {
    std::string path;
    std::vector<FileEntryInfo> entries;
    std::string formatted_table;
    std::string error_message;
};

class FileInspector {
public:
    // Inspect magic bytes and file content to describe file type (like `file` command)
    static std::string InspectFileType(const std::string& path);

    // List directory contents with full information (file command type, size, perms, date)
    static ListDirectoryResult ListDirectory(const std::string& dir_path = ".");

    // Format human readable bytes
    static std::string FormatSize(uintmax_t bytes);

private:
    static std::string GetPermissionsString(uint32_t perms);
    static std::string FormatTime(int64_t sec);
};

} // namespace razor
