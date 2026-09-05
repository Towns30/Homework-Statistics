#include "homework_grader/grading.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace homework_grader {

int scoring_unit_count(const Assignment& assignment) {
    return static_cast<int>(assignment.question_units.size());
}

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
    if (assignment.main_question_count <= 0 ||
        assignment.main_question_count > maximum_question_units) {
        return fail("大题数必须是有效范围内的正整数。");
    }
    if (assignment.total_students <= 0) {
        return fail("总学生数必须是正整数。");
    }
    if (assignment.question_units.size() > static_cast<std::size_t>(maximum_question_units)) {
        return fail("计分单位总数超过上限。");
    }

    std::set<QuestionReference> references;
    std::vector<std::vector<int>> parts(
        static_cast<std::size_t>(assignment.main_question_count + 1));
    QuestionReference previous;
    bool first = true;
    for (const auto& unit : assignment.question_units) {
        const auto reference = unit.reference;
        if (reference.major_number < 1 || reference.major_number > assignment.main_question_count ||
            reference.part_number < 0 || !references.insert(reference).second) {
            return fail("题目结构包含无效或重复的计分单位。");
        }
        if (!first && !(previous < reference)) {
            return fail("计分单位必须按大题号和小问号排列。");
        }
        first = false;
        previous = reference;
        parts[static_cast<std::size_t>(reference.major_number)].push_back(reference.part_number);
    }
    for (int major = 1; major <= assignment.main_question_count; ++major) {
        auto& question_parts = parts[static_cast<std::size_t>(major)];
        std::sort(question_parts.begin(), question_parts.end());
        if (question_parts.size() == 1 && question_parts.front() == 0) {
            continue;
        }
        if (question_parts.size() < 2 || question_parts.front() != 1) {
            return fail("每道大题必须是一个独立计分单位，或包含至少两个连续编号的小问。");
        }
        for (std::size_t index = 0; index < question_parts.size(); ++index) {
            if (question_parts[index] != static_cast<int>(index + 1)) {
                return fail("小问必须从 1 开始连续编号。");
            }
        }
    }

    const int unit_count = scoring_unit_count(assignment);
    const auto& t = assignment.thresholds;
    if (!(unit_count >= t.a_plus && t.a_plus > t.a && t.a > t.b && t.b > t.c && t.c > t.d &&
          t.d >= 0)) {
        return fail("等级下限必须满足：计分单位数 >= A+ > A > B > C > D >= 0。");
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
                             const std::vector<QuestionReference>& wrong_questions) {
    const auto wrong_count = static_cast<int>(wrong_questions.size());
    const int correct_count = scoring_unit_count(assignment) - wrong_count;
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
    lines.push_back(line("A+", t.a_plus, scoring_unit_count(assignment)));
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
