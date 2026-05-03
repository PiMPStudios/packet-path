# M4.5 Multi-Area OSPF Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-interface OSPF area assignment so that a router with ports in two different areas becomes an ABR automatically, inter-area routes appear as `O IA` in the routing table, and students can witness the canonical three-router multi-area OSPF teaching scenario.

**Architecture:** Area IDs live on ports (`ospfPortArea[PORTS_PER_NODE]`), not on the router. The per-area LSDB (`areaLsdbs`) replaces the flat `lsdb`. After per-area Dijkstra produces intra-area `O` routes, a `PropagateSummaryRoutes` pass reads ABR neighbors' per-area LSDbs and injects `O IA` entries with `ROUTE_OSPF_IA`.

**Tech Stack:** C++17, raylib 5.5, GNU Make (`make`), macOS. No test framework — verification is build-clean + visual run.

---

## Codebase snapshot (read before starting)

| File | Role |
| ------ | ------ |
| `src/Device.h` | All data structures — the gate for every other file |
| `src/Device.cpp` | `GetRoutingTable`, `IsAbr` (add here) |
| `src/OspfEngine.h` | `UpdateOspf` declaration, `IsAbr` declaration |
| `src/OspfEngine.cpp` | Engine logic — full rewrite of core functions |
| `src/ConfigPanel.h` | `PanelState`, rect helpers |
| `src/ConfigPanel.cpp` | Rect implementations, `UpdateTextField` |
| `src/NetworkCanvas.h` | Screen constants, draw function declarations |
| `src/NetworkCanvas.cpp` | All draw functions |
| `src/main.cpp` | Game loop, input, OSPF tick |

Key constants (from `src/NetworkCanvas.h`):

- `CANVAS_W = 1000`, `PANEL_W = 280`, `SCREEN_H = 720`
- `CFG_PORT_Y0 = 272`, `CFG_PORT_STRIDE = 44`
- `PORTS_PER_NODE = 4` (in `Device.h`)

---

## Task 1: Data Model

**Files:**

- Modify: `src/Device.h`
- Modify: `src/Device.cpp`

### What changes and why

- `RouteSource` gets `ROUTE_OSPF_IA` — the new route type for inter-area routes.
- `RouteEntry` gets `uint32_t area = 0` — so SPF can tag each route with the area it came from, enabling `PropagateSummaryRoutes` to distinguish "other area" routes on the ABR.
- `RouterLsa` gets `uint32_t area = 0` — area-scoped LSA.
- `OspfNeighbor` gets `uint32_t area = 0` — records which area the adjacency was formed in (set by the Hello FSM based on the port's `ospfPortArea`).
- `DeviceNode` gets `uint32_t ospfPortArea[PORTS_PER_NODE] = {}` — per-port area, all default 0.
- `DeviceNode.lsdb` becomes `areaLsdbs: unordered_map<uint32_t, unordered_map<string, RouterLsa>>` — per-area LSDB.
- `IsAbr(node)` in Device.cpp — a property of `DeviceNode`, callable from both OspfEngine and NetworkCanvas.

- [ ] **Step 1: Replace `src/Device.h` with the updated version**

Replace the entire file content with:

```cpp
#pragma once
#include "raylib.h"
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <unordered_map>

// ── Device geometry constants ─────────────────────────────────────────────
static const int   PORTS_PER_NODE = 4;
static const float NODE_W         = 120.0f;
static const float NODE_H         =  60.0f;
static const int   NODE_FONT_SZ   =  14;
static const float PORT_RADIUS    =   6.0f;

// ── Routing types ─────────────────────────────────────────────────────────
enum RouteSource { ROUTE_CONNECTED, ROUTE_STATIC, ROUTE_OSPF, ROUTE_OSPF_IA };

struct RouteEntry {
    std::string dest;
    std::string nextHop;
    int         outPort;
    RouteSource src;
    uint32_t    area = 0;   // OSPF area (set by SPF; 0 for non-OSPF routes)
};

// ── OSPF types ────────────────────────────────────────────────────────────
enum OspfState { OSPF_DOWN, OSPF_INIT, OSPF_TWOWAY, OSPF_FULL };

struct OspfAdjacency {
    std::string neighborRouterId;
    int         cost = 1;
};

struct RouterLsa {
    std::string                routerId;
    uint32_t                   area = 0;
    std::vector<OspfAdjacency> adjacencies;
    std::vector<std::string>   networks;   // CIDR subnets in this area
};

struct OspfNeighbor {
    std::string neighborRouterId;
    std::string neighborIp;      // neighbor's port IP on the link facing us
    int         neighborNodeId = -1;
    int         localPort      = -1;
    OspfState   state          = OSPF_DOWN;
    float       deadTimer      = 0.f;
    uint32_t    area           = 0;   // area this adjacency was formed in
};

// ── ARP & log types ───────────────────────────────────────────────────────
enum LogType { LOG_FORWARD, LOG_ARP_REQ, LOG_ARP_REPLY, LOG_ARP_HIT, LOG_OSPF };

struct ArpEvent {
    int         nodeId   = 0;
    std::string ip;
    std::string mac;
    bool        cacheHit = false;
};

struct ForwardResult {
    bool                  success = false;
    std::vector<int>      path;
    std::string           reason;
    std::vector<ArpEvent> arpEvents;
};

struct LogEntry {
    bool        success   = false;
    std::string pathStr;
    std::string reason;
    float       timestamp = 0.f;
    LogType     type      = LOG_FORWARD;
};

// ── Device types & node struct ────────────────────────────────────────────
enum DeviceType { PC, ROUTER, SWITCH };

struct DeviceNode {
    int         id       = 0;
    DeviceType  type     = PC;
    Vector2     position = {0.0f, 0.0f};
    std::string label;
    bool        selected = false;
    std::string mgmtIp;
    std::string portIp[PORTS_PER_NODE];
    std::vector<RouteEntry> staticRoutes;
    std::unordered_map<std::string, std::string> arpTable;
    // OSPF state (routers only)
    bool        ospfEnabled  = false;
    std::string routerId;
    float       helloTimer   = 0.f;
    uint32_t    ospfPortArea[PORTS_PER_NODE] = {};  // area per port, default 0
    std::vector<OspfNeighbor>                                      ospfNeighbors;
    std::unordered_map<uint32_t,
        std::unordered_map<std::string, RouterLsa>>                areaLsdbs;
    std::vector<RouteEntry>                                        ospfRoutes;
};

// ── Device geometry helpers (no draw calls) ───────────────────────────────
Color       GetDeviceColor(DeviceType t);
Rectangle   GetNodeRect(const DeviceNode& n);
Vector2     GetPortPosition(const DeviceNode& n, int port);
std::string GetPortName(DeviceType type, int port);
std::vector<RouteEntry> GetRoutingTable(const DeviceNode& n);
bool        IsAbr(const DeviceNode& node);

// ── IP / MAC utilities (no raylib) ────────────────────────────────────────
std::string NetworkAddress(const std::string& cidr);
bool        IpInSubnet(const std::string& ip, const std::string& subnet);
bool        ValidateIPOnly(const std::string& ip);
int         PrefixLen(const std::string& cidr);
bool        ValidateIP(const std::string& ip);
std::string GetDeviceMac(int id);
```

- [ ] **Step 2: Add `IsAbr` to `src/Device.cpp`**

Add this function at the end of `src/Device.cpp`, after `GetDeviceMac`:

```cpp
bool IsAbr(const DeviceNode& node) {
    uint32_t firstArea = UINT32_MAX;
    for (const auto& nbr : node.ospfNeighbors) {
        if (nbr.state != OSPF_FULL) continue;
        if (firstArea == UINT32_MAX) { firstArea = nbr.area; continue; }
        if (nbr.area != firstArea) return true;
    }
    return false;
}
```

- [ ] **Step 3: Build to confirm no compile errors**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
make 2>&1 | tail -20
```

Expected: compile errors because OspfEngine.cpp still references `node.lsdb`. That's expected — we fix it in Task 2. The goal here is that Device.h and Device.cpp compile clean in isolation. If you see errors only in OspfEngine.cpp and main.cpp (not Device.cpp or Device.h itself), the data model is correct.

- [ ] **Step 4: Commit**

```bash
git add src/Device.h src/Device.cpp
git commit -m "feat(m4.5): data model — ROUTE_OSPF_IA, per-port area, areaLsdbs, IsAbr"
```

---

## Task 2: OspfEngine — area-aware adjacency, per-area LSDB, O IA propagation

**Files:**

- Modify: `src/OspfEngine.h`
- Modify: `src/OspfEngine.cpp`

### What changes and why — OspfEngine

- **Hello FSM**: Before forming an adjacency, both sides must have the same area on their port (`ospfPortArea[localPort] == ospfPortArea[bPort]`). The shared area is recorded on both neighbor structs (`nbrAB.area = linkArea`).
- **`GenerateLsa(node, area)`**: Now area-scoped — adjacencies only from neighbors in that area, networks only from ports assigned to that area.
- **`RebuildAllLsdbs`**: For each router, finds all areas it participates in (ports with valid IPs + FULL neighbor areas), then runs a per-area BFS to build each area's LSDB.
- **`RunSpfArea(self, area, areaLsdb)`**: Dijkstra on one area's LSDB. Tags each produced `RouteEntry` with `area` so `PropagateSummaryRoutes` knows which area it came from.
- **`RunSpf(self)`**: Clears `ospfRoutes`, calls `RunSpfArea` for each area in `areaLsdbs`.
- **`PropagateSummaryRoutes(nodes)`**: For each router, for each FULL neighbor that IsAbr, copies the ABR's `ROUTE_OSPF` entries from *other* areas into this router's `ospfRoutes` as `ROUTE_OSPF_IA`.
- **`IsAbr` declaration in OspfEngine.h**: Declared in Device.h/Device.cpp (Task 1), so OspfEngine just calls it directly.

- [ ] **Step 1: Update `src/OspfEngine.h`**

Replace the entire file with:

```cpp
#pragma once
#include "Device.h"
#include "Cable.h"
#include <vector>
#include <string>

inline constexpr float OSPF_HELLO_INTERVAL = 2.0f;
inline constexpr float OSPF_DEAD_INTERVAL  = 8.0f;

// Called once per frame. Advances Hello timers, runs the adjacency FSM,
// rebuilds per-area LSDbs and runs SPF when adjacency state changes.
// Returns human-readable log events (empty most frames).
std::vector<std::string> UpdateOspf(float dt,
                                    std::vector<DeviceNode>& nodes,
                                    const std::vector<Cable>& cables);
```

- [ ] **Step 2: Replace `src/OspfEngine.cpp` with the full updated version**

Replace the entire file with:

```cpp
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

// ── LSA generation ────────────────────────────────────────────────────────

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

// ── Per-area LSDB rebuild (BFS per area) ─────────────────────────────────

static void RebuildAllLsdbs(std::vector<DeviceNode>& nodes) {
    for (auto& node : nodes) {
        if (!node.ospfEnabled || node.routerId.empty()) continue;
        node.areaLsdbs.clear();

        // Collect all areas this router participates in
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

        // Trace back to find the first hop (direct neighbor of self)
        std::unordered_set<std::string> seen;
        std::string cur = rid;
        while (prev.count(cur) && prev.at(cur) != self.routerId) {
            if (!seen.insert(cur).second) break;
            cur = prev.at(cur);
        }
        const std::string& firstHopRid = cur;

        // Find the neighbor entry for firstHopRid in this area
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

static void RunSpf(DeviceNode& self) {
    self.ospfRoutes.clear();
    if (self.areaLsdbs.empty() || self.routerId.empty()) return;
    for (const auto& [area, areaLsdb] : self.areaLsdbs)
        RunSpfArea(self, area, areaLsdb);
}

// ── Inter-area route propagation (Type-3 summary equivalent) ─────────────

static void PropagateSummaryRoutes(std::vector<DeviceNode>& nodes) {
    for (auto& node : nodes) {
        if (!node.ospfEnabled || node.routerId.empty()) continue;

        for (const auto& nbr : node.ospfNeighbors) {
            if (nbr.state != OSPF_FULL) continue;

            // Find the ABR
            DeviceNode* abr = nullptr;
            for (auto& other : nodes)
                if (other.routerId == nbr.neighborRouterId) { abr = &other; break; }
            if (!abr || !IsAbr(*abr)) continue;

            uint32_t linkArea = nbr.area;

            // Copy ABR's intra-area routes from OTHER areas as O IA
            for (const auto& abrRoute : abr->ospfRoutes) {
                if (abrRoute.src != ROUTE_OSPF)    continue;  // only intra-area
                if (abrRoute.area == linkArea)      continue;  // same area — not inter-area

                // Skip if we already know any route to this destination
                bool known = false;
                for (const auto& r : node.ospfRoutes)
                    if (r.dest == abrRoute.dest) { known = true; break; }
                if (known) continue;

                node.ospfRoutes.push_back({abrRoute.dest, nbr.neighborIp, nbr.localPort,
                                           ROUTE_OSPF_IA, abrRoute.area});
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

            // Area check — ports must be in the same area to form adjacency
            uint32_t areaA = nodeA.ospfPortArea[localPort];
            DeviceNode* nodeB = FindNodeMut(nodes, bId);
            if (!nodeB || !nodeB->ospfEnabled || nodeB->type != ROUTER
                       || nodeB->routerId.empty()) continue;
            uint32_t areaB = nodeB->ospfPortArea[bPort];
            if (areaA != areaB) continue;   // area mismatch — no adjacency
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
            OspfState stateBA_current = OSPF_DOWN;
            {
                OspfNeighbor* tmp = findNeighbor(*nodeB, nodeA.id);
                if (tmp) stateBA_current = tmp->state;
            }

            if (stateBA_current >= OSPF_INIT) {
                if (nbrAB.state == OSPF_INIT) nbrAB.state = OSPF_TWOWAY;
            }
            if (nbrAB.state == OSPF_TWOWAY) nbrAB.state = OSPF_FULL;

            // Update B's record of A (may push_back — raw pointer already out of scope)
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
```

- [ ] **Step 3: Build and check for compile errors in OspfEngine**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
make 2>&1 | grep -E "error:|warning:" | head -30
```

Expected: errors in `main.cpp` only (`selNode->lsdb` references). OspfEngine.cpp should compile clean. If OspfEngine.cpp has errors, fix them before continuing.

- [ ] **Step 4: Commit**

```bash
git add src/OspfEngine.h src/OspfEngine.cpp
git commit -m "feat(m4.5): area-aware OSPF engine — per-area LSDB, ABR detection, O IA propagation"
```

---

## Task 3: UI + Wiring

**Files:**

- Modify: `src/ConfigPanel.h`
- Modify: `src/ConfigPanel.cpp`
- Modify: `src/NetworkCanvas.cpp`
- Modify: `src/main.cpp`

### What changes and why — UI and Wiring

- **Config tab**: Each port row gains a small area field to the right of the IP field. The IP field shrinks from 188px to 128px to make room. A tiny "A:" label sits between them.
- **OSPF tab**: The hardcoded "Area: 0" line is replaced with an ABR badge (purple pill) when `IsAbr` is true; otherwise shows the current area of the first configured port.
- **OSPF tab neighbor table**: Gets an "Area" column between State and Dead.
- **Routes tab**: `ROUTE_OSPF_IA` renders as "O" + "IA" superscript in orange, distinct from intra-area "O" in amber.
- **main.cpp**: Area field click/focus, area text input with Enter-to-commit, selection-change reset, OSPF disable clears `areaLsdbs` (was `lsdb`).

### Layout math

```text
Panel x=1000 to x=1280 (PANEL_W=280)
Port row total usable: x=1080 to x=1268 (left pad=80, right margin=12)
  Port IP field:  x=1080, width=128, ends=1208
  "A:" label:     x=1210, width≈12
  Area field:     x=1224, width=44, ends=1268
```

- [ ] **Step 1: Update `src/ConfigPanel.h`**

Replace the entire file with:

```cpp
#pragma once
#include "Device.h"
#include "raylib.h"
#include <string>
#include <vector>

enum PanelTab { TAB_CONFIG, TAB_ROUTES, TAB_ARP, TAB_OSPF };

struct PanelState {
    int         activeField         = -1;
    PanelTab    activeTab           = TAB_CONFIG;
    std::string newRouteDest;
    std::string newRouteNext;
    int         activeRouteField    = -1;
    int         activePortAreaField = -1;  // 0..3 = which port's area field is active
    std::string portAreaBuf;               // edit buffer for area number
};

// Layout rect helpers
Rectangle PnlFieldRect(int yOffset);
Rectangle PnlPortFieldRect(int port);
Rectangle PnlPortAreaFieldRect(int port);
float     PnlTabW();
Rectangle PnlConfigTabRect();
Rectangle PnlRoutesTabRect();
Rectangle PnlArpTabRect();
Rectangle PnlOspfTabRect();
Rectangle PnlOspfEnableRect();
Rectangle PnlRouteDeleteRect(int rowIdx);
Rectangle PnlRouteDestRect();
Rectangle PnlRouteNextRect();
Rectangle PnlRouteAddBtnRect();

// Keyboard input handlers (no draw calls)
void UpdateTextField(std::string& text, int maxLen);
void UpdateRoutesTab(DeviceNode* n, PanelState& ps);
```

- [ ] **Step 2: Update `src/ConfigPanel.cpp` — shrink IP field, add area field rect**

Find this function in `src/ConfigPanel.cpp`:

```cpp
Rectangle PnlPortFieldRect(int port) {
    return {(float)(CANVAS_W + 80), (float)(CFG_PORT_Y0 + port * CFG_PORT_STRIDE),
            (float)(PANEL_W - 92), 24.0f};
}
```

Replace it with:

```cpp
Rectangle PnlPortFieldRect(int port) {
    return {(float)(CANVAS_W + 80), (float)(CFG_PORT_Y0 + port * CFG_PORT_STRIDE),
            128.0f, 24.0f};
}

Rectangle PnlPortAreaFieldRect(int port) {
    return {(float)(CANVAS_W + 224), (float)(CFG_PORT_Y0 + port * CFG_PORT_STRIDE),
            44.0f, 24.0f};
}
```

- [ ] **Step 3: Update `DrawConfigTab` in `src/NetworkCanvas.cpp` — add area field per port**

Find the port-drawing loop in `DrawConfigTab` (around line 287):

```cpp
    for (int i = 0; i < PORTS_PER_NODE; ++i) {
        std::string pname = GetPortName(n->type, i);
        DrawTextField(PnlPortFieldRect(i), "", "x.x.x.x/xx",
                      n->portIp[i], ps.activeField == 2 + i, ValidateIP(n->portIp[i]));
        DrawText(pname.c_str(), CANVAS_W + 16, CFG_PORT_Y0 + i * CFG_PORT_STRIDE + 7,
                 11, Color{148, 163, 184, 255});
    }
```

Replace with:

```cpp
    for (int i = 0; i < PORTS_PER_NODE; ++i) {
        std::string pname = GetPortName(n->type, i);
        DrawTextField(PnlPortFieldRect(i), "", "x.x.x.x/xx",
                      n->portIp[i], ps.activeField == 2 + i, ValidateIP(n->portIp[i]));
        DrawText(pname.c_str(), CANVAS_W + 16, CFG_PORT_Y0 + i * CFG_PORT_STRIDE + 7,
                 11, Color{148, 163, 184, 255});
        // Area field
        DrawText("A:", CANVAS_W + 210, CFG_PORT_Y0 + i * CFG_PORT_STRIDE + 7,
                 10, Color{148, 163, 184, 255});
        std::string areaStr = (ps.activePortAreaField == i)
            ? ps.portAreaBuf
            : std::to_string(n->ospfPortArea[i]);
        DrawTextField(PnlPortAreaFieldRect(i), "", "0", areaStr,
                      ps.activePortAreaField == i, true);
    }
```

- [ ] **Step 4: Update `DrawOspfTab` in `src/NetworkCanvas.cpp` — ABR badge, real area display, area column in neighbor table**

Find the section after `if (!n->ospfEnabled) return;` in `DrawOspfTab`:

```cpp
    // Router ID and Area
    int y = 160;
    DrawText("Router ID", CANVAS_W + 12, y, 11, Color{100,116,139,255});
    DrawText(n->routerId.empty() ? "(none)" : n->routerId.c_str(),
             CANVAS_W + 90, y, 11, WHITE);
    y += 20;
    DrawText("Area", CANVAS_W + 12, y, 11, Color{100,116,139,255});
    DrawText("0", CANVAS_W + 90, y, 11, WHITE);
    y += 24;

    // Separator
    DrawLineEx({(float)CANVAS_W, (float)y}, {(float)(CANVAS_W + PANEL_W), (float)y},
               1.0f, PANEL_BORDER);
    y += 6;
    DrawText("Neighbors", CANVAS_W + 12, y, 11, Color{100,116,139,255});
    y += 18;

    if (n->ospfNeighbors.empty()) {
        DrawText("(none)", CANVAS_W + 20, y, 11, Color{71,85,105,255});
        return;
    }

    // Header row
    DrawText("Router-ID",   CANVAS_W + 12,  y, 10, Color{71,85,105,255});
    DrawText("State",       CANVAS_W + 110, y, 10, Color{71,85,105,255});
    DrawText("Dead",        CANVAS_W + 190, y, 10, Color{71,85,105,255});
    y += 14;
```

Replace with:

```cpp
    // Router ID
    int y = 160;
    DrawText("Router ID", CANVAS_W + 12, y, 11, Color{100,116,139,255});
    DrawText(n->routerId.empty() ? "(none)" : n->routerId.c_str(),
             CANVAS_W + 90, y, 11, WHITE);
    y += 20;

    // ABR badge or area display
    if (IsAbr(*n)) {
        DrawRectangleRounded({(float)(CANVAS_W + 90), (float)y, 34.0f, 16.0f},
                             0.5f, 4, Color{139, 92, 246, 255});
        DrawText("ABR", CANVAS_W + 95, y + 3, 10, WHITE);
    } else {
        uint32_t displayArea = 0;
        for (int i = 0; i < PORTS_PER_NODE; ++i)
            if (ValidateIP(n->portIp[i])) { displayArea = n->ospfPortArea[i]; break; }
        DrawText("Area", CANVAS_W + 12, y, 11, Color{100,116,139,255});
        DrawText(std::to_string(displayArea).c_str(), CANVAS_W + 90, y, 11, WHITE);
    }
    y += 24;

    // Separator
    DrawLineEx({(float)CANVAS_W, (float)y}, {(float)(CANVAS_W + PANEL_W), (float)y},
               1.0f, PANEL_BORDER);
    y += 6;
    DrawText("Neighbors", CANVAS_W + 12, y, 11, Color{100,116,139,255});
    y += 18;

    if (n->ospfNeighbors.empty()) {
        DrawText("(none)", CANVAS_W + 20, y, 11, Color{71,85,105,255});
        return;
    }

    // Header row — added Area column
    DrawText("Router-ID",   CANVAS_W + 12,  y, 10, Color{71,85,105,255});
    DrawText("State",       CANVAS_W + 110, y, 10, Color{71,85,105,255});
    DrawText("Area",        CANVAS_W + 175, y, 10, Color{71,85,105,255});
    DrawText("Dead",        CANVAS_W + 218, y, 10, Color{71,85,105,255});
    y += 14;
```

Also find the neighbor row drawing loop (after `y += 14`):

```cpp
    for (const auto& nbr : n->ospfNeighbors) {
        if (y > CANVAS_H - 20) break;
        std::string rid = nbr.neighborRouterId;
        if ((int)rid.size() > 11) rid = rid.substr(rid.size() - 11);
        DrawText(rid.c_str(), CANVAS_W + 12, y, 10, WHITE);

        int si = std::clamp((int)nbr.state, 0, 3);
        DrawText(stateNames[si], CANVAS_W + 110, y, 10, stateColors[si]);

        char deadBuf[8];
        std::snprintf(deadBuf, sizeof(deadBuf), "%.1fs", nbr.deadTimer);
        DrawText(deadBuf, CANVAS_W + 190, y, 10, Color{148,163,184,255});
        y += 16;
    }
```

Replace with:

```cpp
    for (const auto& nbr : n->ospfNeighbors) {
        if (y > CANVAS_H - 20) break;
        std::string rid = nbr.neighborRouterId;
        if ((int)rid.size() > 11) rid = rid.substr(rid.size() - 11);
        DrawText(rid.c_str(), CANVAS_W + 12, y, 10, WHITE);

        int si = std::clamp((int)nbr.state, 0, 3);
        DrawText(stateNames[si], CANVAS_W + 110, y, 10, stateColors[si]);

        DrawText(std::to_string(nbr.area).c_str(), CANVAS_W + 175, y, 10,
                 Color{148, 163, 184, 255});

        char deadBuf[8];
        std::snprintf(deadBuf, sizeof(deadBuf), "%.1fs", nbr.deadTimer);
        DrawText(deadBuf, CANVAS_W + 218, y, 10, Color{148,163,184,255});
        y += 16;
    }
```

- [ ] **Step 5: Update `DrawRoutesTab` in `src/NetworkCanvas.cpp` — add O IA display**

Find the route color/letter block inside the `for (int i = 0; i < displayed; ++i)` loop:

```cpp
            Color rowColor = (r.src == ROUTE_CONNECTED) ? Color{34, 197, 94, 255}
                           : (r.src == ROUTE_OSPF)      ? Color{234, 179, 8, 255}
                                                        : Color{59, 130, 246, 255};

            // Type letter
            const char* typeLetter = (r.src == ROUTE_CONNECTED) ? "C"
                                   : (r.src == ROUTE_OSPF)      ? "O" : "S";
            DrawText(typeLetter, CANVAS_W + 12, ry + 3, 11, rowColor);
```

Replace with:

```cpp
            Color rowColor;
            if      (r.src == ROUTE_CONNECTED) rowColor = Color{34,  197,  94, 255};
            else if (r.src == ROUTE_OSPF)      rowColor = Color{234, 179,   8, 255};
            else if (r.src == ROUTE_OSPF_IA)   rowColor = Color{249, 115,  22, 255};
            else                               rowColor = Color{ 59, 130, 246, 255};

            // Type indicator
            if (r.src == ROUTE_OSPF_IA) {
                DrawText("O",  CANVAS_W + 12, ry + 3, 11, rowColor);
                DrawText("IA", CANVAS_W + 21, ry + 5,  9, rowColor);
            } else {
                const char* typeLetter = (r.src == ROUTE_CONNECTED) ? "C"
                                       : (r.src == ROUTE_OSPF)      ? "O" : "S";
                DrawText(typeLetter, CANVAS_W + 12, ry + 3, 11, rowColor);
            }
```

- [ ] **Step 6: Update `src/main.cpp` — area field input + OSPF disable fix**

**Change 1**: In the Config tab click handler (inside `if (ps.activeTab == TAB_CONFIG)`, around line 323), add the area field clicks. Find:

```cpp
                if (ps.activeTab == TAB_CONFIG) {
                    ps.activeField = -1;
                    if (CheckCollisionPointRec(screenMouse, PnlFieldRect(CFG_HOSTNAME_Y))) ps.activeField = 0;
                    if (CheckCollisionPointRec(screenMouse, PnlFieldRect(CFG_MGMTIP_Y)))  ps.activeField = 1;
                    for (int i = 0; i < PORTS_PER_NODE; ++i)
                        if (CheckCollisionPointRec(screenMouse, PnlPortFieldRect(i)))
                            ps.activeField = 2 + i;
                }
```

Replace with:

```cpp
                if (ps.activeTab == TAB_CONFIG) {
                    ps.activeField         = -1;
                    ps.activePortAreaField = -1;
                    if (CheckCollisionPointRec(screenMouse, PnlFieldRect(CFG_HOSTNAME_Y))) ps.activeField = 0;
                    if (CheckCollisionPointRec(screenMouse, PnlFieldRect(CFG_MGMTIP_Y)))  ps.activeField = 1;
                    DeviceNode* selNode = nullptr;
                    for (auto& nd : nodes)
                        if (nd.id == selectedId) { selNode = &nd; break; }
                    for (int i = 0; i < PORTS_PER_NODE; ++i) {
                        if (CheckCollisionPointRec(screenMouse, PnlPortFieldRect(i)))
                            ps.activeField = 2 + i;
                        if (CheckCollisionPointRec(screenMouse, PnlPortAreaFieldRect(i))) {
                            ps.activePortAreaField = i;
                            if (selNode)
                                ps.portAreaBuf = std::to_string(selNode->ospfPortArea[i]);
                        }
                    }
                }
```

**Change 2**: In the text field update section, add the area field handler. Find:

```cpp
        if (ps.activeTab == TAB_CONFIG && ps.activeField != -1 && selectedId != -1) {
            DeviceNode* selNode = nullptr;
            for (auto& nd : nodes)
                if (nd.id == selectedId) { selNode = &nd; break; }
            if (selNode) {
                if (ps.activeField == 0) UpdateTextField(selNode->label,   32);
                if (ps.activeField == 1) UpdateTextField(selNode->mgmtIp, 18);
                for (int i = 0; i < PORTS_PER_NODE; ++i)
                    if (ps.activeField == 2 + i) UpdateTextField(selNode->portIp[i], 18);
            }
        } else if (ps.activeTab == TAB_ROUTES && ps.activeRouteField != -1 && selectedId != -1) {
```

Replace with:

```cpp
        if (ps.activeTab == TAB_CONFIG && ps.activeField != -1 && selectedId != -1) {
            DeviceNode* selNode = nullptr;
            for (auto& nd : nodes)
                if (nd.id == selectedId) { selNode = &nd; break; }
            if (selNode) {
                if (ps.activeField == 0) UpdateTextField(selNode->label,   32);
                if (ps.activeField == 1) UpdateTextField(selNode->mgmtIp, 18);
                for (int i = 0; i < PORTS_PER_NODE; ++i)
                    if (ps.activeField == 2 + i) UpdateTextField(selNode->portIp[i], 18);
            }
        } else if (ps.activeTab == TAB_CONFIG && ps.activePortAreaField != -1 && selectedId != -1) {
            DeviceNode* selNode = nullptr;
            for (auto& nd : nodes)
                if (nd.id == selectedId) { selNode = &nd; break; }
            if (selNode) {
                UpdateTextField(ps.portAreaBuf, 5);  // area IDs up to 65535 (5 digits)
                if (IsKeyPressed(KEY_ENTER)) {
                    if (!ps.portAreaBuf.empty()) {
                        uint32_t newArea = (uint32_t)std::stoul(ps.portAreaBuf);
                        selNode->ospfPortArea[ps.activePortAreaField] = newArea;
                        // Force OSPF reconvergence — drop all adjacency state
                        if (selNode->ospfEnabled) {
                            selNode->ospfNeighbors.clear();
                            selNode->areaLsdbs.clear();
                            selNode->ospfRoutes.clear();
                        }
                    }
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                } else if (IsKeyPressed(KEY_ESCAPE)) {
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                }
            }
        } else if (ps.activeTab == TAB_ROUTES && ps.activeRouteField != -1 && selectedId != -1) {
```

**Change 3**: In the selection-change reset block, add the area field reset. Find:

```cpp
        if (selectedId != prevSelectedId) {
            ps.activeField      = -1;
            ps.activeTab        = TAB_CONFIG;
            ps.activeRouteField = -1;
            ps.newRouteDest.clear();
            ps.newRouteNext.clear();
            prevSelectedId      = selectedId;
        }
```

Replace with:

```cpp
        if (selectedId != prevSelectedId) {
            ps.activeField         = -1;
            ps.activeTab           = TAB_CONFIG;
            ps.activeRouteField    = -1;
            ps.activePortAreaField = -1;
            ps.portAreaBuf.clear();
            ps.newRouteDest.clear();
            ps.newRouteNext.clear();
            prevSelectedId         = selectedId;
        }
```

**Change 4**: Fix the OSPF disable toggle to use `areaLsdbs` instead of `lsdb`. Find:

```cpp
                        if (CheckCollisionPointRec(screenMouse, PnlOspfEnableRect())) {
                            selNode->ospfEnabled = !selNode->ospfEnabled;
                            if (!selNode->ospfEnabled) {
                                selNode->ospfNeighbors.clear();
                                selNode->lsdb.clear();
                                selNode->ospfRoutes.clear();
                                selNode->helloTimer = 0.f;
                                selNode->routerId.clear();
                            }
                        }
```

Replace with:

```cpp
                        if (CheckCollisionPointRec(screenMouse, PnlOspfEnableRect())) {
                            selNode->ospfEnabled = !selNode->ospfEnabled;
                            if (!selNode->ospfEnabled) {
                                selNode->ospfNeighbors.clear();
                                selNode->areaLsdbs.clear();
                                selNode->ospfRoutes.clear();
                                selNode->helloTimer = 0.f;
                                selNode->routerId.clear();
                            }
                        }
```

- [ ] **Step 7: Build clean**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
make 2>&1
```

Expected: no errors. The binary `Packet-Path` is rebuilt.

If you see `error: 'lsdb' is not a member of 'DeviceNode'`, search for any remaining `lsdb` reference:

```bash
grep -rn "\.lsdb" src/
```

Fix each occurrence by replacing `.lsdb` with `.areaLsdbs`.

- [ ] **Step 8: Commit**

```bash
git add src/ConfigPanel.h src/ConfigPanel.cpp src/NetworkCanvas.cpp src/main.cpp
git commit -m "feat(m4.5): UI wiring — area input fields, ABR badge, O IA routes display"
```

---

## Task 4: Verification

**Files:** None (read-only verification)

This task verifies the three-router teaching scenario end-to-end.

### The scenario

```text
R1 (area 0) ──── R2 (ABR) ──── R3 (area 1)
              Gi0/0 | Gi0/1
```

- R1: Gi0/0 = `10.0.0.1/24`, area 0
- R2: Gi0/0 = `10.0.0.2/24` area 0, Gi0/1 = `10.1.0.1/24` area 1
- R3: Gi0/0 = `10.1.0.2/24`, area 1
- All three: OSPF enabled, router-IDs auto-assigned from port IPs

### Build and run

- [ ] **Step 1: Build and launch**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
make && ./Packet-Path
```

Expected: window opens, FPS counter visible, no crashes.

### Config tab — area fields visible

- [ ] **Step 2: Verify area fields appear**

Press `R` three times to spawn R1, R2, R3. Click R1. In the Config tab, each port row should have:

- IP field (left, ~128px)
- "A:" label
- Small area field (right, ~44px) — showing "0"

If the area field is missing or overlaps the IP field, check `PnlPortFieldRect` and `PnlPortAreaFieldRect` widths in `ConfigPanel.cpp`.

### Configure IPs and areas

- [ ] **Step 3: Configure R1**

Click R1. Config tab:

- Hostname: `R1`
- Gi0/0 IP: `10.0.0.1/24`, Area field: `0` (default, leave as-is)

- [ ] **Step 4: Configure R2 (the ABR)**

Click R2. Config tab:

- Hostname: `R2`
- Gi0/0 IP: `10.0.0.2/24`, Area field: click area field, type `0`, press Enter
- Gi0/1 IP: `10.1.0.1/24`, Area field: click area field, type `1`, press Enter

- [ ] **Step 5: Configure R3**

Click R3. Config tab:

- Hostname: `R3`
- Gi0/0 IP: `10.1.0.2/24`, Area field: click area field, type `1`, press Enter

### Cable and enable OSPF

- [ ] **Step 6: Cable topology**

Drag from R1's Gi0/0 port (top) to R2's Gi0/0 port (right or top). Then drag from R2's Gi0/1 port to R3's Gi0/0 port.

Two cables should appear as gray bezier curves.

- [ ] **Step 7: Enable OSPF on all three**

Click R1 → OSPF tab → click "OSPF: Disabled" button → turns green.
Click R2 → OSPF tab → enable.
Click R3 → OSPF tab → enable.

### Verify adjacency formation

- [ ] **Step 8: Wait for adjacency and check log**

Wait ~4 seconds. Log console should show:

```text
O  OSPF: adjacency FULL 10.0.0.1 <-> 10.0.0.2 (area 0)
O  OSPF: adjacency FULL 10.1.0.1 <-> 10.1.0.2 (area 1)
```

Cable R1–R2 should turn green. Cable R2–R3 should turn green.

- [ ] **Step 9: Verify R2 shows ABR badge**

Click R2 → OSPF tab. The line below Router ID should show a purple "ABR" pill badge. If it still shows "Area: 0", `IsAbr` is not returning true — check that `OspfNeighbor.area` is being set in the Hello FSM.

Neighbor table for R2 should show:

```text
Router-ID    State    Area   Dead
10.0.0.1     FULL     0      7.8s
10.1.0.1     FULL     1      7.8s
```

### Verify routing tables

- [ ] **Step 10: Check R1's routes**

Click R1 → Routes tab. Should show:

```text
C   10.0.0.0/24   direct      Gi0/0
O   10.0.0.0/24   ...         (if R2 has a network — may deduplicate)
O IA 10.1.0.0/24  10.0.0.2    Gi0/0   (inter-area via R2)
```

Key check: `10.1.0.0/24` should appear with orange "O IA" type indicator. If it's missing, `PropagateSummaryRoutes` is not running — check that `anyChange` is true and Phase 4 calls all three functions.

- [ ] **Step 11: Check R3's routes**

Click R3 → Routes tab. Should show:

```text
C    10.1.0.0/24   direct      Gi0/0
O IA 10.0.0.0/24   10.1.0.1   Gi0/0   (inter-area via R2)
```

`10.0.0.0/24` should appear as orange "O IA".

- [ ] **Step 12: Verify area mismatch blocks adjacency**

Change R3's Gi0/0 area from `1` to `2` (click area field, type `2`, Enter). Wait 4 seconds. The R2–R3 cable should turn gray (no adjacency). R2 loses ABR status. R1 loses its O IA route for 10.1.0.0/24.

Restore R3's Gi0/0 area back to `1`. Adjacency and O IA routes return.

- [ ] **Step 13: Commit verification complete**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
git add -p   # stage any incidental fixes from manual testing
git commit -m "feat(m4.5): complete — multi-area OSPF with ABR detection and O IA routes"
```

If no changes were needed during verification, skip the add/commit.

---

## Self-Review

**Spec coverage check:**

- ✅ Per-interface area assignment (`ospfPortArea[PORTS_PER_NODE]`, area field UI)
- ✅ Area-aware Hello FSM (ports must match area)
- ✅ ABR detection (`IsAbr` — FULL neighbors in 2+ areas)
- ✅ Per-area LSDB (`areaLsdbs`, BFS per area)
- ✅ O IA routes (`ROUTE_OSPF_IA`, `PropagateSummaryRoutes`)
- ✅ ABR badge in OSPF tab
- ✅ O IA display in Routes tab (orange, "O" + "IA" superscript)
- ✅ Area column in neighbor table
- ✅ Area mismatch blocks adjacency
- ✅ OSPF disable clears `areaLsdbs`

**Type consistency:**

- `ospfPortArea` is `uint32_t[]` in Device.h; compared as `uint32_t` in OspfEngine.cpp — consistent.
- `RouteEntry.area` is `uint32_t`; set in `RunSpfArea` and `PropagateSummaryRoutes` — consistent.
- `OspfNeighbor.area` is `uint32_t`; set in Hello FSM, read in `RunSpfArea` neighbor lookup — consistent.
- `IsAbr` declared in Device.h, defined in Device.cpp, called in OspfEngine.cpp and NetworkCanvas.cpp — both files include Device.h — consistent.
- `PnlPortAreaFieldRect` declared in ConfigPanel.h, defined in ConfigPanel.cpp, called in NetworkCanvas.cpp and main.cpp — both include ConfigPanel.h via NetworkCanvas.h — consistent.

**No placeholders:** All steps show complete code.
