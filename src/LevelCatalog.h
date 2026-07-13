#pragma once

#include <string>
#include <vector>

struct LevelCatalogEntry {
    int         number = 0;
    std::string path;
    std::string title;
};

// Discovers level_*.json files, validates their metadata, and returns them in
// numeric order. Invalid files are omitted so the selector never offers a
// level that cannot be loaded.
std::vector<LevelCatalogEntry> DiscoverLevels(const std::string& directory);

const LevelCatalogEntry* FindLevel(const std::vector<LevelCatalogEntry>& catalog,
                                   int number);
const LevelCatalogEntry* FindNextLevel(const std::vector<LevelCatalogEntry>& catalog,
                                       int currentNumber);
