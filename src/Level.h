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
    std::string requiresSrPolicyOnDevice;
    int         requiresSrPolicyId = 0;        // 0 = any active policy on the device
    std::vector<std::string> requiresSrSegments;
    std::string requiresSrPathVia;              // optional device label in activePath
    std::string requiresSrv6PolicyOnDevice;
    int         requiresSrv6PolicyId = 0;
    std::vector<std::string> requiresSrv6Segments;
    std::string requiresSrv6PathVia;
    std::string requiresSdwanPolicyOnDevice;
    int         requiresSdwanPolicyId = 0;
    int         requiresSdwanPreferredPort = -1;
    int         requiresSdwanBackupPort = -1;
    int         requiresSdwanSelectedPort = -1;
    float       requiresSdwanMaxLatencyMs = 0.f;
    float       requiresSdwanMaxJitterMs = 0.f;
    float       requiresSdwanMaxLossPct = 0.f;
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
