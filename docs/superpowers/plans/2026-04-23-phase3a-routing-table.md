# Phase 3a: Routing Table + Forwarding Engine — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a real routing table to every device — connected routes auto-derived from interface IPs, user-editable static routes in a new "Routes" panel tab, and a pure-logic forwarding engine (longest-prefix match) ready for Phase 3b packet animation. Fix the deferred hoverItem draw-coupling as housekeeping.

**Architecture:** Everything stays in `src/main.cpp` (~675 lines → ~955 lines). `RouteEntry` structs live directly in `DeviceNode.staticRoutes`; connected routes are computed on-the-fly by `GetRoutingTable()`. The panel gains a tab header (Config | Routes); `DrawConfigTab` and `DrawRoutesTab` are called from `DrawPanel`. `SimulateForward` returns a `ForwardResult` path that Phase 3b will animate.

**Tech Stack:** C++17, raylib 5.5, macOS, single `src/main.cpp`.

---

## Codebase Starting Point

Branch: `phase-3a-routing-table` (create from `main` @ `fd2720a`, 675 lines).

Key facts the implementer must know:

- `std::clamp` throughout — **never** `Clamp()` (triggers ~53 warnings)
- `FindNode` returns `const DeviceNode*` — do not change this signature
- `DrawSplineSegmentBezierCubic` for bezier cables — not `DrawLineBezierCubic`
- `DrawContextMenu` currently takes `ContextMenu&` (non-const) and writes `hoverItem` — Task 1 fixes this
- `PanelState` currently has only `int activeField` — Task 1 extends it
- `PnlFieldRect(y)` returns `{CANVAS_W+12, y, PANEL_W-24, 26}` — used throughout
- `PnlPortFieldRect(port)` returns `{CANVAS_W+80, 240+port*44, PANEL_W-92, 24}` — updated in Task 2
- Panel click handler in `main()` (line ~603) currently checks `PnlFieldRect(126)` and `PnlFieldRect(178)` — updated in Task 2

---

## File Map

| File | Status | Responsibility |
| --- | --- | --- |
| `src/main.cpp` | Modify | All Phase 3a changes — data model, helpers, tab UI, routing engine |

---

## Task 1: M3a.1 — Fix #3, Data Model, and IP Helpers

**Files:**

- Modify: `src/main.cpp`

Add `#include <cstdint>`, new enums/structs (`RouteSource`, `RouteEntry`, `PanelTab`), extend `DeviceNode` and `PanelState`, add IP helper functions, fix the hoverItem draw-coupling.

No UI changes in this task — only data structures and pure functions.

- [ ] **Step 1: Add `#include <cstdint>` to the includes block**

Find the includes block (lines 1-6). Add after `#include <cstdio>`:

```cpp
#include "raylib.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>    // sscanf
#include <cstdint>   // uint32_t
```

- [ ] **Step 2: Add layout constants for Config and Routes tabs**

Find the constants block. After `static const int CONTEXT_MENU_W = 160;`, add:

```cpp
// ── Config tab layout ─────────────────────────────────────────────────────
static const int CFG_HOSTNAME_Y   = 158;  // hostname field Y
static const int CFG_MGMTIP_Y     = 210;  // mgmt IP field Y
static const int CFG_IFACE_SEP_Y  = 246;  // separator above interfaces section
static const int CFG_PORT_Y0      = 272;  // first port row Y
static const int CFG_PORT_STRIDE  = 44;   // port row vertical stride
// ── Routes tab layout ─────────────────────────────────────────────────────
static const int RTE_ROW_Y0       = 142;  // first route row Y
static const int RTE_ROW_H        = 22;   // route row height
static const int RTE_ADD_SEP_Y    = 420;  // separator above add-route form
static const int RTE_DEST_Y       = 464;  // destination field Y
static const int RTE_NEXT_Y       = 516;  // next-hop field Y
static const int RTE_BTN_Y        = 554;  // [Add] button Y
```

- [ ] **Step 3: Add `RouteSource` enum and `RouteEntry` struct**

Find `// ── Device types & node struct`. Add immediately before it:

```cpp
// ── Routing ───────────────────────────────────────────────────────────────
enum RouteSource { ROUTE_CONNECTED, ROUTE_STATIC };

struct RouteEntry {
    std::string dest;    // network prefix e.g. "10.0.0.0/24" or "0.0.0.0/0"
    std::string nextHop; // "direct" for connected; "10.0.0.1" for static
    int         outPort; // port index 0-3 for connected; -1 for mgmt/static
    RouteSource src;
};
```

- [ ] **Step 4: Extend `DeviceNode` with `staticRoutes`**

Find `struct DeviceNode`. Add one field after `portIp[4]`:

```cpp
struct DeviceNode {
    int         id       = 0;
    DeviceType  type     = PC;
    Vector2     position = {0.0f, 0.0f};
    std::string label;
    bool        selected = false;
    std::string mgmtIp;
    std::string portIp[4];
    std::vector<RouteEntry> staticRoutes;
};
```

- [ ] **Step 5: Add `PanelTab` enum and extend `PanelState`**

Find `// ── Panel UI state`. Replace the entire `PanelState` struct:

```cpp
// ── Panel UI state ────────────────────────────────────────────────────────
enum PanelTab { TAB_CONFIG, TAB_ROUTES };

struct PanelState {
    int         activeField      = -1;    // Config tab: -1=none 0=label 1=mgmtIp 2-5=portIp[0-3]
    PanelTab    activeTab        = TAB_CONFIG;
    std::string newRouteDest;             // Routes tab add-form: destination buffer
    std::string newRouteNext;             // Routes tab add-form: next-hop buffer
    int         activeRouteField = -1;   // -1=none, 0=newRouteDest, 1=newRouteNext
};
```

- [ ] **Step 6: Add `NetworkAddress`, `IpInSubnet`, `ValidateIPOnly`, `PrefixLen`, `GetRoutingTable`**

Find `bool ValidateIP(...)`. Add these five functions immediately before `ValidateIP`:

```cpp
std::string NetworkAddress(const std::string& cidr) {
    int a, b, c, d, prefix;
    if (std::sscanf(cidr.c_str(), "%d.%d.%d.%d/%d", &a, &b, &c, &d, &prefix) != 5)
        return cidr;
    uint32_t ip   = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
                    ((uint32_t)c <<  8) |  (uint32_t)d;
    uint32_t mask = prefix ? (~0u << (32 - prefix)) : 0u;
    uint32_t net  = ip & mask;
    return std::to_string((net >> 24) & 0xFF) + "." +
           std::to_string((net >> 16) & 0xFF) + "." +
           std::to_string((net >>  8) & 0xFF) + "." +
           std::to_string( net        & 0xFF) + "/" +
           std::to_string(prefix);
}

bool IpInSubnet(const std::string& ip, const std::string& subnet) {
    int a1, b1, c1, d1, a2, b2, c2, d2, prefix;
    if (std::sscanf(ip.c_str(),     "%d.%d.%d.%d",    &a1,&b1,&c1,&d1) != 4) return false;
    if (std::sscanf(subnet.c_str(), "%d.%d.%d.%d/%d", &a2,&b2,&c2,&d2,&prefix) != 5) return false;
    uint32_t ipBits  = ((uint32_t)a1 << 24) | ((uint32_t)b1 << 16) |
                       ((uint32_t)c1 <<  8) |  (uint32_t)d1;
    uint32_t netBits = ((uint32_t)a2 << 24) | ((uint32_t)b2 << 16) |
                       ((uint32_t)c2 <<  8) |  (uint32_t)d2;
    uint32_t mask    = prefix ? (~0u << (32 - prefix)) : 0u;
    return (ipBits & mask) == (netBits & mask);
}

bool ValidateIPOnly(const std::string& ip) {
    if (ip.empty()) return false;
    int a, b, c, d, consumed = 0;
    std::sscanf(ip.c_str(), "%d.%d.%d.%d%n", &a, &b, &c, &d, &consumed);
    return (consumed == (int)ip.size() &&
            a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
            c >= 0 && c <= 255 && d >= 0 && d <= 255);
}

int PrefixLen(const std::string& cidr) {
    const char* slash = std::strchr(cidr.c_str(), '/');
    return slash ? std::atoi(slash + 1) : 0;
}

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

- [ ] **Step 7: Add `UpdateContextMenuHover` before `DrawContextMenu`**

Find `// ── Context menu draw & action`. Add immediately before `void DrawContextMenu(...)`:

```cpp
void UpdateContextMenuHover(ContextMenu& menu, Vector2 screenMouse) {
    if (!menu.visible) { menu.hoverItem = -1; return; }

    static const char* nodeItems[]   = {"Rename", "Delete", nullptr};
    static const char* cableItems[]  = {"Delete Cable", nullptr};
    static const char* canvasItems[] = {"Add PC Here", "Add Router Here",
                                        "Add Switch Here", "Reset View", nullptr};
    const char** items = nullptr;
    if      (menu.ctx == CTX_NODE)   items = nodeItems;
    else if (menu.ctx == CTX_CABLE)  items = cableItems;
    else if (menu.ctx == CTX_CANVAS) items = canvasItems;
    else { menu.hoverItem = -1; return; }

    int count = 0;
    while (items[count]) ++count;

    float h = (float)(count * MENU_ITEM_H + 8);
    float x = std::min(menu.screenPos.x, (float)(CANVAS_W - CONTEXT_MENU_W - 4));
    float y = std::min(menu.screenPos.y, (float)(SCREEN_H - (int)h - 4));

    menu.hoverItem = -1;
    for (int i = 0; i < count; ++i) {
        Rectangle ir = {x + 4, y + 4 + (float)(i * MENU_ITEM_H),
                        (float)(CONTEXT_MENU_W - 8), (float)MENU_ITEM_H};
        if (CheckCollisionPointRec(screenMouse, ir)) { menu.hoverItem = i; break; }
    }
}
```

- [ ] **Step 8: Fix `DrawContextMenu` — make it `const`, remove hoverItem writes**

The current `DrawContextMenu` takes `ContextMenu& menu` and sets `menu.hoverItem`. Replace the entire function with the `const` version that reads `menu.hoverItem` instead of computing it:

```cpp
void DrawContextMenu(const ContextMenu& menu, Vector2 screenMouse) {
    if (!menu.visible) return;

    static const char* nodeItems[]   = {"Rename", "Delete", nullptr};
    static const char* cableItems[]  = {"Delete Cable", nullptr};
    static const char* canvasItems[] = {"Add PC Here", "Add Router Here",
                                        "Add Switch Here", "Reset View", nullptr};

    const char** items = nullptr;
    if      (menu.ctx == CTX_NODE)   items = nodeItems;
    else if (menu.ctx == CTX_CABLE)  items = cableItems;
    else if (menu.ctx == CTX_CANVAS) items = canvasItems;
    else return;

    int count = 0;
    while (items[count]) ++count;

    float h = (float)(count * MENU_ITEM_H + 8);
    float x = std::min(menu.screenPos.x, (float)(CANVAS_W - CONTEXT_MENU_W - 4));
    float y = std::min(menu.screenPos.y, (float)(SCREEN_H - (int)h - 4));

    DrawRectangleRounded({x, y, (float)CONTEXT_MENU_W, h}, 0.08f, 4, Color{30, 41, 59, 255});
    DrawRectangleRoundedLinesEx({x, y, (float)CONTEXT_MENU_W, h}, 0.08f, 4, 1.0f, PANEL_BORDER);

    for (int i = 0; i < count; ++i) {
        Rectangle ir = {x + 4, y + 4 + (float)(i * MENU_ITEM_H),
                        (float)(CONTEXT_MENU_W - 8), (float)MENU_ITEM_H};
        if (menu.hoverItem == i)
            DrawRectangleRounded(ir, 0.08f, 4, Color{51, 65, 85, 255});
        DrawText(items[i], (int)ir.x + 8, (int)ir.y + 7, 13, WHITE);
    }
}
```

- [ ] **Step 9: Call `UpdateContextMenuHover` in `main()` input phase**

Find the `// ── LMB pressed` comment in `main()`. Add the call immediately before the `if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))` block:

```cpp
        UpdateContextMenuHover(contextMenu, screenMouse);

        // ── LMB pressed ───────────────────────────────────────────────
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
```

- [ ] **Step 10: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make
```

Expected: zero warnings, zero errors. The context menu must still work (hover highlights, click actions) — no visual regression.

- [ ] **Step 11: Run and verify Fix #3**

```bash
./packet-path
```

- Right-click a node → context menu appears, hover highlights work, click executes action. ✅
- Right-click canvas → Add PC/Router/Switch/Reset View works. ✅
- Context menu still dismisses on LMB anywhere. ✅

- [ ] **Step 12: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m3a.1): routing data model, IP helpers, hoverItem input-phase fix"
```

---

## Task 2: M3a.2 — Panel Tab Header + Config Tab Extraction

**Files:**

- Modify: `src/main.cpp`

Add the tab header to `DrawPanel`, extract `DrawConfigTab` (with updated Y coordinates), add tab-click handling and selection-reset logic.

- [ ] **Step 1: Add route-tab rectangle helpers after `PnlPortFieldRect`**

Find `Rectangle PnlPortFieldRect(int port)`. Replace it and add new helpers:

```cpp
Rectangle PnlPortFieldRect(int port) {
    return {(float)(CANVAS_W + 80), (float)(CFG_PORT_Y0 + port * CFG_PORT_STRIDE),
            (float)(PANEL_W - 92), 24.0f};
}

static float PnlTabW() { return (float)((PANEL_W - 24 - 4) / 2); }
Rectangle PnlConfigTabRect() {
    return {(float)(CANVAS_W + 12), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlRoutesTabRect() {
    return {(float)(CANVAS_W + 12) + PnlTabW() + 4.0f, 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlRouteDeleteRect(int rowIdx) {
    return {(float)(CANVAS_W + PANEL_W - 22),
            (float)(RTE_ROW_Y0 + rowIdx * RTE_ROW_H + 4), 16.0f, 14.0f};
}
Rectangle PnlRouteDestRect()   { return {(float)(CANVAS_W+12),(float)RTE_DEST_Y,(float)(PANEL_W-24),26.0f}; }
Rectangle PnlRouteNextRect()   { return {(float)(CANVAS_W+12),(float)RTE_NEXT_Y,(float)(PANEL_W-24),26.0f}; }
Rectangle PnlRouteAddBtnRect() { return {(float)(CANVAS_W+12),(float)RTE_BTN_Y, (float)(PANEL_W-24),28.0f}; }
```

- [ ] **Step 2: Add `DrawConfigTab` function before `DrawPanel`**

```cpp
void DrawConfigTab(const DeviceNode* n, const PanelState& ps) {
    DrawText("GENERAL", CANVAS_W + 12, 124, 10, Color{100, 116, 139, 255});
    DrawTextField(PnlFieldRect(CFG_HOSTNAME_Y), "Hostname", nullptr,
                  n->label, ps.activeField == 0, !n->label.empty());
    DrawTextField(PnlFieldRect(CFG_MGMTIP_Y), "Mgmt IP", "x.x.x.x/xx",
                  n->mgmtIp, ps.activeField == 1, ValidateIP(n->mgmtIp));
    DrawLineEx({(float)CANVAS_W,           (float)CFG_IFACE_SEP_Y},
               {(float)(CANVAS_W+PANEL_W), (float)CFG_IFACE_SEP_Y},
               1.0f, PANEL_BORDER);
    DrawText("INTERFACES", CANVAS_W + 12, CFG_IFACE_SEP_Y + 10, 10, Color{100, 116, 139, 255});
    for (int i = 0; i < PORTS_PER_NODE; ++i) {
        std::string pname = GetPortName(n->type, i);
        DrawTextField(PnlPortFieldRect(i), "", "x.x.x.x/xx",
                      n->portIp[i], ps.activeField == 2 + i, ValidateIP(n->portIp[i]));
        DrawText(pname.c_str(), CANVAS_W + 16, CFG_PORT_Y0 + i * CFG_PORT_STRIDE + 7,
                 11, Color{148, 163, 184, 255});
    }
}
```

- [ ] **Step 3: Add `DrawRoutesTab` stub before `DrawPanel`**

```cpp
void DrawRoutesTab(const DeviceNode* n, const PanelState& ps) {
    DrawText("ROUTING TABLE", CANVAS_W + 12, 124, 10, Color{100, 116, 139, 255});
    DrawText("(coming in Task 3)", CANVAS_W + 12, 142, 11, Color{51, 65, 85, 255});
}
```

- [ ] **Step 4: Rewrite `DrawPanel` to add tab header and dispatch**

Replace the entire `DrawPanel` function body with:

```cpp
void DrawPanel(int selectedId, const std::vector<DeviceNode>& nodes,
               const PanelState& ps)
{
    DrawRectangle(CANVAS_W, 0, PANEL_W, SCREEN_H, PANEL_BG);
    DrawLineEx({(float)CANVAS_W, 0.0f}, {(float)CANVAS_W, (float)SCREEN_H},
               1.0f, PANEL_BORDER);
    DrawText("CONFIGURATION", CANVAS_W + 12, 14, 10, Color{100, 116, 139, 255});
    DrawLineEx({(float)CANVAS_W, 38.0f}, {(float)(CANVAS_W + PANEL_W), 38.0f},
               1.0f, PANEL_BORDER);

    if (selectedId == -1) {
        const char* msg = "<- Select a device";
        int tw = MeasureText(msg, 13);
        DrawText(msg, CANVAS_W + (PANEL_W - tw) / 2, SCREEN_H / 2 - 8, 13,
                 Color{100, 116, 139, 255});
        return;
    }

    const DeviceNode* n = FindNode(nodes, selectedId);
    if (!n) return;

    // Device type badge
    const char* typeNames[] = {"PC", "Router", "Switch"};
    int bw = MeasureText(typeNames[(int)n->type], 11) + 16;
    DrawRectangleRounded({(float)(CANVAS_W + 12), 50.0f, (float)bw, 22.0f},
                         0.5f, 4, GetDeviceColor(n->type));
    DrawText(typeNames[(int)n->type], CANVAS_W + 20, 56, 11, WHITE);
    DrawText(n->label.c_str(), CANVAS_W + 16 + bw, 56, 13, WHITE);
    DrawLineEx({(float)CANVAS_W, 84.0f}, {(float)(CANVAS_W + PANEL_W), 84.0f},
               1.0f, PANEL_BORDER);

    // Tab header
    Rectangle cfgTab = PnlConfigTabRect();
    Rectangle rteTab = PnlRoutesTabRect();

    bool cfgActive = (ps.activeTab == TAB_CONFIG);
    DrawRectangleRec(cfgTab, cfgActive ? Color{30,41,59,255} : PANEL_BG);
    if (cfgActive)
        DrawLineEx({cfgTab.x, cfgTab.y + cfgTab.height},
                   {cfgTab.x + cfgTab.width, cfgTab.y + cfgTab.height}, 2.0f,
                   Color{59, 130, 246, 255});
    {
        int tw2 = MeasureText("Config", 12);
        DrawText("Config", (int)(cfgTab.x + (cfgTab.width - tw2) / 2),
                 (int)(cfgTab.y + 7), 12,
                 cfgActive ? WHITE : Color{100, 116, 139, 255});
    }

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

    DrawLineEx({(float)CANVAS_W, 116.0f}, {(float)(CANVAS_W + PANEL_W), 116.0f},
               1.0f, PANEL_BORDER);

    // Tab content
    if (ps.activeTab == TAB_CONFIG)
        DrawConfigTab(n, ps);
    else
        DrawRoutesTab(n, ps);
}
```

- [ ] **Step 5: Update panel click handler in `main()` for tab clicks and new field positions**

Find `// ── Panel click-to-focus` in `main()`. Replace the entire block:

```cpp
        // ── Panel click-to-focus ───────────────────────────────────────
        if (!inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (selectedId != -1) {
                // Tab clicks
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
                // Config tab field focus
                if (ps.activeTab == TAB_CONFIG) {
                    ps.activeField = -1;
                    if (CheckCollisionPointRec(screenMouse, PnlFieldRect(CFG_HOSTNAME_Y))) ps.activeField = 0;
                    if (CheckCollisionPointRec(screenMouse, PnlFieldRect(CFG_MGMTIP_Y)))  ps.activeField = 1;
                    for (int i = 0; i < PORTS_PER_NODE; ++i)
                        if (CheckCollisionPointRec(screenMouse, PnlPortFieldRect(i)))
                            ps.activeField = 2 + i;
                }
                // Routes tab interactions handled in Task 4
            }
        }
```

- [ ] **Step 6: Update device selection reset in `main()` to clear tab state**

Find the selection-reset block (currently `if (selectedId != prevSelectedId)`). Replace:

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

- [ ] **Step 7: Update ESC handler to also clear `activeRouteField`**

Find `if (IsKeyPressed(KEY_ESCAPE))`. Update the body:

```cpp
        if (IsKeyPressed(KEY_ESCAPE)) {
            contextMenu.visible = false;
            if (ps.activeField != -1) {
                ps.activeField = -1;
            } else if (ps.activeRouteField != -1) {
                ps.activeRouteField = -1;
            } else if (connecting) {
                connecting  = false;
                hoverNodeId = -1;
                hoverPort   = -1;
            }
        }
```

- [ ] **Step 8: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make
```

Expected: zero warnings. The panel must show the tab header on any selected device.

- [ ] **Step 9: Run and verify**

```bash
./packet-path
```

- Select a node → panel shows `[Config]` and `[Routes]` tab buttons at the top ✅
- Click `[Config]` → Config tab highlighted (blue underline), existing hostname/IP fields visible at shifted positions ✅
- Click `[Routes]` → Routes tab highlighted, stub "(coming in Task 3)" text visible ✅
- Select a different node → tab resets to Config ✅
- ESC while in a config field → clears field focus ✅
- Config tab field interaction unchanged (hostname, mgmt IP, port IPs all work) ✅

- [ ] **Step 10: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m3a.2): panel tab header, DrawConfigTab extraction, selection reset"
```

---

## Task 3: M3a.3 — Routes Tab Display (Read-Only)

**Files:**

- Modify: `src/main.cpp`

Implement full `DrawRoutesTab` with connected route rows (green "C"), static route rows (blue "S") with `[×]` delete buttons, empty-state placeholder, and column headers. Add-form fields come in Task 4 — they are stubbed here as static visuals only.

- [ ] **Step 1: Replace the `DrawRoutesTab` stub with the full implementation**

Find `void DrawRoutesTab(...)`. Replace it entirely:

```cpp
void DrawRoutesTab(const DeviceNode* n, const PanelState& ps) {
    // Column headers
    DrawText("T", CANVAS_W + 12, 124, 10, Color{100, 116, 139, 255});
    DrawText("DESTINATION",  CANVAS_W + 30, 124, 10, Color{100, 116, 139, 255});
    DrawText("NEXT-HOP",     CANVAS_W + 130, 124, 10, Color{100, 116, 139, 255});
    DrawText("VIA",          CANVAS_W + 210, 124, 10, Color{100, 116, 139, 255});
    DrawLineEx({(float)CANVAS_W, 136.0f}, {(float)(CANVAS_W+PANEL_W), 136.0f},
               1.0f, PANEL_BORDER);

    auto table = GetRoutingTable(*n);

    if (table.empty()) {
        DrawText("No routes configured", CANVAS_W + 20, RTE_ROW_Y0, 11,
                 Color{51, 65, 85, 255});
    } else {
        int displayed = std::min((int)table.size(), 8);
        int staticIdx = 0;
        for (int i = 0; i < displayed; ++i) {
            const RouteEntry& r = table[i];
            int ry = RTE_ROW_Y0 + i * RTE_ROW_H;
            Color rowColor = (r.src == ROUTE_CONNECTED)
                             ? Color{34, 197, 94, 255}
                             : Color{59, 130, 246, 255};

            // Type letter
            const char* typeLetter = (r.src == ROUTE_CONNECTED) ? "C" : "S";
            DrawText(typeLetter, CANVAS_W + 12, ry + 3, 11, rowColor);

            // Destination (truncated if long)
            DrawText(r.dest.c_str(), CANVAS_W + 30, ry + 3, 10, rowColor);

            // Next-hop
            DrawText(r.nextHop.c_str(), CANVAS_W + 130, ry + 3, 10, rowColor);

            // Via (port name or "—" for mgmt/static)
            if (r.outPort >= 0) {
                std::string via = GetPortName(n->type, r.outPort);
                DrawText(via.c_str(), CANVAS_W + 210, ry + 3, 10, rowColor);
            } else {
                DrawText("\xe2\x80\x94", CANVAS_W + 210, ry + 3, 10, rowColor);
            }

            // [×] delete button for static routes only
            if (r.src == ROUTE_STATIC) {
                Rectangle delBtn = PnlRouteDeleteRect(i);
                DrawRectangleRounded(delBtn, 0.3f, 4, Color{51, 65, 85, 255});
                DrawText("x", (int)(delBtn.x + 4), (int)(delBtn.y + 1), 11,
                         Color{239, 68, 68, 255});
                ++staticIdx;
            }
        }
    }

    // Add-form separator and labels (fields wired up in Task 4)
    DrawLineEx({(float)CANVAS_W,           (float)RTE_ADD_SEP_Y},
               {(float)(CANVAS_W+PANEL_W), (float)RTE_ADD_SEP_Y},
               1.0f, PANEL_BORDER);
    DrawText("ADD STATIC ROUTE", CANVAS_W + 12, RTE_ADD_SEP_Y + 8, 10,
             Color{100, 116, 139, 255});

    // Destination field (stub — active=false, not interactive yet)
    DrawTextField(PnlRouteDestRect(), "Destination", "x.x.x.x/xx",
                  ps.newRouteDest, false, ValidateIP(ps.newRouteDest));

    // Next-hop field (stub — active=false)
    DrawTextField(PnlRouteNextRect(), "Next-Hop", "x.x.x.x",
                  ps.newRouteNext, false, ValidateIPOnly(ps.newRouteNext));

    // [Add] button (stub — always dim)
    bool canAdd = ValidateIP(ps.newRouteDest) && ValidateIPOnly(ps.newRouteNext);
    DrawRectangleRec(PnlRouteAddBtnRect(),
                     canAdd ? Color{30, 58, 138, 255} : Color{22, 33, 62, 255});
    DrawRectangleLinesEx(PnlRouteAddBtnRect(), 1.0f,
                         canAdd ? Color{59, 130, 246, 255} : PANEL_BORDER);
    {
        int tw = MeasureText("Add Route", 12);
        Rectangle btn = PnlRouteAddBtnRect();
        DrawText("Add Route",
                 (int)(btn.x + (btn.width - tw) / 2),
                 (int)(btn.y + 8), 12,
                 canAdd ? WHITE : Color{51, 65, 85, 255});
    }
}
```

- [ ] **Step 2: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make
```

Expected: zero warnings.

- [ ] **Step 3: Run and verify Routes tab display**

```bash
./packet-path
```

- Select a Router. Click "Routes" tab.
- No IPs configured → "No routes configured" placeholder. ✅
- Click "Config" tab. Set Gi0/0 to `10.0.0.1/24`. Click "Routes" tab.
- One green "C" row: `C  10.0.0.0/24  direct  Gi0/0`. ✅
- Set Gi0/1 to `10.0.1.1/24`. Routes tab shows two "C" rows. ✅
- Set Mgmt IP to `192.168.99.1/24`. Third "C" row appears. ✅
- Change Gi0/0 IP to `172.16.0.1/16`. The route updates immediately to `172.16.0.0/16`. ✅
- Add form separator, "ADD STATIC ROUTE" header, two dim field outlines, and dim "Add Route" button visible. ✅
- No static routes yet — no `[×]` buttons. ✅

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m3a.3): routes tab display with connected routes and static route rows"
```

---

## Task 4: M3a.4 — Routes Tab Interactions (Add + Delete)

**Files:**

- Modify: `src/main.cpp`

Wire up the add-form fields for keyboard input, `KEY_TAB` focus cycling, `[Add]` button click, and `[×]` delete buttons. Update `DrawRoutesTab` to pass active state to fields. Add `UpdateRoutesTab` for keyboard handling.

- [ ] **Step 1: Update `DrawRoutesTab` to pass real active state to add-form fields**

In `DrawRoutesTab`, find the two `DrawTextField` calls for Destination and Next-Hop (currently `false` for active). Update them:

```cpp
    DrawTextField(PnlRouteDestRect(), "Destination", "x.x.x.x/xx",
                  ps.newRouteDest, ps.activeRouteField == 0, ValidateIP(ps.newRouteDest));

    DrawTextField(PnlRouteNextRect(), "Next-Hop", "x.x.x.x",
                  ps.newRouteNext, ps.activeRouteField == 1, ValidateIPOnly(ps.newRouteNext));
```

- [ ] **Step 2: Add `UpdateRoutesTab` function before `DrawConfigTab`**

```cpp
void UpdateRoutesTab(DeviceNode* n, PanelState& ps) {
    if (ps.activeRouteField == -1) return;
    if (ps.activeRouteField == 0) UpdateTextField(ps.newRouteDest, 18);
    if (ps.activeRouteField == 1) UpdateTextField(ps.newRouteNext, 15);

    // KEY_TAB cycles focus between the two fields
    if (IsKeyPressed(KEY_TAB)) {
        ps.activeRouteField = (ps.activeRouteField == 0) ? 1 : 0;
        while (GetCharPressed() > 0) {}  // flush the tab character itself
    }

    // KEY_ENTER commits if valid
    if (IsKeyPressed(KEY_ENTER)) {
        if (ValidateIP(ps.newRouteDest) && ValidateIPOnly(ps.newRouteNext)) {
            n->staticRoutes.push_back({ps.newRouteDest, ps.newRouteNext, -1, ROUTE_STATIC});
            ps.newRouteDest.clear();
            ps.newRouteNext.clear();
            ps.activeRouteField = 0;
        }
    }
}
```

- [ ] **Step 3: Call `UpdateRoutesTab` in the text-field update block in `main()`**

Find `// ── Text field update` in `main()`. After the Config tab `UpdateTextField` block and the `else { while (GetCharPressed() > 0) {} }` flush, add Routes tab input handling. Replace the entire text-field update section:

```cpp
        // ── Text field update ──────────────────────────────────────────
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
            DeviceNode* selNode = nullptr;
            for (auto& nd : nodes)
                if (nd.id == selectedId) { selNode = &nd; break; }
            if (selNode) UpdateRoutesTab(selNode, ps);
        } else {
            while (GetCharPressed() > 0) {}  // flush char queue when no field active
        }
```

- [ ] **Step 4: Update KEY_DELETE guard to also block when routes field is active**

Find `if (ps.activeField == -1 && IsKeyPressed(KEY_DELETE) && selectedId != -1)`. Update guard:

```cpp
        if (ps.activeField == -1 && ps.activeRouteField == -1 &&
            IsKeyPressed(KEY_DELETE) && selectedId != -1) {
```

- [ ] **Step 5: Add Routes tab click handling to the panel click-to-focus block**

Find the `// Routes tab interactions handled in Task 4` comment added in Task 2. Replace it with:

```cpp
                // Routes tab: field focus, [×] delete, [Add] button
                if (ps.activeTab == TAB_ROUTES) {
                    ps.activeRouteField = -1;

                    // Add-form field focus
                    if (CheckCollisionPointRec(screenMouse, PnlRouteDestRect()))
                        ps.activeRouteField = 0;
                    if (CheckCollisionPointRec(screenMouse, PnlRouteNextRect()))
                        ps.activeRouteField = 1;

                    // [Add] button
                    if (CheckCollisionPointRec(screenMouse, PnlRouteAddBtnRect())) {
                        DeviceNode* selNode = nullptr;
                        for (auto& nd : nodes)
                            if (nd.id == selectedId) { selNode = &nd; break; }
                        if (selNode && ValidateIP(ps.newRouteDest)
                                    && ValidateIPOnly(ps.newRouteNext)) {
                            selNode->staticRoutes.push_back(
                                {ps.newRouteDest, ps.newRouteNext, -1, ROUTE_STATIC});
                            ps.newRouteDest.clear();
                            ps.newRouteNext.clear();
                            ps.activeRouteField = 0;
                        }
                    }

                    // [×] delete buttons — check each displayed route row
                    DeviceNode* selNode = nullptr;
                    for (auto& nd : nodes)
                        if (nd.id == selectedId) { selNode = &nd; break; }
                    if (selNode) {
                        auto table = GetRoutingTable(*selNode);
                        int numConnected = 0;
                        for (const auto& r : table)
                            if (r.src == ROUTE_CONNECTED) ++numConnected;
                        int displayed = std::min((int)table.size(), 8);
                        for (int i = 0; i < displayed; ++i) {
                            if (table[i].src == ROUTE_STATIC) {
                                if (CheckCollisionPointRec(screenMouse, PnlRouteDeleteRect(i))) {
                                    int staticIdx = i - numConnected;
                                    if (staticIdx >= 0 &&
                                        staticIdx < (int)selNode->staticRoutes.size()) {
                                        selNode->staticRoutes.erase(
                                            selNode->staticRoutes.begin() + staticIdx);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
```

- [ ] **Step 6: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make
```

Expected: zero warnings.

- [ ] **Step 7: Run and verify full Routes tab interaction**

```bash
./packet-path
```

- Select a Router. Set Gi0/0 = `10.0.0.1/24`, Gi0/1 = `10.0.1.1/24`.
- Switch to Routes tab. Two green C rows visible.
- Click "Destination" field → white border, blinking cursor. Type `0.0.0.0/0` → green border (valid prefix).
- Click "Next-Hop" field → cursor moves there. Type `10.0.0.254` → green border (valid IP).
- "[Add Route]" button lights up (blue). Click it → blue "S" row appears: `S  0.0.0.0/0  10.0.0.254  —  [×]`. Both fields clear. ✅
- Add a second static route: `192.168.1.0/24` → `10.0.1.2`. Press Enter to submit. Appears as second S row. ✅
- Click `[×]` on first static route → it disappears. ✅
- Press `KEY_TAB` while in Destination field → focus jumps to Next-Hop. ✅
- Press `ESC` while a field is active → clears `activeRouteField`. ✅
- Select a different node → routes tab clears add-form, resets to Config tab. ✅

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m3a.4): static route add/delete, form keyboard input, KEY_TAB cycling"
```

---

## Task 5: M3a.5 — Forwarding Engine

**Files:**

- Modify: `src/main.cpp`

Add `ForwardResult` struct and implement `SimulateForward` — a pure-logic, longest-prefix-match forwarding function. No UI. Verify with startup assertions, then remove the assertion block.

- [ ] **Step 1: Add `ForwardResult` struct**

Find `enum RouteSource { ROUTE_CONNECTED, ROUTE_STATIC };`. Add immediately after the `RouteEntry` struct:

```cpp
struct ForwardResult {
    bool             success = false;
    std::vector<int> path;    // node IDs from src to dst (inclusive)
    std::string      reason;  // "delivered" | "no route to X" | "next-hop unreachable: X" | "loop detected"
};
```

- [ ] **Step 2: Add `SimulateForward` before `// ── Spawn helper`**

```cpp
ForwardResult SimulateForward(int srcId, const std::string& destIp,
                              const std::vector<DeviceNode>& nodes,
                              const std::vector<Cable>& cables)
{
    if (!ValidateIPOnly(destIp))
        return {false, {srcId}, "invalid destination"};

    int currentId = srcId;
    std::vector<int> path = {srcId};

    for (int hop = 0; hop < 16; ++hop) {
        const DeviceNode* cur = FindNode(nodes, currentId);
        if (!cur) return {false, path, "node not found"};

        // Build and sort routing table by longest prefix first
        auto table = GetRoutingTable(*cur);
        std::sort(table.begin(), table.end(), [](const RouteEntry& a, const RouteEntry& b) {
            return PrefixLen(a.dest) > PrefixLen(b.dest);
        });

        bool matched = false;
        for (const auto& route : table) {
            if (!IpInSubnet(destIp, route.dest)) continue;

            if (route.src == ROUTE_CONNECTED) {
                // Destination is on a directly attached subnet — delivered
                return {true, path, "delivered"};
            }

            // Static route — find neighbor reachable via route.nextHop
            int neighborId = -1;
            for (const auto& cable : cables) {
                // Try both directions of each cable
                int candidateId = -1;
                if (cable.fromId == currentId) candidateId = cable.toId;
                else if (cable.toId == currentId) candidateId = cable.fromId;
                if (candidateId == -1) continue;

                const DeviceNode* neighbor = FindNode(nodes, candidateId);
                if (!neighbor) continue;

                // Check if neighbor has an interface in the next-hop's subnet
                // by checking if route.nextHop falls in any of neighbor's connected subnets
                auto nbTable = GetRoutingTable(*neighbor);
                for (const auto& nbRoute : nbTable) {
                    if (nbRoute.src == ROUTE_CONNECTED &&
                        IpInSubnet(route.nextHop, nbRoute.dest)) {
                        neighborId = candidateId;
                        break;
                    }
                }
                if (neighborId != -1) break;
            }

            if (neighborId == -1)
                return {false, path, "next-hop unreachable: " + route.nextHop};

            path.push_back(neighborId);
            currentId = neighborId;
            matched = true;
            break;
        }

        if (!matched)
            return {false, path, "no route to " + destIp};
    }

    return {false, path, "loop detected"};
}
```

- [ ] **Step 3: Add startup assertions to `main()` to verify `SimulateForward`**

Find `InitWindow(SCREEN_W, SCREEN_H, "Packet Path");` in `main()`. Add a test block immediately before it:

```cpp
    // ── SimulateForward verification (remove after confirming) ─────────────
    {
        std::vector<DeviceNode> tnodes;
        std::vector<Cable>      tcables;

        // PC1: id=100, portIp[0] = "10.0.0.2/24", static default route → 10.0.0.1
        DeviceNode pc1;
        pc1.id = 100; pc1.type = PC; pc1.position = {-200.0f, 0.0f};
        pc1.label = "PC1"; pc1.portIp[0] = "10.0.0.2/24";
        pc1.staticRoutes.push_back({"0.0.0.0/0", "10.0.0.1", -1, ROUTE_STATIC});
        tnodes.push_back(pc1);

        // Router1: id=101, portIp[0] = "10.0.0.1/24", portIp[1] = "10.0.1.1/24"
        DeviceNode r1;
        r1.id = 101; r1.type = ROUTER; r1.position = {0.0f, 0.0f};
        r1.label = "R1"; r1.portIp[0] = "10.0.0.1/24"; r1.portIp[1] = "10.0.1.1/24";
        tnodes.push_back(r1);

        // PC2: id=102, portIp[0] = "10.0.1.2/24"
        DeviceNode pc2;
        pc2.id = 102; pc2.type = PC; pc2.position = {200.0f, 0.0f};
        pc2.label = "PC2"; pc2.portIp[0] = "10.0.1.2/24";
        tnodes.push_back(pc2);

        // Cable: PC1 port0 ↔ Router1 port0
        tcables.push_back({100, 0, 101, 0});
        // Cable: Router1 port1 ↔ PC2 port0
        tcables.push_back({101, 1, 102, 0});

        // Test 1: PC1 → 10.0.1.2 (via Router1's connected route to 10.0.1.0/24)
        auto r = SimulateForward(100, "10.0.1.2", tnodes, tcables);
        assert(r.success == true);
        assert(r.path.size() == 2 && r.path[0] == 100 && r.path[1] == 101);
        assert(r.reason == "delivered");

        // Test 2: PC1 → 10.0.0.2 (same subnet as PC1's own port — connected on Router? No.
        // PC1 wants to reach 10.0.0.2 — its own portIp is on 10.0.0.0/24 — connected route
        // But sending FROM PC1 means PC1's routing table: connected 10.0.0.0/24 via port0 → delivered at hop 0
        auto r2 = SimulateForward(100, "10.0.0.5", tnodes, tcables);
        assert(r2.success == true);
        assert(r2.path.size() == 1 && r2.path[0] == 100);

        // Test 3: no route → fail
        auto r3 = SimulateForward(100, "192.168.99.1", tnodes, tcables);
        assert(r3.success == false);
        assert(r3.reason == "no route to 192.168.99.1");

        // Test 4: invalid dest
        auto r4 = SimulateForward(100, "not-an-ip", tnodes, tcables);
        assert(r4.success == false);
        assert(r4.reason == "invalid destination");
    }
    // ── End verification block ─────────────────────────────────────────────
```

Also add `#include <cassert>` to the includes block (temporarily).

- [ ] **Step 4: Build and run to verify assertions pass**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make && ./packet-path
```

Expected: game window opens (assertions did not fire = all tests passed). If an assertion fires, the process aborts with no window — investigate the failing test.

- [ ] **Step 5: Remove the assertion block and `#include <cassert>`**

Delete the entire `// ── SimulateForward verification` block from `main()` and remove `#include <cassert>` from the includes.

- [ ] **Step 6: Build clean**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make
```

Expected: zero warnings, zero errors.

- [ ] **Step 7: Run and verify the game works end-to-end**

```bash
./packet-path
```

- All Phase 2 features still work (panel, context menu, cable connections). ✅
- Config tab: hostname, mgmt IP, port IPs — all interactive. ✅
- Routes tab: connected routes auto-populate from configured IPs. ✅
- Static routes: add via form, delete via `[×]`. ✅
- No crash, no warnings. ✅

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m3a.5): SimulateForward longest-prefix match forwarding engine"
```

---

## Phase 3a Exit Commit

After all five milestones pass:

```bash
git log --oneline
```

Expected history:

```text
feat(m3a.5): SimulateForward longest-prefix match forwarding engine
feat(m3a.4): static route add/delete, form keyboard input, KEY_TAB cycling
feat(m3a.3): routes tab display with connected routes and static route rows
feat(m3a.2): panel tab header, DrawConfigTab extraction, selection reset
feat(m3a.1): routing data model, IP helpers, hoverItem input-phase fix
```

Final build:

```bash
make && ./packet-path
```

Expected: zero warnings, game runs cleanly, routing table fully functional, forwarding engine ready for Phase 3b animation.
