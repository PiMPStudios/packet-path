#include "Campaign.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unordered_set>

using json = nlohmann::json;

namespace {

bool ContainsCampaignLevel(const CampaignDefinition& campaign, int levelNumber) {
    return FindCampaignChapter(campaign, levelNumber) != nullptr;
}

std::filesystem::path HomeDirectory() {
    if (const char* home = std::getenv("HOME")) return home;
    if (const char* profile = std::getenv("USERPROFILE")) return profile;
    return {};
}

}  // namespace

bool LoadCampaignDefinition(const std::string& path,
                            const std::vector<LevelCatalogEntry>& catalog,
                            CampaignDefinition& out) {
    try {
        std::ifstream input(path);
        if (!input) return false;

        json root;
        input >> root;
        if (!root.is_object() || root.value("version", 0) != 1) return false;

        CampaignDefinition parsed;
        parsed.version  = 1;
        parsed.id       = root.value("id", std::string{});
        parsed.title    = root.value("title", std::string{});
        parsed.subtitle = root.value("subtitle", std::string{});
        if (parsed.id.empty() || parsed.title.empty()) return false;

        const json chapters = root.value("chapters", json::array());
        if (!chapters.is_array() || chapters.empty()) return false;

        std::unordered_set<std::string> chapterIds;
        std::unordered_set<int> campaignLevels;
        for (const auto& chapterJson : chapters) {
            if (!chapterJson.is_object()) return false;

            CampaignChapter chapter;
            chapter.id          = chapterJson.value("id", std::string{});
            chapter.title       = chapterJson.value("title", std::string{});
            chapter.description = chapterJson.value("description", std::string{});
            if (chapter.id.empty() || chapter.title.empty() ||
                !chapterIds.insert(chapter.id).second) return false;

            const json levels = chapterJson.value("levels", json::array());
            if (!levels.is_array() || levels.empty()) return false;
            for (const auto& levelJson : levels) {
                if (!levelJson.is_number_integer()) return false;
                const int levelNumber = levelJson.get<int>();
                if (!FindLevel(catalog, levelNumber) ||
                    !campaignLevels.insert(levelNumber).second) return false;
                chapter.levels.push_back(levelNumber);
            }
            parsed.chapters.push_back(std::move(chapter));
        }

        out = std::move(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool LoadCampaignProgress(const std::string& path, CampaignProgress& out) {
    try {
        std::ifstream input(path);
        if (!input) return false;

        json root;
        input >> root;
        if (!root.is_object() || root.value("version", 0) != 1) return false;

        const json stars = root.value("bestStars", json::object());
        if (!stars.is_object() || stars.size() > 10000) return false;

        CampaignProgress parsed;
        for (const auto& [key, value] : stars.items()) {
            if (!value.is_number_integer()) return false;
            int levelNumber = 0;
            const auto converted =
                std::from_chars(key.data(), key.data() + key.size(), levelNumber);
            const int best = value.get<int>();
            if (converted.ec != std::errc{} ||
                converted.ptr != key.data() + key.size() ||
                levelNumber <= 0 || best < 1 || best > 3) return false;
            parsed.bestStars[levelNumber] = best;
        }

        out = std::move(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool SaveCampaignProgress(const std::string& path,
                          const CampaignProgress& progress) {
    const std::filesystem::path target(path);
    if (target.empty()) return false;

    std::error_code error;
    if (!target.parent_path().empty()) {
        std::filesystem::create_directories(target.parent_path(), error);
        if (error) return false;
    }

    json stars = json::object();
    for (const auto& [levelNumber, best] : progress.bestStars) {
        if (levelNumber > 0 && best >= 1 && best <= 3)
            stars[std::to_string(levelNumber)] = best;
    }
    const json root = {{"version", 1}, {"bestStars", std::move(stars)}};

    std::filesystem::path temporary = target;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) return false;
        output << root.dump(2) << '\n';
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

    error.clear();
    std::filesystem::remove(target, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    error.clear();
    std::filesystem::rename(temporary, target, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

std::string DefaultCampaignProgressPath() {
    if (const char* overridePath = std::getenv("PACKET_PATH_PROGRESS_FILE")) {
        if (*overridePath != '\0') return overridePath;
    }

#if defined(_WIN32)
    if (const char* appData = std::getenv("APPDATA"))
        return (std::filesystem::path(appData) / "PacketPath" /
                "campaign-progress.json").string();
#elif defined(__APPLE__)
    const auto home = HomeDirectory();
    if (!home.empty())
        return (home / "Library" / "Application Support" / "PacketPath" /
                "campaign-progress.json").string();
#else
    if (const char* xdgData = std::getenv("XDG_DATA_HOME"))
        return (std::filesystem::path(xdgData) / "packet-path" /
                "campaign-progress.json").string();
    const auto home = HomeDirectory();
    if (!home.empty())
        return (home / ".local" / "share" / "packet-path" /
                "campaign-progress.json").string();
#endif

    return (std::filesystem::current_path() / "campaign-progress.json").string();
}

std::vector<int> CampaignMissionOrder(const CampaignDefinition& campaign) {
    std::vector<int> result;
    for (const auto& chapter : campaign.chapters)
        result.insert(result.end(), chapter.levels.begin(), chapter.levels.end());
    return result;
}

const CampaignChapter* FindCampaignChapter(const CampaignDefinition& campaign,
                                           int levelNumber) {
    for (const auto& chapter : campaign.chapters) {
        if (std::find(chapter.levels.begin(), chapter.levels.end(), levelNumber) !=
            chapter.levels.end()) return &chapter;
    }
    return nullptr;
}

int CampaignChapterIndex(const CampaignDefinition& campaign, int levelNumber) {
    for (int i = 0; i < static_cast<int>(campaign.chapters.size()); ++i) {
        const auto& levels = campaign.chapters[i].levels;
        if (std::find(levels.begin(), levels.end(), levelNumber) != levels.end())
            return i;
    }
    return -1;
}

int CampaignMissionCount(const CampaignDefinition& campaign) {
    int count = 0;
    for (const auto& chapter : campaign.chapters)
        count += static_cast<int>(chapter.levels.size());
    return count;
}

int CampaignBestStars(const CampaignProgress& progress, int levelNumber) {
    const auto found = progress.bestStars.find(levelNumber);
    return found == progress.bestStars.end() ? 0 : found->second;
}

int CampaignCompletedCount(const CampaignDefinition& campaign,
                           const CampaignProgress& progress) {
    int completed = 0;
    for (const auto& chapter : campaign.chapters)
        for (const int levelNumber : chapter.levels)
            if (CampaignBestStars(progress, levelNumber) > 0) ++completed;
    return completed;
}

int CampaignTotalStars(const CampaignDefinition& campaign,
                       const CampaignProgress& progress) {
    int stars = 0;
    for (const auto& chapter : campaign.chapters)
        for (const int levelNumber : chapter.levels)
            stars += CampaignBestStars(progress, levelNumber);
    return stars;
}

bool IsCampaignLevelUnlocked(const CampaignDefinition& campaign,
                             const CampaignProgress& progress,
                             int levelNumber) {
    int previousLevel = 0;
    for (const auto& chapter : campaign.chapters) {
        for (const int currentLevel : chapter.levels) {
            if (currentLevel == levelNumber)
                return previousLevel == 0 ||
                       CampaignBestStars(progress, previousLevel) > 0;
            previousLevel = currentLevel;
        }
    }
    return false;
}

int FindCampaignContinueLevel(const CampaignDefinition& campaign,
                              const CampaignProgress& progress) {
    for (const auto& chapter : campaign.chapters)
        for (const int levelNumber : chapter.levels)
            if (CampaignBestStars(progress, levelNumber) == 0) return levelNumber;
    return 0;
}

int FindNextCampaignLevel(const CampaignDefinition& campaign,
                          int currentLevel) {
    bool returnNext = false;
    for (const auto& chapter : campaign.chapters) {
        for (const int levelNumber : chapter.levels) {
            if (returnNext) return levelNumber;
            if (levelNumber == currentLevel) returnNext = true;
        }
    }
    return 0;
}

bool RecordCampaignCompletion(const CampaignDefinition& campaign,
                              CampaignProgress& progress,
                              int levelNumber,
                              int stars) {
    if (!ContainsCampaignLevel(campaign, levelNumber) || stars <= 0) return false;
    const int clampedStars = std::min(stars, 3);
    const int previous = CampaignBestStars(progress, levelNumber);
    if (clampedStars <= previous) return false;
    progress.bestStars[levelNumber] = clampedStars;
    return true;
}
