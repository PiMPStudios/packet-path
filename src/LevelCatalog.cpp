#include "LevelCatalog.h"

#include "Level.h"
#include "SceneSerializer.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <regex>
#include <utility>

std::vector<LevelCatalogEntry> DiscoverLevels(const std::string& directory) {
    std::vector<LevelCatalogEntry> result;
    const std::regex levelName(R"(^level_([0-9]+)\.json$)");

    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;

        std::smatch match;
        const std::string filename = it->path().filename().string();
        if (!std::regex_match(filename, match, levelName)) continue;

        const std::string digits = match[1].str();
        int number = 0;
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() ||
            number <= 0) continue;

        LevelDef definition;
        const std::string path = it->path().generic_string();
        if (!LoadLevel(path, definition)) continue;

        LevelCatalogEntry entry;
        entry.number = number;
        entry.path   = path;
        entry.title  = definition.title.empty()
                         ? "Level " + std::to_string(entry.number)
                         : definition.title;
        result.push_back(std::move(entry));
    }

    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.number < rhs.number;
    });
    result.erase(std::unique(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.number == rhs.number;
    }), result.end());
    return result;
}

const LevelCatalogEntry* FindLevel(const std::vector<LevelCatalogEntry>& catalog,
                                   int number) {
    const auto found = std::find_if(catalog.begin(), catalog.end(), [number](const auto& entry) {
        return entry.number == number;
    });
    return found == catalog.end() ? nullptr : &*found;
}

const LevelCatalogEntry* FindNextLevel(const std::vector<LevelCatalogEntry>& catalog,
                                       int currentNumber) {
    const auto found = std::find_if(catalog.begin(), catalog.end(), [currentNumber](const auto& entry) {
        return entry.number > currentNumber;
    });
    return found == catalog.end() ? nullptr : &*found;
}
