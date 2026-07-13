#pragma once

#include "LevelCatalog.h"

#include <map>
#include <string>
#include <vector>

struct CampaignChapter {
    std::string id;
    std::string title;
    std::string description;
    std::vector<int> levels;
};

struct CampaignDefinition {
    int version = 1;
    std::string id;
    std::string title;
    std::string subtitle;
    std::vector<CampaignChapter> chapters;
};

struct CampaignProgress {
    int version = 1;
    std::map<int, int> bestStars;
};

bool LoadCampaignDefinition(const std::string& path,
                            const std::vector<LevelCatalogEntry>& catalog,
                            CampaignDefinition& out);

bool LoadCampaignProgress(const std::string& path, CampaignProgress& out);
bool SaveCampaignProgress(const std::string& path,
                          const CampaignProgress& progress);
std::string DefaultCampaignProgressPath();

std::vector<int> CampaignMissionOrder(const CampaignDefinition& campaign);
const CampaignChapter* FindCampaignChapter(const CampaignDefinition& campaign,
                                           int levelNumber);
int CampaignChapterIndex(const CampaignDefinition& campaign, int levelNumber);
int CampaignMissionCount(const CampaignDefinition& campaign);
int CampaignCompletedCount(const CampaignDefinition& campaign,
                           const CampaignProgress& progress);
int CampaignTotalStars(const CampaignDefinition& campaign,
                       const CampaignProgress& progress);
int CampaignBestStars(const CampaignProgress& progress, int levelNumber);
bool IsCampaignLevelUnlocked(const CampaignDefinition& campaign,
                             const CampaignProgress& progress,
                             int levelNumber);
int FindCampaignContinueLevel(const CampaignDefinition& campaign,
                              const CampaignProgress& progress);
int FindNextCampaignLevel(const CampaignDefinition& campaign,
                          int currentLevel);
bool RecordCampaignCompletion(const CampaignDefinition& campaign,
                              CampaignProgress& progress,
                              int levelNumber,
                              int stars);
