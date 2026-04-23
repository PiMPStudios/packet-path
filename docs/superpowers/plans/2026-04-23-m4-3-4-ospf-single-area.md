# M4.3 + M4.4 — OSPF Single-Area Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement simplified 4-state OSPF (Down → Init → 2-Way → Full) with Hello protocol, per-device LSDB, Dijkstra SPF, and a 4th "OSPF" panel tab that shows neighbor state and lets the user enable/disable OSPF per router.

**Architecture:** A standalone `OspfEngine.cpp` owns all OSPF logic (Hello ticks, FSM transitions, LSDB rebuild, SPF). Device.h holds the OSPF state structs and new fields on `DeviceNode`. The engine is called once per frame from `main.cpp`, returns log strings, and mutates `DeviceNode` fields in-place (neighbors, LSDB, ospfRoutes). No DR/BDR election; all links are treated as point-to-point. ExStart/Exchange/Loading states are skipped — 2-Way transitions immediately to Full.

**Tech Stack:** C++17, raylib 5.5, GNU Make (`$(wildcard src/*.cpp)` — adding `OspfEngine.cpp` is sufficient).

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `src/Device.h` | Modify | Add `ROUTE_OSPF`, `OspfState`, `OspfAdjacency`, `RouterLsa`, `OspfNeighbor`; extend `DeviceNode` |
| `src/Device.cpp` | Modify | Append `ospfRoutes` in `GetRoutingTable` |
| `src/Cable.h` | Modify | Declare `FindNodeMut` |
| `src/Cable.cpp` | Modify | Implement `FindNodeMut` |
| `src/OspfEngine.h` | Create | Declare `UpdateOspf` + constants |
| `src/OspfEngine.cpp` | Create | Implement full OSPF engine |
| `src/ConfigPanel.h` | Modify | Add `TAB_OSPF`; declare `PnlOspfTabRect`, `PnlOspfEnableRect` |
| `src/ConfigPanel.cpp` | Modify | Update `PnlTabW` to 4-tab formula; add new rect functions |
| `src/NetworkCanvas.h` | Modify | Declare `DrawOspfTab` |
| `src/NetworkCanvas.cpp` | Modify | Implement `DrawOspfTab`; update `DrawPanel`; update `DrawAllCables` for OSPF cable coloring |
| `src/main.cpp` | Modify | Include OspfEngine; call `UpdateOspf`; add TAB_OSPF click and enable-toggle handlers |

---

## Task 1: Data Model — Device.h, Device.cpp, Cable.h, Cable.cpp

**Files:**
- Modify: `src/Device.h`
- Modify: `src/Device.cpp`
- Modify: `src/Cable.h`
- Modify: `src/Cable.cpp`

No new tests possible without a test harness; verify by `make` succeeding and the app launching.

- [ ] **Step 1: Extend the `RouteSource` enum in Device.h**

In `src/Device.h`, replace line 18:
```cpp
enum RouteSource { ROUTE_CONNECTED, ROUTE_STATIC };
```
with:
```cpp
enum RouteSource { ROUTE_CONNECTED, ROUTE_STATIC, ROUTE_OSPF };
```

- [ ] **Step 2: Add OSPF state + struct declarations to Device.h**

After the `RouteEntry` struct (after line 25 in the current file), insert:
```cpp
// ── OSPF types ────────────────────────────────────────────────────────────
enum OspfState { OSPF_DOWN, OSPF_INIT, OSPF_TWOWAY, OSPF_FULL };

struct OspfAdjacency {
    std::string neighborRouterId;
    int         cost = 1;
};

struct RouterLsa {
    std::string                routerId;
    std::vector<OspfAdjacency> adjacencies;
    std::vector<std::string>   networks;   // CIDR subnets owned by this router
};

struct OspfNeighbor {
    std::string neighborRouterId;
    std::string neighborIp;      // neighbor's port IP on the link facing us (used as next-hop)
    int         neighborNodeId = -1;
    int         localPort      = -1;
    OspfState   state          = OSPF_DOWN;
    float       deadTimer      = 0.f;
};
```

- [ ] **Step 3: Add OSPF fields to DeviceNode**

Inside `struct DeviceNode { ... }` in `src/Device.h`, after the `arpTable` field (currently the last field), add:
```cpp
    // OSPF state (routers only)
    bool        ospfEnabled  = false;
    std::string routerId;
    float       helloTimer   = 0.f;
    std::vector<OspfNeighbor>                  ospfNeighbors;
    std::unordered_map<std::string, RouterLsa> lsdb;
    std::vector<RouteEntry>                    ospfRoutes;
```

- [ ] **Step 4: Update `GetRoutingTable` in Device.cpp to append OSPF routes**

In `src/Device.cpp`, the `GetRoutingTable` function currently ends at line 89. Change the function body to append `ospfRoutes` before returning:

Replace:
```cpp
std::vector<RouteEntry> GetRoutingTable(const DeviceNode& n) {
    std::vector<RouteEntry> table;
    if (ValidateIP(n.mgmtIp))
        table.push_back({NetworkAddress(n.mgmtIp), "direct", -1, ROUTE_CONNECTED});
    for (int i = 0; i < PORTS_PER_NODE; ++i)
        if (ValidateIP(n.portIp[i]))
            table.push_back({NetworkAddress(n.portIp[i]), "direct", i, ROUTE_CONNECTED});
    for (const auto& r : n.staticRoutes)
        table.push_back(r);
    return table;
}
```
with:
```cpp
std::vector<RouteEntry> GetRoutingTable(const DeviceNode& n) {
    std::vector<RouteEntry> table;
    if (ValidateIP(n.mgmtIp))
        table.push_back({NetworkAddress(n.mgmtIp), "direct", -1, ROUTE_CONNECTED});
    for (int i = 0; i < PORTS_PER_NODE; ++i)
        if (ValidateIP(n.portIp[i]))
            table.push_back({NetworkAddress(n.portIp[i]), "direct", i, ROUTE_CONNECTED});
    for (const auto& r : n.staticRoutes)
        table.push_back(r);
    for (const auto& r : n.ospfRoutes)
        table.push_back(r);
    return table;
}
```

- [ ] **Step 5: Add `FindNodeMut` to Cable.h**

In `src/Cable.h`, after the existing `FindNode` declaration (line 10), add:
```cpp
DeviceNode*       FindNodeMut(std::vector<DeviceNode>& nodes, int id);
```

- [ ] **Step 6: Implement `FindNodeMut` in Cable.cpp**

In `src/Cable.cpp`, after the existing `FindNode` implementation, add:
```cpp
DeviceNode* FindNodeMut(std::vector<DeviceNode>& nodes, int id) {
    for (auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}
```

- [ ] **Step 7: Build and verify**

```bash
make
```
Expected: compiles without errors or new warnings. App launches normally.

- [ ] **Step 8: Commit**

```bash
git add src/Device.h src/Device.cpp src/Cable.h src/Cable.cpp
git commit -m "feat(m4.3): add OSPF data model — OspfState, RouterLsa, OspfNeighbor, DeviceNode OSPF fields"
```

---

## Task 2: OspfEngine.h + OspfEngine.cpp

**Files:**
- Create: `src/OspfEngine.h`
- Create: `src/OspfEngine.cpp`

The Makefile uses `$(wildcard src/*.cpp)` — no Makefile change needed.

- [ ] **Step 1: Create OspfEngine.h**

Create `src/OspfEngine.h`:
```cpp
#pragma once
#include "Device.h"
#include "Cable.h"
#include <vector>
#include <string>

static const float OSPF_HELLO_INTERVAL = 2.0f;
static const float OSPF_DEAD_INTERVAL  = 8.0f;

// Called once per frame. Advances Hello timers, runs the adjacency FSM,
// rebuilds LSDbs and runs SPF when adjacency state changes.
// Returns human-readable log events (empty most frames).
std::vector<std::string> UpdateOspf(float dt,
                                    std::vector<DeviceNode>& nodes,
                                    const std::vector<Cable>& cables);
```

- [ ] **Step 2: Create OspfEngine.cpp — helper functions**

Create `src/OspfEngine.cpp` with the following content (build the file in stages across steps 2–6):

```cpp
#include "OspfEngine.h"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <climits>
#include <algorithm>
#include <cstdio>

// ── Helpers ───────────────────────────────────────────────────────────────

// Find or create a neighbor entry in `node`'s ospfNeighbors by neighborNodeId.
static OspfNeighbor& findOrCreateNeighbor(DeviceNode& node, int neighborNodeId) {
    for (auto& nbr : node.ospfNeighbors)
        if (nbr.neighborNodeId == neighborNodeId) return nbr;
    OspfNeighbor fresh;
    fresh.neighborNodeId = neighborNodeId;
    node.ospfNeighbors.push_back(fresh);
    return node.ospfNeighbors.back();
}

// Find an existing neighbor entry (read-only pointer; nullptr if not found).
static OspfNeighbor* findNeighbor(DeviceNode& node, int neighborNodeId) {
    for (auto& nbr : node.ospfNeighbors)
        if (nbr.neighborNodeId == neighborNodeId) return &nbr;
    return nullptr;
}

// Build the Router LSA for one OSPF-enabled router.
// Only FULL adjacencies appear in the adjacency list.
// All non-empty port IPs contribute a directly-connected network.
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

// BFS through the FULL adjacency graph — each OSPF router collects the LSAs
// of every router it can reach and stores them in its own LSDB.
static void RebuildAllLsdbs(std::vector<DeviceNode>& nodes) {
    for (auto& node : nodes) {
        if (!node.ospfEnabled || node.routerId.empty()) continue;
        node.lsdb.clear();

        // Seed with self
        node.lsdb[node.routerId] = GenerateLsa(node);

        std::unordered_set<std::string> visited;
        std::queue<std::string> q;
        visited.insert(node.routerId);
        q.push(node.routerId);

        while (!q.empty()) {
            std::string rid = q.front(); q.pop();
            const RouterLsa& lsa = node.lsdb[rid];
            for (const auto& adj : lsa.adjacencies) {
                if (visited.count(adj.neighborRouterId)) continue;
                visited.insert(adj.neighborRouterId);
                // Find the router with this routerId and copy its LSA
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

// Dijkstra SPF over `self.lsdb`. Produces ROUTE_OSPF entries in self.ospfRoutes.
// Next-hop IP is taken from OspfNeighbor.neighborIp (the neighbor's port facing us).
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
                dist[adj.neighborRouterId]  = nd;
                prev[adj.neighborRouterId]  = u;
                pq.push({nd, adj.neighborRouterId});
            }
        }
    }

    // Generate routes: for each reachable remote router, add routes to its networks.
    for (const auto& [rid, lsa] : self.lsdb) {
        if (rid == self.routerId) continue;
        if (dist[rid] == INT_MAX) continue;

        // Trace path back to find the direct first-hop routerId
        std::string cur = rid;
        while (prev.count(cur) && prev.at(cur) != self.routerId)
            cur = prev.at(cur);
        const std::string& firstHopRid = cur;

        // Map firstHopRid → neighborIp and localPort via ospfNeighbors
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
```

- [ ] **Step 3: Implement `UpdateOspf` — Phase 1 (routerId init) and Phase 2 (Hello FSM)**

Append to `src/OspfEngine.cpp`:

```cpp
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
        // Use mgmtIp host portion, else first valid portIp host portion
        if (ValidateIP(node.mgmtIp)) {
            // Strip prefix — just use the a.b.c.d part
            const char* slash = std::strchr(node.mgmtIp.c_str(), '/');
            node.routerId = slash ? node.mgmtIp.substr(0, slash - node.mgmtIp.c_str())
                                  : node.mgmtIp;
        } else {
            for (int i = 0; i < PORTS_PER_NODE && node.routerId.empty(); ++i) {
                if (ValidateIP(node.portIp[i])) {
                    const char* slash = std::strchr(node.portIp[i].c_str(), '/');
                    node.routerId = slash
                        ? node.portIp[i].substr(0, slash - node.portIp[i].c_str())
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

        // Fire a Hello on every cable connected to nodeA
        for (const auto& cable : cables) {
            int  localPort, bPort;
            int  bId;
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

            DeviceNode* nodeB = FindNodeMut(nodes, bId);
            if (!nodeB || !nodeB->ospfEnabled || nodeB->type != ROUTER
                       || nodeB->routerId.empty()) continue;

            // ── Update A's record of B ────────────────────────────────────
            OspfNeighbor& nbrAB = findOrCreateNeighbor(nodeA, bId);
            nbrAB.neighborRouterId = nodeB->routerId;
            nbrAB.neighborIp       = nodeB->portIp[bPort];  // B's port facing A
            nbrAB.neighborNodeId   = bId;
            nbrAB.localPort        = localPort;
            nbrAB.deadTimer        = OSPF_DEAD_INTERVAL;

            OspfState prevAB = nbrAB.state;
            if (nbrAB.state == OSPF_DOWN) nbrAB.state = OSPF_INIT;

            // Check bidirectionality: does B already know about A?
            OspfNeighbor* nbrBA = findNeighbor(*nodeB, nodeA.id);
            if (nbrBA && nbrBA->state >= OSPF_INIT) {
                if (nbrAB.state == OSPF_INIT) nbrAB.state = OSPF_TWOWAY;
            }
            if (nbrAB.state == OSPF_TWOWAY) nbrAB.state = OSPF_FULL;

            // ── Update B's record of A ────────────────────────────────────
            OspfNeighbor& nbrBA2 = findOrCreateNeighbor(*nodeB, nodeA.id);
            nbrBA2.neighborRouterId = nodeA.routerId;
            nbrBA2.neighborIp       = nodeA.portIp[localPort];  // A's port facing B
            nbrBA2.neighborNodeId   = nodeA.id;
            nbrBA2.localPort        = bPort;
            nbrBA2.deadTimer        = OSPF_DEAD_INTERVAL;

            OspfState prevBA = nbrBA2.state;
            if (nbrBA2.state == OSPF_DOWN) nbrBA2.state = OSPF_INIT;
            // A is >= INIT (we just set it), so B can confirm bidirectionality
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

    // Phase 3: Dead timer decay — remove expired neighbors
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
```

- [ ] **Step 4: Build and verify**

```bash
make
```
Expected: compiles without errors. App launches. No functional change yet — `UpdateOspf` is not called from `main.cpp` yet.

- [ ] **Step 5: Commit**

```bash
git add src/OspfEngine.h src/OspfEngine.cpp
git commit -m "feat(m4.3): add OspfEngine — Hello FSM, LSDB rebuild, Dijkstra SPF"
```

---

## Task 3: main.cpp Integration

**Files:**
- Modify: `src/main.cpp`

Wire the engine into the game loop and panel input handlers.

- [ ] **Step 1: Add `#include "OspfEngine.h"` to main.cpp**

At the top of `src/main.cpp`, among the existing includes, add:
```cpp
#include "OspfEngine.h"
```

- [ ] **Step 2: Call `UpdateOspf` once per frame in the game loop**

Find the frame update section in `main.cpp`. The `UpdatePacketAnim` call happens around line 390. Add the `UpdateOspf` call **before** `UpdatePacketAnim`, and push any returned events to the log:

```cpp
        // OSPF engine tick — runs every frame, fires Hello every 2s
        {
            auto ospfEvents = UpdateOspf(dt, nodes, cables);
            for (const auto& msg : ospfEvents) {
                LogEntry e;
                e.success   = true;
                e.pathStr   = msg;
                e.type      = LOG_FORWARD;
                e.timestamp = GetTime();
                pushLog(e);
            }
        }
```

Find the line `float dt = GetFrameTime();` or the existing `UpdatePacketAnim` call to anchor the insertion point. Place the block immediately before `UpdatePacketAnim`.

- [ ] **Step 3: Add TAB_OSPF click handler**

In the tab click section (currently lines 300–314 in `main.cpp`), after the existing ARP tab handler:
```cpp
                if (CheckCollisionPointRec(screenMouse, PnlArpTabRect())) {
                    ps.activeTab        = TAB_ARP;
                    ps.activeField      = -1;
                    ps.activeRouteField = -1;
                }
```
add:
```cpp
                if (CheckCollisionPointRec(screenMouse, PnlOspfTabRect())) {
                    ps.activeTab        = TAB_OSPF;
                    ps.activeField      = -1;
                    ps.activeRouteField = -1;
                }
```

- [ ] **Step 4: Add OSPF enable/disable toggle handler**

Still in the same `if (!inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))` block, after the Routes tab handler (around line 366), add:

```cpp
                // OSPF tab: enable/disable toggle
                if (ps.activeTab == TAB_OSPF) {
                    DeviceNode* selNode = nullptr;
                    for (auto& nd : nodes)
                        if (nd.id == selectedId) { selNode = &nd; break; }
                    if (selNode && selNode->type == ROUTER) {
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
                    }
                }
```

- [ ] **Step 5: Build and verify**

```bash
make
```
Expected: compiles without errors. App launches. OSPF tab click does nothing visible yet (TAB_OSPF case not handled in DrawPanel). Console may show a compile-time note about `TAB_OSPF` not handled in the switch — this is expected and resolved in Task 4.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m4.3): wire UpdateOspf into game loop — call per frame, push log events"
```

---

## Task 4: Config Panel OSPF Tab UI

**Files:**
- Modify: `src/ConfigPanel.h`
- Modify: `src/ConfigPanel.cpp`
- Modify: `src/NetworkCanvas.h`
- Modify: `src/NetworkCanvas.cpp`

- [ ] **Step 1: Add `TAB_OSPF` to the PanelTab enum in ConfigPanel.h**

In `src/ConfigPanel.h`, replace line 7:
```cpp
enum PanelTab { TAB_CONFIG, TAB_ROUTES, TAB_ARP };
```
with:
```cpp
enum PanelTab { TAB_CONFIG, TAB_ROUTES, TAB_ARP, TAB_OSPF };
```

- [ ] **Step 2: Declare the new rect functions in ConfigPanel.h**

After the existing `PnlArpTabRect()` declaration, add:
```cpp
Rectangle PnlOspfTabRect();
Rectangle PnlOspfEnableRect();
```

- [ ] **Step 3: Update `PnlTabW()` for 4 tabs in ConfigPanel.cpp**

In `src/ConfigPanel.cpp`, replace line 13:
```cpp
float PnlTabW() { return (PANEL_W - 24 - 8) / 3.0f; }
```
with:
```cpp
float PnlTabW() { return (PANEL_W - 24 - 12) / 4.0f; }
```

This changes tab width from ~82.7 px to 61.0 px. The existing `PnlConfigTabRect`, `PnlRoutesTabRect`, and `PnlArpTabRect` all call `PnlTabW()` so they shrink and reposition automatically — no further changes needed to those three functions.

- [ ] **Step 4: Add the new rect functions to ConfigPanel.cpp**

After the `PnlArpTabRect()` implementation in `src/ConfigPanel.cpp`, add:
```cpp
Rectangle PnlOspfTabRect() {
    return {(float)(CANVAS_W + 12) + 3.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlOspfEnableRect() {
    return {(float)(CANVAS_W + 12), 120.0f, (float)(PANEL_W - 24), 26.0f};
}
```

- [ ] **Step 5: Declare `DrawOspfTab` in NetworkCanvas.h**

In `src/NetworkCanvas.h`, after the `DrawArpTab` declaration (line 64), add:
```cpp
void DrawOspfTab(const DeviceNode* n);
```

- [ ] **Step 6: Implement `DrawOspfTab` in NetworkCanvas.cpp**

Add the following function to `src/NetworkCanvas.cpp`, immediately after the `DrawArpTab` function:

```cpp
void DrawOspfTab(const DeviceNode* n) {
    if (!n) {
        DrawText("No device selected", CANVAS_W + 20, 130, 12, Color{100,116,139,255});
        return;
    }
    if (n->type != ROUTER) {
        DrawText("OSPF: routers only", CANVAS_W + 20, 130, 12, Color{100,116,139,255});
        return;
    }

    // Enable/disable button
    Rectangle btn = PnlOspfEnableRect();
    Color btnColor = n->ospfEnabled ? Color{34,197,94,255} : Color{51,65,85,255};
    DrawRectangleRec(btn, btnColor);
    DrawRectangleLinesEx(btn, 1.0f, Color{71,85,105,255});
    const char* btnLabel = n->ospfEnabled ? "OSPF: Enabled" : "OSPF: Disabled";
    int tw = MeasureText(btnLabel, 12);
    DrawText(btnLabel, (int)(btn.x + (btn.width - tw) / 2), (int)(btn.y + 7), 12,
             n->ospfEnabled ? Color{15,23,42,255} : Color{148,163,184,255});

    if (!n->ospfEnabled) return;

    // Router ID
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

    // Neighbor table header
    DrawText("Router-ID",   CANVAS_W + 12,  y, 10, Color{71,85,105,255});
    DrawText("State",       CANVAS_W + 110, y, 10, Color{71,85,105,255});
    DrawText("Dead",        CANVAS_W + 190, y, 10, Color{71,85,105,255});
    y += 14;

    static const char* stateNames[] = { "DOWN", "INIT", "2WAY", "FULL" };
    static const Color stateColors[] = {
        {100,116,139,255}, {234,179,8,255}, {59,130,246,255}, {34,197,94,255}
    };

    for (const auto& nbr : n->ospfNeighbors) {
        if (y > CANVAS_H - 20) break;
        // Trim router-id to fit: show last 11 chars
        std::string rid = nbr.neighborRouterId;
        if ((int)rid.size() > 11) rid = rid.substr(rid.size() - 11);
        DrawText(rid.c_str(), CANVAS_W + 12, y, 10, WHITE);

        int si = (int)nbr.state;
        DrawText(stateNames[si], CANVAS_W + 110, y, 10, stateColors[si]);

        char deadBuf[8];
        std::snprintf(deadBuf, sizeof(deadBuf), "%.1fs", nbr.deadTimer);
        DrawText(deadBuf, CANVAS_W + 190, y, 10, Color{148,163,184,255});
        y += 16;
    }
}
```

- [ ] **Step 7: Update `DrawPanel` to handle TAB_OSPF**

In `src/NetworkCanvas.cpp`, inside `DrawPanel`, there are two changes:

**7a. Add the OSPF tab button rendering** — after the ARP tab block (which ends around line 456), before the horizontal separator line at ~line 458, add:

```cpp
    Rectangle ospfTab    = PnlOspfTabRect();
    bool      ospfActive = (ps.activeTab == TAB_OSPF);
    DrawRectangleRec(ospfTab, ospfActive ? Color{30,41,59,255} : PANEL_BG);
    if (ospfActive)
        DrawLineEx({ospfTab.x, ospfTab.y + ospfTab.height},
                   {ospfTab.x + ospfTab.width, ospfTab.y + ospfTab.height}, 2.0f,
                   Color{59, 130, 246, 255});
    {
        int twO = MeasureText("OSPF", 12);
        DrawText("OSPF", (int)(ospfTab.x + (ospfTab.width - twO) / 2),
                 (int)(ospfTab.y + 7), 12,
                 ospfActive ? WHITE : Color{100, 116, 139, 255});
    }
```

**7b. Add the TAB_OSPF branch to the tab content section** — replace:
```cpp
    // Tab content
    if (ps.activeTab == TAB_CONFIG)
        DrawConfigTab(n, ps);
    else if (ps.activeTab == TAB_ROUTES)
        DrawRoutesTab(n, ps);
    else
        DrawArpTab(n);
```
with:
```cpp
    // Tab content
    if (ps.activeTab == TAB_CONFIG)
        DrawConfigTab(n, ps);
    else if (ps.activeTab == TAB_ROUTES)
        DrawRoutesTab(n, ps);
    else if (ps.activeTab == TAB_ARP)
        DrawArpTab(n);
    else
        DrawOspfTab(n);
```

- [ ] **Step 8: Build and verify**

```bash
make
```
Expected: compiles without errors. Launch the app. Add two routers, select one — you should see 4 tabs: Config | Routes | ARP | OSPF. Click OSPF tab — see "OSPF: Disabled" button. For a PC or Switch, the OSPF tab should show "OSPF: routers only".

- [ ] **Step 9: Commit**

```bash
git add src/ConfigPanel.h src/ConfigPanel.cpp src/NetworkCanvas.h src/NetworkCanvas.cpp
git commit -m "feat(m4.3): add OSPF panel tab — enable toggle, router-id display, neighbor table"
```

---

## Task 5: Cable OSPF State Coloring

**Files:**
- Modify: `src/NetworkCanvas.cpp` (`DrawAllCables` function)

Cables between two FULL OSPF neighbors render green; cables where at least one side is INIT or 2-WAY render yellow; otherwise the default slate-gray.

- [ ] **Step 1: Update `DrawAllCables` to check OSPF state**

In `src/NetworkCanvas.cpp`, replace the entire `DrawAllCables` function (currently lines 39–54):

```cpp
void DrawAllCables(const std::vector<Cable>& cables,
                   const std::vector<DeviceNode>& nodes)
{
    for (const auto& c : cables) {
        const DeviceNode* from = FindNode(nodes, c.fromId);
        const DeviceNode* to   = FindNode(nodes, c.toId);
        if (!from || !to) continue;

        Vector2 p0 = GetPortPosition(*from, c.fromPort);
        Vector2 p3 = GetPortPosition(*to,   c.toPort);

        // Determine cable color based on OSPF adjacency state
        Color cableColor = Color{148, 163, 184, 255};  // default slate-gray
        if (from->ospfEnabled && to->ospfEnabled) {
            // Find the adjacency record from->to
            OspfState stateAB = OSPF_DOWN, stateBA = OSPF_DOWN;
            for (const auto& nbr : from->ospfNeighbors)
                if (nbr.neighborNodeId == to->id) { stateAB = nbr.state; break; }
            for (const auto& nbr : to->ospfNeighbors)
                if (nbr.neighborNodeId == from->id) { stateBA = nbr.state; break; }

            OspfState best = std::max(stateAB, stateBA);
            if (best == OSPF_FULL)
                cableColor = Color{34, 197, 94, 220};   // green
            else if (best >= OSPF_INIT)
                cableColor = Color{234, 179, 8, 220};   // yellow
        }

        DrawSplineSegmentBezierCubic(p0, BezierCtrl(p0, c.fromPort),
                                     BezierCtrl(p3, c.toPort), p3,
                                     2.0f, cableColor);
    }
}
```

- [ ] **Step 2: Build and verify**

```bash
make
```
Expected: compiles without errors. Launch the app. Add two routers, cable them, give each a port IP on the same /30 subnet, enable OSPF on both. After ~2 seconds the cable turns green.

- [ ] **Step 3: Commit**

```bash
git add src/NetworkCanvas.cpp
git commit -m "feat(m4.4): color cables by OSPF adjacency state — green=FULL, yellow=INIT"
```

---

## Task 6: End-to-End Verification

**No new files.** Manual gameplay test to confirm the full M4.3+M4.4 feature works as intended.

- [ ] **Step 1: Build a clean binary**

```bash
make clean && make
```
Expected: zero errors, zero warnings beyond any pre-existing ones.

- [ ] **Step 2: Run the app and exercise the OSPF teaching scenario**

Launch `./packet-path`. Perform the following sequence:

**Setup:**
1. Right-click canvas → Add Router Here (place two routers: R1, R2)
2. Select R1 → Config tab → set Mgmt IP `10.0.0.1/32`, port Gi0/0 IP `192.168.1.1/30`
3. Select R2 → Config tab → set Mgmt IP `10.0.0.2/32`, port Gi0/0 IP `192.168.1.2/30`
4. Cable R1 Gi0/0 (port 0) to R2 Gi0/0 (port 0)
5. Select R1 → OSPF tab → click "OSPF: Disabled" → becomes "OSPF: Enabled"
6. Select R2 → OSPF tab → click "OSPF: Disabled" → becomes "OSPF: Enabled"

**Verify adjacency forms:**
7. Watch the cable — after ~2 seconds it should turn **yellow** briefly then **green**
8. Log console should show: `OSPF: router-id 10.0.0.1 assigned to R1` and `OSPF: adjacency FULL 10.0.0.1 <-> 10.0.0.2`
9. Select R1 → OSPF tab → neighbor table should show R2's router-id, state `FULL`, dead timer ~7–8s

**Verify routes appear:**
10. Select R1 → Routes tab → should show OSPF route to `192.168.1.0/30` (R2's network, already connected) and R2's loopback `10.0.0.2/32` if the SPF trace reaches it
    - More precisely: R1 should see a route to any network on R2 that is not already directly connected

**Verify dead timer:**
11. Select R2 → OSPF tab → click "OSPF: Enabled" → disables OSPF on R2
12. Watch the cable — within 8 seconds it should turn gray (dead timer expires on R1)
13. Log console should show: `OSPF: adjacency DOWN 10.0.0.2 (dead timer expired)`
14. Select R1 → Routes tab → OSPF routes should be gone

**Verify packet forwarding uses OSPF routes:**
15. Add a PC (PC1) with IP `192.168.1.5/24`, cable it to R1's second port
16. Re-enable OSPF on R2, wait for adjacency
17. Right-click R1 → "Send Packet To…" → select R2
18. Packet should animate R1 → R2 successfully

- [ ] **Step 3: Commit verification note**

```bash
git add -A
git commit -m "chore(m4.3+m4.4): OSPF single-area verified — Hello, FSM, LSDB, SPF, cable coloring all working"
```
(Only commit if there are unstaged changes; this step may be a no-op if Tasks 1–5 committed everything.)

---

## Self-Review Checklist

**Spec coverage:**
- [x] 4-state FSM (Down → Init → 2-Way → Full) — Phase 2 of UpdateOspf
- [x] Hello interval (2s) and Dead interval (8s) — OSPF_HELLO_INTERVAL, OSPF_DEAD_INTERVAL constants
- [x] Router LSA with adjacencies[] + networks[] — GenerateLsa
- [x] LSDB rebuilt after any adjacency change — RebuildAllLsdbs called from anyChange block
- [x] Dijkstra SPF with priority_queue min-heap — RunSpf
- [x] OSPF routes appear in GetRoutingTable — GetRoutingTable appends n.ospfRoutes
- [x] 4th panel tab: OSPF — ConfigPanel TAB_OSPF + DrawOspfTab
- [x] Enable/disable toggle per router — PnlOspfEnableRect + main.cpp handler
- [x] Cable coloring: green=FULL, yellow=INIT — DrawAllCables
- [x] Log events for adjacency changes — UpdateOspf returns std::vector<std::string>
- [x] Only on ROUTER type devices — guards throughout

**No placeholders:** All code is complete and compilable.

**Type consistency:**
- `OspfNeighbor::neighborNodeId` (int) matches `DeviceNode::id` (int) throughout
- `OspfNeighbor::neighborRouterId` (std::string) matches `RouterLsa::routerId` (std::string)
- `ROUTE_OSPF` added to both enum and GetRoutingTable
- `TAB_OSPF` added to enum, rect, click handler, and DrawPanel branch
