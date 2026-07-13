#include "RsvpEngine.h"
#include <unordered_map>
#include <queue>
#include <climits>
#include <algorithm>
#include <cstdio>
#include <functional>

static int FindNodeIdByIp(const std::vector<DeviceNode>& nodes,
                          const std::string& ip)
{
    for (const auto& node : nodes) {
        for (int port = 0; port < PORTS_PER_NODE; ++port) {
            const auto slash = node.portIp[port].find('/');
            const std::string plain = node.portIp[port].substr(0, slash);
            if (plain == ip) return node.id;
        }
    }
    return -1;
}

static int FindNodeIdByRouterId(const std::vector<DeviceNode>& nodes,
                                const std::string& routerId)
{
    for (const auto& node : nodes)
        if (node.routerId == routerId) return node.id;
    return -1;
}

// Returns ordered node IDs [head, ..., tail], or empty if no BW-constrained path exists.
// Walks the head-end router's OSPF LSDBs and prunes links without reservable bandwidth.
static std::vector<int> CspfDijkstra(
    int headId,
    const std::string& destIp,
    uint32_t requiredBw,
    const std::vector<DeviceNode>& nodes,
    const std::unordered_map<uint64_t, uint32_t>& availBwMap,
    std::function<uint64_t(int,int)> cableKey)
{
    const int tailId = FindNodeIdByIp(nodes, destIp);
    if (tailId == -1 || tailId == headId) return {};
    const DeviceNode* head = FindNode(nodes, headId);
    const DeviceNode* tail = FindNode(nodes, tailId);
    if (!head || !tail || head->crashed || tail->crashed ||
        !head->ospfEnabled || !tail->ospfEnabled ||
        head->type != ROUTER || tail->type != ROUTER ||
        head->routerId.empty() || tail->routerId.empty()) {
        return {};
    }

    std::unordered_map<int, std::vector<std::pair<int, int>>> graph;
    for (const auto& areaEntry : head->areaLsdbs) {
        const auto& lsdb = areaEntry.second;
        for (const auto& lsaEntry : lsdb) {
            const RouterLsa& lsa = lsaEntry.second;
            const int fromId = FindNodeIdByRouterId(nodes, lsa.routerId);
            const DeviceNode* from = FindNode(nodes, fromId);
            if (!from || from->crashed || !from->ospfEnabled || from->type != ROUTER)
                continue;

            for (const auto& adjacency : lsa.adjacencies) {
                const int toId = FindNodeIdByRouterId(nodes, adjacency.neighborRouterId);
                const DeviceNode* to = FindNode(nodes, toId);
                if (!to || to->crashed || !to->ospfEnabled || to->type != ROUTER)
                    continue;
                graph[fromId].push_back({toId, std::max(1, adjacency.cost)});
            }
        }
    }

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
        const auto neighbors = graph.find(u);
        if (neighbors == graph.end()) continue;
        for (const auto& edge : neighbors->second) {
            const int v = edge.first;
            // Prune links without enough BW
            auto it = availBwMap.find(cableKey(u, v));
            if (it == availBwMap.end() || it->second < requiredBw) continue;
            int nd = dist[u] + edge.second;
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
            if (c.broken) continue;
            if ((c.fromId == path[i] && c.toId == path[i+1]) ||
                (c.toId   == path[i] && c.fromId == path[i+1])) {
                outPort = (c.fromId == path[i]) ? c.fromPort : c.toPort;
                break;
            }
        }

        if (outPort < 0) continue;

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
        if (c.broken || c.fromPort < 0 || c.fromPort >= PORTS_PER_NODE ||
            c.toPort < 0 || c.toPort >= PORTS_PER_NODE) {
            continue;
        }
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
        if (!n.rsvpEnabled || n.type != ROUTER) continue;
        for (const auto& t : n.teTunnels) {
            if (!t.isUp || t.activePath.size() < 2) continue;
            for (size_t i = 0; i + 1 < t.activePath.size(); ++i) {
                reservedMap[cableKey(t.activePath[i], t.activePath[i+1])] += t.bandwidth;
            }
        }
    }

    auto adjustReservation = [&](const std::vector<int>& path, uint32_t bandwidth,
                                 bool add) {
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            const uint64_t key = cableKey(path[i], path[i + 1]);
            uint32_t& reserved = reservedMap[key];
            if (add) reserved += bandwidth;
            else     reserved = reserved > bandwidth ? reserved - bandwidth : 0u;
        }
    };

    auto availableBandwidth = [&]() {
        std::unordered_map<uint64_t, uint32_t> available;
        for (const auto& [key, maximum] : maxBwMap) {
            const auto reserved = reservedMap.find(key);
            const uint32_t used = reserved != reservedMap.end() ? reserved->second : 0u;
            available[key] = used < maximum ? maximum - used : 0u;
        }
        return available;
    };

    // Transit entries are shared state. Clear them once before any head-end
    // rebuilds its tunnels so a later router cannot erase an earlier path.
    for (auto& node : nodes) node.teLfib.clear();

    // ── Phase 2: compute tunnel states ───────────────────────────────────
    for (auto& n : nodes) {
        if (!n.rsvpEnabled || n.type != ROUTER) continue;

        for (auto& t : n.teTunnels) {
            const std::vector<int> oldPath = t.isUp ? t.activePath : std::vector<int>{};
            if (!oldPath.empty()) adjustReservation(oldPath, t.bandwidth, false);

            const auto availBwMap = availableBandwidth();
            std::vector<int> newPath;
            std::string failureStatus;

            if (!t.useExplicit) {
                // CSPF
                newPath = CspfDijkstra(n.id, t.destIp, t.bandwidth,
                                       nodes, availBwMap, cableKey);
                if (newPath.empty()) failureStatus = "No CSPF path";
            } else {
                // Explicit hops are waypoints; append the configured tail when
                // the user did not repeat it in the hop list.
                const int tailId = FindNodeIdByIp(nodes, t.destIp);
                if (tailId < 0) {
                    failureStatus = "Invalid tunnel destination";
                } else {
                    newPath.push_back(n.id);
                    for (int hop : t.explicitHops) newPath.push_back(hop);
                    if (newPath.back() != tailId) newPath.push_back(tailId);

                    bool ok = true;
                    for (size_t i = 0; i + 1 < newPath.size(); ++i) {
                        auto it = availBwMap.find(cableKey(newPath[i], newPath[i+1]));
                        const DeviceNode* from = FindNode(nodes, newPath[i]);
                        const DeviceNode* to   = FindNode(nodes, newPath[i + 1]);
                        if (!from || !to || from->crashed || to->crashed ||
                            it == availBwMap.end()) {
                            failureStatus = "Invalid explicit hop";
                            ok = false;
                            break;
                        }
                        if (it->second < t.bandwidth) {
                            failureStatus = "BW insufficient on explicit path";
                            ok = false;
                            break;
                        }
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
                bool wasNewTunnel = (t.activePath.empty() && !t.isUp);
                t.activePath = newPath;
                t.isUp       = true;
                t.statusMsg  = "Up";
                n.pendingTunnels.erase(
                    std::remove_if(n.pendingTunnels.begin(), n.pendingTunnels.end(),
                                   [&](const TeTunnel& p){ return p.id == t.id; }),
                    n.pendingTunnels.end());
                BuildTeLfib(t, nodes, cables);
                adjustReservation(t.activePath, t.bandwidth, true);

                if (pathChanged && t.headLabel != 0) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf),
                                  "RSVP-TE: %s Tunnel-%d %s (label %u)",
                                  n.label.c_str(), t.id,
                                  wasNewTunnel ? "up" : "rerouted", t.headLabel);
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
                    adjustReservation(t.activePath, t.bandwidth, true);
                } else if (t.isUp || alreadyPending) {
                    // Second tick (or first tick for pending): go Down
                    t.isUp      = false;
                    t.activePath.clear();
                    t.statusMsg = failureStatus.empty() ? "No valid path" : failureStatus;
                    n.pendingTunnels.erase(
                        std::remove_if(n.pendingTunnels.begin(), n.pendingTunnels.end(),
                                       [&](const TeTunnel& p){ return p.id == t.id; }),
                        n.pendingTunnels.end());
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "RSVP-TE: %s Tunnel-%d down: %s",
                                  n.label.c_str(), t.id, t.statusMsg.c_str());
                    log.push_back(buf);
                } else {
                    t.isUp = false;
                    t.activePath.clear();
                    t.statusMsg = failureStatus.empty() ? "No valid path" : failureStatus;
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

        bool resolved = false;
        for (const auto& n : nodes) {
            bool found = false;
            for (int p = 0; p < PORTS_PER_NODE; ++p) {
                auto sl = n.portIp[p].find('/');
                std::string np = (sl != std::string::npos)
                                 ? n.portIp[p].substr(0, sl) : n.portIp[p];
                if (np == ip) {
                    t.explicitHops.push_back(n.id);
                    found = true;
                    resolved = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!resolved) t.explicitHops.push_back(-1);
    }
}
