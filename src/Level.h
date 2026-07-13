#pragma once
#include "Device.h"
#include "Cable.h"
#include <string>
#include <vector>

struct WinCondition {
    std::string srcLabel;
    std::string dstLabel;
    std::string description;
    bool        requiresFix = false;
    std::string requiresNatOnDevice;  // if non-empty, win also requires this device's natEnabled=true
    std::string requiresTeTunnelOnDevice;
    int         requiresTeTunnelId = 0;       // 0 = any UP tunnel on the required device
    uint32_t    requiresTeMinBandwidth = 0;   // Mbps
    std::string requiresTePathVia;             // optional device label in activePath
    std::string requiresTeMode;                // "cspf", "explicit", or empty
};

struct LevelDef {
    int                       id = 0;
    std::string               title;
    std::string               briefing;
    std::vector<DeviceNode>   devices;
    std::vector<Cable>        cables;
    std::vector<WinCondition> winConditions;
};

void ApplyLevel(const LevelDef& def,
                std::vector<DeviceNode>& nodes,
                std::vector<Cable>& cables,
                int& selectedId);

int CheckWinConditions(const LevelDef& def,
                       const std::vector<DeviceNode>& nodes,
                       const std::vector<Cable>& cables);

int ComputeStars(int failedAttempts);
