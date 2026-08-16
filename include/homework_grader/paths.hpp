#pragma once

#include <filesystem>
#include <string>

namespace homework_grader {

[[nodiscard]] std::filesystem::path default_database_path();
[[nodiscard]] std::filesystem::path resolve_database_path(int argc, char* argv[]);
[[nodiscard]] std::string usage_text(const std::string& program_name);

}  // namespace homework_grader
