#include "homework_grader/database.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include "homework_grader/grading.hpp"

namespace homework_grader {
namespace {

[[noreturn]] void throw_sqlite(sqlite3* database, const std::string& context) {
    throw DatabaseError(context + "：" + sqlite3_errmsg(database));
}

class Statement {
   public:
    Statement(sqlite3* database, const char* sql) : database_(database) {
        if (sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr) != SQLITE_OK) {
            throw_sqlite(database_, "准备 SQL 失败");
        }
    }

    ~Statement() {
        sqlite3_finalize(statement_);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(int index, Id value) {
        if (sqlite3_bind_int64(statement_, index, value) != SQLITE_OK) {
            throw_sqlite(database_, "绑定整数参数失败");
        }
    }

    void bind_int(int index, int value) {
        if (sqlite3_bind_int(statement_, index, value) != SQLITE_OK) {
            throw_sqlite(database_, "绑定整数参数失败");
        }
    }

    void bind(int index, const std::string& value) {
        if (sqlite3_bind_text(statement_, index, value.c_str(), -1, SQLITE_TRANSIENT) !=
            SQLITE_OK) {
            throw_sqlite(database_, "绑定文本参数失败");
        }
    }

    [[nodiscard]] bool step_row() {
        const int result = sqlite3_step(statement_);
        if (result == SQLITE_ROW) {
            return true;
        }
        if (result == SQLITE_DONE) {
            return false;
        }
        throw_sqlite(database_, "执行 SQL 失败");
    }

    void execute() {
        if (sqlite3_step(statement_) != SQLITE_DONE) {
            throw_sqlite(database_, "执行 SQL 失败");
        }
    }

    void reset() {
        if (sqlite3_reset(statement_) != SQLITE_OK ||
            sqlite3_clear_bindings(statement_) != SQLITE_OK) {
            throw_sqlite(database_, "重置 SQL 失败");
        }
    }

    [[nodiscard]] Id column_id(int index) const {
        return sqlite3_column_int64(statement_, index);
    }
    [[nodiscard]] int column_int(int index) const {
        return sqlite3_column_int(statement_, index);
    }
    [[nodiscard]] std::string column_text(int index) const {
        const auto* value = sqlite3_column_text(statement_, index);
        return value == nullptr ? std::string{} : reinterpret_cast<const char*>(value);
    }

   private:
    sqlite3* database_{};
    sqlite3_stmt* statement_{};
};

void execute_static(sqlite3* database, const char* sql, const std::string& context) {
    Statement statement(database, sql);
    try {
        statement.execute();
    } catch (const DatabaseError&) {
        throw DatabaseError(context + "：" + sqlite3_errmsg(database));
    }
}

class Transaction {
   public:
    explicit Transaction(sqlite3* database) : database_(database) {
        execute_static(database_, "BEGIN IMMEDIATE", "开始事务失败");
    }
    ~Transaction() {
        if (!finished_) {
            sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit() {
        execute_static(database_, "COMMIT", "提交事务失败");
        finished_ = true;
    }

   private:
    sqlite3* database_{};
    bool finished_{false};
};

Assignment read_assignment(Statement& statement) {
    Assignment assignment;
    assignment.id = statement.column_id(0);
    assignment.name = statement.column_text(1);
    assignment.main_question_count = statement.column_int(2);
    assignment.total_students = statement.column_int(3);
    assignment.thresholds = {statement.column_int(4), statement.column_int(5),
                             statement.column_int(6), statement.column_int(7),
                             statement.column_int(8)};
    assignment.created_at = statement.column_text(9);
    return assignment;
}

void load_question_units(sqlite3* database, Assignment& assignment) {
    Statement statement(database,
                        "SELECT id, major_number, part_number FROM question_units WHERE "
                        "assignment_id = ? ORDER BY sort_order");
    statement.bind(1, assignment.id);
    while (statement.step_row()) {
        assignment.question_units.push_back(
            {statement.column_id(0), {statement.column_int(1), statement.column_int(2)}});
    }
}

std::vector<QuestionReference> validate_wrong_questions(
    const Assignment& assignment, const std::vector<QuestionReference>& wrong_questions) {
    std::set<QuestionReference> valid;
    for (const auto& unit : assignment.question_units) {
        valid.insert(unit.reference);
    }
    std::vector<QuestionReference> sorted = wrong_questions;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
        throw std::invalid_argument("错题编号不能重复。");
    }
    for (const auto& question : sorted) {
        if (!valid.contains(question)) {
            throw std::invalid_argument("错题编号超出作业题目范围。");
        }
    }
    return sorted;
}

Id find_question_unit_id(sqlite3* database, Id assignment_id, const QuestionReference& reference) {
    Statement statement(database,
                        "SELECT id FROM question_units WHERE assignment_id = ? AND major_number "
                        "= ? AND part_number = ?");
    statement.bind(1, assignment_id);
    statement.bind_int(2, reference.major_number);
    statement.bind_int(3, reference.part_number);
    if (!statement.step_row()) {
        throw std::invalid_argument("错题编号超出作业题目范围。");
    }
    return statement.column_id(0);
}

}  // namespace

struct Database::Impl {
    explicit Impl(const std::filesystem::path& path) {
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error) {
                throw DatabaseError("无法创建数据库目录：" + error.message());
            }
        }

        const auto path_bytes = path.u8string();
        const std::string utf8_path(reinterpret_cast<const char*>(path_bytes.data()),
                                    path_bytes.size());
        if (sqlite3_open_v2(utf8_path.c_str(), &database,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
            const std::string message = database == nullptr ? "未知错误" : sqlite3_errmsg(database);
            if (database != nullptr) {
                sqlite3_close(database);
                database = nullptr;
            }
            throw DatabaseError("无法打开数据库：" + message);
        }

        try {
            sqlite3_busy_timeout(database, 5000);
            execute_static(database, "PRAGMA foreign_keys = ON", "启用外键失败");
            {
                Statement journal(database, "PRAGMA journal_mode = WAL");
                if (!journal.step_row()) {
                    throw DatabaseError("启用 WAL 模式失败");
                }
            }
            migrate();
        } catch (...) {
            sqlite3_close(database);
            database = nullptr;
            throw;
        }
    }

    ~Impl() {
        if (database != nullptr) {
            sqlite3_close(database);
        }
    }

    void migrate() {
        Transaction transaction(database);
        execute_static(database,
                       "CREATE TABLE IF NOT EXISTS schema_version ("
                       "version INTEGER PRIMARY KEY, "
                       "applied_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))) ",
                       "创建版本表失败");
        int current_version{};
        {
            Statement version(database, "SELECT COALESCE(MAX(version), 0) FROM schema_version");
            if (!version.step_row()) {
                throw DatabaseError("读取数据库版本失败");
            }
            current_version = version.column_int(0);
        }
        if (current_version > 2) {
            throw DatabaseError("数据库版本高于本程序支持的版本，请升级程序。");
        }
        if (current_version == 0) {
            execute_static(
                database,
                "CREATE TABLE assignments ("
                "id INTEGER PRIMARY KEY, name TEXT NOT NULL, total_questions INTEGER NOT NULL "
                "CHECK(total_questions > 0), total_students INTEGER NOT NULL CHECK(total_students "
                "> 0), "
                "a_plus_threshold INTEGER NOT NULL, a_threshold INTEGER NOT NULL, "
                "b_threshold INTEGER NOT NULL, c_threshold INTEGER NOT NULL, d_threshold INTEGER "
                "NOT "
                "NULL, created_at TEXT NOT NULL DEFAULT "
                "(strftime('%Y-%m-%dT%H:%M:%fZ','now')), CHECK(total_questions >= "
                "a_plus_threshold AND a_plus_threshold > a_threshold AND a_threshold > b_threshold "
                "AND b_threshold > c_threshold AND c_threshold > d_threshold AND d_threshold >= "
                "0))",
                "创建作业表失败");
            execute_static(
                database,
                "CREATE TABLE submissions ("
                "id INTEGER PRIMARY KEY, assignment_id INTEGER NOT NULL REFERENCES assignments(id) "
                "ON "
                "DELETE CASCADE, sequence INTEGER NOT NULL CHECK(sequence > 0), created_at TEXT "
                "NOT NULL "
                "DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')), updated_at TEXT NOT NULL DEFAULT "
                "(strftime('%Y-%m-%dT%H:%M:%fZ','now')), UNIQUE(assignment_id, sequence))",
                "创建提交表失败");
            execute_static(
                database,
                "CREATE TABLE wrong_questions ("
                "submission_id INTEGER NOT NULL REFERENCES submissions(id) ON DELETE CASCADE, "
                "question_number INTEGER NOT NULL CHECK(question_number > 0), "
                "PRIMARY KEY(submission_id, question_number))",
                "创建错题表失败");
            execute_static(database,
                           "CREATE INDEX idx_submissions_assignment ON submissions(assignment_id, "
                           "sequence)",
                           "创建索引失败");
            {
                Statement record(database, "INSERT INTO schema_version(version) VALUES (?)");
                record.bind_int(1, 1);
                record.execute();
            }
            current_version = 1;
        }
        if (current_version == 1) {
            execute_static(database,
                           "ALTER TABLE assignments ADD COLUMN main_question_count INTEGER NOT "
                           "NULL DEFAULT 1 CHECK(main_question_count > 0)",
                           "为作业表增加大题数失败");
            execute_static(database, "UPDATE assignments SET main_question_count = total_questions",
                           "迁移作业大题数失败");
            execute_static(
                database,
                "CREATE TABLE question_units ("
                "id INTEGER PRIMARY KEY, assignment_id INTEGER NOT NULL REFERENCES "
                "assignments(id) ON DELETE CASCADE, major_number INTEGER NOT NULL "
                "CHECK(major_number > 0), part_number INTEGER NOT NULL CHECK(part_number >= 0), "
                "sort_order INTEGER NOT NULL CHECK(sort_order > 0), "
                "UNIQUE(assignment_id, major_number, part_number), "
                "UNIQUE(assignment_id, sort_order))",
                "创建计分单位表失败");
            execute_static(
                database,
                "WITH RECURSIVE generated(assignment_id, number, maximum) AS ("
                "SELECT id, 1, total_questions FROM assignments "
                "UNION ALL SELECT assignment_id, number + 1, maximum FROM generated WHERE "
                "number < maximum) "
                "INSERT INTO question_units(assignment_id, major_number, part_number, "
                "sort_order) SELECT assignment_id, number, 0, number FROM generated",
                "迁移旧作业题目结构失败");
            execute_static(
                database,
                "CREATE TABLE wrong_question_units ("
                "submission_id INTEGER NOT NULL REFERENCES submissions(id) ON DELETE CASCADE, "
                "question_unit_id INTEGER NOT NULL REFERENCES question_units(id) ON DELETE "
                "CASCADE, PRIMARY KEY(submission_id, question_unit_id))",
                "创建小问错题表失败");
            execute_static(database,
                           "INSERT INTO wrong_question_units(submission_id, question_unit_id) "
                           "SELECT w.submission_id, q.id FROM wrong_questions w "
                           "JOIN submissions s ON s.id = w.submission_id "
                           "JOIN question_units q ON q.assignment_id = s.assignment_id "
                           "AND q.major_number = w.question_number AND q.part_number = 0",
                           "迁移旧错题数据失败");
            execute_static(database, "DROP TABLE wrong_questions", "移除旧错题表失败");
            execute_static(database,
                           "CREATE INDEX idx_question_units_assignment ON "
                           "question_units(assignment_id, sort_order)",
                           "创建计分单位索引失败");
            {
                Statement record(database, "INSERT INTO schema_version(version) VALUES (?)");
                record.bind_int(1, 2);
                record.execute();
            }
        }
        transaction.commit();
    }

    sqlite3* database{};
};

Database::Database(const std::filesystem::path& path) : impl_(std::make_unique<Impl>(path)) {}
Database::~Database() = default;
Database::Database(Database&&) noexcept = default;
Database& Database::operator=(Database&&) noexcept = default;

Id Database::create_assignment(const Assignment& assignment) {
    std::string error;
    if (!valid_assignment_config(assignment, &error)) {
        throw std::invalid_argument(error);
    }
    Transaction transaction(impl_->database);
    Id id{};
    {
        Statement statement(
            impl_->database,
            "INSERT INTO assignments(name, total_questions, total_students, a_plus_threshold, "
            "a_threshold, b_threshold, c_threshold, d_threshold, main_question_count) VALUES "
            "(?, ?, ?, ?, ?, ?, ?, ?, ?)");
        statement.bind(1, assignment.name);
        statement.bind_int(2, scoring_unit_count(assignment));
        statement.bind_int(3, assignment.total_students);
        statement.bind_int(4, assignment.thresholds.a_plus);
        statement.bind_int(5, assignment.thresholds.a);
        statement.bind_int(6, assignment.thresholds.b);
        statement.bind_int(7, assignment.thresholds.c);
        statement.bind_int(8, assignment.thresholds.d);
        statement.bind_int(9, assignment.main_question_count);
        statement.execute();
        id = sqlite3_last_insert_rowid(impl_->database);
    }
    {
        Statement insert_unit(
            impl_->database,
            "INSERT INTO question_units(assignment_id, major_number, part_number, sort_order) "
            "VALUES (?, ?, ?, ?)");
        int sort_order = 1;
        for (const auto& unit : assignment.question_units) {
            insert_unit.bind(1, id);
            insert_unit.bind_int(2, unit.reference.major_number);
            insert_unit.bind_int(3, unit.reference.part_number);
            insert_unit.bind_int(4, sort_order);
            insert_unit.execute();
            insert_unit.reset();
            ++sort_order;
        }
    }
    transaction.commit();
    return id;
}

bool Database::delete_assignment(Id id) {
    Transaction transaction(impl_->database);
    int deleted{};
    {
        Statement statement(impl_->database, "DELETE FROM assignments WHERE id = ?");
        statement.bind(1, id);
        statement.execute();
        deleted = sqlite3_changes(impl_->database);
    }
    transaction.commit();
    return deleted > 0;
}

std::vector<Assignment> Database::list_assignments() const {
    Statement statement(impl_->database,
                        "SELECT id, name, main_question_count, total_students, a_plus_threshold, "
                        "a_threshold, b_threshold, c_threshold, d_threshold, created_at FROM "
                        "assignments ORDER BY id");
    std::vector<Assignment> assignments;
    while (statement.step_row()) {
        auto assignment = read_assignment(statement);
        load_question_units(impl_->database, assignment);
        assignments.push_back(std::move(assignment));
    }
    return assignments;
}

std::optional<Assignment> Database::get_assignment(Id id) const {
    Statement statement(impl_->database,
                        "SELECT id, name, main_question_count, total_students, a_plus_threshold, "
                        "a_threshold, b_threshold, c_threshold, d_threshold, created_at FROM "
                        "assignments WHERE id = ?");
    statement.bind(1, id);
    if (!statement.step_row()) {
        return std::nullopt;
    }
    auto assignment = read_assignment(statement);
    load_question_units(impl_->database, assignment);
    return assignment;
}

int Database::submission_count(Id assignment_id) const {
    Statement statement(impl_->database,
                        "SELECT COUNT(*) FROM submissions WHERE assignment_id = ?");
    statement.bind(1, assignment_id);
    if (!statement.step_row()) {
        throw DatabaseError("读取已录入人数失败");
    }
    return statement.column_int(0);
}

Id Database::add_submission(Id assignment_id,
                            const std::vector<QuestionReference>& wrong_questions) {
    Transaction transaction(impl_->database);
    int total_students{};
    {
        Statement assignment_statement(impl_->database,
                                       "SELECT total_students FROM assignments WHERE id = ?");
        assignment_statement.bind(1, assignment_id);
        if (!assignment_statement.step_row()) {
            throw std::invalid_argument("作业不存在。");
        }
        total_students = assignment_statement.column_int(0);
    }

    int count{};
    int next_sequence{};
    {
        Statement count_statement(
            impl_->database,
            "SELECT COUNT(*), COALESCE(MAX(sequence), 0) + 1 FROM submissions "
            "WHERE assignment_id = ?");
        count_statement.bind(1, assignment_id);
        if (!count_statement.step_row()) {
            throw DatabaseError("读取已录入人数失败");
        }
        count = count_statement.column_int(0);
        next_sequence = count_statement.column_int(1);
    }
    if (count >= total_students) {
        throw std::invalid_argument("已达到总学生数，不能继续新增。");
    }

    const auto assignment = get_assignment(assignment_id);
    if (!assignment.has_value()) {
        throw std::invalid_argument("作业不存在。");
    }
    const auto sorted = validate_wrong_questions(*assignment, wrong_questions);

    Id submission_id{};
    {
        Statement insert(impl_->database,
                         "INSERT INTO submissions(assignment_id, sequence) VALUES (?, ?)");
        insert.bind(1, assignment_id);
        insert.bind_int(2, next_sequence);
        insert.execute();
        submission_id = sqlite3_last_insert_rowid(impl_->database);
    }
    {
        Statement insert_wrong(
            impl_->database,
            "INSERT INTO wrong_question_units(submission_id, question_unit_id) VALUES (?, ?)");
        for (const auto& question : sorted) {
            insert_wrong.bind(1, submission_id);
            insert_wrong.bind(2, find_question_unit_id(impl_->database, assignment_id, question));
            insert_wrong.execute();
            insert_wrong.reset();
        }
    }
    transaction.commit();
    return submission_id;
}

std::vector<Submission> Database::list_submissions(Id assignment_id) const {
    Statement statement(
        impl_->database,
        "SELECT id, assignment_id, sequence, created_at, updated_at FROM submissions "
        "WHERE assignment_id = ? ORDER BY sequence");
    statement.bind(1, assignment_id);
    std::vector<Submission> submissions;
    while (statement.step_row()) {
        Submission submission;
        submission.id = statement.column_id(0);
        submission.assignment_id = statement.column_id(1);
        submission.sequence = statement.column_int(2);
        submission.created_at = statement.column_text(3);
        submission.updated_at = statement.column_text(4);

        Statement wrong_statement(
            impl_->database,
            "SELECT q.major_number, q.part_number FROM wrong_question_units w JOIN "
            "question_units q ON q.id = w.question_unit_id WHERE w.submission_id = ? ORDER BY "
            "q.sort_order");
        wrong_statement.bind(1, submission.id);
        while (wrong_statement.step_row()) {
            submission.wrong_questions.push_back(
                {wrong_statement.column_int(0), wrong_statement.column_int(1)});
        }
        submissions.push_back(std::move(submission));
    }
    return submissions;
}

std::optional<Submission> Database::get_submission_by_reverse_position(Id assignment_id,
                                                                       int reverse_position) const {
    if (reverse_position <= 0) {
        return std::nullopt;
    }
    Statement statement(
        impl_->database,
        "SELECT id, assignment_id, sequence, created_at, updated_at FROM submissions WHERE "
        "assignment_id = ? ORDER BY sequence DESC LIMIT 1 OFFSET ?");
    statement.bind(1, assignment_id);
    statement.bind_int(2, reverse_position - 1);
    if (!statement.step_row()) {
        return std::nullopt;
    }
    Submission submission;
    submission.id = statement.column_id(0);
    submission.assignment_id = statement.column_id(1);
    submission.sequence = statement.column_int(2);
    submission.created_at = statement.column_text(3);
    submission.updated_at = statement.column_text(4);
    Statement wrong_statement(
        impl_->database,
        "SELECT q.major_number, q.part_number FROM wrong_question_units w JOIN question_units q "
        "ON q.id = w.question_unit_id WHERE w.submission_id = ? ORDER BY q.sort_order");
    wrong_statement.bind(1, submission.id);
    while (wrong_statement.step_row()) {
        submission.wrong_questions.push_back(
            {wrong_statement.column_int(0), wrong_statement.column_int(1)});
    }
    return submission;
}

void Database::update_submission(Id submission_id,
                                 const std::vector<QuestionReference>& wrong_questions) {
    Transaction transaction(impl_->database);
    Id assignment_id{};
    {
        Statement bounds(impl_->database, "SELECT assignment_id FROM submissions WHERE id = ?");
        bounds.bind(1, submission_id);
        if (!bounds.step_row()) {
            throw std::invalid_argument("学生记录不存在。");
        }
        assignment_id = bounds.column_id(0);
    }
    const auto assignment = get_assignment(assignment_id);
    if (!assignment.has_value()) {
        throw std::invalid_argument("作业不存在。");
    }
    const auto sorted = validate_wrong_questions(*assignment, wrong_questions);

    {
        Statement remove(impl_->database,
                         "DELETE FROM wrong_question_units WHERE submission_id = ?");
        remove.bind(1, submission_id);
        remove.execute();
    }
    {
        Statement insert_wrong(
            impl_->database,
            "INSERT INTO wrong_question_units(submission_id, question_unit_id) VALUES (?, ?)");
        for (const auto& question : sorted) {
            insert_wrong.bind(1, submission_id);
            insert_wrong.bind(2, find_question_unit_id(impl_->database, assignment_id, question));
            insert_wrong.execute();
            insert_wrong.reset();
        }
    }
    {
        Statement touch(
            impl_->database,
            "UPDATE submissions SET updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id "
            "= ?");
        touch.bind(1, submission_id);
        touch.execute();
    }
    transaction.commit();
}

bool Database::delete_submission(Id submission_id) {
    Transaction transaction(impl_->database);
    int deleted{};
    {
        Statement statement(impl_->database, "DELETE FROM submissions WHERE id = ?");
        statement.bind(1, submission_id);
        statement.execute();
        deleted = sqlite3_changes(impl_->database);
    }
    transaction.commit();
    return deleted > 0;
}

}  // namespace homework_grader
