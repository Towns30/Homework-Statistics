#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "homework_grader/app.hpp"
#include "homework_grader/database.hpp"
#include "homework_grader/grading.hpp"
#include "homework_grader/input.hpp"
#include "homework_grader/statistics.hpp"

namespace {

using homework_grader::App;
using homework_grader::Assignment;
using homework_grader::Database;
using homework_grader::Grade;
using homework_grader::Id;
using homework_grader::Submission;

int assertions = 0;

void check(bool condition, const std::string& message) {
    ++assertions;
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void check_near(double actual, double expected, const std::string& message) {
    check(std::abs(actual - expected) < 0.001,
          message + "（实际值 " + std::to_string(actual) + "）");
}

template <typename Exception, typename Callable>
void check_throws(Callable&& callable, const std::string& message) {
    ++assertions;
    try {
        callable();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

Assignment sample_assignment(int students = 3) {
    Assignment assignment;
    assignment.name = "第一周作业";
    assignment.total_questions = 10;
    assignment.total_students = students;
    assignment.thresholds = {9, 8, 6, 4, 2};
    return assignment;
}

Submission sample_submission(int sequence, std::vector<int> wrong_questions) {
    Submission submission;
    submission.sequence = sequence;
    submission.wrong_questions = std::move(wrong_questions);
    return submission;
}

class TemporaryDatabasePath {
   public:
    TemporaryDatabasePath() {
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path() /
                     ("homework-grader-test-" + std::to_string(stamp));
        path_ = directory_ / "test.db";
    }
    ~TemporaryDatabasePath() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }
    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

   private:
    std::filesystem::path directory_;
    std::filesystem::path path_;
};

void test_grading_boundaries() {
    const auto assignment = sample_assignment();
    const auto& thresholds = assignment.thresholds;
    check(homework_grader::grade_for(10, thresholds) == Grade::a_plus, "全对应为 A+");
    check(homework_grader::grade_for(9, thresholds) == Grade::a_plus, "A+ 下限");
    check(homework_grader::grade_for(8, thresholds) == Grade::a, "A 下限 / A+ 下限少 1");
    check(homework_grader::grade_for(7, thresholds) == Grade::b, "比 A 下限少 1");
    check(homework_grader::grade_for(6, thresholds) == Grade::b, "B 下限");
    check(homework_grader::grade_for(5, thresholds) == Grade::c, "比 B 下限少 1");
    check(homework_grader::grade_for(4, thresholds) == Grade::c, "C 下限");
    check(homework_grader::grade_for(3, thresholds) == Grade::d, "比 C 下限少 1");
    check(homework_grader::grade_for(2, thresholds) == Grade::d, "D 下限");
    check(homework_grader::grade_for(1, thresholds) == Grade::below_d, "D 下限少 1");
    check(homework_grader::grade_for(0, thresholds) == Grade::below_d, "全错低于 D");

    auto no_below_d = assignment;
    no_below_d.thresholds = {8, 6, 4, 2, 0};
    check(homework_grader::grade_for(0, no_below_d.thresholds) == Grade::d,
          "D 下限为 0 时全错仍为 D");
    std::string error;
    auto invalid = assignment;
    invalid.thresholds.a = invalid.thresholds.a_plus;
    check(!homework_grader::valid_assignment_config(invalid, &error) && !error.empty(),
          "拒绝不严格递减的等级下限");
    invalid = assignment;
    invalid.total_questions = 3;
    invalid.thresholds = {3, 2, 1, 0, 0};
    check(!homework_grader::valid_assignment_config(invalid, &error),
          "题目过少而无法形成五个严格递减下限时拒绝配置");
}

void test_wrong_question_parsing() {
    check(homework_grader::parse_wrong_questions("", 10).questions.empty(), "空输入表示全对");
    check(homework_grader::parse_wrong_questions("无", 10).questions.empty(), "无表示全对");
    check(homework_grader::parse_wrong_questions("2 5 7", 10).questions ==
              std::vector<int>({2, 5, 7}),
          "解析空格分隔");
    check(homework_grader::parse_wrong_questions("7,2,5", 10).questions ==
              std::vector<int>({2, 5, 7}),
          "解析逗号并排序");
    check(homework_grader::parse_wrong_questions("2, 5 7", 10).questions ==
              std::vector<int>({2, 5, 7}),
          "解析混合分隔");
    check(!homework_grader::parse_wrong_questions("2 2", 10).ok(), "拒绝重复题号");
    check(!homework_grader::parse_wrong_questions("11", 10).ok(), "拒绝超过上限");
    check(!homework_grader::parse_wrong_questions("0", 10).ok(), "拒绝零题号");
    check(!homework_grader::parse_wrong_questions("abc", 10).ok(), "拒绝非整数");
    check(!homework_grader::parse_wrong_questions("-1", 10).ok(), "拒绝负数");
    check(!homework_grader::parse_wrong_questions("2.5", 10).ok(), "拒绝小数");
}

void test_add_and_capacity() {
    TemporaryDatabasePath temporary;
    Database database(temporary.path());
    const auto assignment = sample_assignment(2);
    const Id assignment_id = database.create_assignment(assignment);
    static_cast<void>(database.add_submission(assignment_id, {2, 5}));
    static_cast<void>(database.add_submission(assignment_id, {}));
    const auto submissions = database.list_submissions(assignment_id);
    check(submissions.size() == 2, "新增两份记录");
    check(submissions[0].sequence == 1 && submissions[1].sequence == 2, "录入顺序从 1 递增");
    const auto evaluated = homework_grader::evaluate(assignment, submissions[0].wrong_questions);
    check(evaluated.correct_count == 8 && evaluated.grade == Grade::a,
          "新增记录的正确题数和等级正确");
    check_throws<std::invalid_argument>(
        [&] { static_cast<void>(database.add_submission(assignment_id, {1})); },
        "达到总学生数后拒绝新增");
}

void test_reverse_update_and_live_statistics() {
    TemporaryDatabasePath temporary;
    Database database(temporary.path());
    const auto assignment = sample_assignment(3);
    const Id assignment_id = database.create_assignment(assignment);
    static_cast<void>(database.add_submission(assignment_id, {1}));
    static_cast<void>(database.add_submission(assignment_id, {2}));
    static_cast<void>(database.add_submission(assignment_id, {}));

    const auto latest = database.get_submission_by_reverse_position(assignment_id, 1);
    const auto second_latest = database.get_submission_by_reverse_position(assignment_id, 2);
    check(latest.has_value() && latest->sequence == 3, "倒数第 1 份是最近记录");
    check(second_latest.has_value() && second_latest->sequence == 2, "倒数第 2 份选择正确");
    check(!database.get_submission_by_reverse_position(assignment_id, 4).has_value(),
          "倒序位置超限时拒绝");

    database.update_submission(second_latest->id, {1});
    const auto submissions = database.list_submissions(assignment_id);
    check(submissions[1].sequence == 2 && submissions[1].wrong_questions == std::vector<int>({1}),
          "修改错题后原始顺序不变");
    const auto statistics = homework_grader::calculate_statistics(assignment, submissions);
    check(statistics.questions[0].wrong_count == 2 && statistics.questions[0].correct_count == 1,
          "修改后第 1 题统计立即更新");
    check(statistics.questions[1].wrong_count == 0 && statistics.questions[1].correct_count == 3,
          "修改后第 2 题统计立即更新");
    check_near(statistics.questions[0].correct_percentage, 100.0 / 3.0, "每题正确率保留精确计算值");
}

void test_statistics_zero_partial_and_complete() {
    const auto assignment = sample_assignment(4);
    const auto empty = homework_grader::calculate_statistics(assignment, {});
    check(empty.completed == 0 && empty.remaining == 4, "零份记录人数正确");
    check_near(empty.completion_percentage, 0.0, "零份记录完成率为零");
    check(empty.questions[0].correct_count == 0 && empty.questions[0].wrong_count == 0,
          "零份记录不伪造题目样本");
    for (const auto& grade : empty.grades) {
        check(grade.count == 0 && grade.percentage == 0.0, "零份记录等级统计为零");
    }

    std::vector<Submission> partial = {sample_submission(1, {}), sample_submission(2, {1, 2})};
    const auto partial_statistics = homework_grader::calculate_statistics(assignment, partial);
    check(partial_statistics.completed == 2 && partial_statistics.remaining == 2,
          "部分完成的人数正确");
    check_near(partial_statistics.completion_percentage, 50.0, "部分完成率正确");
    check(partial_statistics.questions[0].correct_count == 1 &&
              partial_statistics.questions[0].wrong_count == 1,
          "部分样本每题人数正确");
    check_near(partial_statistics.questions[0].correct_percentage, 50.0, "部分样本每题正确率正确");
    check(partial_statistics.grades[0].count == 1 && partial_statistics.grades[1].count == 1,
          "等级人数正确");
    check_near(partial_statistics.grades[0].percentage, 50.0, "等级百分比正确");

    partial.push_back(sample_submission(3, {1, 2, 3, 4}));
    partial.push_back(sample_submission(4, {1, 2, 3, 4, 5, 6, 7, 8}));
    const auto complete = homework_grader::calculate_statistics(assignment, partial);
    check_near(complete.completion_percentage, 100.0, "全部完成率为 100%");
    int grade_total = 0;
    for (const auto& grade : complete.grades) {
        grade_total += grade.count;
    }
    check(grade_total == complete.completed, "等级人数之和等于已录入人数");
}

void test_persistence() {
    TemporaryDatabasePath temporary;
    Id assignment_id{};
    {
        Database database(temporary.path());
        assignment_id = database.create_assignment(sample_assignment(2));
        static_cast<void>(database.add_submission(assignment_id, {3, 7}));
        static_cast<void>(database.add_submission(assignment_id, {}));
    }
    {
        Database reopened(temporary.path());
        const auto assignment = reopened.get_assignment(assignment_id);
        check(assignment.has_value() && assignment->name == "第一周作业", "重新打开后作业存在");
        const auto submissions = reopened.list_submissions(assignment_id);
        check(submissions.size() == 2, "重新打开后记录数量正确");
        check(submissions[0].sequence == 1 &&
                  submissions[0].wrong_questions == std::vector<int>({3, 7}),
              "重新打开后错题和录入顺序正确");
        check(submissions[1].sequence == 2 && submissions[1].wrong_questions.empty(),
              "重新打开后全对记录正确");
    }
}

void test_delete_assignment() {
    TemporaryDatabasePath temporary;
    Id deleted_id{};
    Id retained_id{};
    {
        Database database(temporary.path());
        deleted_id = database.create_assignment(sample_assignment(2));
        auto retained = sample_assignment(1);
        retained.name = "保留的作业";
        retained_id = database.create_assignment(retained);
        static_cast<void>(database.add_submission(deleted_id, {1, 3}));
        static_cast<void>(database.add_submission(deleted_id, {}));

        check(!database.delete_assignment(999'999), "删除不存在的作业时返回失败");
        check(database.delete_assignment(deleted_id), "删除存在的作业时返回成功");
        check(!database.get_assignment(deleted_id).has_value(), "作业主记录已删除");
        check(database.list_submissions(deleted_id).empty(), "学生记录随作业级联删除");
        const auto assignments = database.list_assignments();
        check(assignments.size() == 1 && assignments[0].id == retained_id,
              "删除目标作业不会影响其他作业");
    }
    {
        Database reopened(temporary.path());
        check(!reopened.get_assignment(deleted_id).has_value(), "重新打开后删除结果仍然有效");
        check(reopened.get_assignment(retained_id).has_value(), "重新打开后其他作业仍然存在");
    }
}

void test_confirmation_uses_yn() {
    TemporaryDatabasePath temporary;
    Database database(temporary.path());
    std::istringstream input(
        "1\n"
        "英文输入测试\n"
        "4\n"
        "1\n"
        "4\n"
        "3\n"
        "2\n"
        "1\n"
        "0\n"
        "是\n"
        "Y\n"
        "3\n"
        "1\n"
        "N\n"
        "4\n");
    std::ostringstream output;

    App app(database, input, output);
    check(app.run() == 0, "y/n 确认流程正常退出");
    check(database.list_assignments().size() == 1, "Y 保存作业且 N 取消删除");
    check(output.str().find("（y/n）：") != std::string::npos, "确认提示使用 y/n");
    check(output.str().find("输入无效：请输入 y 或 n。") != std::string::npos,
          "中文是/否不再作为确认输入");
    check(output.str().find("（是/否）") == std::string::npos, "界面不再提示中文是/否");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"等级边界", test_grading_boundaries},
        {"错题解析", test_wrong_question_parsing},
        {"新增与容量", test_add_and_capacity},
        {"倒序修改与实时统计", test_reverse_update_and_live_statistics},
        {"统计口径", test_statistics_zero_partial_and_complete},
        {"持久化", test_persistence},
        {"删除作业", test_delete_assignment},
        {"y/n 确认交互", test_confirmation_uses_yn},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[通过] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[失败] " << name << "：" << error.what() << '\n';
        }
    }
    std::cout << "执行 " << assertions << " 项断言，失败测试组：" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
