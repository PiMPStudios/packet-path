#include "RsvpEngine.h"
#include <unordered_map>
#include <queue>
#include <climits>
#include <algorithm>
#include <cstdio>

std::vector<std::string> UpdateRsvp(std::vector<DeviceNode>& nodes,
                                     const std::vector<Cable>& cables)
{
    std::vector<std::string> log;

    // ── Phase 1: build available-BW map keyed by sorted node-pair ────────
    // key = (min_id << 32) | max_id  →  avail Mbps
    auto cableKey = [](int a, int b) -> uint64_t {
        return ((uint64_t)std::min(a,b) << 32) | (uint32_t)std::max(a,b);
    };

    // max capacity per cable = min(portA_bw, portB_bw)
    std::unordered_map<uint64_t, uint32_t> maxBwMap;
    for (const auto& c : cables) {
        const DeviceNode* a = FindNode(nodes, c.fromId);
        const DeviceNode* b = FindNode(nodes, c.toId);
        if (!a || !b) continue;
        uint32_t bwA = a->portBandwidth[c.fromPort];
        uint32_t bwB = b->portBandwidth[c.toPort];
        maxBwMap[cableKey(c.fromId, c.toId)] = std::min(bwA, bwB);
    }

    // sum active tunnel reservations from last tick
    std::unordered_map<uint64_t, uint32_t> reservedMap;
    for (const auto& n : nodes) {
        for (const auto& t : n.teTunnels) {
            if (!t.isUp || t.activePath.size() < 2) continue;
            for (size_t i = 0; i + 1 < t.activePath.size(); ++i) {
                reservedMap[cableKey(t.activePath[i], t.activePath[i+1])] += t.bandwidth;
            }
        }
    }

    // available = max - reserved, clamped to 0
    std::unordered_map<uint64_t, uint32_t> availBwMap;
    for (auto& [key, maxBw] : maxBwMap) {
        uint32_t res = reservedMap.count(key) ? reservedMap[key] : 0u;
        availBwMap[key] = (res < maxBw) ? (maxBw - res) : 0u;
    }

    // Phase 2 placeholder — will be filled in Tasks 4–7
    (void)log;
    return log;
}

void ResolveExplicitHops(TeTunnel& t, const std::vector<DeviceNode>& nodes)
{
    (void)t; (void)nodes;
}
