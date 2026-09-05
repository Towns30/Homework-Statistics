#include <sqlite3.h>

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
using homework_grader::QuestionReference;
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
    assignment.main_question_count = 10;
    assignment.question_units =
        homework_grader::parse_question_structure("", 10, 1'000'000).question_units;
    assignment.total_students = students;
    assignment.thresholds = {9, 8, 6, 4, 2};
    return assignment;
}

QuestionReference question(int major, int part = 0) {
    return {major, part};
}

Assignment split_assignment(int students = 4) {
    Assignment assignment;
    assignment.name = "含小问作业";
    assignment.main_question_count = 5;
    assignment.question_units =
        homework_grader::parse_question_structure("2=3 5=2", 5, 1'000'000).question_units;
    assignment.total_students = students;
    assignment.thresholds = {7, 6, 4, 2, 0};
    return assignment;
}

Submission sample_submission(int sequence, std::vector<QuestionReference> wrong_questions) {
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

void execute_sql(sqlite3* database, const char* sql) {
    char* message = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
    if (result != SQLITE_OK) {
        const std::string detail = message == nullptr ? "未知 SQLite 错误" : message;
        sqlite3_free(message);
        throw std::runtime_error(detail);
    }
}

int scalar_int(const std::filesystem::path& path, const char* sql) {
    sqlite3* database = nullptr;
    if (sqlite3_open(path.string().c_str(), &database) != SQLITE_OK) {
        const std::string message =
            database == nullptr ? "无法打开数据库" : sqlite3_errmsg(database);
        if (database != nullptr) {
            sqlite3_close(database);
        }
        throw std::runtime_error(message);
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_ROW) {
        const std::string message = sqlite3_errmsg(database);
        sqlite3_finalize(statement);
        sqlite3_close(database);
        throw std::runtime_error(message);
    }
    const int value = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return value;
}

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
    invalid.main_question_count = 3;
    invalid.question_units =
        homework_grader::parse_question_structure("", 3, 1'000'000).question_units;
    invalid.thresholds = {3, 2, 1, 0, 0};
    check(!homework_grader::valid_assignment_config(invalid, &error),
          "题目过少而无法形成五个严格递减下限时拒绝配置");
}

void test_question_structure_parsing() {
    const auto flat = homework_grader::parse_question_structure("", 5, 1'000'000);
    check(flat.ok() && flat.question_units.size() == 5, "空结构输入生成无小问作业");
    check(flat.question_units[1].reference == question(2), "无小问题目使用 part_number 0");

    const auto split = homework_grader::parse_question_structure("2=3, 5=2", 5, 1'000'000);
    check(split.ok() && split.question_units.size() == 8, "解析混合分隔的小问结构");
    check(split.question_units[1].reference == question(2, 1) &&
              split.question_units[3].reference == question(2, 3) &&
              split.question_units[6].reference == question(5, 1),
          "小问按大题和小问顺序生成");
    check(!homework_grader::parse_question_structure("2=3 2=4", 5, 100).ok(), "拒绝重复声明大题");
    check(!homework_grader::parse_question_structure("6=2", 5, 100).ok(), "拒绝越界大题");
    check(!homework_grader::parse_question_structure("2=1", 5, 100).ok(), "拒绝少于两个小问");
    check(!homework_grader::parse_question_structure("2:3", 5, 100).ok(), "拒绝非法格式");
    check(!homework_grader::parse_question_structure("2=100", 5, 10).ok(), "拒绝超过计分单位上限");
}

void test_wrong_question_parsing() {
    const auto flat = sample_assignment();
    const auto split = split_assignment();
    check(homework_grader::parse_wrong_questions("", flat).questions.empty(), "空输入表示全对");
    check(homework_grader::parse_wrong_questions("无", flat).questions.empty(), "无表示全对");
    check(homework_grader::parse_wrong_questions("2 5 7", flat).questions ==
              std::vector<QuestionReference>({question(2), question(5), question(7)}),
          "解析空格分隔");
    check(homework_grader::parse_wrong_questions("7,2,5", flat).questions ==
              std::vector<QuestionReference>({question(2), question(5), question(7)}),
          "解析逗号并排序");
    check(homework_grader::parse_wrong_questions("2.3, 1 2.1", split).questions ==
              std::vector<QuestionReference>({question(1), question(2, 1), question(2, 3)}),
          "解析混合分隔");
    check(homework_grader::parse_wrong_questions("2", split).questions ==
              std::vector<QuestionReference>({question(2, 1), question(2, 2), question(2, 3)}),
          "大题号展开成全部小问");
    check(!homework_grader::parse_wrong_questions("2 2.1", split).ok(), "拒绝整题与小问重叠");
    check(!homework_grader::parse_wrong_questions("2.1 2.1", split).ok(), "拒绝重复小问");
    check(!homework_grader::parse_wrong_questions("2.4", split).ok(), "拒绝越界小问");
    check(!homework_grader::parse_wrong_questions("1.1", split).ok(), "拒绝不存在的小问");
    check(!homework_grader::parse_wrong_questions("6", split).ok(), "拒绝超过大题上限");
    check(!homework_grader::parse_wrong_questions("abc", split).ok(), "拒绝非法题号");
    check(!homework_grader::parse_wrong_questions("-1", split).ok(), "解析器拒绝负数");
}

void test_add_and_capacity() {
    TemporaryDatabasePath temporary;
    Database database(temporary.path());
    const auto assignment = sample_assignment(2);
    const Id assignment_id = database.create_assignment(assignment);
    static_cast<void>(database.add_submission(assignment_id, {question(2), question(5)}));
    static_cast<void>(database.add_submission(assignment_id, {}));
    const auto submissions = database.list_submissions(assignment_id);
    check(submissions.size() == 2, "新增两份记录");
    check(submissions[0].sequence == 1 && submissions[1].sequence == 2, "录入顺序从 1 递增");
    const auto evaluated = homework_grader::evaluate(assignment, submissions[0].wrong_questions);
    check(evaluated.correct_unit_count == 8 && evaluated.grade == Grade::a,
          "新增记录的正确题数和等级正确");
    check_throws<std::invalid_argument>(
        [&] { static_cast<void>(database.add_submission(assignment_id, {question(1)})); },
        "达到总学生数后拒绝新增");
}

void test_reverse_update_and_live_statistics() {
    TemporaryDatabasePath temporary;
    Database database(temporary.path());
    const auto assignment = sample_assignment(3);
    const Id assignment_id = database.create_assignment(assignment);
    static_cast<void>(database.add_submission(assignment_id, {question(1)}));
    static_cast<void>(database.add_submission(assignment_id, {question(2)}));
    static_cast<void>(database.add_submission(assignment_id, {}));

    const auto latest = database.get_submission_by_reverse_position(assignment_id, 1);
    const auto second_latest = database.get_submission_by_reverse_position(assignment_id, 2);
    check(latest.has_value() && latest->sequence == 3, "倒数第 1 份是最近记录");
    check(second_latest.has_value() && second_latest->sequence == 2, "倒数第 2 份选择正确");
    check(!database.get_submission_by_reverse_position(assignment_id, 4).has_value(),
          "倒序位置超限时拒绝");

    database.update_submission(second_latest->id, {question(1)});
    const auto submissions = database.list_submissions(assignment_id);
    check(submissions[1].sequence == 2 &&
              submissions[1].wrong_questions == std::vector<QuestionReference>({question(1)}),
          "修改错题后原始顺序不变");
    const auto statistics = homework_grader::calculate_statistics(assignment, submissions);
    check(statistics.questions[0].units[0].wrong_count == 2 &&
              statistics.questions[0].units[0].correct_count == 1,
          "修改后第 1 题统计立即更新");
    check(statistics.questions[1].units[0].wrong_count == 0 &&
              statistics.questions[1].units[0].correct_count == 3,
          "修改后第 2 题统计立即更新");
    check_near(statistics.questions[0].units[0].correct_percentage, 100.0 / 3.0,
               "每题正确率保留精确计算值");
}

void test_statistics_zero_partial_and_complete() {
    const auto assignment = sample_assignment(4);
    const auto empty = homework_grader::calculate_statistics(assignment, {});
    check(empty.completed == 0 && empty.remaining == 4, "零份记录人数正确");
    check_near(empty.completion_percentage, 0.0, "零份记录完成率为零");
    check(empty.questions[0].units[0].correct_count == 0 &&
              empty.questions[0].units[0].wrong_count == 0,
          "零份记录不伪造题目样本");
    for (const auto& grade : empty.grades) {
        check(grade.count == 0 && grade.percentage == 0.0, "零份记录等级统计为零");
    }

    std::vector<Submission> partial = {sample_submission(1, {}),
                                       sample_submission(2, {question(1), question(2)})};
    const auto partial_statistics = homework_grader::calculate_statistics(assignment, partial);
    check(partial_statistics.completed == 2 && partial_statistics.remaining == 2,
          "部分完成的人数正确");
    check_near(partial_statistics.completion_percentage, 50.0, "部分完成率正确");
    check(partial_statistics.questions[0].units[0].correct_count == 1 &&
              partial_statistics.questions[0].units[0].wrong_count == 1,
          "部分样本每题人数正确");
    check_near(partial_statistics.questions[0].units[0].correct_percentage, 50.0,
               "部分样本每题正确率正确");
    check(partial_statistics.grades[0].count == 1 && partial_statistics.grades[1].count == 1,
          "等级人数正确");
    check_near(partial_statistics.grades[0].percentage, 50.0, "等级百分比正确");

    partial.push_back(sample_submission(3, {question(1), question(2), question(3), question(4)}));
    partial.push_back(sample_submission(4, {question(1), question(2), question(3), question(4),
                                            question(5), question(6), question(7), question(8)}));
    const auto complete = homework_grader::calculate_statistics(assignment, partial);
    check_near(complete.completion_percentage, 100.0, "全部完成率为 100%");
    int grade_total = 0;
    for (const auto& grade : complete.grades) {
        grade_total += grade.count;
    }
    check(grade_total == complete.completed, "等级人数之和等于已录入人数");
}

void test_split_scoring_statistics_and_persistence() {
    TemporaryDatabasePath temporary;
    const auto assignment = split_assignment(3);
    const auto evaluated = homework_grader::evaluate(assignment, {question(2, 1), question(5, 2)});
    check(homework_grader::scoring_unit_count(assignment) == 8, "小问计入计分单位总数");
    check(evaluated.correct_unit_count == 6 && evaluated.grade == Grade::a,
          "按错误小问数计算正确计分单位数和等级");

    Id assignment_id{};
    {
        Database database(temporary.path());
        assignment_id = database.create_assignment(assignment);
        static_cast<void>(database.add_submission(assignment_id, {question(2, 1)}));
        static_cast<void>(database.add_submission(assignment_id, {question(2, 2)}));
        static_cast<void>(database.add_submission(assignment_id, {}));
        const auto stored = database.get_assignment(assignment_id);
        check(stored.has_value() && stored->main_question_count == 5 &&
                  stored->question_units.size() == 8,
              "数据库保存大题数和计分单位结构");
        const auto statistics = homework_grader::calculate_statistics(
            *stored, database.list_submissions(assignment_id));
        check(statistics.questions[1].units[0].question == question(2, 1) &&
                  statistics.questions[1].units[0].wrong_count == 1 &&
                  statistics.questions[1].units[0].correct_count == 2,
              "具体小问正确率人数正确");
        check(statistics.questions[1].fully_correct_count == 1, "全题正确人数要求所有小问正确");
        check_near(statistics.questions[1].fully_correct_percentage, 100.0 / 3.0, "全题正确率正确");
    }
    {
        Database reopened(temporary.path());
        const auto stored = reopened.get_assignment(assignment_id);
        const auto submissions = reopened.list_submissions(assignment_id);
        check(stored.has_value() && stored->question_units.size() == 8, "重新打开后小问结构存在");
        check(submissions.size() == 3 && submissions[0].wrong_questions ==
                                             std::vector<QuestionReference>({question(2, 1)}),
              "重新打开后具体小问错误存在");
    }
}

void test_version_one_migration() {
    TemporaryDatabasePath temporary;
    std::filesystem::create_directories(temporary.path().parent_path());
    sqlite3* raw = nullptr;
    if (sqlite3_open(temporary.path().string().c_str(), &raw) != SQLITE_OK) {
        throw std::runtime_error("无法创建迁移测试数据库");
    }
    try {
        execute_sql(
            raw,
            "PRAGMA foreign_keys = ON;"
            "CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL "
            "DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
            "INSERT INTO schema_version(version) VALUES (1);"
            "CREATE TABLE assignments (id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "total_questions INTEGER NOT NULL CHECK(total_questions > 0), total_students INTEGER "
            "NOT NULL CHECK(total_students > 0), a_plus_threshold INTEGER NOT NULL, a_threshold "
            "INTEGER NOT NULL, b_threshold INTEGER NOT NULL, c_threshold INTEGER NOT NULL, "
            "d_threshold INTEGER NOT NULL, created_at TEXT NOT NULL DEFAULT "
            "(strftime('%Y-%m-%dT%H:%M:%fZ','now')), CHECK(total_questions >= a_plus_threshold "
            "AND a_plus_threshold > a_threshold AND a_threshold > b_threshold AND b_threshold > "
            "c_threshold AND c_threshold > d_threshold AND d_threshold >= 0));"
            "CREATE TABLE submissions (id INTEGER PRIMARY KEY, assignment_id INTEGER NOT NULL "
            "REFERENCES assignments(id) ON DELETE CASCADE, sequence INTEGER NOT NULL "
            "CHECK(sequence > 0), created_at TEXT NOT NULL DEFAULT "
            "(strftime('%Y-%m-%dT%H:%M:%fZ','now')), updated_at TEXT NOT NULL DEFAULT "
            "(strftime('%Y-%m-%dT%H:%M:%fZ','now')), UNIQUE(assignment_id, sequence));"
            "CREATE TABLE wrong_questions (submission_id INTEGER NOT NULL REFERENCES "
            "submissions(id) ON DELETE CASCADE, question_number INTEGER NOT NULL "
            "CHECK(question_number > 0), PRIMARY KEY(submission_id, question_number));"
            "INSERT INTO assignments(id, name, total_questions, total_students, a_plus_threshold, "
            "a_threshold, b_threshold, c_threshold, d_threshold) VALUES "
            "(7, '旧作业', 10, 3, 9, 8, 6, 4, 2);"
            "INSERT INTO submissions(id, assignment_id, sequence) VALUES (11, 7, 1);"
            "INSERT INTO wrong_questions(submission_id, question_number) VALUES (11, 3), (11, 7);");
    } catch (...) {
        sqlite3_close(raw);
        throw;
    }
    sqlite3_close(raw);

    {
        Database migrated(temporary.path());
        const auto assignment = migrated.get_assignment(7);
        check(assignment.has_value() && assignment->main_question_count == 10 &&
                  assignment->question_units.size() == 10,
              "v1 作业迁移为无小问的计分单位");
        const auto submissions = migrated.list_submissions(7);
        check(submissions.size() == 1 && submissions[0].sequence == 1 &&
                  submissions[0].wrong_questions ==
                      std::vector<QuestionReference>({question(3), question(7)}),
              "v1 错题集合和录入顺序准确迁移");
        const auto evaluated =
            homework_grader::evaluate(*assignment, submissions[0].wrong_questions);
        check(evaluated.correct_unit_count == 8 && evaluated.grade == Grade::a,
              "v1 迁移前后计分和等级一致");
    }
    check(scalar_int(temporary.path(), "SELECT MAX(version) FROM schema_version") == 2,
          "迁移后 schema version 为 2");
    check(scalar_int(temporary.path(), "SELECT COUNT(*) FROM question_units") == 10,
          "迁移生成全部旧题目的计分单位");
    check(scalar_int(temporary.path(),
                     "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND "
                     "name='wrong_questions'") == 0,
          "迁移后移除旧错题关系表");
}

void test_persistence() {
    TemporaryDatabasePath temporary;
    Id assignment_id{};
    {
        Database database(temporary.path());
        assignment_id = database.create_assignment(sample_assignment(2));
        static_cast<void>(database.add_submission(assignment_id, {question(3), question(7)}));
        static_cast<void>(database.add_submission(assignment_id, {}));
    }
    {
        Database reopened(temporary.path());
        const auto assignment = reopened.get_assignment(assignment_id);
        check(assignment.has_value() && assignment->name == "第一周作业", "重新打开后作业存在");
        const auto submissions = reopened.list_submissions(assignment_id);
        check(submissions.size() == 2, "重新打开后记录数量正确");
        check(submissions[0].sequence == 1 &&
                  submissions[0].wrong_questions ==
                      std::vector<QuestionReference>({question(3), question(7)}),
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
        static_cast<void>(database.add_submission(deleted_id, {question(1), question(3)}));
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
    check(scalar_int(temporary.path(), "SELECT COUNT(*) FROM question_units") == 10,
          "删除作业后其计分单位被级联删除");
    check(scalar_int(temporary.path(), "SELECT COUNT(*) FROM wrong_question_units") == 0,
          "删除作业后其错题关联被级联删除");
}

void test_split_interactive_flow() {
    TemporaryDatabasePath temporary;
    Id assignment_id{};
    {
        Database database(temporary.path());
        std::istringstream input(
            "1\n"
            "小问交互测试\n"
            "5\n"
            "2=3 5=2\n"
            "2\n"
            "7\n"
            "6\n"
            "4\n"
            "2\n"
            "0\n"
            "y\n"
            "2\n"
            "1\n"
            "1\n"
            "2 5.1\n"
            "y\n"
            "1\n"
            "2.1\n"
            "y\n"
            "2\n"
            "1\n"
            "4\n"
            "y\n"
            "3\n"
            "4\n"
            "0\n"
            "4\n");
        std::ostringstream output;
        App app(database, input, output);
        check(app.run() == 0, "含小问的完整终端流程正常退出");
        const auto assignments = database.list_assignments();
        check(assignments.size() == 1, "交互创建含小问作业");
        assignment_id = assignments.front().id;
        const auto submissions = database.list_submissions(assignment_id);
        check(submissions.size() == 2 &&
                  submissions[0].wrong_questions ==
                      std::vector<QuestionReference>(
                          {question(2, 1), question(2, 2), question(2, 3), question(5, 1)}) &&
                  submissions[1].wrong_questions == std::vector<QuestionReference>({question(4)}),
              "整题展开、具体小问和修改结果均已保存");
        check(output.str().find("共 8 个计分单位") != std::string::npos, "创建时显示计分单位数");
        check(output.str().find("2（全部小问） 5.1") != std::string::npos,
              "保存前折叠显示全部错误小问");
        check(output.str().find("2.1：答对") != std::string::npos &&
                  output.str().find("全题正确：") != std::string::npos,
              "终端统计按小问分组并显示全题正确率");
    }
    {
        Database reopened(temporary.path());
        const auto assignment = reopened.get_assignment(assignment_id);
        const auto submissions = reopened.list_submissions(assignment_id);
        check(assignment.has_value() && assignment->question_units.size() == 8 &&
                  submissions.size() == 2,
              "终端流程退出后重新打开仍可读取结构和记录");
    }
}

void test_confirmation_uses_yn() {
    TemporaryDatabasePath temporary;
    Database database(temporary.path());
    std::istringstream input(
        "1\n"
        "英文输入测试\n"
        "4\n"
        "\n"
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

void test_minus_one_goes_back() {
    TemporaryDatabasePath temporary;
    Database database(temporary.path());
    const Id assignment_id = database.create_assignment(sample_assignment());
    static_cast<void>(database.add_submission(assignment_id, {question(2), question(5)}));

    std::ostringstream scripted_input;
    scripted_input << "1\n"
                   << "-1\n"
                   << "1\n"
                   << "临时作业\n"
                   << "-1\n"
                   << "1\n"
                   << "结构取消作业\n"
                   << "4\n"
                   << "-1\n"
                   << "2\n"
                   << "-1\n"
                   << "3\n"
                   << "-1\n"
                   << "2\n"
                   << assignment_id << '\n'
                   << "-1\n"
                   << "0\n"
                   << "2\n"
                   << assignment_id << '\n'
                   << "1\n"
                   << "-1\n"
                   << "2\n"
                   << "-1\n"
                   << "2\n"
                   << "1\n"
                   << "-1\n"
                   << "5\n";
    std::istringstream input(scripted_input.str());
    std::ostringstream output;

    App app(database, input, output);
    check(app.run() == 0, "-1 返回流程正常退出");
    check(database.list_assignments().size() == 1, "取消新建和删除后作业保持不变");
    const auto submissions = database.list_submissions(assignment_id);
    check(submissions.size() == 1 && submissions[0].wrong_questions ==
                                         std::vector<QuestionReference>({question(2), question(5)}),
          "取消新增和修改后学生记录保持不变");
    check(output.str().find("输入 -1 可取消新建并返回主菜单。") != std::string::npos,
          "新建作业提示 -1 可取消");
    check(output.str().find("已取消删除。") != std::string::npos, "-1 可取消删除");
    check(output.str().find("已取消，本次记录未保存。") != std::string::npos, "-1 可取消新增记录");
    check(output.str().find("已取消修改，原记录保持不变。") != std::string::npos,
          "-1 可在修改流程中返回");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"等级边界", test_grading_boundaries},
        {"题目结构解析", test_question_structure_parsing},
        {"错题解析", test_wrong_question_parsing},
        {"新增与容量", test_add_and_capacity},
        {"倒序修改与实时统计", test_reverse_update_and_live_statistics},
        {"统计口径", test_statistics_zero_partial_and_complete},
        {"小问计分统计与持久化", test_split_scoring_statistics_and_persistence},
        {"v1 数据迁移", test_version_one_migration},
        {"持久化", test_persistence},
        {"删除作业", test_delete_assignment},
        {"小问终端流程", test_split_interactive_flow},
        {"y/n 确认交互", test_confirmation_uses_yn},
        {"-1 返回上一界面", test_minus_one_goes_back},
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
