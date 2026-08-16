#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace homework_grader {

using Id = std::int64_t;

struct GradeThresholds {
    int a_plus{};
    int a{};
    int b{};
    int c{};
    int d{};
};

struct Assignment {
    Id id{};
    std::string name;
    int total_questions{};
    int total_students{};
    GradeThresholds thresholds;
    std::string created_at;
};

struct Submission {
    Id id{};
    Id assignment_id{};
    int sequence{};
    std::vector<int> wrong_questions;
    std::string created_at;
    std::string updated_at;
};

enum class Grade { a_plus, a, b, c, d, below_d };

struct EvaluatedSubmission {
    int correct_count{};
    Grade grade{Grade::below_d};
};

}  // namespace homework_grader
