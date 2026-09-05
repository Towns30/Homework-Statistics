#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "homework_grader/model.hpp"

namespace homework_grader {

struct WrongQuestionParseResult {
    std::vector<QuestionReference> questions;
    std::string error;

    [[nodiscard]] bool ok() const {
        return error.empty();
    }
};

struct QuestionStructureParseResult {
    std::vector<QuestionUnit> question_units;
    std::string error;

    [[nodiscard]] bool ok() const {
        return error.empty();
    }
};

[[nodiscard]] std::string trim(std::string_view input);
[[nodiscard]] std::optional<int> parse_integer(std::string_view input);
[[nodiscard]] QuestionStructureParseResult parse_question_structure(std::string_view input,
                                                                    int main_question_count,
                                                                    int maximum_units);
[[nodiscard]] WrongQuestionParseResult parse_wrong_questions(std::string_view input,
                                                             const Assignment& assignment);

}  // namespace homework_grader
