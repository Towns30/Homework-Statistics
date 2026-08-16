#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "homework_grader/app.hpp"
#include "homework_grader/database.hpp"
#include "homework_grader/paths.hpp"

int main(int argc, char* argv[]) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            std::cout << homework_grader::usage_text(argv[0]) << '\n';
            return 0;
        }
    }
    try {
        const std::filesystem::path database_path =
            homework_grader::resolve_database_path(argc, argv);
        homework_grader::Database database(database_path);
        std::cout << "数据库：" << database_path << '\n';
        homework_grader::App app(database, std::cin, std::cout);
        return app.run();
    } catch (const std::exception& error) {
        std::cerr << "错误：" << error.what() << '\n';
        return 1;
    }
}
