#pragma once

#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "homework_grader/database.hpp"
#include "homework_grader/model.hpp"

namespace homework_grader {

class App {
   public:
    App(Database& database, std::istream& input, std::ostream& output);
    int run();

   private:
    enum class AssignmentAction { back, exit_program };

    Database& database_;
    std::istream& input_;
    std::ostream& output_;
    bool input_closed_{false};

    [[nodiscard]] std::string read_line(const std::string& prompt);
    [[nodiscard]] int read_integer(const std::string& prompt, int minimum, int maximum,
                                   bool allow_back = false);
    [[nodiscard]] bool confirm(const std::string& prompt);
    [[nodiscard]] std::vector<int> read_wrong_questions(int total_questions);
    [[nodiscard]] Assignment read_assignment();
    [[nodiscard]] bool create_assignment_interactive();
    void delete_assignment_interactive();
    [[nodiscard]] bool open_assignment_interactive();
    [[nodiscard]] AssignmentAction assignment_loop(const Assignment& assignment);
    void add_submission_interactive(const Assignment& assignment);
    void update_submission_interactive(const Assignment& assignment);
    void show_assignment_summary(const Assignment& assignment);
    void show_statistics(const Assignment& assignment);
    void show_submission(const Assignment& assignment, const Submission& submission,
                         const std::string& heading);
};

}  // namespace homework_grader
