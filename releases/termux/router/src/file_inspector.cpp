#include "file_inspector.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <sys/stat.h>
#include <ctime>
#include <algorithm>

namespace fs = std::filesystem;

namespace razor {

std::string FileInspector::FormatSize(uintmax_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    double kb = bytes / 1024.0;
    if (kb < 1024) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << kb << " KB";
        return ss.str();
    }
    double mb = kb / 1024.0;
    if (mb < 1024) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << mb << " MB";
        return ss.str();
    }
    double gb = mb / 1024.0;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << gb << " GB";
    return ss.str();
}

std::string FileInspector::GetPermissionsString(uint32_t mode) {
    std::string perms = "---------";
    if (mode & S_IRUSR) perms[0] = 'r';
    if (mode & S_IWUSR) perms[1] = 'w';
    if (mode & S_IXUSR) perms[2] = 'x';
    if (mode & S_IRGRP) perms[3] = 'r';
    if (mode & S_IWGRP) perms[4] = 'w';
    if (mode & S_IXGRP) perms[5] = 'x';
    if (mode & S_IROTH) perms[6] = 'r';
    if (mode & S_IWOTH) perms[7] = 'w';
    if (mode & S_IXOTH) perms[8] = 'x';
    return perms;
}

std::string FileInspector::FormatTime(int64_t sec) {
    std::time_t t = static_cast<std::time_t>(sec);
    std::tm tm_buf;
    localtime_r(&t, &tm_buf);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buf);
}

std::string FileInspector::InspectFileType(const std::string& path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) {
        return "cannot open (No such file or directory)";
    }

    if (S_ISDIR(st.st_mode)) return "directory";
    if (S_ISLNK(st.st_mode)) return "symbolic link";
    if (S_ISCHR(st.st_mode)) return "character special device";
    if (S_ISBLK(st.st_mode)) return "block special device";
    if (S_ISFIFO(st.st_mode)) return "fifo (named pipe)";
    if (S_ISSOCK(st.st_mode)) return "socket";

    if (st.st_size == 0) return "empty";

    // Read initial bytes (up to 512 bytes)
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "unreadable";
    }

    std::vector<unsigned char> header(512);
    file.read(reinterpret_cast<char*>(header.data()), 512);
    std::streamsize bytes_read = file.gcount();
    header.resize(bytes_read);
    file.close();

    if (bytes_read == 0) return "empty";

    // Magic bytes checking
    // ELF Binary
    if (bytes_read >= 4 && header[0] == 0x7F && header[1] == 'E' && header[2] == 'L' && header[3] == 'F') {
        std::string bitness = (header[4] == 2) ? "64-bit" : "32-bit";
        std::string endian = (header[5] == 1) ? "LSB" : "MSB";
        std::string elf_type = "executable";
        if (bytes_read >= 18) {
            uint16_t type_val = header[16] | (header[17] << 8);
            if (type_val == 1) elf_type = "relocatable";
            else if (type_val == 2) elf_type = "executable";
            else if (type_val == 3) elf_type = "shared object (dynamically linked)";
            else if (type_val == 4) elf_type = "core file";
        }
        return "ELF " + bitness + " " + endian + " " + elf_type;
    }

    // Zip / JAR / APK
    if (bytes_read >= 4 && header[0] == 0x50 && header[1] == 0x4B && header[2] == 0x03 && header[3] == 0x04) {
        return "Zip archive data";
    }

    // Gzip
    if (bytes_read >= 2 && header[0] == 0x1F && header[1] == 0x8B) {
        return "gzip compressed data";
    }

    // Tar
    if (bytes_read >= 262 && std::memcmp(header.data() + 257, "ustar", 5) == 0) {
        return "POSIX tar archive";
    }

    // XZ
    if (bytes_read >= 6 && header[0] == 0xFD && header[1] == '7' && header[2] == 'z' && header[3] == 'X' && header[4] == 'Z' && header[5] == 0x00) {
        return "XZ compressed data";
    }

    // 7z
    if (bytes_read >= 6 && header[0] == '7' && header[1] == 'z' && header[2] == 0xBC && header[3] == 0xAF && header[4] == 0x27 && header[5] == 0x1C) {
        return "7-zip archive data";
    }

    // Bzip2
    if (bytes_read >= 3 && header[0] == 'B' && header[1] == 'Z' && header[2] == 'h') {
        return "bzip2 compressed data";
    }

    // Images
    if (bytes_read >= 8 && header[0] == 0x89 && header[1] == 'P' && header[2] == 'N' && header[3] == 'G' && header[4] == 0x0D && header[5] == 0x0A && header[6] == 0x1A && header[7] == 0x0A) {
        return "PNG image data";
    }
    if (bytes_read >= 3 && header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) {
        return "JPEG image data";
    }
    if (bytes_read >= 6 && (std::memcmp(header.data(), "GIF87a", 6) == 0 || std::memcmp(header.data(), "GIF89a", 6) == 0)) {
        return "GIF image data";
    }
    if (bytes_read >= 12 && std::memcmp(header.data(), "RIFF", 4) == 0 && std::memcmp(header.data() + 8, "WEBP", 4) == 0) {
        return "WebP image data";
    }

    // PDF
    if (bytes_read >= 4 && std::memcmp(header.data(), "%PDF", 4) == 0) {
        return "PDF document";
    }

    // SQLite3
    if (bytes_read >= 16 && std::memcmp(header.data(), "SQLite format 3", 15) == 0) {
        return "SQLite 3.x database";
    }

    // Shebang checks
    if (bytes_read >= 2 && header[0] == '#' && header[1] == '!') {
        std::string line;
        for (size_t i = 0; i < header.size() && header[i] != '\n'; ++i) {
            line += static_cast<char>(header[i]);
        }
        if (line.find("python") != std::string::npos) return "Python script text executable";
        if (line.find("bash") != std::string::npos) return "Bourne-Again shell script text executable";
        if (line.find("sh") != std::string::npos) return "POSIX shell script text executable";
        if (line.find("node") != std::string::npos) return "Node.js script text executable";
        if (line.find("perl") != std::string::npos) return "Perl script text executable";
        if (line.find("ruby") != std::string::npos) return "Ruby script text executable";
        return "Script text executable (" + line.substr(0, 30) + ")";
    }

    // Check if text (ASCII or UTF-8)
    bool is_binary = false;
    for (size_t i = 0; i < header.size(); ++i) {
        if (header[i] == 0) {
            is_binary = true;
            break;
        }
    }

    if (!is_binary) {
        fs::path fpath(path);
        if (fpath.filename() == "CMakeLists.txt") return "ASCII text (CMake script)";
        if (fpath.filename() == "Makefile") return "ASCII text (Makefile script)";

        // Inspect extension
        std::string ext = fpath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") return "C++ source, ASCII text";
        if (ext == ".hpp" || ext == ".h" || ext == ".hxx") return "C/C++ header, ASCII text";
        if (ext == ".c") return "C source, ASCII text";
        if (ext == ".py") return "Python script, ASCII text";
        if (ext == ".json") return "JSON data, ASCII text";
        if (ext == ".yaml" || ext == ".yml") return "YAML document, ASCII text";
        if (ext == ".md" || ext == ".markdown") return "Markdown document, ASCII text";
        if (ext == ".txt" || ext == ".log") return "ASCII text";
        if (ext == ".rs") return "Rust source, ASCII text";
        if (ext == ".go") return "Go source, ASCII text";
        if (ext == ".js" || ext == ".ts") return "JavaScript/TypeScript source, ASCII text";
        if (ext == ".html" || ext == ".htm") return "HTML document, ASCII text";
        if (ext == ".css") return "CSS stylesheet, ASCII text";

        return "ASCII text";
    }

    return "data";
}

ListDirectoryResult FileInspector::ListDirectory(const std::string& dir_path) {
    ListDirectoryResult res;
    res.path = dir_path;

    std::error_code ec;
    fs::path target_path(dir_path);

    if (!fs::exists(target_path, ec)) {
        res.error_message = "Path does not exist: " + dir_path;
        return res;
    }

    if (!fs::is_directory(target_path, ec)) {
        res.error_message = "Path is not a directory: " + dir_path;
        return res;
    }

    std::vector<FileEntryInfo> entries;

    for (const auto& entry : fs::directory_iterator(target_path, fs::directory_options::skip_permission_denied, ec)) {
        FileEntryInfo info;
        info.name = entry.path().filename().string();
        info.path = entry.path().string();

        struct stat st;
        if (lstat(info.path.c_str(), &st) == 0) {
            info.size = st.st_size;
            info.permissions = (S_ISDIR(st.st_mode) ? "d" : (S_ISLNK(st.st_mode) ? "l" : "-")) + GetPermissionsString(st.st_mode);
            info.modified_time = FormatTime(st.st_mtime);
            info.is_directory = S_ISDIR(st.st_mode);
            info.is_symlink = S_ISLNK(st.st_mode);
            info.is_executable = (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
        } else {
            info.permissions = "??????????";
            info.modified_time = "Unknown";
        }

        info.file_type_description = InspectFileType(info.path);
        if (info.is_directory) {
            info.name += "/";
        }

        entries.push_back(info);
    }

    // Sort: directories first, then alphabetically
    std::sort(entries.begin(), entries.end(), [](const FileEntryInfo& a, const FileEntryInfo& b) {
        if (a.is_directory != b.is_directory) {
            return a.is_directory > b.is_directory;
        }
        return a.name < b.name;
    });

    res.entries = entries;

    // Generate formatted table
    std::ostringstream ss;
    ss << "Directory listing of: " << dir_path << "\n";
    ss << std::left << std::setw(28) << "Name"
       << std::setw(12) << "Size"
       << std::setw(13) << "Permissions"
       << std::setw(22) << "Modified"
       << "Type\n";
    ss << std::string(105, '-') << "\n";

    size_t dir_count = 0;
    size_t file_count = 0;
    size_t link_count = 0;
    uintmax_t total_file_size = 0;

    for (const auto& e : entries) {
        if (e.is_directory) dir_count++;
        else if (e.is_symlink) link_count++;
        else {
            file_count++;
            total_file_size += e.size;
        }

        ss << std::left << std::setw(28) << e.name
           << std::setw(12) << FormatSize(e.size)
           << std::setw(13) << e.permissions
           << std::setw(22) << e.modified_time
           << e.file_type_description << "\n";
    }

    ss << std::string(105, '-') << "\n";
    ss << "Total: " << entries.size() << " entries ("
       << dir_count << " directories, "
       << file_count << " files [" << FormatSize(total_file_size) << "], "
       << link_count << " symlinks)\n";

    res.formatted_table = ss.str();
    return res;
}

} // namespace razor
