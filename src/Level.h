#pragma once
#include "Device.h"
#include "Cable.h"
#include <string>
#include <vector>

struct WinCondition {
    std::string srcLabel;
    std::string dstLabel;
    std::string description;
    bool        requiresFix = false;   // true → win condition requires restoration after injected failure
};

struct LevelDef {
    int                       id = 0;
    std::string               title;
    std::string               briefing;
    std::vector<DeviceNode>   devices;
    std::vector<Cable>        cables;
    std::vector<WinCondition> winConditions;
};

bool LoadLevel(const std::string& path, LevelDef& out);

void ApplyLevel(const LevelDef& def,
                std::vector<DeviceNode>& nodes,
                std::vector<Cable>& cables,
                int& selectedId);

int CheckWinConditions(const LevelDef& def,
                       const std::vector<DeviceNode>& nodes,
                       const std::vector<Cable>& cables);

// Returns 1-3 stars based on failed simulations before winning.
// 0 failures → 3, 1-2 → 2, 3+ → 1.
int ComputeStars(int failedAttempts);
