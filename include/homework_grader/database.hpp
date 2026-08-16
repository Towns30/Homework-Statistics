#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "homework_grader/model.hpp"

namespace homework_grader {

class DatabaseError : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

class Database {
   public:
    explicit Database(const std::filesystem::path& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;

    [[nodiscard]] Id create_assignment(const Assignment& assignment);
    [[nodiscard]] bool delete_assignment(Id id);
    [[nodiscard]] std::vector<Assignment> list_assignments() const;
    [[nodiscard]] std::optional<Assignment> get_assignment(Id id) const;
    [[nodiscard]] int submission_count(Id assignment_id) const;
    [[nodiscard]] Id add_submission(Id assignment_id, const std::vector<int>& wrong_questions);
    [[nodiscard]] std::vector<Submission> list_submissions(Id assignment_id) const;
    [[nodiscard]] std::optional<Submission> get_submission_by_reverse_position(
        Id assignment_id, int reverse_position) const;
    void update_submission(Id submission_id, const std::vector<int>& wrong_questions);

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace homework_grader
