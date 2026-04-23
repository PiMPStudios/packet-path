#include "OspfEngine.h"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <climits>
#include <algorithm>
#include <cstdio>
#include <cstring>

// ── Neighbor helpers ──────────────────────────────────────────────────────

static OspfNeighbor& findOrCreateNeighbor(DeviceNode& node, int neighborNodeId) {
    for (auto& nbr : node.ospfNeighbors)
        if (nbr.neighborNodeId == neighborNodeId) return nbr;
    OspfNeighbor fresh;
    fresh.neighborNodeId = neighborNodeId;
    node.ospfNeighbors.push_back(fresh);
    return node.ospfNeighbors.back();
}

static OspfNeighbor* findNeighbor(DeviceNode& node, int neighborNodeId) {
    for (auto& nbr : node.ospfNeighbors)
        if (nbr.neighborNodeId == neighborNodeId) return &nbr;
    return nullptr;
}

// ── LSA generation (area-scoped) ──────────────────────────────────────────

// Generates a Router LSA for `node` scoped to `area`:
// - adjacencies: only FULL neighbors whose OspfNeighbor::area == area
// - networks: only ports where ospfPortArea[i] == area and portIp[i] is valid
static RouterLsa GenerateLsa(const DeviceNode& node, uint32_t area) {
    RouterLsa lsa;
    lsa.routerId = node.routerId;
    lsa.area     = area;
    for (const auto& nbr : node.ospfNeighbors)
        if (nbr.state == OSPF_FULL && nbr.area == area)
            lsa.adjacencies.push_back({nbr.neighborRouterId, 1});
    for (int i = 0; i < PORTS_PER_NODE; ++i)
        if (node.ospfPortArea[i] == area && ValidateIP(node.portIp[i]))
            lsa.networks.push_back(NetworkAddress(node.portIp[i]));
    return lsa;
}

// ── Per-area LSDB rebuild ─────────────────────────────────────────────────

// Rebuilds areaLsdbs for every OSPF-enabled router.
// For each router, finds all areas it participates in (ports with valid IPs
// + areas of FULL neighbors), then BFS-floods each area's LSDB.
static void RebuildAllLsdbs(std::vector<DeviceNode>& nodes) {
    for (auto& node : nodes) {
        if (!node.ospfEnabled || node.routerId.empty()) continue;
        node.areaLsdbs.clear();

        // Collect areas this router is active in
        std::unordered_set<uint32_t> myAreas;
        for (int i = 0; i < PORTS_PER_NODE; ++i)
            if (ValidateIP(node.portIp[i]))
                myAreas.insert(node.ospfPortArea[i]);
        for (const auto& nbr : node.ospfNeighbors)
            if (nbr.state == OSPF_FULL)
                myAreas.insert(nbr.area);

        for (uint32_t area : myAreas) {
            auto& areaLsdb = node.areaLsdbs[area];
            areaLsdb[node.routerId] = GenerateLsa(node, area);

            std::unordered_set<std::string> visited;
            std::queue<std::string>         q;
            visited.insert(node.routerId);
            q.push(node.routerId);

            while (!q.empty()) {
                std::string rid = q.front(); q.pop();
                RouterLsa lsa = areaLsdb[rid];   // value copy — safe across map insertions
                for (const auto& adj : lsa.adjacencies) {
                    if (visited.count(adj.neighborRouterId)) continue;
                    visited.insert(adj.neighborRouterId);
                    for (const auto& other : nodes) {
                        if (other.ospfEnabled && other.routerId == adj.neighborRouterId) {
                            areaLsdb[adj.neighborRouterId] = GenerateLsa(other, area);
                            q.push(adj.neighborRouterId);
                            break;
                        }
                    }
                }
            }
        }
    }
}

// ── Per-area SPF (Dijkstra) ───────────────────────────────────────────────

// Runs Dijkstra on one area's LSDB. Appends ROUTE_OSPF entries to self.ospfRoutes,
// tagged with `area` so PropagateSummaryRoutes can distinguish them later.
static void RunSpfArea(DeviceNode& self, uint32_t area,
                       const std::unordered_map<std::string, RouterLsa>& areaLsdb) {
    std::unordered_map<std::string, int>         dist;
    std::unordered_map<std::string, std::string> prev;

    for (const auto& [rid, _] : areaLsdb)
        dist[rid] = INT_MAX;
    dist[self.routerId] = 0;

    using P = std::pair<int, std::string>;
    std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
    pq.push({0, self.routerId});

    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > dist[u]) continue;
        auto it = areaLsdb.find(u);
        if (it == areaLsdb.end()) continue;
        for (const auto& adj : it->second.adjacencies) {
            auto dit = dist.find(adj.neighborRouterId);
            if (dit == dist.end()) continue;
            int nd = d + adj.cost;
            if (nd < dit->second) {
                dit->second = nd;
                prev[adj.neighborRouterId] = u;
                pq.push({nd, adj.neighborRouterId});
            }
        }
    }

    for (const auto& [rid, lsa] : areaLsdb) {
        if (rid == self.routerId) continue;
        auto dit = dist.find(rid);
        if (dit == dist.end() || dit->second == INT_MAX) continue;

        // Trace first hop back to self
        std::unordered_set<std::string> seen;
        std::string cur = rid;
        while (prev.count(cur) && prev.at(cur) != self.routerId) {
            if (!seen.insert(cur).second) break;
            cur = prev.at(cur);
        }
        const std::string& firstHopRid = cur;

        // Match first hop to a neighbor entry in this area
        std::string nextHopIp;
        int         outPort = -1;
        for (const auto& nbr : self.ospfNeighbors) {
            if (nbr.neighborRouterId == firstHopRid && nbr.area == area) {
                nextHopIp = nbr.neighborIp;
                outPort   = nbr.localPort;
                break;
            }
        }
        if (nextHopIp.empty()) continue;

        for (const auto& net : lsa.networks)
            self.ospfRoutes.push_back({net, nextHopIp, outPort, ROUTE_OSPF, area});
    }
}

// Clears ospfRoutes and runs RunSpfArea for every area in areaLsdbs.
static void RunSpf(DeviceNode& self) {
    self.ospfRoutes.clear();
    if (self.areaLsdbs.empty() || self.routerId.empty()) return;
    for (const auto& [area, areaLsdb] : self.areaLsdbs)
        RunSpfArea(self, area, areaLsdb);
}

// ── Inter-area route propagation ──────────────────────────────────────────

// For each router, finds its FULL ABR neighbors, then copies the ABR's
// intra-area (ROUTE_OSPF) routes from *other* areas into this router's
// ospfRoutes as ROUTE_OSPF_IA.
//
// This models Type-3 Summary LSA flooding: the ABR summarizes area X into
// area Y, and routers in area Y learn O IA routes via the ABR.
//
// Must be called AFTER RunSpf on all nodes (needs ABR's ospfRoutes populated).
static void PropagateSummaryRoutes(std::vector<DeviceNode>& nodes) {
    for (auto& node : nodes) {
        if (!node.ospfEnabled || node.routerId.empty()) continue;

        for (const auto& nbr : node.ospfNeighbors) {
            if (nbr.state != OSPF_FULL) continue;

            DeviceNode* abr = nullptr;
            for (auto& other : nodes)
                if (other.routerId == nbr.neighborRouterId) { abr = &other; break; }
            if (!abr || !IsAbr(*abr)) continue;

            uint32_t linkArea = nbr.area;

            for (const auto& abrRoute : abr->ospfRoutes) {
                if (abrRoute.src != ROUTE_OSPF)  continue;  // only intra-area routes
                if (abrRoute.area == linkArea)   continue;  // same area as our link — not inter-area

                // Skip if we already know any route to this destination
                bool known = false;
                for (const auto& r : node.ospfRoutes)
                    if (r.dest == abrRoute.dest) { known = true; break; }
                if (known) continue;

                node.ospfRoutes.push_back({abrRoute.dest, nbr.neighborIp,
                                           nbr.localPort, ROUTE_OSPF_IA, abrRoute.area});
            }
        }
    }
}

// ── Main engine entry point ────────────────────────────────────────────────

std::vector<std::string> UpdateOspf(float dt,
                                    std::vector<DeviceNode>& nodes,
                                    const std::vector<Cable>& cables)
{
    std::vector<std::string> events;
    bool anyChange = false;

    // Phase 1: Initialize routerIds for newly-enabled routers
    for (auto& node : nodes) {
        if (!node.ospfEnabled || node.type != ROUTER) continue;
        if (!node.routerId.empty()) continue;
        if (ValidateIP(node.mgmtIp)) {
            const char* slash = std::strchr(node.mgmtIp.c_str(), '/');
            node.routerId = slash
                ? node.mgmtIp.substr(0, (size_t)(slash - node.mgmtIp.c_str()))
                : node.mgmtIp;
        } else {
            for (int i = 0; i < PORTS_PER_NODE && node.routerId.empty(); ++i) {
                if (ValidateIP(node.portIp[i])) {
                    const char* slash = std::strchr(node.portIp[i].c_str(), '/');
                    node.routerId = slash
                        ? node.portIp[i].substr(0, (size_t)(slash - node.portIp[i].c_str()))
                        : node.portIp[i];
                }
            }
        }
        if (!node.routerId.empty()) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "OSPF: router-id %s assigned to %s",
                          node.routerId.c_str(), node.label.c_str());
            events.push_back(buf);
        }
    }

    // Phase 2: Hello timer and adjacency FSM
    for (auto& nodeA : nodes) {
        if (!nodeA.ospfEnabled || nodeA.type != ROUTER || nodeA.routerId.empty()) continue;

        nodeA.helloTimer += dt;
        if (nodeA.helloTimer < OSPF_HELLO_INTERVAL) continue;
        nodeA.helloTimer = 0.f;

        for (const auto& cable : cables) {
            int localPort, bPort, bId;
            if (cable.fromId == nodeA.id) {
                localPort = cable.fromPort;
                bPort     = cable.toPort;
                bId       = cable.toId;
            } else if (cable.toId == nodeA.id) {
                localPort = cable.toPort;
                bPort     = cable.fromPort;
                bId       = cable.fromId;
            } else {
                continue;
            }

            if (localPort < 0 || localPort >= PORTS_PER_NODE ||
                bPort    < 0 || bPort    >= PORTS_PER_NODE)
                continue;

            // Area check — both ports must be in the same area to form adjacency
            uint32_t areaA = nodeA.ospfPortArea[localPort];
            DeviceNode* nodeB = FindNodeMut(nodes, bId);
            if (!nodeB || !nodeB->ospfEnabled || nodeB->type != ROUTER
                       || nodeB->routerId.empty()) continue;
            uint32_t areaB = nodeB->ospfPortArea[bPort];
            if (areaA != areaB) continue;
            uint32_t linkArea = areaA;

            // Update A's record of B
            OspfNeighbor& nbrAB = findOrCreateNeighbor(nodeA, bId);
            nbrAB.neighborRouterId = nodeB->routerId;
            nbrAB.neighborIp       = nodeB->portIp[bPort];
            nbrAB.neighborNodeId   = bId;
            nbrAB.localPort        = localPort;
            nbrAB.deadTimer        = OSPF_DEAD_INTERVAL;
            nbrAB.area             = linkArea;

            OspfState prevAB = nbrAB.state;
            if (nbrAB.state == OSPF_DOWN) nbrAB.state = OSPF_INIT;

            // Snapshot B's state before any push_back into nodeB->ospfNeighbors
            // (push_back may reallocate — raw pointer would be dangling)
            OspfState stateBA_current = OSPF_DOWN;
            {
                OspfNeighbor* tmp = findNeighbor(*nodeB, nodeA.id);
                if (tmp) stateBA_current = tmp->state;
            }

            if (stateBA_current >= OSPF_INIT) {
                if (nbrAB.state == OSPF_INIT) nbrAB.state = OSPF_TWOWAY;
            }
            if (nbrAB.state == OSPF_TWOWAY) nbrAB.state = OSPF_FULL;

            // Update B's record of A
            OspfNeighbor& nbrBA2 = findOrCreateNeighbor(*nodeB, nodeA.id);
            nbrBA2.neighborRouterId = nodeA.routerId;
            nbrBA2.neighborIp       = nodeA.portIp[localPort];
            nbrBA2.neighborNodeId   = nodeA.id;
            nbrBA2.localPort        = bPort;
            nbrBA2.deadTimer        = OSPF_DEAD_INTERVAL;
            nbrBA2.area             = linkArea;

            OspfState prevBA = nbrBA2.state;
            if (nbrBA2.state == OSPF_DOWN) nbrBA2.state = OSPF_INIT;
            if (nbrAB.state >= OSPF_INIT) {
                if (nbrBA2.state == OSPF_INIT) nbrBA2.state = OSPF_TWOWAY;
            }
            if (nbrBA2.state == OSPF_TWOWAY) nbrBA2.state = OSPF_FULL;

            if (nbrAB.state != prevAB || nbrBA2.state != prevBA) {
                anyChange = true;
                if (nbrAB.state == OSPF_FULL) {
                    char buf[128];
                    std::snprintf(buf, sizeof(buf),
                                  "OSPF: adjacency FULL %s <-> %s (area %u)",
                                  nodeA.routerId.c_str(), nodeB->routerId.c_str(),
                                  linkArea);
                    events.push_back(buf);
                }
            }
        }
    }

    // Phase 3: Dead timer decay
    for (auto& node : nodes) {
        if (!node.ospfEnabled) continue;
        for (auto& nbr : node.ospfNeighbors) {
            if (nbr.state == OSPF_DOWN) continue;
            nbr.deadTimer -= dt;
            if (nbr.deadTimer <= 0.f) {
                char buf[128];
                std::snprintf(buf, sizeof(buf),
                              "OSPF: adjacency DOWN %s (dead timer expired)",
                              nbr.neighborRouterId.c_str());
                events.push_back(buf);
                nbr.state = OSPF_DOWN;
                anyChange = true;
            }
        }
        node.ospfNeighbors.erase(
            std::remove_if(node.ospfNeighbors.begin(), node.ospfNeighbors.end(),
                           [](const OspfNeighbor& n){ return n.state == OSPF_DOWN; }),
            node.ospfNeighbors.end());
    }

    // Phase 4: Rebuild LSDB + SPF + inter-area propagation on any change
    if (anyChange) {
        RebuildAllLsdbs(nodes);
        for (auto& node : nodes)
            if (node.ospfEnabled && !node.routerId.empty())
                RunSpf(node);
        PropagateSummaryRoutes(nodes);
    }

    return events;
}
