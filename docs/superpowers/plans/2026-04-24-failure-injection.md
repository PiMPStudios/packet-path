# Failure Injection / Troubleshooting Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let players manually inject link and device failures via right-click context menu, restore them the same way, and toggle a Troubleshoot Mode overlay (T key) that annotates all active failures — with full simulation awareness and log console feedback.

**Architecture:** Two boolean flags (`Cable.broken`, `DeviceNode.crashed`) carry all fault state. `SimulationEngine` skips broken cables and fails immediately on crashed devices. The context menu gains dynamic items that flip based on current fault state. A `ContextMenu.targetBroken` field set at right-click time drives the dynamic rendering. A new `std::vector<LogEntry>&` parameter on `ExecuteMenuAction` lets fault actions push `LINK DOWN` / `DEVICE CRASHED` / `RESTORED` entries directly into the log. Level JSON can pre-inject faults for later "troubleshoot and fix" scenarios.

**Tech Stack:** C++17, raylib 5.5, nlohmann/json (already linked), GNU Make.

---

## File Map

| Action | File | Responsibility |
| -------- | ------ | ---------------- |
| Modify | `src/Cable.h` | Add `bool broken = false` |
| Modify | `src/Device.h` | Add `bool crashed = false` to DeviceNode; add `LOG_LINK_DOWN`, `LOG_DEVICE_CRASH`, `LOG_RESTORED` to LogType |
| Modify | `src/Level.h` | Add `bool requiresFix = false` to WinCondition |
| Modify | `src/Level.cpp` | `LoadLevel` reads `broken`, `crashed`, `requiresFix` from JSON |
| Modify | `src/SimulationEngine.cpp` | Skip broken cables in L2 BFS; fail immediately on crashed src/intermediate device |
| Modify | `src/NetworkCanvas.cpp` | Red cable rendering; crashed-device overlay; new log icon cases; updated DrawContextMenu dynamic items; new DrawTroubleshootOverlay |
| Modify | `src/NetworkCanvas.h` | Declare `DrawTroubleshootOverlay` |
| Modify | `src/UI.h` | Add `targetBroken` to ContextMenu; add `logEntries` param to ExecuteMenuAction |
| Modify | `src/UI.cpp` | Dynamic context menu item arrays; Cut/Restore Link; Crash/Restore Device with log push |
| Modify | `src/main.cpp` | Set `targetBroken` on menu open; pass `logEntries` to ExecuteMenuAction; `troubleshootMode` bool + T key; call DrawTroubleshootOverlay; HUD badge |

---

## Task 1: Data Model Flags and LogType Extensions

**Files:**

- Modify: `src/Cable.h`
- Modify: `src/Device.h`

- [ ] **Step 1: Add `broken` flag to Cable struct**

Open `src/Cable.h`. Replace the `Cable` struct (current lines 5–8) with:

```cpp
struct Cable {
    int  fromId, fromPort;
    int  toId,   toPort;
    bool broken = false;
};
```

- [ ] **Step 2: Add `crashed` flag to DeviceNode**

Open `src/Device.h`. Find `DeviceNode` (line ~140). Locate the `bool selected = false;` field and add `crashed` immediately after it:

```cpp
    bool        selected = false;
    bool        crashed  = false;
    std::string mgmtIp;
```

- [ ] **Step 3: Extend LogType enum**

In `src/Device.h`, find (line ~97):

```cpp
enum LogType { LOG_FORWARD, LOG_ARP_REQ, LOG_ARP_REPLY, LOG_ARP_HIT, LOG_OSPF };
```

Replace with:

```cpp
enum LogType { LOG_FORWARD, LOG_ARP_REQ, LOG_ARP_REPLY, LOG_ARP_HIT, LOG_OSPF,
               LOG_LINK_DOWN, LOG_DEVICE_CRASH, LOG_RESTORED };
```

- [ ] **Step 4: Build to verify clean**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | tail -5
```

Expected: zero errors. The entire codebase recompiles against the new struct fields; all existing code is unaffected because the new fields have defaults.

- [ ] **Step 5: Commit**

```bash
git add src/Cable.h src/Device.h
git commit -m "feat: add Cable.broken, DeviceNode.crashed, LOG_LINK_DOWN/DEVICE_CRASH/RESTORED"
```

---

## Task 2: Level JSON Support

**Files:**

- Modify: `src/Level.h`
- Modify: `src/Level.cpp`

- [ ] **Step 1: Add `requiresFix` to WinCondition**

Open `src/Level.h`. Replace the `WinCondition` struct (current lines 7–11):

```cpp
struct WinCondition {
    std::string srcLabel;
    std::string dstLabel;
    std::string description;
    bool        requiresFix = false;   // true → win condition requires restoration after injected failure
};
```

- [ ] **Step 2: LoadLevel reads `broken` from cable entries**

Open `src/Level.cpp`. In `LoadLevel`, find the cables loop (around line 82–88):

```cpp
    for (const auto& c : j.value("cables", json::array())) {
        Cable cable;
        cable.fromId   = c.value("from",     0);
        cable.fromPort = c.value("fromPort", 0);
        cable.toId     = c.value("to",       0);
        cable.toPort   = c.value("toPort",   0);
        cable.broken   = c.value("broken",   false);   // ← add this line
        out.cables.push_back(cable);
    }
```

- [ ] **Step 3: LoadLevel reads `crashed` from device entries**

In the devices loop, find the block of boolean fields being read (around line 38–41):

```cpp
        n.ospfEnabled = d.value("ospfEnabled", false);
        n.ldpEnabled  = d.value("ldpEnabled",  false);
        n.bgpEnabled       = d.value("bgpEnabled",       false);
        n.isRouteReflector = d.value("isRouteReflector", false);
```

Add `n.crashed` after `n.ldpEnabled`:

```cpp
        n.ospfEnabled = d.value("ospfEnabled", false);
        n.ldpEnabled  = d.value("ldpEnabled",  false);
        n.crashed     = d.value("crashed",     false);   // ← add this line
        n.bgpEnabled       = d.value("bgpEnabled",       false);
        n.isRouteReflector = d.value("isRouteReflector", false);
```

- [ ] **Step 4: LoadLevel reads `requiresFix` from winCondition entries**

Find the winConditions loop (around line 91–96):

```cpp
    for (const auto& wc : j.value("winConditions", json::array())) {
        WinCondition w;
        w.srcLabel    = wc.value("src",         "");
        w.dstLabel    = wc.value("dst",         "");
        w.description = wc.value("description", "");
        w.requiresFix = wc.value("requiresFix", false);   // ← add this line
        out.winConditions.push_back(w);
    }
```

- [ ] **Step 5: Build to verify clean**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | tail -5
```

Expected: zero errors.

- [ ] **Step 6: Verify pre-injected failure JSON works**

Create a minimal test file `test-broken.json` in the project root:

```json
{
  "id": 99, "title": "Broken Test", "briefing": "Fix the link",
  "devices": [
    {"id": 1, "type": "PC", "label": "PC-A", "x": 200, "y": 300,
     "portIp0": "10.0.0.1/24"},
    {"id": 2, "type": "PC", "label": "PC-B", "x": 600, "y": 300,
     "portIp0": "10.0.0.2/24"}
  ],
  "cables": [
    {"from": 1, "fromPort": 0, "to": 2, "toPort": 0, "broken": true}
  ],
  "winConditions": [
    {"src": "PC-A", "dst": "PC-B",
     "description": "Restore connectivity: PC-A to PC-B",
     "requiresFix": true}
  ]
}
```

After Task 6 ships you can load this via `LoadLevel`. For now, just confirm the build passes.

- [ ] **Step 7: Commit**

```bash
git add src/Level.h src/Level.cpp
git commit -m "feat: LoadLevel reads broken/crashed flags and requiresFix from JSON"
```

---

## Task 3: SimulationEngine Respects Failures

**Files:**

- Modify: `src/SimulationEngine.cpp`

Two sites to change: (1) `FindL2Path` BFS skips broken cables, (2) `SimulateForward` fails early on crashed src or intermediate device.

- [ ] **Step 1: Skip broken cables in FindL2Path BFS**

Open `src/SimulationEngine.cpp`. Find the inner BFS loop in `FindL2Path` (around line 80–84):

```cpp
        for (const auto& cable : cables) {
            int myPort = -1, nextId = -1, nextPort = -1;
            if      (cable.fromId == curId) { myPort = cable.fromPort; nextId = cable.toId;   nextPort = cable.toPort; }
            else if (cable.toId   == curId) { myPort = cable.toPort;   nextId = cable.fromId; nextPort = cable.fromPort; }
            if (nextId == -1 || visited.count(nextId)) continue;
```

Add the broken-cable skip immediately after the port/id extraction:

```cpp
        for (const auto& cable : cables) {
            int myPort = -1, nextId = -1, nextPort = -1;
            if      (cable.fromId == curId) { myPort = cable.fromPort; nextId = cable.toId;   nextPort = cable.toPort; }
            else if (cable.toId   == curId) { myPort = cable.toPort;   nextId = cable.fromId; nextPort = cable.fromPort; }
            if (cable.broken) continue;                                    // ← add
            if (nextId == -1 || visited.count(nextId)) continue;
```

- [ ] **Step 2: Fail immediately if the source device is crashed**

In `SimulateForward`, find the source-not-found guard (line ~127–128):

```cpp
    if (!FindNode(nodes, srcId))
        return {false, {}, "source node not found", {}, {}};
```

Add the crashed-source check immediately after:

```cpp
    if (!FindNode(nodes, srcId))
        return {false, {}, "source node not found", {}, {}};

    {
        const DeviceNode* srcNode = FindNode(nodes, srcId);
        if (srcNode && srcNode->crashed)
            return {false, {srcId},
                    srcNode->label + " is crashed \xe2\x80\x94 device offline", {}, {}};
    }
```

- [ ] **Step 3: Fail on crashed intermediate device**

In the main `for` loop in `SimulateForward`, find the null-node guard (line ~143–144):

```cpp
        const DeviceNode* cur = FindNode(nodes, currentId);
        if (!cur) { result.reason = "node not found"; return result; }
```

Add the crashed check immediately after:

```cpp
        const DeviceNode* cur = FindNode(nodes, currentId);
        if (!cur)          { result.reason = "node not found"; return result; }
        if (cur->crashed)  { result.reason = cur->label + " is crashed \xe2\x80\x94 device offline"; return result; }
```

- [ ] **Step 4: Build to verify clean**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | tail -5
```

Expected: zero errors.

- [ ] **Step 5: Manual verification**

Since the UI for injecting failures isn't wired yet, temporarily hard-code a test in main.cpp (revert after): set `cables[0].broken = true;` before the main loop, launch the game, send a packet that would cross that cable — verify the simulation returns a failure and the broken path overlay shows. Remove the hard-code afterward.

Alternatively, skip this manual step and trust the type-system; full E2E verification comes in Task 6.

- [ ] **Step 6: Commit**

```bash
git add src/SimulationEngine.cpp
git commit -m "feat: SimulationEngine skips broken cables and fails on crashed devices"
```

---

## Task 4: Visual Indicators

**Files:**

- Modify: `src/NetworkCanvas.cpp`

Three draw-function changes: (1) broken cables render red with a midpoint ✗ badge, (2) crashed devices get a red overlay and ✗ badge, (3) log console renders `LOG_LINK_DOWN` / `LOG_DEVICE_CRASH` / `LOG_RESTORED` icons.

- [ ] **Step 1: Broken cable — red bezier + midpoint badge in DrawAllCables**

Open `src/NetworkCanvas.cpp`. In `DrawAllCables`, find the per-cable loop body (around line 66–74). After obtaining `p0` and `p3` and **before** the `Color cableColor` line, add:

```cpp
        Vector2 p0 = GetPortPosition(*from, c.fromPort);
        Vector2 p3 = GetPortPosition(*to,   c.toPort);

        if (c.broken) {
            DrawSplineSegmentBezierCubic(p0, BezierCtrl(p0, c.fromPort),
                                         BezierCtrl(p3, c.toPort), p3,
                                         3.0f, Color{239, 68, 68, 255});
            Vector2 mid = {(p0.x + p3.x) / 2.0f, (p0.y + p3.y) / 2.0f};
            DrawCircle((int)mid.x, (int)mid.y, 7.f, Color{239, 68, 68, 255});
            const char* xmark = "\xe2\x9c\x97";
            int xw = MeasureText(xmark, 9);
            DrawText(xmark, (int)mid.x - xw / 2, (int)(mid.y - 5.f), 9, WHITE);
            continue;   // skip normal color/OSPF/trunk logic for this cable
        }

        Color cableColor = Color{148, 163, 184, 255};  // ← existing line, unchanged
```

- [ ] **Step 2: Crashed device — red overlay + badge in DrawDeviceNode**

In `DrawDeviceNode`, find the selection-highlight block (around line 9–10):

```cpp
    if (n.selected)
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, 2.5f, WHITE);
```

Add the crashed overlay immediately after that block:

```cpp
    if (n.selected)
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, 2.5f, WHITE);

    if (n.crashed) {
        DrawRectangleRounded(r, 0.3f, 8, Color{239, 68, 68, 50});
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, 2.0f, Color{239, 68, 68, 255});
        float bx = n.position.x;
        float by = n.position.y - NODE_H / 2.f - 18.f;
        DrawCircle((int)bx, (int)by, 12.f, Color{239, 68, 68, 255});
        const char* xmark = "\xe2\x9c\x97";
        int xw = MeasureText(xmark, 11);
        DrawText(xmark, (int)bx - xw / 2, (int)(by - 6.f), 11, WHITE);
    }
```

- [ ] **Step 3: New log icon cases in DrawLogConsole**

In `DrawLogConsole`, find the switch statement (around line 302–323). Add three new cases **before** the `default:` case:

```cpp
            case LOG_LINK_DOWN:
                icon    = "!";
                icColor = Color{239, 68, 68, 255};
                break;
            case LOG_DEVICE_CRASH:
                icon    = "!";
                icColor = Color{239, 68, 68, 255};
                break;
            case LOG_RESTORED:
                icon    = "+";
                icColor = Color{34, 197, 94, 255};
                break;
            default:
```

- [ ] **Step 4: Build to verify clean**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | tail -5
```

Expected: zero errors.

- [ ] **Step 5: Commit**

```bash
git add src/NetworkCanvas.cpp
git commit -m "feat: broken cables red, crashed devices red overlay, log icons for fault events"
```

---

## Task 5: Context Menu — Dynamic Items and Fault Actions

**Files:**

- Modify: `src/UI.h`
- Modify: `src/UI.cpp`
- Modify: `src/NetworkCanvas.cpp`

Menu items are now dynamic (Cut Link vs Restore Link; Crash Device vs Restore Device). A `targetBroken` field on `ContextMenu` (set at right-click time in main.cpp — Task 6) drives the selection. `ExecuteMenuAction` gains a `logEntries` reference param.

**Note on item numbering shift:** The node menu gains a new item at position 1 (Crash/Restore Device). Delete moves from item 1 → item 2. Send Packet moves from item 2 → item 3. The full replacement below is authoritative.

- [ ] **Step 1: Add `targetBroken` to ContextMenu in UI.h**

Replace the `ContextMenu` struct:

```cpp
struct ContextMenu {
    bool        visible      = false;
    Vector2     screenPos    = {0.0f, 0.0f};
    Vector2     worldPos     = {0.0f, 0.0f};
    ContextType ctx          = CTX_NONE;
    int         targetId     = -1;
    int         hoverItem    = -1;
    bool        targetBroken = false;   // cable.broken or node.crashed at menu-open time
};
```

- [ ] **Step 2: Add `logEntries` param to ExecuteMenuAction declaration in UI.h**

```cpp
void ExecuteMenuAction(ContextMenu& menu,
                       std::vector<DeviceNode>& nodes,
                       std::vector<Cable>& cables,
                       int& selectedId,
                       PanelState& ps,
                       Camera2D& camera,
                       SimState& simState,
                       std::vector<LogEntry>& logEntries);
```

- [ ] **Step 3: Replace UpdateContextMenuHover in UI.cpp**

Full replacement (dynamic item arrays, same hover-detection logic):

```cpp
void UpdateContextMenuHover(ContextMenu& menu, Vector2 screenMouse) {
    if (!menu.visible) { menu.hoverItem = -1; return; }

    static const char* nodeItemsNormal[]  = {"Rename", "Crash Device", "Delete",
                                              "Send Packet To\xe2\x80\xa6", nullptr};
    static const char* nodeItemsCrashed[] = {"Rename", "Restore Device", "Delete",
                                              "Send Packet To\xe2\x80\xa6", nullptr};
    static const char* cableItemsNormal[] = {"Cut Link", "Delete Cable", nullptr};
    static const char* cableItemsBroken[] = {"Restore Link", "Delete Cable", nullptr};
    static const char* canvasItems[]      = {"Add PC Here", "Add Router Here",
                                             "Add Switch Here", "Reset View", nullptr};

    const char** items = nullptr;
    if      (menu.ctx == CTX_NODE)   items = menu.targetBroken ? nodeItemsCrashed : nodeItemsNormal;
    else if (menu.ctx == CTX_CABLE)  items = menu.targetBroken ? cableItemsBroken : cableItemsNormal;
    else if (menu.ctx == CTX_CANVAS) items = canvasItems;
    else { menu.hoverItem = -1; return; }

    int count = 0;
    while (items[count]) ++count;

    float h = (float)(count * MENU_ITEM_H + 8);
    float x = std::min(menu.screenPos.x, (float)(CANVAS_W - CONTEXT_MENU_W - 4));
    float y = std::min(menu.screenPos.y, (float)(CANVAS_H - (int)h - 4));

    menu.hoverItem = -1;
    for (int i = 0; i < count; ++i) {
        Rectangle ir = {x + 4, y + 4 + (float)(i * MENU_ITEM_H),
                        (float)(CONTEXT_MENU_W - 8), (float)MENU_ITEM_H};
        if (CheckCollisionPointRec(screenMouse, ir)) { menu.hoverItem = i; break; }
    }
}
```

- [ ] **Step 4: Replace ExecuteMenuAction in UI.cpp**

Full replacement. Node items: 0=Rename, 1=Crash/Restore Device, 2=Delete, 3=Send Packet. Cable items: 0=Cut/Restore Link, 1=Delete Cable.

```cpp
void ExecuteMenuAction(ContextMenu& menu, std::vector<DeviceNode>& nodes,
                       std::vector<Cable>& cables, int& selectedId,
                       PanelState& ps, Camera2D& camera, SimState& simState,
                       std::vector<LogEntry>& logEntries)
{
    if (simState.mode == SIM_ANIMATING) return;
    int item = menu.hoverItem;

    auto pushFaultLog = [&](LogType type, bool success, const std::string& path) {
        LogEntry le;
        le.type      = type;
        le.success   = success;
        le.pathStr   = path;
        le.timestamp = GetTime();
        if (logEntries.size() >= 50) logEntries.erase(logEntries.begin());
        logEntries.push_back(le);
    };

    if (menu.ctx == CTX_NODE) {
        if (item == 0) {  // Rename
            selectedId = menu.targetId;
            for (auto& n : nodes) n.selected = (n.id == selectedId);
            ps.activeTab   = TAB_CONFIG;
            ps.activeField = 0;
        } else if (item == 1) {  // Crash Device / Restore Device
            for (auto& n : nodes) {
                if (n.id != menu.targetId) continue;
                n.crashed = !n.crashed;
                if (n.crashed)
                    pushFaultLog(LOG_DEVICE_CRASH, false, "DEVICE CRASHED: " + n.label);
                else
                    pushFaultLog(LOG_RESTORED,     true,  "RESTORED: "       + n.label);
                break;
            }
        } else if (item == 2) {  // Delete
            cables.erase(std::remove_if(cables.begin(), cables.end(),
                [&](const Cable& c){
                    return c.fromId == menu.targetId || c.toId == menu.targetId;
                }), cables.end());
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const DeviceNode& n){ return n.id == menu.targetId; }),
                nodes.end());
            if (selectedId == menu.targetId) { selectedId = -1; ps.activeField = -1; }
            if (simState.srcId == menu.targetId) { simState.mode = SIM_IDLE; simState.srcId = -1; }
        } else if (item == 3) {  // Send Packet To…
            if (simState.mode == SIM_IDLE || simState.mode == SIM_SELECTING_DST) {
                simState.mode  = SIM_SELECTING_DST;
                simState.srcId = menu.targetId;
            }
        }
    } else if (menu.ctx == CTX_CABLE) {
        if (item == 0) {  // Cut Link / Restore Link
            if (menu.targetId >= 0 && menu.targetId < (int)cables.size()) {
                Cable& cab = cables[menu.targetId];
                cab.broken = !cab.broken;
                const DeviceNode* from = FindNode(nodes, cab.fromId);
                const DeviceNode* to   = FindNode(nodes, cab.toId);
                std::string label = (from ? from->label : "?") +
                                    " \xe2\x80\x94 " +
                                    (to   ? to->label   : "?");
                if (cab.broken)
                    pushFaultLog(LOG_LINK_DOWN, false, "LINK DOWN: "  + label);
                else
                    pushFaultLog(LOG_RESTORED,  true,  "RESTORED: "   + label);
            }
        } else if (item == 1) {  // Delete Cable
            if (menu.targetId >= 0 && menu.targetId < (int)cables.size())
                cables.erase(cables.begin() + menu.targetId);
        }
    } else if (menu.ctx == CTX_CANVAS) {
        if      (item == 0) nodes.push_back(SpawnNode(PC,     menu.worldPos));
        else if (item == 1) nodes.push_back(SpawnNode(ROUTER, menu.worldPos));
        else if (item == 2) nodes.push_back(SpawnNode(SWITCH, menu.worldPos));
        else if (item == 3) { camera.target = {0.0f, 0.0f}; camera.zoom = 1.0f; }
    }
}
```

- [ ] **Step 5: Replace DrawContextMenu in NetworkCanvas.cpp (same dynamic arrays)**

`DrawContextMenu` is at line 1067 in NetworkCanvas.cpp. Replace the entire function:

```cpp
void DrawContextMenu(const ContextMenu& menu, Vector2 screenMouse) {
    (void)screenMouse;
    if (!menu.visible) return;

    static const char* nodeItemsNormal[]  = {"Rename", "Crash Device", "Delete",
                                              "Send Packet To\xe2\x80\xa6", nullptr};
    static const char* nodeItemsCrashed[] = {"Rename", "Restore Device", "Delete",
                                              "Send Packet To\xe2\x80\xa6", nullptr};
    static const char* cableItemsNormal[] = {"Cut Link", "Delete Cable", nullptr};
    static const char* cableItemsBroken[] = {"Restore Link", "Delete Cable", nullptr};
    static const char* canvasItems[]      = {"Add PC Here", "Add Router Here",
                                             "Add Switch Here", "Reset View", nullptr};

    const char** items = nullptr;
    if      (menu.ctx == CTX_NODE)   items = menu.targetBroken ? nodeItemsCrashed : nodeItemsNormal;
    else if (menu.ctx == CTX_CABLE)  items = menu.targetBroken ? cableItemsBroken : cableItemsNormal;
    else if (menu.ctx == CTX_CANVAS) items = canvasItems;
    else return;

    int count = 0;
    while (items[count]) ++count;

    float h = (float)(count * MENU_ITEM_H + 8);
    float x = std::min(menu.screenPos.x, (float)(CANVAS_W - CONTEXT_MENU_W - 4));
    float y = std::min(menu.screenPos.y, (float)(CANVAS_H - (int)h - 4));

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

- [ ] **Step 6: Build to verify clean**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | tail -5
```

Expected: compile error on `ExecuteMenuAction` call in main.cpp — missing `logEntries` argument. This is expected and will be fixed in Task 6 Step 3. Alternatively, add a dummy `logEntries` argument temporarily to get a clean build, then remove it in Task 6.

If you want a clean build here: in `main.cpp` line ~228, temporarily change to:

```cpp
ExecuteMenuAction(contextMenu, nodes, cables, selectedId, ps, camera, simState, logEntries);
```

(logEntries is already declared at line 39, so this is the correct final form — add it now.)

Expected after: zero errors.

- [ ] **Step 7: Commit**

```bash
git add src/UI.h src/UI.cpp src/NetworkCanvas.cpp
git commit -m "feat: context menu Cut/Restore Link, Crash/Restore Device with log push"
```

---

## Task 6: main.cpp Wiring and Troubleshoot Mode

**Files:**

- Modify: `src/NetworkCanvas.h`
- Modify: `src/NetworkCanvas.cpp`
- Modify: `src/main.cpp`

Wires `targetBroken` at right-click time, passes `logEntries` to `ExecuteMenuAction`, adds `troubleshootMode` bool + T key, implements `DrawTroubleshootOverlay`, and draws the HUD badge.

- [ ] **Step 1: Declare DrawTroubleshootOverlay in NetworkCanvas.h**

Open `src/NetworkCanvas.h`. Add at the bottom of the draw-functions block (after `DrawBrokenPath`):

```cpp
void DrawTroubleshootOverlay(const std::vector<DeviceNode>& nodes,
                              const std::vector<Cable>& cables);
```

- [ ] **Step 2: Implement DrawTroubleshootOverlay in NetworkCanvas.cpp**

Add after `DrawBrokenPath` (line ~368):

```cpp
void DrawTroubleshootOverlay(const std::vector<DeviceNode>& nodes,
                              const std::vector<Cable>& cables)
{
    // "LINK DOWN" text badge below each broken cable's midpoint
    for (const auto& c : cables) {
        if (!c.broken) continue;
        const DeviceNode* from = FindNode(nodes, c.fromId);
        const DeviceNode* to   = FindNode(nodes, c.toId);
        if (!from || !to) continue;
        Vector2 p0  = GetPortPosition(*from, c.fromPort);
        Vector2 p3  = GetPortPosition(*to,   c.toPort);
        Vector2 mid = {(p0.x + p3.x) / 2.0f, (p0.y + p3.y) / 2.0f};
        const char* txt = "LINK DOWN";
        int tw = MeasureText(txt, 9);
        DrawRectangle((int)(mid.x - tw / 2 - 3), (int)(mid.y + 10), tw + 6, 14,
                      Color{239, 68, 68, 200});
        DrawText(txt, (int)(mid.x - tw / 2), (int)(mid.y + 12), 9, WHITE);
    }

    // "CRASHED" text badge below each crashed device
    for (const auto& n : nodes) {
        if (!n.crashed) continue;
        const char* txt = "CRASHED";
        int tw = MeasureText(txt, 9);
        int bx = (int)n.position.x;
        int by = (int)(n.position.y + NODE_H / 2.f + 4.f);
        DrawRectangle(bx - tw / 2 - 3, by, tw + 6, 14, Color{239, 68, 68, 200});
        DrawText(txt, bx - tw / 2, by + 2, 9, WHITE);
    }
}
```

- [ ] **Step 3: Add troubleshootMode state variable in main.cpp**

Find the state variable block near the top of main (around line 39–50). Add:

```cpp
    bool        troubleshootMode        = false;
```

Place it near `bool traceModalOpen = false;` or similar bool state variables.

- [ ] **Step 4: Set targetBroken when the right-click context menu opens**

Find the right-click handler in main.cpp (around line 442–456). Update all three branches:

```cpp
                if (hitIdx != -1) {
                    contextMenu.visible      = true;
                    contextMenu.ctx          = CTX_NODE;
                    contextMenu.targetId     = nodes[hitIdx].id;
                    contextMenu.targetBroken = nodes[hitIdx].crashed;      // ← add
                } else {
                    int ci = HitTestCable(cables, nodes, worldMouse, 6.0f);
                    if (ci != -1) {
                        contextMenu.visible      = true;
                        contextMenu.ctx          = CTX_CABLE;
                        contextMenu.targetId     = ci;
                        contextMenu.targetBroken = cables[ci].broken;      // ← add
                    } else {
                        contextMenu.visible      = true;
                        contextMenu.ctx          = CTX_CANVAS;
                        contextMenu.targetId     = -1;
                        contextMenu.targetBroken = false;                  // ← add
                    }
                }
```

- [ ] **Step 5: Pass logEntries to ExecuteMenuAction**

Find the `ExecuteMenuAction` call (line ~228):

```cpp
                    ExecuteMenuAction(contextMenu, nodes, cables, selectedId, ps, camera, simState, logEntries);
```

(If this was already updated at the end of Task 5 Step 6, confirm it's present. If not, add `logEntries` as the final argument.)

- [ ] **Step 6: Add T-key Troubleshoot Mode toggle**

Find the keyboard input section in main.cpp (where level hotkeys `1`–`0` are handled). Add the T-key handler in the same block:

```cpp
        if (IsKeyPressed(KEY_T) && gameMode != GAME_WIN)
            troubleshootMode = !troubleshootMode;
```

- [ ] **Step 7: Call DrawTroubleshootOverlay in the render loop**

Find the `DrawBrokenPath` call (line ~910). Add the troubleshoot overlay call immediately after:

```cpp
                    DrawBrokenPath(nodes, cables, lastFailedTrace);
                if (troubleshootMode)
                    DrawTroubleshootOverlay(nodes, cables);
```

- [ ] **Step 8: Draw HUD badge when troubleshoot mode is active**

Find the `DrawLevelHUD` call (line ~963–967). Add the badge immediately after:

```cpp
            DrawLevelHUD(currentLevel, activeLevelDef.title,
                         lastConditionsPassed,
                         (int)activeLevelDef.winConditions.size(),
                         starsEarned);
            if (troubleshootMode) {
                DrawRectangle(8, 34, 148, 18, Color{239, 68, 68, 200});
                DrawRectangleLinesEx({8, 34, 148, 18}, 1.0f, Color{239, 68, 68, 255});
                DrawText("TROUBLESHOOT [T]", 14, 38, 9, WHITE);
            }
```

- [ ] **Step 9: Build to verify clean**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | tail -5
```

Expected: zero errors.

- [ ] **Step 10: Full end-to-end smoke test**

Launch `./packet-path`. Press `1` for Level 1. Build a simple topology if needed.

**Cut Link test:**

1. Right-click a cable → menu shows "Cut Link" and "Delete Cable"
2. Click "Cut Link" → cable turns red with midpoint ✗ badge; log shows `! LINK DOWN: X — Y`
3. Send a packet across the broken cable → simulation fails, broken path overlay shows
4. Right-click the red cable → menu shows "Restore Link" and "Delete Cable"
5. Click "Restore Link" → cable returns to normal color; log shows `+ RESTORED: X — Y`
6. Send the packet again → succeeds

**Crash Device test:**

1. Right-click a device → menu shows "Rename", "Crash Device", "Delete", "Send Packet To…"
2. Click "Crash Device" → device gets red border + ✗ badge above; log shows `! DEVICE CRASHED: X`
3. Send a packet through the crashed device → simulation fails with "X is crashed" reason
4. Right-click the crashed device → menu shows "Rename", "Restore Device", "Delete", "Send Packet To…"
5. Click "Restore Device" → red border removed; log shows `+ RESTORED: X`

**Troubleshoot Mode test:**

1. Break one cable and crash one device
2. Press `T` → HUD shows `TROUBLESHOOT [T]` red badge; "LINK DOWN" and "CRASHED" text labels appear
3. Press `T` again → labels disappear

**Level JSON pre-injection test:**
Create `levels/level11.json` (copy any level, add `"broken": true` to one cable entry). Load it via `LoadLevel` path if your build supports it. Confirm the cable loads already broken and the win condition requires restoration.

- [ ] **Step 11: Regression — Levels 1–10 unaffected**

Quick pass: load each level (keys `1`–`0`), confirm no visual regressions (no accidental red cables, no crashed devices, menus still show correct items, win overlay still shows correctly). Confirm the Delete node and Delete cable actions still work (items shifted from 1→2 for Delete).

- [ ] **Step 12: Commit**

```bash
git add src/NetworkCanvas.h src/NetworkCanvas.cpp src/main.cpp
git commit -m "feat: wire failure injection — targetBroken, troubleshootMode, DrawTroubleshootOverlay, HUD badge"
```

---

## Level JSON Authoring Guide

For level designers adding pre-injected failures to later levels:

**Broken cable** — add `"broken": true` to the cable entry:

```json
{
  "from": 1, "fromPort": 0, "to": 2, "toPort": 1,
  "broken": true
}
```

**Crashed device** — add `"crashed": true` to the device entry:

```json
{
  "id": 3, "type": "ROUTER", "label": "R1",
  "x": 400, "y": 300,
  "crashed": true,
  "portIp0": "10.0.0.1/24"
}
```

**Fix-required win condition** — add `"requiresFix": true` to the condition:

```json
{
  "src": "PC-A", "dst": "PC-B",
  "description": "Restore connectivity: PC-A to PC-B",
  "requiresFix": true
}
```

Win conditions still check src→dst reachability. A pre-injected broken cable blocks the check; once the player restores it, the condition passes automatically. No logic change to `CheckWinConditions` is needed.

---

## Self-Review

### Spec Coverage

| Requirement | Task |
| ------------- | ------ |
| Right-click cable → Cut Link | Task 5 `ExecuteMenuAction` CTX_CABLE item 0 |
| Right-click cable (broken) → Restore Link | Task 5 `ExecuteMenuAction` CTX_CABLE item 0 toggle |
| Right-click device → Crash Device | Task 5 `ExecuteMenuAction` CTX_NODE item 1 |
| Right-click device (crashed) → Restore Device | Task 5 `ExecuteMenuAction` CTX_NODE item 1 toggle |
| Broken cable renders red | Task 4 `DrawAllCables` |
| Crashed device renders red overlay + badge | Task 4 `DrawDeviceNode` |
| Log console: LINK DOWN / DEVICE CRASHED / RESTORED | Tasks 4 + 5 |
| Simulation respects broken cables | Task 3 `FindL2Path` |
| Simulation fails on crashed device | Task 3 `SimulateForward` |
| Level JSON pre-injects `broken` and `crashed` | Task 2 `LoadLevel` |
| Win conditions: `requiresFix` field | Task 2 `WinCondition` |
| Troubleshoot Mode toggle (T key) | Task 6 |
| Troubleshoot Mode: text labels over all active failures | Task 6 `DrawTroubleshootOverlay` |
| Troubleshoot Mode: HUD badge | Task 6 main.cpp |
| No random/timed failures | Out of scope — not present anywhere |

### Placeholder Scan

No TBDs, no "add validation", no "similar to Task N" references. All code blocks are complete and show exact replacements or insertions.

### Type Consistency

- `Cable.broken` is `bool` throughout (Cable.h, SimulationEngine.cpp, NetworkCanvas.cpp, UI.cpp, main.cpp) ✓
- `DeviceNode.crashed` is `bool` throughout (Device.h, SimulationEngine.cpp, NetworkCanvas.cpp, UI.cpp, main.cpp) ✓
- `ContextMenu.targetBroken` is `bool` (UI.h); set in main.cpp; read in UI.cpp + NetworkCanvas.cpp ✓
- `ExecuteMenuAction` new signature: `..., std::vector<LogEntry>& logEntries` — declaration in UI.h and definition in UI.cpp match ✓
- `DrawTroubleshootOverlay` signature declared in NetworkCanvas.h; defined in NetworkCanvas.cpp; called in main.cpp — all match ✓
- `LOG_LINK_DOWN`, `LOG_DEVICE_CRASH`, `LOG_RESTORED` declared in Device.h (LogType enum); used in UI.cpp (push) and NetworkCanvas.cpp (switch) ✓

### Build Ordering

Task 1 → 2 → 3 each build clean independently (new fields have defaults; SimulationEngine gains guards that no caller triggers yet). Task 4 adds rendering that reads the new fields. Task 5 changes the UI — one expected compile error on `ExecuteMenuAction` call in main.cpp (fixed by adding `logEntries` at end of Task 5 or in Task 6). Task 6 is the final integration. No task leaves the build broken beyond the bridging note in Task 5 Step 6.
