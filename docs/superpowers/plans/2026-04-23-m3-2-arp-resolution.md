# M3.2 ARP Resolution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add ARP resolution to the simulation — each hop's next-hop lookup emits "ARP request / reply" log entries, the resolved IP→MAC mapping is cached in a per-device ARP table, and a new ARP tab in the Config Panel displays that table.

**Architecture:** `SimulateForward` returns `ArpEvent` records inside `ForwardResult` (pure, no side effects). `main.cpp` applies those events to the nodes' `arpTable` maps after each simulation run, then pushes ARP-typed `LogEntry` records before the routing summary. A new `TAB_ARP` in the Config Panel calls `DrawArpTab`, which renders the node's current `arpTable`. No separate ARP packet animation is added in this milestone.

**Tech Stack:** C++17, raylib 5.5, GNU Make, macOS

---

## File Map

| File | Change |
| ------ | -------- |
| `src/Device.h` | Add `#include <unordered_map>`, `LogType` enum, `ArpEvent` struct, `arpEvents` to `ForwardResult`, `type` to `LogEntry`, `arpTable` to `DeviceNode`, declare `GetDeviceMac` |
| `src/Device.cpp` | Implement `GetDeviceMac` |
| `src/SimulationEngine.cpp` | Rewrite forwarding loop to emit ARP events, use mutable `result` local |
| `src/main.cpp` | Apply ARP events to nodes, push ARP log entries, add `TAB_ARP` click handler |
| `src/ConfigPanel.h` | Add `TAB_ARP` to `PanelTab`, declare `PnlArpTabRect` |
| `src/ConfigPanel.cpp` | Update `PnlTabW` for 3 tabs, add `PnlArpTabRect` |
| `src/NetworkCanvas.h` | Declare `DrawArpTab` |
| `src/NetworkCanvas.cpp` | Implement `DrawArpTab`, update `DrawPanel` for 3rd tab, update `DrawLogConsole` for `LogType` |

---

## Task 1: Data model — MAC generation, arpTable, ArpEvent, LogType

**Files:**

- Modify: `src/Device.h`
- Modify: `src/Device.cpp`

### Context

The current `DeviceNode` has no ARP table. `ForwardResult` has no ARP event output. `LogEntry` has no type field to distinguish ARP entries from routing entries. This task adds all those data model pieces — no behavior changes yet.

- [ ] **Step 1: Replace `src/Device.h` with the new version**

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
enum RouteSource { ROUTE_CONNECTED, ROUTE_STATIC };

struct RouteEntry {
    std::string dest;
    std::string nextHop;
    int         outPort;
    RouteSource src;
};

// ── ARP & log types ───────────────────────────────────────────────────────
enum LogType { LOG_FORWARD, LOG_ARP_REQ, LOG_ARP_REPLY, LOG_ARP_HIT };

struct ArpEvent {
    int         nodeId;
    std::string ip;
    std::string mac;
    bool        cacheHit;
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
};

// ── Device geometry helpers (no draw calls) ───────────────────────────────
Color       GetDeviceColor(DeviceType t);
Rectangle   GetNodeRect(const DeviceNode& n);
Vector2     GetPortPosition(const DeviceNode& n, int port);
std::string GetPortName(DeviceType type, int port);
std::vector<RouteEntry> GetRoutingTable(const DeviceNode& n);

// ── IP / MAC utilities (no raylib) ────────────────────────────────────────
std::string NetworkAddress(const std::string& cidr);
bool        IpInSubnet(const std::string& ip, const std::string& subnet);
bool        ValidateIPOnly(const std::string& ip);
int         PrefixLen(const std::string& cidr);
bool        ValidateIP(const std::string& ip);
std::string GetDeviceMac(int id);
```

- [ ] **Step 2: Add `GetDeviceMac` to the bottom of `src/Device.cpp`**

Append this function after the existing `GetPortName` function:

```cpp
std::string GetDeviceMac(int id) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "de:ad:be:ef:%02x:%02x",
                  (id >> 8) & 0xFF, id & 0xFF);
    return buf;
}
```

- [ ] **Step 3: Build and verify zero errors and zero warnings**

```bash
make -j4 2>&1
```

Expected: compiles all `.cpp` files, links, no errors, no warnings with `-Wall -Wextra`.

- [ ] **Step 4: Commit**

```bash
git add src/Device.h src/Device.cpp
git commit -m "feat(arp): data model — ArpEvent, LogType, arpTable, GetDeviceMac"
```

---

## Task 2: ARP resolution in SimulateForward

**Files:**

- Modify: `src/SimulationEngine.cpp`

### Context — Task 2

`SimulateForward` currently uses inline brace-init returns (`return {false, path, "reason"}`). Now that `ForwardResult` has an `arpEvents` vector, those returns would silently drop any accumulated events. This task rewrites the function to build a mutable `result` local and populate it throughout, while adding ARP resolution logic before each static-route next-hop lookup.

The function remains `const`-correct — it reads `cur->arpTable` but never modifies nodes. Events are returned in `result.arpEvents`; the caller (`main.cpp`) applies them to nodes after the call.

- [ ] **Step 1: Replace the entire body of `src/SimulationEngine.cpp`**

```cpp
#include "SimulationEngine.h"
#include <algorithm>
#include <unordered_set>

// ── Forwarding engine ─────────────────────────────────────────────────────
ForwardResult SimulateForward(int srcId, const std::string& destIp,
                              const std::vector<DeviceNode>& nodes,
                              const std::vector<Cable>& cables)
{
    if (!FindNode(nodes, srcId))
        return {false, {}, "source node not found"};

    if (!ValidateIPOnly(destIp))
        return {false, {srcId}, "invalid destination"};

    static constexpr int MAX_HOPS = 16;

    ForwardResult result;
    result.path = {srcId};
    int currentId = srcId;
    std::unordered_set<int> visited = {srcId};

    for (int i = 0; i < MAX_HOPS; ++i) {
        const DeviceNode* cur = FindNode(nodes, currentId);
        if (!cur) { result.reason = "node not found"; return result; }

        auto table = GetRoutingTable(*cur);
        std::sort(table.begin(), table.end(), [](const RouteEntry& a, const RouteEntry& b) {
            return PrefixLen(a.dest) > PrefixLen(b.dest);
        });

        bool matched = false;
        for (const auto& route : table) {
            if (!IpInSubnet(destIp, route.dest)) continue;

            if (route.src == ROUTE_CONNECTED) {
                result.success = true;
                result.reason  = "delivered";
                return result;
            }

            // ARP cache check for this next-hop
            bool        arpHit    = cur->arpTable.count(route.nextHop) > 0;
            std::string cachedMac = arpHit ? cur->arpTable.at(route.nextHop) : "";

            // Find the directly-connected neighbor that owns route.nextHop
            int         neighborId  = -1;
            std::string resolvedMac;
            for (const auto& cable : cables) {
                int candidateId = -1;
                if      (cable.fromId == currentId) candidateId = cable.toId;
                else if (cable.toId   == currentId) candidateId = cable.fromId;
                if (candidateId == -1) continue;

                const DeviceNode* neighbor = FindNode(nodes, candidateId);
                if (!neighbor) continue;

                auto nbTable = GetRoutingTable(*neighbor);
                for (const auto& nbRoute : nbTable) {
                    if (nbRoute.src == ROUTE_CONNECTED &&
                        IpInSubnet(route.nextHop, nbRoute.dest)) {
                        neighborId  = candidateId;
                        resolvedMac = GetDeviceMac(candidateId);
                        break;
                    }
                }
                if (neighborId != -1) break;
            }

            // Emit ARP event
            if (arpHit) {
                result.arpEvents.push_back({currentId, route.nextHop, cachedMac, true});
            } else if (neighborId != -1) {
                result.arpEvents.push_back({currentId, route.nextHop, resolvedMac, false});
            } else {
                result.arpEvents.push_back({currentId, route.nextHop, "", false});
                result.reason = "ARP: who has " + route.nextHop + "? — no reply";
                return result;
            }

            if (visited.count(neighborId)) {
                result.reason = "loop detected";
                return result;
            }

            visited.insert(neighborId);
            result.path.push_back(neighborId);
            currentId = neighborId;
            matched   = true;
            break;
        }

        if (!matched) { result.reason = "no route to " + destIp; return result; }
    }

    result.reason = "ttl exceeded";
    return result;
}
```

- [ ] **Step 2: Build and verify zero errors and zero warnings**

```bash
make -j4 2>&1
```

Expected: compiles cleanly.

- [ ] **Step 3: Commit**

```bash
git add src/SimulationEngine.cpp
git commit -m "feat(arp): ARP resolution in SimulateForward — emits ArpEvent per next-hop"
```

---

## Task 3: Wire ARP events in main.cpp

**Files:**

- Modify: `src/main.cpp`

### Context — Task 3

`main.cpp` calls `SimulateForward` and builds one `LogEntry`. After this task it also:

1. Applies ARP cache updates from `fr.arpEvents` to the matching `DeviceNode`'s `arpTable`.
2. Pushes one or two `LogEntry` records per ARP event (before the routing entry).
3. Handles clicks on the new ARP tab button.

Three separate edit sites in main.cpp:

- **Site A** (≈ line 124): the `else` branch where `SimulateForward` is called.
- **Site B** (≈ line 264): the panel tab-click block.

- [ ] **Step 1: Replace Site A — the `else` branch inside the sim-dispatch block**

Find this block (it starts just after `std::string destIp = dst ? GetFirstValidIp(*dst) : "";`):

```cpp
    } else {
        ForwardResult fr = SimulateForward(simState.srcId, destIp, nodes, cables);
        simState.anim = PacketAnim{fr, 0, 0.f, false, 0.f};
        le.success    = fr.success;
        le.pathStr    = BuildPathStr(fr.path, nodes);
        le.reason     = fr.reason;
        le.timestamp  = GetTime();
        simState.mode = SIM_ANIMATING;
    }
```

Replace it with:

```cpp
    } else {
        ForwardResult fr = SimulateForward(simState.srcId, destIp, nodes, cables);

        // Apply ARP cache updates to nodes
        for (const auto& ev : fr.arpEvents) {
            if (!ev.cacheHit && !ev.mac.empty()) {
                for (auto& n : nodes)
                    if (n.id == ev.nodeId) { n.arpTable[ev.ip] = ev.mac; break; }
            }
        }

        // Push ARP log entries before the routing summary
        auto pushLog = [&](LogEntry entry) {
            if (logEntries.size() >= 50) logEntries.erase(logEntries.begin());
            logEntries.push_back(entry);
        };
        for (const auto& ev : fr.arpEvents) {
            if (ev.cacheHit) {
                LogEntry e;
                e.type = LOG_ARP_HIT; e.success = true;
                e.pathStr   = "ARP cache hit: " + ev.ip + " \xe2\x86\x92 " + ev.mac;
                e.timestamp = GetTime();
                pushLog(e);
            } else if (!ev.mac.empty()) {
                LogEntry req;
                req.type = LOG_ARP_REQ; req.success = true;
                req.pathStr   = "ARP who has " + ev.ip + "?";
                req.timestamp = GetTime();
                pushLog(req);
                LogEntry rep;
                rep.type = LOG_ARP_REPLY; rep.success = true;
                rep.pathStr   = ev.ip + " is at " + ev.mac;
                rep.timestamp = GetTime();
                pushLog(rep);
            } else {
                LogEntry e;
                e.type = LOG_ARP_REQ; e.success = false;
                e.pathStr   = "ARP who has " + ev.ip + "? \xe2\x80\x94 no reply";
                e.timestamp = GetTime();
                pushLog(e);
            }
        }

        simState.anim = PacketAnim{fr, 0, 0.f, false, 0.f};
        le.success    = fr.success;
        le.pathStr    = BuildPathStr(fr.path, nodes);
        le.reason     = fr.reason;
        le.type       = LOG_FORWARD;
        le.timestamp  = GetTime();
        simState.mode = SIM_ANIMATING;
    }
```

- [ ] **Step 2: Add the ARP tab click handler at Site B**

Find the panel tab-click block (inside `if (!inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))`):

```cpp
                if (CheckCollisionPointRec(screenMouse, PnlConfigTabRect())) {
                    ps.activeTab        = TAB_CONFIG;
                    ps.activeField      = -1;
                    ps.activeRouteField = -1;
                }
                if (CheckCollisionPointRec(screenMouse, PnlRoutesTabRect())) {
                    ps.activeTab        = TAB_ROUTES;
                    ps.activeField      = -1;
                    ps.activeRouteField = -1;
                }
```

Add the ARP tab handler immediately after the Routes tab handler:

```cpp
                if (CheckCollisionPointRec(screenMouse, PnlArpTabRect())) {
                    ps.activeTab        = TAB_ARP;
                    ps.activeField      = -1;
                    ps.activeRouteField = -1;
                }
```

- [ ] **Step 3: Build and verify zero errors and zero warnings**

```bash
make -j4 2>&1
```

Expected: compiles cleanly. `TAB_ARP` will be an unknown symbol until Task 4 — if the build fails here for that reason alone, that's expected. Complete Task 4 first, then re-check.

> **Note:** If `TAB_ARP` and `PnlArpTabRect` don't exist yet (they're added in Task 4), the build will fail at this step. That's acceptable — proceed to Task 4 immediately. The build will pass after Task 4.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat(arp): wire ARP events — apply cache updates and push log entries"
```

---

## Task 4: ARP tab UI + log console coloring

**Files:**

- Modify: `src/ConfigPanel.h`
- Modify: `src/ConfigPanel.cpp`
- Modify: `src/NetworkCanvas.h`
- Modify: `src/NetworkCanvas.cpp`

### Context — Task 4

This task adds the visible ARP tab to the panel and updates the log console to color ARP entries differently from routing entries. Four files change but all changes are additive or small replacements.

#### 4a — ConfigPanel.h: add TAB_ARP and PnlArpTabRect declaration

- [ ] **Step 1: Replace the PanelTab enum in `src/ConfigPanel.h`**

Old:

```cpp
enum PanelTab { TAB_CONFIG, TAB_ROUTES };
```

New:

```cpp
enum PanelTab { TAB_CONFIG, TAB_ROUTES, TAB_ARP };
```

- [ ] **Step 2: Add `PnlArpTabRect` declaration to the layout helpers block in `src/ConfigPanel.h`**

After `Rectangle PnlRoutesTabRect();` add:

```cpp
Rectangle PnlArpTabRect();
```

#### 4b — ConfigPanel.cpp: update tab width and add PnlArpTabRect

- [ ] **Step 3: Update `PnlTabW` in `src/ConfigPanel.cpp` for 3 equal-width tabs**

Old:

```cpp
float PnlTabW() { return (PANEL_W - 24 - 4) / 2.0f; }
```

New:

```cpp
float PnlTabW() { return (PANEL_W - 24 - 8) / 3.0f; }
```

`PnlConfigTabRect` and `PnlRoutesTabRect` call `PnlTabW()` automatically — their x/w values update with no other changes needed.

- [ ] **Step 4: Add `PnlArpTabRect` to `src/ConfigPanel.cpp`**

Insert immediately after `PnlRoutesTabRect`:

```cpp
Rectangle PnlArpTabRect() {
    return {(float)(CANVAS_W + 12) + 2.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
```

#### 4c — NetworkCanvas.h: declare DrawArpTab

- [ ] **Step 5: Add `DrawArpTab` declaration to `src/NetworkCanvas.h`**

After the `DrawRoutesTab` declaration add:

```cpp
void DrawArpTab(const DeviceNode* n);
```

#### 4d — NetworkCanvas.cpp: implement DrawArpTab, update DrawPanel and DrawLogConsole

- [ ] **Step 6: Add the `DrawArpTab` function to `src/NetworkCanvas.cpp`**

Insert this function after `DrawRoutesTab` (before `DrawPanel`):

```cpp
void DrawArpTab(const DeviceNode* n) {
    if (!n) return;
    DrawText("ARP CACHE", CANVAS_W + 12, 124, 10, Color{100, 116, 139, 255});

    if (n->arpTable.empty()) {
        DrawText("(empty)", CANVAS_W + 12, 148, 11, Color{51, 65, 85, 255});
        DrawText("Run a simulation to populate the cache.",
                 CANVAS_W + 12, 164, 10, Color{51, 65, 85, 255});
        return;
    }

    DrawText("IP ADDRESS",  CANVAS_W + 12,  142, 9, Color{100, 116, 139, 255});
    DrawText("MAC ADDRESS", CANVAS_W + 138, 142, 9, Color{100, 116, 139, 255});
    DrawLineEx({(float)CANVAS_W,           156.0f},
               {(float)(CANVAS_W+PANEL_W), 156.0f}, 0.5f, PANEL_BORDER);

    int y = 162;
    for (const auto& [ip, mac] : n->arpTable) {
        if (y > CANVAS_H - LOG_H - 20) break;
        DrawText(ip.c_str(),  CANVAS_W + 12,  y, 10, Color{34, 197, 94, 255});
        DrawText(mac.c_str(), CANVAS_W + 138, y, 9,  Color{148, 163, 184, 255});
        y += 18;
    }
}
```

- [ ] **Step 7: Add the third tab button rendering inside `DrawPanel` in `src/NetworkCanvas.cpp`**

Find the existing Routes tab block in `DrawPanel`:

```cpp
    bool rteActive = (ps.activeTab == TAB_ROUTES);
    DrawRectangleRec(rteTab, rteActive ? Color{30,41,59,255} : PANEL_BG);
    if (rteActive)
        DrawLineEx({rteTab.x, rteTab.y + rteTab.height},
                   {rteTab.x + rteTab.width, rteTab.y + rteTab.height}, 2.0f,
                   Color{59, 130, 246, 255});
    {
        int tw3 = MeasureText("Routes", 12);
        DrawText("Routes", (int)(rteTab.x + (rteTab.width - tw3) / 2),
                 (int)(rteTab.y + 7), 12,
                 rteActive ? WHITE : Color{100, 116, 139, 255});
    }
```

Append the ARP tab block directly after it (before the separator `DrawLineEx`):

```cpp
    Rectangle arpTab   = PnlArpTabRect();
    bool      arpActive = (ps.activeTab == TAB_ARP);
    DrawRectangleRec(arpTab, arpActive ? Color{30,41,59,255} : PANEL_BG);
    if (arpActive)
        DrawLineEx({arpTab.x, arpTab.y + arpTab.height},
                   {arpTab.x + arpTab.width, arpTab.y + arpTab.height}, 2.0f,
                   Color{59, 130, 246, 255});
    {
        int tw4 = MeasureText("ARP", 12);
        DrawText("ARP", (int)(arpTab.x + (arpTab.width - tw4) / 2),
                 (int)(arpTab.y + 7), 12,
                 arpActive ? WHITE : Color{100, 116, 139, 255});
    }
```

- [ ] **Step 8: Update the tab content switch at the bottom of `DrawPanel`**

Old:

```cpp
    if (ps.activeTab == TAB_CONFIG)
        DrawConfigTab(n, ps);
    else
        DrawRoutesTab(n, ps);
```

New:

```cpp
    if (ps.activeTab == TAB_CONFIG)
        DrawConfigTab(n, ps);
    else if (ps.activeTab == TAB_ROUTES)
        DrawRoutesTab(n, ps);
    else
        DrawArpTab(n);
```

- [ ] **Step 9: Update `DrawLogConsole` in `src/NetworkCanvas.cpp` for LogType coloring**

Find this section inside `DrawLogConsole`:

```cpp
        const char* icon    = e.success ? "\xe2\x9c\x93" : "\xe2\x9c\x97";
        Color       icColor = e.success ? Color{34, 197, 94, 255}
                                        : Color{239, 68, 68, 255};
        DrawText(icon, 90, lineY, 10, icColor);

        std::string msg = e.pathStr + "  \xe2\x80\x94  " + e.reason;
        DrawText(msg.c_str(), 108, lineY, 10, icColor);
```

Replace it with:

```cpp
        const char* icon;
        Color       icColor;
        switch (e.type) {
            case LOG_ARP_REQ:
                icon    = "?";
                icColor = Color{100, 160, 240, 255};
                break;
            case LOG_ARP_REPLY:
                icon    = "!";
                icColor = Color{80, 200, 180, 255};
                break;
            case LOG_ARP_HIT:
                icon    = "~";
                icColor = Color{140, 140, 140, 255};
                break;
            default:
                icon    = e.success ? "\xe2\x9c\x93" : "\xe2\x9c\x97";
                icColor = e.success ? Color{34, 197, 94, 255}
                                    : Color{239, 68, 68, 255};
                break;
        }
        DrawText(icon, 90, lineY, 10, icColor);

        std::string msg = e.pathStr;
        if (!e.reason.empty()) msg += "  \xe2\x80\x94  " + e.reason;
        DrawText(msg.c_str(), 108, lineY, 10, icColor);
```

- [ ] **Step 10: Build and verify zero errors and zero warnings**

```bash
make -j4 2>&1
```

Expected: full clean build. All four files compile without errors or warnings.

- [ ] **Step 11: Commit**

```bash
git add src/ConfigPanel.h src/ConfigPanel.cpp src/NetworkCanvas.h src/NetworkCanvas.cpp
git commit -m "feat(arp): ARP tab in config panel + LogType coloring in log console"
```

---

## Task 5: Verification

**Files:** none (build + runtime check only)

### Context — Task 5

Confirm the full feature works end-to-end: ARP events appear in the log, the ARP tab populates on subsequent runs, and cache-hit entries appear on re-runs. Confirm no regressions in routing, cable drawing, or config panel.

- [ ] **Step 1: Full clean build**

```bash
make clean && make -j4 2>&1
```

Expected: zero errors, zero warnings.

- [ ] **Step 2: Launch the app and set up a 2-hop test network**

```bash
./packet-path
```

Spawn two PCs and a Router:

- Press `P` twice (PC1, PC2), `R` once (Router1) on the canvas
- Connect PC1 port → Router1 port (drag port dot to port dot)
- Connect Router1 port → PC2 port

Configure IPs via the Config tab (click each device):

- PC1 — Gi0/0 IP: `10.0.0.1/24`; static route: dest `10.0.1.0/24`, next-hop `10.0.0.254`
- Router1 — Gi0/0 IP: `10.0.0.254/24`, Gi0/1 IP: `10.0.1.254/24`
- PC2 — Gi0/0 IP: `10.0.1.1/24`

- [ ] **Step 3: Run first simulation (cache empty → ARP request+reply expected)**

Right-click PC1 → "Send Packet To…" → click PC2.

Expected log console (bottom strip, newest at top):

- `[mm:ss]  ✓  PC1 → Router1 → PC2  —  delivered`  (green, LOG_FORWARD)
- `[mm:ss]  !  10.0.0.254 is at de:ad:be:ef:00:XX`  (cyan, LOG_ARP_REPLY)
- `[mm:ss]  ?  ARP who has 10.0.0.254?`              (blue, LOG_ARP_REQ)

The log shows 3 lines max (newest at top), so the ARP pair is visible.

Expected ARP tab (click PC1, then click "ARP" tab):

```text
ARP CACHE
IP ADDRESS       MAC ADDRESS
10.0.0.254       de:ad:be:ef:00:XX
```

- [ ] **Step 4: Run second simulation (cache populated → ARP cache-hit expected)**

Right-click PC1 → "Send Packet To…" → click PC2 again.

Expected log console:

- `[mm:ss]  ✓  PC1 → Router1 → PC2  —  delivered`  (green)
- `[mm:ss]  ~  ARP cache hit: 10.0.0.254 → de:ad:be:ef:00:XX`  (gray)

No second ARP request/reply — the cache was used.

- [ ] **Step 5: Test ARP failure path**

Remove the static route from PC1 (Routes tab → [×] on the static route), then add an intentionally unreachable next-hop static route: dest `10.0.1.0/24`, next-hop `10.0.99.1`.

Run simulation PC1 → PC2.

Expected log:

- `[mm:ss]  ?  ARP who has 10.0.99.1? — no reply`  (blue, failed ARP)
- `[mm:ss]  ✗  PC1  —  ARP: who has 10.0.99.1? — no reply`  (red)

- [ ] **Step 6: Verify no regressions**

- [ ] Cable drawing still works (drag ports, cables appear as bezier curves)
- [ ] Config tab still edits hostname, mgmt IP, port IPs
- [ ] Routes tab still adds/deletes static routes
- [ ] Context menu (right-click) still works for all three item types

- [ ] **Step 7: Commit and push**

```bash
git add -p  # stage any debug leftovers if any; otherwise nothing to stage
git log --oneline -6
git push
```

Expected: last 3–4 commits visible, push succeeds.

---

## Self-Review

**Spec coverage:**

- ✅ ARP table per device (`arpTable` on `DeviceNode`) — Task 1
- ✅ MAC address generation (`GetDeviceMac`) — Task 1
- ✅ ARP resolution in forwarding (miss → emit req+reply event, hit → emit hit event) — Task 2
- ✅ ARP events applied to nodes after simulation — Task 3
- ✅ Log entries: ARP request (blue), reply (cyan), cache hit (gray), forward (green/red) — Tasks 3 + 4
- ✅ ARP tab in config panel showing IP→MAC rows — Task 4
- ✅ No separate ARP animation (only the IP packet animates) — by design, not added

**Placeholder scan:** None found.

**Type consistency:**

- `ArpEvent` fields (`nodeId`, `ip`, `mac`, `cacheHit`) used consistently across Task 2 (population) and Task 3 (consumption).
- `LogType` enum values (`LOG_FORWARD`, `LOG_ARP_REQ`, `LOG_ARP_REPLY`, `LOG_ARP_HIT`) used consistently in Task 3 (setting) and Task 4 (`DrawLogConsole` switch).
- `PnlArpTabRect()` declared in `ConfigPanel.h` (Task 4a), defined in `ConfigPanel.cpp` (Task 4b), called in `NetworkCanvas.cpp` (Task 4d) and `main.cpp` (Task 3). ✅
- `DrawArpTab(const DeviceNode* n)` declared in `NetworkCanvas.h`, defined and called in `NetworkCanvas.cpp`. ✅
- `TAB_ARP` added to enum in `ConfigPanel.h`, used in `main.cpp` and `NetworkCanvas.cpp`. ✅
