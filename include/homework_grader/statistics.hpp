#pragma once

#include <array>
#include <vector>

#include "homework_grader/model.hpp"

namespace homework_grader {

struct UnitStatistics {
    QuestionReference question;
    int correct_count{};
    int wrong_count{};
    double correct_percentage{};
};

struct MajorQuestionStatistics {
    int major_number{};
    std::vector<UnitStatistics> units;
    int fully_correct_count{};
    double fully_correct_percentage{};
};

struct GradeStatistics {
    Grade grade{Grade::below_d};
    int count{};
    double percentage{};
};

struct AssignmentStatistics {
    int completed{};
    int total_students{};
    int remaining{};
    double completion_percentage{};
    std::vector<MajorQuestionStatistics> questions;
    std::array<GradeStatistics, 6> grades{};
};

[[nodiscard]] AssignmentStatistics calculate_statistics(const Assignment& assignment,
                                                        const std::vector<Submission>& submissions);

}  // namespace homework_grader
