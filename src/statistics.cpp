#include "homework_grader/statistics.hpp"

#include <algorithm>
#include <map>
#include <set>

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

    std::map<QuestionReference, std::size_t> unit_indexes;
    for (std::size_t index = 0; index < assignment.question_units.size(); ++index) {
        unit_indexes.emplace(assignment.question_units[index].reference, index);
    }
    std::vector<int> wrong_counts(assignment.question_units.size(), 0);
    std::map<int, int> submissions_with_wrong_part;
    std::array<int, 6> grade_counts{};
    for (const auto& submission : submissions) {
        std::set<int> wrong_majors;
        for (const auto& question : submission.wrong_questions) {
            const auto found = unit_indexes.find(question);
            if (found != unit_indexes.end()) {
                ++wrong_counts[found->second];
                wrong_majors.insert(question.major_number);
            }
        }
        for (const int major : wrong_majors) {
            ++submissions_with_wrong_part[major];
        }
        const Grade grade = evaluate(assignment, submission.wrong_questions).grade;
        ++grade_counts[grade_index(grade)];
    }

    result.questions.reserve(static_cast<std::size_t>(assignment.main_question_count));
    for (std::size_t index = 0; index < assignment.question_units.size(); ++index) {
        const auto reference = assignment.question_units[index].reference;
        if (result.questions.empty() ||
            result.questions.back().major_number != reference.major_number) {
            const int fully_correct =
                result.completed - submissions_with_wrong_part[reference.major_number];
            result.questions.push_back({reference.major_number,
                                        {},
                                        fully_correct,
                                        percentage(fully_correct, result.completed)});
        }
        const int wrong = wrong_counts[index];
        const int correct = result.completed - wrong;
        result.questions.back().units.push_back(
            {reference, correct, wrong, percentage(correct, result.completed)});
    }

    const auto grades = all_grades();
    for (std::size_t index = 0; index < grades.size(); ++index) {
        result.grades[index] = {grades[index], grade_counts[index],
                                percentage(grade_counts[index], result.completed)};
    }
    return result;
}

}  // namespace homework_grader
