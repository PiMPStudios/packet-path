#include "Level.h"
#include "SimulationEngine.h"
#include "UI.h"
#include <algorithm>

void ApplyLevel(const LevelDef& def,
                std::vector<DeviceNode>& nodes,
                std::vector<Cable>& cables,
                int& selectedId) {
    nodes      = def.devices;
    cables     = def.cables;
    selectedId = -1;
    int maxId  = 0;
    for (const auto& n : nodes) maxId = std::max(maxId, n.id);
    SetNextId(maxId + 1);
}

int CheckWinConditions(const LevelDef& def,
                       const std::vector<DeviceNode>& nodes,
                       const std::vector<Cable>& cables) {
    int passed = 0;
    for (const auto& wc : def.winConditions) {
        const DeviceNode* src = nullptr;
        const DeviceNode* dst = nullptr;
        for (const auto& n : nodes) {
            if (n.label == wc.srcLabel) src = &n;
            if (n.label == wc.dstLabel) dst = &n;
        }
        if (!src || !dst) continue;
        std::string dstIp = GetFirstValidIp(*dst);
        if (dstIp.empty()) continue;
        std::string wcsrcIp = GetFirstValidIp(*src);
        ForwardResult fr = SimulateForward(src->id, dstIp, nodes, cables, wcsrcIp);
        if (fr.success) {
            if (!wc.requiresNatOnDevice.empty()) {
                bool natOk = false;
                for (const auto& nd : nodes)
                    if (nd.label == wc.requiresNatOnDevice && nd.natEnabled)
                        { natOk = true; break; }
                if (natOk) ++passed;
            } else {
                ++passed;
            }
        }
    }
    return passed;
}

int ComputeStars(int failedAttempts) {
    if (failedAttempts == 0) return 3;
    if (failedAttempts <= 2) return 2;
    return 1;
}
