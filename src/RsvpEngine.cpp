#include "RsvpEngine.h"
#include <unordered_map>
#include <queue>
#include <climits>
#include <algorithm>
#include <cstdio>
#include <functional>

// Returns ordered node IDs [head, ..., tail], or empty if no BW-constrained path exists.
// Walks the cable graph (not the OSPF LSDB) for simplicity; cost = hop count.
static std::vector<int> CspfDijkstra(
    int headId,
    const std::string& destIp,
    uint32_t requiredBw,
    const std::vector<DeviceNode>& nodes,
    const std::vector<Cable>& cables,
    const std::unordered_map<uint64_t, uint32_t>& availBwMap,
    std::function<uint64_t(int,int)> cableKey)
{
    // Resolve dest IP → node ID
    int tailId = -1;
    for (const auto& n : nodes) {
        for (int p = 0; p < PORTS_PER_NODE; ++p) {
            auto sl = n.portIp[p].find('/');
            std::string plain = (sl != std::string::npos)
                                ? n.portIp[p].substr(0, sl) : n.portIp[p];
            if (plain == destIp) { tailId = n.id; break; }
        }
        if (tailId != -1) break;
    }
    if (tailId == -1 || tailId == headId) return {};

    // Dijkstra: dist + prev maps
    std::unordered_map<int, int> dist, prev;
    for (const auto& n : nodes) dist[n.id] = INT_MAX;
    dist[headId] = 0;

    // min-heap: {cost, nodeId}
    std::priority_queue<std::pair<int,int>,
                        std::vector<std::pair<int,int>>,
                        std::greater<>> pq;
    pq.push({0, headId});

    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > dist[u]) continue;
        for (const auto& c : cables) {
            int v = (c.fromId == u) ? c.toId : (c.toId == u) ? c.fromId : -1;
            if (v < 0) continue;
            const DeviceNode* nbr = FindNode(nodes, v);
            if (!nbr || nbr->crashed) continue;
            // Prune links without enough BW
            auto it = availBwMap.find(cableKey(u, v));
            uint32_t avail = (it != availBwMap.end()) ? it->second : 1000u;
            if (avail < requiredBw) continue;
            int nd = dist[u] + 1;
            if (nd < dist[v]) {
                dist[v] = nd;
                prev[v] = u;
                pq.push({nd, v});
            }
        }
    }

    if (dist[tailId] == INT_MAX) return {};

    std::vector<int> path;
    for (int cur = tailId; cur != headId; ) {
        path.push_back(cur);
        auto it = prev.find(cur);
        if (it == prev.end()) return {};  // disconnected
        cur = it->second;
    }
    path.push_back(headId);
    std::reverse(path.begin(), path.end());
    return path;
}

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
