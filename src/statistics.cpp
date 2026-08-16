#include "homework_grader/statistics.hpp"

#include <algorithm>

#include "homework_grader/grading.hpp"

namespace homework_grader {
namespace {

std::size_t grade_index(Grade grade) {
    switch (grade) {
        case Grade::a_plus:
            return 0;
        case Grade::a:
            return 1;
        case Grade::b:
            return 2;
        case Grade::c:
            return 3;
        case Grade::d:
            return 4;
        case Grade::below_d:
            return 5;
    }
    return 5;
}

double percentage(int numerator, int denominator) {
    if (denominator == 0) {
        return 0.0;
    }
    return static_cast<double>(numerator) * 100.0 / static_cast<double>(denominator);
}

}  // namespace

AssignmentStatistics calculate_statistics(const Assignment& assignment,
                                          const std::vector<Submission>& submissions) {
    AssignmentStatistics result;
    result.completed = static_cast<int>(submissions.size());
    result.total_students = assignment.total_students;
    result.remaining = std::max(0, result.total_students - result.completed);
    result.completion_percentage = percentage(result.completed, result.total_students);

    std::vector<int> wrong_counts(static_cast<std::size_t>(assignment.total_questions), 0);
    std::array<int, 6> grade_counts{};
    for (const auto& submission : submissions) {
        for (const int question : submission.wrong_questions) {
            if (question >= 1 && question <= assignment.total_questions) {
                ++wrong_counts[static_cast<std::size_t>(question - 1)];
            }
        }
        const Grade grade = evaluate(assignment, submission.wrong_questions).grade;
        ++grade_counts[grade_index(grade)];
    }

    result.questions.reserve(static_cast<std::size_t>(assignment.total_questions));
    for (int question = 1; question <= assignment.total_questions; ++question) {
        const int wrong = wrong_counts[static_cast<std::size_t>(question - 1)];
        const int correct = result.completed - wrong;
        result.questions.push_back(
            {question, correct, wrong, percentage(correct, result.completed)});
    }

    const auto grades = all_grades();
    for (std::size_t index = 0; index < grades.size(); ++index) {
        result.grades[index] = {grades[index], grade_counts[index],
                                percentage(grade_counts[index], result.completed)};
    }
    return result;
}

}  // namespace homework_grader
