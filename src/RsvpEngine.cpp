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

// Populates node.teLfib entries for a single tunnel whose activePath is set.
// Labels: head uses t.headLabel; each transit hop uses headLabel + hopIdx;
// penultimate egress uses MPLS_IMPLICIT_NULL (PHP).
static void BuildTeLfib(TeTunnel& t, std::vector<DeviceNode>& nodes,
                         const std::vector<Cable>& cables)
{
    const auto& path = t.activePath;
    if (path.size() < 2) return;

    for (size_t i = 0; i + 1 < path.size(); ++i) {
        DeviceNode* cur = nullptr;
        for (auto& n : nodes) { if (n.id == path[i]) { cur = &n; break; } }
        if (!cur) continue;

        uint32_t inLbl  = t.headLabel + (uint32_t)i;
        uint32_t outLbl = (i + 2 == path.size())
                          ? MPLS_IMPLICIT_NULL
                          : t.headLabel + (uint32_t)(i + 1);

        // Find outPort toward path[i+1]
        int outPort = -1;
        for (const auto& c : cables) {
            if ((c.fromId == path[i] && c.toId == path[i+1]) ||
                (c.toId   == path[i] && c.fromId == path[i+1])) {
                outPort = (c.fromId == path[i]) ? c.fromPort : c.toPort;
                break;
            }
        }

        TeLfibEntry entry;
        entry.inLabel  = inLbl;
        entry.outLabel = outLbl;
        entry.outPort  = outPort;
        entry.tunnelId = t.id;
        cur->teLfib[inLbl] = entry;
    }
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

    // ── Phase 2: compute tunnel states ───────────────────────────────────
    for (auto& n : nodes) {
        if (!n.rsvpEnabled || n.type != ROUTER) continue;
        n.teLfib.clear();

        for (auto& t : n.teTunnels) {
            std::vector<int> newPath;

            if (!t.useExplicit) {
                // CSPF
                newPath = CspfDijkstra(n.id, t.destIp, t.bandwidth,
                                       nodes, cables, availBwMap, cableKey);
            } else {
                // Explicit path: prepend head, check each hop has enough BW
                if (!t.explicitHops.empty()) {
                    newPath.push_back(n.id);
                    for (int hop : t.explicitHops) newPath.push_back(hop);
                    bool ok = true;
                    for (size_t i = 0; i + 1 < newPath.size(); ++i) {
                        auto it = availBwMap.find(cableKey(newPath[i], newPath[i+1]));
                        uint32_t avail = (it != availBwMap.end()) ? it->second : 1000u;
                        if (avail < t.bandwidth) { ok = false; break; }
                    }
                    if (!ok) newPath.clear();
                }
            }

            bool pathChanged = (newPath != t.activePath);

            if (!newPath.empty()) {
                // Allocate a new label only when path changes or tunnel is new
                if (t.headLabel == 0 || pathChanged) {
                    t.headLabel = n.nextTeLabel;
                    n.nextTeLabel += 10;  // reserve 10 labels per tunnel (max 9 hops)
                }
                t.activePath = newPath;
                t.isUp       = true;
                t.statusMsg  = "Up";
                n.pendingTunnels.erase(
                    std::remove_if(n.pendingTunnels.begin(), n.pendingTunnels.end(),
                                   [&](const TeTunnel& p){ return p.id == t.id; }),
                    n.pendingTunnels.end());
                BuildTeLfib(t, nodes, cables);

                if (pathChanged && t.headLabel != 0) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf),
                                  "RSVP-TE: %s Tunnel-%d %s (label %u)",
                                  n.label.c_str(), t.id,
                                  pathChanged ? "rerouted" : "up", t.headLabel);
                    log.push_back(buf);
                }
            } else {
                // No path — MBB hold for one tick via pendingTunnels
                bool alreadyPending = false;
                for (const auto& p : n.pendingTunnels)
                    if (p.id == t.id) { alreadyPending = true; break; }

                if (t.isUp && !alreadyPending) {
                    // First tick without path: hold current state one more tick
                    n.pendingTunnels.push_back(t);
                    BuildTeLfib(t, nodes, cables);  // keep forwarding for one tick
                } else {
                    // Second tick: go Down
                    t.isUp      = false;
                    t.activePath.clear();
                    t.statusMsg = t.explicitHops.empty()
                                  ? "No CSPF path" : "BW insufficient on explicit path";
                    n.pendingTunnels.erase(
                        std::remove_if(n.pendingTunnels.begin(), n.pendingTunnels.end(),
                                       [&](const TeTunnel& p){ return p.id == t.id; }),
                        n.pendingTunnels.end());
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "RSVP-TE: %s Tunnel-%d down: %s",
                                  n.label.c_str(), t.id, t.statusMsg.c_str());
                    log.push_back(buf);
                }
            }
        }
    }

    return log;
}

void ResolveExplicitHops(TeTunnel& t, const std::vector<DeviceNode>& nodes)
{
    t.explicitHops.clear();
    for (const auto& rawIp : t.explicitHopIps) {
        // Trim whitespace and strip any mask
        std::string ip = rawIp;
        auto slash = ip.find('/');
        if (slash != std::string::npos) ip = ip.substr(0, slash);
        // Trim leading/trailing spaces
        size_t s = ip.find_first_not_of(' ');
        if (s == std::string::npos) continue;
        ip = ip.substr(s, ip.find_last_not_of(' ') - s + 1);
        if (ip.empty()) continue;

        for (const auto& n : nodes) {
            bool found = false;
            for (int p = 0; p < PORTS_PER_NODE; ++p) {
                auto sl = n.portIp[p].find('/');
                std::string np = (sl != std::string::npos)
                                 ? n.portIp[p].substr(0, sl) : n.portIp[p];
                if (np == ip) { t.explicitHops.push_back(n.id); found = true; break; }
            }
            if (found) break;
        }
    }
}
