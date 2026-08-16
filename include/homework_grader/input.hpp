#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace homework_grader {

struct WrongQuestionParseResult {
    std::vector<int> questions;
    std::string error;

    [[nodiscard]] bool ok() const {
        return error.empty();
    }
};

[[nodiscard]] std::string trim(std::string_view input);
[[nodiscard]] std::optional<int> parse_integer(std::string_view input);
[[nodiscard]] WrongQuestionParseResult parse_wrong_questions(std::string_view input,
                                                             int total_questions);

}  // namespace homework_grader
