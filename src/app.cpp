#include "homework_grader/app.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include "homework_grader/grading.hpp"
#include "homework_grader/input.hpp"
#include "homework_grader/statistics.hpp"

namespace homework_grader {
namespace {

class InputClosed : public std::exception {};
class BackRequested : public std::exception {};

std::string question_label(const QuestionReference& question) {
    if (question.part_number == 0) {
        return std::to_string(question.major_number);
    }
    return std::to_string(question.major_number) + "." + std::to_string(question.part_number);
}

std::string question_list(const Assignment& assignment,
                          const std::vector<QuestionReference>& questions) {
    if (questions.empty()) {
        return "无（全部正确）";
    }
    std::set<QuestionReference> wrong(questions.begin(), questions.end());
    std::map<int, std::vector<QuestionReference>> units_by_major;
    for (const auto& unit : assignment.question_units) {
        units_by_major[unit.reference.major_number].push_back(unit.reference);
    }

    std::ostringstream stream;
    bool first = true;
    for (const auto& [major, units] : units_by_major) {
        const auto wrong_count = std::count_if(
            units.begin(), units.end(),
            [&wrong](const QuestionReference& reference) { return wrong.contains(reference); });
        if (wrong_count == 0) {
            continue;
        }
        const bool split = units.front().part_number != 0;
        if (split && wrong_count == static_cast<std::ptrdiff_t>(units.size())) {
            if (!first) {
                stream << ' ';
            }
            stream << major << "（全部小问）";
            first = false;
            continue;
        }
        for (const auto& unit : units) {
            if (!wrong.contains(unit)) {
                continue;
            }
            if (!first) {
                stream << ' ';
            }
            stream << question_label(unit);
            first = false;
        }
    }
    return stream.str();
}

void show_question_structure(std::ostream& output, const Assignment& assignment) {
    output << "\n题目结构：\n";
    int current_major{};
    for (const auto& unit : assignment.question_units) {
        if (unit.reference.major_number != current_major) {
            if (current_major != 0) {
                output << '\n';
            }
            output << "  ";
            current_major = unit.reference.major_number;
        } else {
            output << ' ';
        }
        output << question_label(unit.reference);
    }
    output << "\n共 " << scoring_unit_count(assignment) << " 个计分单位\n";
}

}  // namespace

App::App(Database& database, std::istream& input, std::ostream& output)
    : database_(database), input_(input), output_(output) {}

int App::run() {
    output_ << "\n作业批改统计程序\n";
    try {
        while (true) {
            output_ << "\n主菜单\n"
                    << "1. 新建作业\n"
                    << "2. 打开已有作业\n"
                    << "3. 删除已有作业\n"
                    << "4. 退出\n";
            const int choice = read_integer("请选择：", 1, 4);
            if (choice == 1) {
                static_cast<void>(create_assignment_interactive());
            } else if (choice == 2) {
                if (open_assignment_interactive()) {
                    return 0;
                }
            } else if (choice == 3) {
                delete_assignment_interactive();
            } else {
                output_ << "数据已保存，再见。\n";
                return 0;
            }
        }
    } catch (const InputClosed&) {
        output_ << "\n输入已结束，已保存的数据不会丢失。\n";
        return 0;
    }
}

std::string App::read_line(const std::string& prompt) {
    output_ << prompt;
    output_.flush();
    std::string line;
    if (!std::getline(input_, line)) {
        input_closed_ = true;
        throw InputClosed{};
    }
    return line;
}

int App::read_integer(const std::string& prompt, int minimum, int maximum, bool allow_back) {
    while (true) {
        const std::string line = read_line(prompt);
        const auto value = parse_integer(line);
        if (!value.has_value()) {
            output_ << "输入无效：请输入整数。\n";
            continue;
        }
        if (allow_back && *value == -1) {
            throw BackRequested{};
        }
        if (*value < minimum || *value > maximum) {
            output_ << "输入无效：请输入 " << minimum << " ～ " << maximum << " 之间的整数。\n";
            continue;
        }
        return *value;
    }
}

bool App::confirm(const std::string& prompt) {
    while (true) {
        const std::string answer = trim(read_line(prompt + "（y/n）："));
        if (answer == "y" || answer == "Y") {
            return true;
        }
        if (answer == "n" || answer == "N") {
            return false;
        }
        output_ << "输入无效：请输入 y 或 n。\n";
    }
}

std::vector<QuestionReference> App::read_wrong_questions(const Assignment& assignment) {
    while (true) {
        const std::string input = read_line(
            "请输入错题编号（如 1 2.1；有小问的大题号表示全部小问；逗号或空格分隔；"
            "全对请直接回车或输入“无”；输入 -1 取消）：");
        if (trim(input) == "-1") {
            throw BackRequested{};
        }
        const auto parsed = parse_wrong_questions(input, assignment);
        if (parsed.ok()) {
            return parsed.questions;
        }
        output_ << "输入无效：" << parsed.error << '\n';
    }
}

Assignment App::read_assignment() {
    Assignment assignment;
    do {
        assignment.name = trim(read_line("作业名称："));
        if (assignment.name == "-1") {
            throw BackRequested{};
        }
        if (assignment.name.empty()) {
            output_ << "输入无效：作业名称不能为空。\n";
        }
    } while (assignment.name.empty());

    constexpr int maximum_value = maximum_question_units;
    assignment.main_question_count = read_integer("大题数：", 1, maximum_value, true);
    while (true) {
        const std::string structure =
            read_line("含小问的大题（例如 2=3 5=2；没有请直接回车；输入 -1 取消）：");
        if (trim(structure) == "-1") {
            throw BackRequested{};
        }
        const auto parsed =
            parse_question_structure(structure, assignment.main_question_count, maximum_value);
        if (!parsed.ok()) {
            output_ << "输入无效：" << parsed.error << '\n';
            continue;
        }
        if (parsed.question_units.size() < 4) {
            output_ << "输入无效：至少需要 4 个计分单位，才能设置五个严格递减的等级下限。\n";
            continue;
        }
        assignment.question_units = parsed.question_units;
        break;
    }
    show_question_structure(output_, assignment);
    assignment.total_students = read_integer("总学生数：", 1, maximum_value, true);
    const int unit_count = scoring_unit_count(assignment);
    while (true) {
        output_ << "请设置五个等级的答对计分单位数下限。\n";
        assignment.thresholds.a_plus = read_integer("A+ 下限：", 0, unit_count, true);
        assignment.thresholds.a = read_integer("A 下限：", 0, unit_count, true);
        assignment.thresholds.b = read_integer("B 下限：", 0, unit_count, true);
        assignment.thresholds.c = read_integer("C 下限：", 0, unit_count, true);
        assignment.thresholds.d = read_integer("D 下限：", 0, unit_count, true);
        std::string error;
        if (valid_assignment_config(assignment, &error)) {
            break;
        }
        output_ << "等级配置无效：" << error << " 请重新输入五个下限。\n";
    }
    return assignment;
}

bool App::create_assignment_interactive() {
    output_ << "\n新建作业\n输入 -1 可取消新建并返回主菜单。\n";
    try {
        const Assignment assignment = read_assignment();
        output_ << "\n请确认等级区间（答对计分单位数）：\n";
        for (const auto& line : grade_range_lines(assignment)) {
            output_ << "  " << line << '\n';
        }
        if (!confirm("保存这份作业吗？")) {
            output_ << "已取消新建作业。\n";
            return false;
        }
        const Id id = database_.create_assignment(assignment);
        output_ << "作业已保存，ID 为 " << id << "。\n";
        return true;
    } catch (const BackRequested&) {
        output_ << "已取消新建作业。\n";
        return false;
    }
}

void App::delete_assignment_interactive() {
    const auto assignments = database_.list_assignments();
    if (assignments.empty()) {
        output_ << "\n目前没有可删除的作业。\n";
        return;
    }

    output_ << "\n删除已有作业\n";
    for (const auto& assignment : assignments) {
        output_ << "ID " << assignment.id << " | " << assignment.name << " | "
                << assignment.main_question_count << " 道大题 | " << scoring_unit_count(assignment)
                << " 个计分单位 | " << database_.submission_count(assignment.id) << '/'
                << assignment.total_students << " 人 | 创建于 " << assignment.created_at << '\n';
    }

    while (true) {
        const auto id =
            parse_integer(read_line("请输入要删除的作业 ID（输入 0 或 -1 取消删除）："));
        if (!id.has_value() || *id < -1) {
            output_ << "输入无效：请输入列表中的作业 ID。\n";
            continue;
        }
        if (*id == 0 || *id == -1) {
            output_ << "已取消删除。\n";
            return;
        }
        const auto assignment = database_.get_assignment(static_cast<Id>(*id));
        if (!assignment.has_value()) {
            output_ << "不存在这个作业 ID，请重新输入。\n";
            continue;
        }

        const int completed = database_.submission_count(assignment->id);
        output_ << "\n即将永久删除：\n"
                << "作业 ID：" << assignment->id << '\n'
                << "作业名称：" << assignment->name << '\n'
                << "已录入学生记录：" << completed << " 份\n"
                << "警告：该作业的所有学生记录和错题数据都会一并删除，且无法撤销。\n";
        if (!confirm("确认永久删除这份作业吗？")) {
            output_ << "已取消删除，数据保持不变。\n";
            return;
        }
        if (database_.delete_assignment(assignment->id)) {
            output_ << "作业“" << assignment->name << "”及其全部数据已删除。\n";
        } else {
            output_ << "作业已不存在，没有删除任何数据。\n";
        }
        return;
    }
}

bool App::open_assignment_interactive() {
    while (true) {
        const auto assignments = database_.list_assignments();
        if (assignments.empty()) {
            output_ << "\n目前没有已创建的作业。\n";
            return false;
        }
        output_ << "\n已有作业\n";
        for (const auto& assignment : assignments) {
            output_ << "ID " << assignment.id << " | " << assignment.name << " | "
                    << assignment.main_question_count << " 道大题 | "
                    << scoring_unit_count(assignment) << " 个计分单位 | "
                    << database_.submission_count(assignment.id) << '/' << assignment.total_students
                    << " 人 | 创建于 " << assignment.created_at << '\n';
        }

        while (true) {
            const auto id =
                parse_integer(read_line("请输入要打开的作业 ID（输入 0 或 -1 返回主菜单）："));
            if (!id.has_value() || *id < -1) {
                output_ << "输入无效：请输入列表中的作业 ID。\n";
                continue;
            }
            if (*id == 0 || *id == -1) {
                return false;
            }
            const auto assignment = database_.get_assignment(static_cast<Id>(*id));
            if (!assignment.has_value()) {
                output_ << "不存在这个作业 ID，请重新输入。\n";
                continue;
            }
            const auto action = assignment_loop(*assignment);
            if (action == AssignmentAction::exit_program) {
                return true;
            }
            break;
        }
    }
}

App::AssignmentAction App::assignment_loop(const Assignment& assignment) {
    show_assignment_summary(assignment);
    try {
        while (true) {
            output_ << "\n作业菜单\n"
                    << "1. 新增一个学生的批改结果\n"
                    << "2. 修改以前录入的批改结果\n"
                    << "3. 查看统计\n"
                    << "4. 返回作业列表\n"
                    << "5. 退出程序\n";
            const int choice = read_integer("请选择（输入 -1 也可返回作业列表）：", 1, 5, true);
            if (choice == 1) {
                add_submission_interactive(assignment);
            } else if (choice == 2) {
                update_submission_interactive(assignment);
            } else if (choice == 3) {
                show_statistics(assignment);
            } else if (choice == 4) {
                return AssignmentAction::back;
            } else {
                output_ << "数据已保存，再见。\n";
                return AssignmentAction::exit_program;
            }
        }
    } catch (const BackRequested&) {
        return AssignmentAction::back;
    }
}

void App::add_submission_interactive(const Assignment& assignment) {
    const int completed = database_.submission_count(assignment.id);
    if (completed >= assignment.total_students) {
        output_ << "已录入全部 " << assignment.total_students
                << " 位学生，不能继续新增；可以修改记录或查看统计。\n";
        return;
    }
    std::vector<QuestionReference> questions;
    try {
        questions = read_wrong_questions(assignment);
    } catch (const BackRequested&) {
        output_ << "已取消，本次记录未保存。\n";
        return;
    }
    const auto result = evaluate(assignment, questions);
    output_ << "\n待保存：第 " << completed + 1 << " 位学生\n"
            << "错题编号：" << question_list(assignment, questions) << '\n'
            << "错误计分单位数：" << questions.size() << '\n'
            << "答对计分单位数：" << result.correct_unit_count << '\n'
            << "等级：" << grade_label(result.grade) << '\n';
    if (!confirm("确认保存吗？")) {
        output_ << "已取消，本次记录未保存。\n";
        return;
    }
    static_cast<void>(database_.add_submission(assignment.id, questions));
    output_ << "第 " << completed + 1 << " 位学生的记录已保存：答对计分单位数 "
            << result.correct_unit_count << "，等第 " << grade_label(result.grade) << "。\n";
}

void App::show_submission(const Assignment& assignment, const Submission& submission,
                          const std::string& heading) {
    const auto result = evaluate(assignment, submission.wrong_questions);
    output_ << heading << '\n'
            << "原始录入序号：" << submission.sequence << '\n'
            << "错题编号：" << question_list(assignment, submission.wrong_questions) << '\n'
            << "答对计分单位数：" << result.correct_unit_count << '\n'
            << "等级：" << grade_label(result.grade) << '\n';
}

void App::update_submission_interactive(const Assignment& assignment) {
    const int completed = database_.submission_count(assignment.id);
    if (completed == 0) {
        output_ << "尚未录入任何学生，暂时没有可修改的记录。\n";
        return;
    }
    try {
        const int reverse_position = read_integer(
            "要修改已经录入学生中的倒数第几个？（输入 -1 取消）：", 1, completed, true);
        const auto original =
            database_.get_submission_by_reverse_position(assignment.id, reverse_position);
        if (!original.has_value()) {
            throw DatabaseError("记录在读取期间发生变化，请重试。");
        }
        show_submission(assignment, *original, "\n修改前：");
        const auto new_questions = read_wrong_questions(assignment);
        Submission changed = *original;
        changed.wrong_questions = new_questions;
        show_submission(assignment, changed, "\n修改后：");
        if (!confirm("确认替换这份记录吗？")) {
            output_ << "已取消，原记录保持不变。\n";
            return;
        }
        database_.update_submission(original->id, new_questions);
        const auto updated = evaluate(assignment, new_questions);
        output_ << "第 " << original->sequence << " 位学生的记录已更新：答对计分单位数 "
                << updated.correct_unit_count << "，等第 " << grade_label(updated.grade)
                << "；录入序号保持不变。\n";
    } catch (const BackRequested&) {
        output_ << "已取消修改，原记录保持不变。\n";
    }
}

void App::show_assignment_summary(const Assignment& assignment) {
    const int completed = database_.submission_count(assignment.id);
    const double completion =
        static_cast<double>(completed) * 100.0 / static_cast<double>(assignment.total_students);
    output_ << "\n作业：" << assignment.name << '\n'
            << "大题数：" << assignment.main_question_count << '\n'
            << "计分单位数：" << scoring_unit_count(assignment) << '\n'
            << "等级区间（答对计分单位数）：\n";
    for (const auto& line : grade_range_lines(assignment)) {
        output_ << "  " << line << '\n';
    }
    output_ << "已完成：" << completed << '/' << assignment.total_students << '\n'
            << std::fixed << std::setprecision(2) << "当前完成率：" << completion << "%\n";
}

void App::show_statistics(const Assignment& assignment) {
    const auto statistics =
        calculate_statistics(assignment, database_.list_submissions(assignment.id));
    output_ << "\n每道题正确率\n";
    for (const auto& question : statistics.questions) {
        const bool split = question.units.front().question.part_number != 0;
        if (!split) {
            const auto& unit = question.units.front();
            output_ << "第 " << question.major_number << " 题：答对 " << unit.correct_count
                    << " 人，答错 " << unit.wrong_count << " 人，正确率 ";
            if (statistics.completed == 0) {
                output_ << "暂无数据\n";
            } else {
                output_ << std::fixed << std::setprecision(2) << unit.correct_percentage << "%\n";
            }
            continue;
        }

        output_ << "第 " << question.major_number << " 题：\n";
        for (const auto& unit : question.units) {
            output_ << "  " << question_label(unit.question) << "：答对 " << unit.correct_count
                    << " 人，答错 " << unit.wrong_count << " 人，正确率 ";
            if (statistics.completed == 0) {
                output_ << "暂无数据\n";
            } else {
                output_ << std::fixed << std::setprecision(2) << unit.correct_percentage << "%\n";
            }
        }
        output_ << "  全题正确：" << question.fully_correct_count << " 人，正确率 ";
        if (statistics.completed == 0) {
            output_ << "暂无数据\n";
        } else {
            output_ << std::fixed << std::setprecision(2) << question.fully_correct_percentage
                    << "%\n";
        }
    }

    output_ << "\n等级分布\n";
    for (const auto& grade : statistics.grades) {
        if (grade.grade == Grade::below_d && assignment.thresholds.d == 0) {
            continue;
        }
        output_ << grade_label(grade.grade) << "：" << grade.count << " 人，百分比 ";
        if (statistics.completed == 0) {
            output_ << "暂无数据\n";
        } else {
            output_ << std::fixed << std::setprecision(2) << grade.percentage << "%\n";
        }
    }

    output_ << "\n作业完成率\n"
            << "已录入学生数：" << statistics.completed << '\n'
            << "总学生数：" << statistics.total_students << '\n'
            << "未录入学生数：" << statistics.remaining << '\n'
            << std::fixed << std::setprecision(2) << "完成率：" << statistics.completion_percentage
            << "%\n";
}

}  // namespace homework_grader
