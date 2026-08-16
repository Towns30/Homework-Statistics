#include "homework_grader/input.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <set>

namespace homework_grader {

std::string trim(std::string_view input) {
    const auto is_space = [](unsigned char character) { return std::isspace(character) != 0; };
    auto begin = input.begin();
    while (begin != input.end() && is_space(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    auto end = input.end();
    while (end != begin && is_space(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return {begin, end};
}

std::optional<int> parse_integer(std::string_view input) {
    const std::string cleaned = trim(input);
    if (cleaned.empty()) {
        return std::nullopt;
    }
    int value{};
    const char* begin = cleaned.data();
    const char* end = begin + cleaned.size();
    const auto [position, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || position != end) {
        return std::nullopt;
    }
    return value;
}

WrongQuestionParseResult parse_wrong_questions(std::string_view input, int total_questions) {
    std::string cleaned = trim(input);
    if (cleaned.empty() || cleaned == "无") {
        return {};
    }

    for (char& character : cleaned) {
        if (character == ',') {
            character = ' ';
        }
    }

    std::vector<int> questions;
    std::set<int> seen;
    std::size_t position = 0;
    while (position < cleaned.size()) {
        while (position < cleaned.size() &&
               std::isspace(static_cast<unsigned char>(cleaned[position])) != 0) {
            ++position;
        }
        if (position == cleaned.size()) {
            break;
        }
        const std::size_t token_begin = position;
        while (position < cleaned.size() &&
               std::isspace(static_cast<unsigned char>(cleaned[position])) == 0) {
            ++position;
        }
        const std::string token = cleaned.substr(token_begin, position - token_begin);
        const auto question = parse_integer(token);
        if (!question.has_value()) {
            return {{}, "“" + token + "”不是有效的整数题号。"};
        }
        if (*question < 1 || *question > total_questions) {
            return {{},
                    "题号 " + std::to_string(*question) + " 超出范围（1 ～ " +
                        std::to_string(total_questions) + "）。"};
        }
        if (!seen.insert(*question).second) {
            return {{}, "题号 " + std::to_string(*question) + " 重复，请每题只输入一次。"};
        }
        questions.push_back(*question);
    }

    std::sort(questions.begin(), questions.end());
    return {questions, {}};
}

}  // namespace homework_grader
