#include "homework_grader/grading.hpp"

#include <sstream>

namespace homework_grader {

bool valid_assignment_config(const Assignment& assignment, std::string* error) {
    const auto fail = [error](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    if (assignment.name.empty()) {
        return fail("作业名称不能为空。");
    }
    if (assignment.total_questions <= 0) {
        return fail("总题目数必须是正整数。");
    }
    if (assignment.total_students <= 0) {
        return fail("总学生数必须是正整数。");
    }
    const auto& t = assignment.thresholds;
    if (!(assignment.total_questions >= t.a_plus && t.a_plus > t.a && t.a > t.b && t.b > t.c &&
          t.c > t.d && t.d >= 0)) {
        return fail("等级下限必须满足：总题目数 >= A+ > A > B > C > D >= 0。");
    }
    return true;
}

Grade grade_for(int correct_count, const GradeThresholds& thresholds) {
    if (correct_count >= thresholds.a_plus) {
        return Grade::a_plus;
    }
    if (correct_count >= thresholds.a) {
        return Grade::a;
    }
    if (correct_count >= thresholds.b) {
        return Grade::b;
    }
    if (correct_count >= thresholds.c) {
        return Grade::c;
    }
    if (correct_count >= thresholds.d) {
        return Grade::d;
    }
    return Grade::below_d;
}

std::string grade_label(Grade grade) {
    switch (grade) {
        case Grade::a_plus:
            return "A+";
        case Grade::a:
            return "A";
        case Grade::b:
            return "B";
        case Grade::c:
            return "C";
        case Grade::d:
            return "D";
        case Grade::below_d:
            return "低于 D";
    }
    return "未知";
}

EvaluatedSubmission evaluate(const Assignment& assignment,
                             const std::vector<int>& wrong_questions) {
    const auto wrong_count = static_cast<int>(wrong_questions.size());
    const int correct_count = assignment.total_questions - wrong_count;
    return {correct_count, grade_for(correct_count, assignment.thresholds)};
}

std::vector<std::string> grade_range_lines(const Assignment& assignment) {
    const auto& t = assignment.thresholds;
    const auto line = [](const std::string& label, int lower, int upper) {
        std::ostringstream stream;
        stream << label << "：" << lower << " ～ " << upper;
        return stream.str();
    };

    std::vector<std::string> lines;
    lines.push_back(line("A+", t.a_plus, assignment.total_questions));
    lines.push_back(line("A", t.a, t.a_plus - 1));
    lines.push_back(line("B", t.b, t.a - 1));
    lines.push_back(line("C", t.c, t.b - 1));
    lines.push_back(line("D", t.d, t.c - 1));
    if (t.d > 0) {
        lines.push_back(line("低于 D", 0, t.d - 1));
    }
    return lines;
}

}  // namespace homework_grader
