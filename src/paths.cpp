#include "homework_grader/paths.hpp"

#include <cstdlib>
#include <stdexcept>

namespace homework_grader {
namespace {

std::filesystem::path environment_path(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return {};
    }
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value)));
}

std::filesystem::path require_home_directory() {
#ifdef _WIN32
    auto home = environment_path("USERPROFILE");
    if (home.empty()) {
        const auto drive = environment_path("HOMEDRIVE");
        const auto path = environment_path("HOMEPATH");
        if (!drive.empty() && !path.empty()) {
            home = drive / path.relative_path();
        }
    }
#else
    auto home = environment_path("HOME");
#endif
    if (home.empty()) {
        throw std::runtime_error("无法确定用户主目录，请使用 --db 指定数据库路径。");
    }
    return home;
}

}  // namespace

std::filesystem::path default_database_path() {
#ifdef _WIN32
    auto base = environment_path("LOCALAPPDATA");
    if (base.empty()) {
        base = require_home_directory() / "AppData" / "Local";
    }
#elif defined(__APPLE__)
    const auto base = require_home_directory() / "Library" / "Application Support";
#else
    auto base = environment_path("XDG_DATA_HOME");
    if (base.empty()) {
        base = require_home_directory() / ".local" / "share";
    }
#endif
    return base / "homework-grader" / "homework.db";
}

std::filesystem::path resolve_database_path(int argc, char* argv[]) {
    std::filesystem::path command_line_path;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--db") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("--db 后必须提供数据库路径。\n" + usage_text(argv[0]));
            }
            command_line_path = std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t*>(argv[++index])));
        } else if (argument == "--help" || argument == "-h") {
            throw std::invalid_argument(usage_text(argv[0]));
        } else {
            throw std::invalid_argument("未知参数：" + argument + "\n" + usage_text(argv[0]));
        }
    }
    if (!command_line_path.empty()) {
        return command_line_path;
    }
    const auto environment = environment_path("HOMEWORK_GRADER_DB");
    if (!environment.empty()) {
        return environment;
    }
    return default_database_path();
}

std::string usage_text(const std::string& program_name) {
    return "用法：" + program_name + " [--db <数据库路径>]";
}

}  // namespace homework_grader
