#pragma once

#include <array>
#include <string>
#include <vector>

#include "homework_grader/model.hpp"

namespace homework_grader {

[[nodiscard]] bool valid_assignment_config(const Assignment& assignment,
                                           std::string* error = nullptr);
[[nodiscard]] Grade grade_for(int correct_count, const GradeThresholds& thresholds);
[[nodiscard]] std::string grade_label(Grade grade);
[[nodiscard]] EvaluatedSubmission evaluate(const Assignment& assignment,
                                           const std::vector<int>& wrong_questions);
[[nodiscard]] std::vector<std::string> grade_range_lines(const Assignment& assignment);
[[nodiscard]] constexpr std::array<Grade, 6> all_grades() {
    return {Grade::a_plus, Grade::a, Grade::b, Grade::c, Grade::d, Grade::below_d};
}

}  // namespace homework_grader
