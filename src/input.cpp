#include "homework_grader/input.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
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

namespace {

std::vector<std::string> tokens(std::string_view input) {
    std::string cleaned = trim(input);
    for (char& character : cleaned) {
        if (character == ',') {
            character = ' ';
        }
    }
    std::vector<std::string> result;
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
        result.push_back(cleaned.substr(token_begin, position - token_begin));
    }
    return result;
}

std::map<int, std::vector<int>> parts_by_major(const Assignment& assignment) {
    std::map<int, std::vector<int>> result;
    for (const auto& unit : assignment.question_units) {
        result[unit.reference.major_number].push_back(unit.reference.part_number);
    }
    return result;
}

}  // namespace

QuestionStructureParseResult parse_question_structure(std::string_view input,
                                                      int main_question_count, int maximum_units) {
    if (main_question_count <= 0 || maximum_units <= 0) {
        return {{}, "大题数和计分单位上限必须是正整数。"};
    }

    std::map<int, int> part_counts;
    for (const auto& token : tokens(input)) {
        const auto equals = token.find('=');
        if (equals == std::string::npos || equals == 0 || equals + 1 >= token.size() ||
            token.find('=', equals + 1) != std::string::npos) {
            return {{}, "“" + token + "”不是有效的小问声明，请使用“大题号=小问数”。"};
        }
        const auto major = parse_integer(token.substr(0, equals));
        const auto count = parse_integer(token.substr(equals + 1));
        if (!major.has_value() || !count.has_value()) {
            return {{}, "“" + token + "”不是有效的小问声明，请使用整数。"};
        }
        if (*major < 1 || *major > main_question_count) {
            return {{},
                    "大题号 " + std::to_string(*major) + " 超出范围（1 ～ " +
                        std::to_string(main_question_count) + "）。"};
        }
        if (*count < 2) {
            return {{}, "第 " + std::to_string(*major) + " 题的小问数必须至少为 2。"};
        }
        if (!part_counts.emplace(*major, *count).second) {
            return {{}, "第 " + std::to_string(*major) + " 题重复声明。"};
        }
    }

    std::int64_t unit_count = main_question_count;
    for (const auto& [major, count] : part_counts) {
        static_cast<void>(major);
        unit_count += static_cast<std::int64_t>(count) - 1;
    }
    if (unit_count > maximum_units) {
        return {{}, "计分单位总数不能超过 " + std::to_string(maximum_units) + "。"};
    }

    std::vector<QuestionUnit> units;
    units.reserve(static_cast<std::size_t>(unit_count));
    for (int major = 1; major <= main_question_count; ++major) {
        const auto found = part_counts.find(major);
        if (found == part_counts.end()) {
            units.push_back({0, {major, 0}});
            continue;
        }
        for (int part = 1; part <= found->second; ++part) {
            units.push_back({0, {major, part}});
        }
    }
    return {units, {}};
}

WrongQuestionParseResult parse_wrong_questions(std::string_view input,
                                               const Assignment& assignment) {
    const std::string cleaned = trim(input);
    if (cleaned.empty() || cleaned == "无") {
        return {};
    }

    const auto structure = parts_by_major(assignment);
    std::set<QuestionReference> questions;
    std::set<int> whole_split_questions;
    for (const auto& token : tokens(cleaned)) {
        const auto dot = token.find('.');
        if (dot != std::string::npos && token.find('.', dot + 1) != std::string::npos) {
            return {{}, "“" + token + "”不是有效的题号。"};
        }
        const std::string major_text = token.substr(0, dot);
        const auto major = parse_integer(major_text);
        if (!major.has_value() || *major < 1 || *major > assignment.main_question_count) {
            return {{},
                    "“" + token + "”中的大题号无效；有效范围为 1 ～ " +
                        std::to_string(assignment.main_question_count) + "。"};
        }
        const auto found = structure.find(*major);
        if (found == structure.end()) {
            return {{}, "第 " + std::to_string(*major) + " 题不存在。"};
        }
        const bool is_split = !found->second.empty() && found->second.front() != 0;

        if (dot == std::string::npos) {
            if (!is_split) {
                if (!questions.insert({*major, 0}).second) {
                    return {{}, "第 " + std::to_string(*major) + " 题重复，请只输入一次。"};
                }
                continue;
            }
            if (!whole_split_questions.insert(*major).second) {
                return {{}, "第 " + std::to_string(*major) + " 题重复，请只输入一次。"};
            }
            for (const int part : found->second) {
                if (questions.contains({*major, part})) {
                    return {{},
                            "第 " + std::to_string(*major) +
                                " 题与已输入的小问重叠，请不要同时输入整题和小问。"};
                }
            }
            for (const int part : found->second) {
                questions.insert({*major, part});
            }
            continue;
        }

        const auto part = parse_integer(token.substr(dot + 1));
        if (!part.has_value() || *part <= 0) {
            return {{}, "“" + token + "”中的小问号无效。"};
        }
        if (!is_split) {
            return {{}, "第 " + std::to_string(*major) + " 题没有小问。"};
        }
        if (std::find(found->second.begin(), found->second.end(), *part) == found->second.end()) {
            return {
                {},
                "第 " + std::to_string(*major) + " 题不存在小问 " + std::to_string(*part) + "。"};
        }
        if (whole_split_questions.contains(*major)) {
            return {{},
                    "第 " + std::to_string(*major) + " 题与其小问重叠，请不要同时输入整题和小问。"};
        }
        if (!questions.insert({*major, *part}).second) {
            return {{},
                    "小问 " + std::to_string(*major) + "." + std::to_string(*part) +
                        " 重复，请只输入一次。"};
        }
    }
    return {{questions.begin(), questions.end()}, {}};
}

}  // namespace homework_grader
