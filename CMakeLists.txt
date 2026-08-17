cmake_minimum_required(VERSION 3.20)

project(homework_grader VERSION 1.0.0 LANGUAGES CXX)

include(CTest)
find_package(SQLite3 REQUIRED)

add_library(homework_grader_core
    src/database.cpp
    src/grading.cpp
    src/input.cpp
    src/paths.cpp
    src/statistics.cpp
)
target_include_directories(homework_grader_core PUBLIC include)
target_link_libraries(homework_grader_core PUBLIC SQLite::SQLite3)
target_compile_features(homework_grader_core PUBLIC cxx_std_20)

add_executable(homework-grader src/main.cpp src/app.cpp)
target_link_libraries(homework-grader PRIVATE homework_grader_core)

function(enable_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
endfunction()

enable_project_warnings(homework_grader_core)
enable_project_warnings(homework-grader)

if(BUILD_TESTING)
    add_executable(homework_grader_tests tests/tests.cpp src/app.cpp)
    target_link_libraries(homework_grader_tests PRIVATE homework_grader_core)
    enable_project_warnings(homework_grader_tests)
    add_test(NAME homework_grader_tests COMMAND homework_grader_tests)
endif()

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    set(FORMAT_FILES
        include/homework_grader/app.hpp
        include/homework_grader/database.hpp
        include/homework_grader/grading.hpp
        include/homework_grader/input.hpp
        include/homework_grader/model.hpp
        include/homework_grader/paths.hpp
        include/homework_grader/statistics.hpp
        src/app.cpp
        src/database.cpp
        src/grading.cpp
        src/input.cpp
        src/main.cpp
        src/paths.cpp
        src/statistics.cpp
        tests/tests.cpp
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        VERBATIM
    )
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror ${FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        VERBATIM
    )
endif()
