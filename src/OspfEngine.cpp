#include "OspfEngine.h"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <climits>
#include <algorithm>
#include <cstdio>
#include <cstring>

// ── Helpers ───────────────────────────────────────────────────────────────

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

static RouterLsa GenerateLsa(const DeviceNode& node) {
    RouterLsa lsa;
    lsa.routerId = node.routerId;
    for (const auto& nbr : node.ospfNeighbors)
        if (nbr.state == OSPF_FULL)
            lsa.adjacencies.push_back({nbr.neighborRouterId, 1});
    for (int i = 0; i < PORTS_PER_NODE; ++i)
        if (ValidateIP(node.portIp[i]))
            lsa.networks.push_back(NetworkAddress(node.portIp[i]));
    return lsa;
}

static void RebuildAllLsdbs(std::vector<DeviceNode>& nodes) {
    for (auto& node : nodes) {
        if (!node.ospfEnabled || node.routerId.empty()) continue;
        node.lsdb.clear();

        node.lsdb[node.routerId] = GenerateLsa(node);

        std::unordered_set<std::string> visited;
        std::queue<std::string> q;
        visited.insert(node.routerId);
        q.push(node.routerId);

        while (!q.empty()) {
            std::string rid = q.front(); q.pop();
            RouterLsa lsa = node.lsdb[rid];
            for (const auto& adj : lsa.adjacencies) {
                if (visited.count(adj.neighborRouterId)) continue;
                visited.insert(adj.neighborRouterId);
                for (const auto& other : nodes) {
                    if (other.ospfEnabled && other.routerId == adj.neighborRouterId) {
                        node.lsdb[adj.neighborRouterId] = GenerateLsa(other);
                        q.push(adj.neighborRouterId);
                        break;
                    }
                }
            }
        }
    }
}

static void RunSpf(DeviceNode& self) {
    self.ospfRoutes.clear();
    if (self.lsdb.empty() || self.routerId.empty()) return;

    std::unordered_map<std::string, int>         dist;
    std::unordered_map<std::string, std::string> prev;

    for (const auto& [rid, _] : self.lsdb)
        dist[rid] = INT_MAX;
    dist[self.routerId] = 0;

    using P = std::pair<int, std::string>;
    std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
    pq.push({0, self.routerId});

    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > dist[u]) continue;
        auto it = self.lsdb.find(u);
        if (it == self.lsdb.end()) continue;
        for (const auto& adj : it->second.adjacencies) {
            int nd = d + adj.cost;
            if (nd < dist[adj.neighborRouterId]) {
                dist[adj.neighborRouterId] = nd;
                prev[adj.neighborRouterId] = u;
                pq.push({nd, adj.neighborRouterId});
            }
        }
    }

    for (const auto& [rid, lsa] : self.lsdb) {
        if (rid == self.routerId) continue;
        if (dist[rid] == INT_MAX) continue;

        std::unordered_set<std::string> seen;
        std::string cur = rid;
        while (prev.count(cur) && prev.at(cur) != self.routerId) {
            if (!seen.insert(cur).second) break;
            cur = prev.at(cur);
        }
        const std::string& firstHopRid = cur;

        std::string nextHopIp;
        int         outPort = -1;
        for (const auto& nbr : self.ospfNeighbors) {
            if (nbr.neighborRouterId == firstHopRid) {
                nextHopIp = nbr.neighborIp;
                outPort   = nbr.localPort;
                break;
            }
        }
        if (nextHopIp.empty()) continue;

        for (const auto& net : lsa.networks)
            self.ospfRoutes.push_back({net, nextHopIp, outPort, ROUTE_OSPF});
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

            DeviceNode* nodeB = FindNodeMut(nodes, bId);
            if (!nodeB || !nodeB->ospfEnabled || nodeB->type != ROUTER
                       || nodeB->routerId.empty()) continue;

            // Update A's record of B
            OspfNeighbor& nbrAB = findOrCreateNeighbor(nodeA, bId);
            nbrAB.neighborRouterId = nodeB->routerId;
            nbrAB.neighborIp       = nodeB->portIp[bPort];
            nbrAB.neighborNodeId   = bId;
            nbrAB.localPort        = localPort;
            nbrAB.deadTimer        = OSPF_DEAD_INTERVAL;

            OspfState prevAB = nbrAB.state;
            if (nbrAB.state == OSPF_DOWN) nbrAB.state = OSPF_INIT;

            // Check B's current knowledge of A BEFORE any push_back into nodeB's vector.
            // findNeighbor returns a raw pointer that may be invalidated by push_back,
            // so we snapshot the state here and discard the pointer immediately.
            OspfState stateBA_current = OSPF_DOWN;
            {
                OspfNeighbor* tmp = findNeighbor(*nodeB, nodeA.id);
                if (tmp) stateBA_current = tmp->state;
            }

            // Advance A's state using B's snapshot
            if (stateBA_current >= OSPF_INIT) {
                if (nbrAB.state == OSPF_INIT) nbrAB.state = OSPF_TWOWAY;
            }
            if (nbrAB.state == OSPF_TWOWAY) nbrAB.state = OSPF_FULL;

            // Update B's record of A (may reallocate nodeB->ospfNeighbors — raw pointer
            // nbrBA_snapshot is already out of scope and not used again).
            OspfNeighbor& nbrBA2 = findOrCreateNeighbor(*nodeB, nodeA.id);
            nbrBA2.neighborRouterId = nodeA.routerId;
            nbrBA2.neighborIp       = nodeA.portIp[localPort];
            nbrBA2.neighborNodeId   = nodeA.id;
            nbrBA2.localPort        = bPort;
            nbrBA2.deadTimer        = OSPF_DEAD_INTERVAL;

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
                                  "OSPF: adjacency FULL %s <-> %s",
                                  nodeA.routerId.c_str(), nodeB->routerId.c_str());
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

    // Phase 4: Rebuild LSDB + SPF on any adjacency change
    if (anyChange) {
        RebuildAllLsdbs(nodes);
        for (auto& node : nodes)
            if (node.ospfEnabled && !node.routerId.empty())
                RunSpf(node);
    }

    return events;
}
