#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace homework_grader {

using Id = std::int64_t;
inline constexpr int maximum_question_units = 1'000'000;

struct GradeThresholds {
    int a_plus{};
    int a{};
    int b{};
    int c{};
    int d{};
};

struct QuestionReference {
    int major_number{};
    // Zero denotes a main question without parts; split questions start at part one.
    int part_number{};

    auto operator<=>(const QuestionReference&) const = default;
};

struct QuestionUnit {
    Id id{};
    QuestionReference reference;
};

struct Assignment {
    Id id{};
    std::string name;
    int main_question_count{};
    std::vector<QuestionUnit> question_units;
    int total_students{};
    GradeThresholds thresholds;
    std::string created_at;
};

struct Submission {
    Id id{};
    Id assignment_id{};
    int sequence{};
    std::vector<QuestionReference> wrong_questions;
    std::string created_at;
    std::string updated_at;
};

enum class Grade { a_plus, a, b, c, d, below_d };

struct EvaluatedSubmission {
    int correct_unit_count{};
    Grade grade{Grade::below_d};
};

[[nodiscard]] int scoring_unit_count(const Assignment& assignment);

}  // namespace homework_grader
