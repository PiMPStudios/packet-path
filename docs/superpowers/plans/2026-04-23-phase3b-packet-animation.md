# Phase 3b: Packet Animation + Log Console — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Animate packets hop-by-hop along forwarding paths using a right-click "Send Packet To…" trigger, a glowing dot that travels along bezier cables, failure highlighting at the last reachable node, and a full-width log console strip at the bottom of the window.

**Architecture:** All changes are in `src/main.cpp` (currently ~1101 lines). A `SimState` struct (mode + srcId + `PacketAnim`) tracks the one-at-a-time simulation. `UpdatePacketAnim` advances a frame-time interpolation; `DrawPacketAnim` renders the glowing dot inside `BeginMode2D`. `DrawLogConsole` renders the bottom strip outside `BeginMode2D`. The forwarding engine (`SimulateForward`) from Phase 3a is called as-is — Phase 3b only adds the UX trigger and visual layer.

**Tech Stack:** C++17, raylib 5.5, macOS, `make` build system.

---

## File Structure

Single file modified: `src/main.cpp`

New symbols added (in order they appear in the file):
1. Constants: `LOG_H`, `CANVAS_H`, `HOP_DURATION`
2. Structs: `LogEntry`, `SimMode` enum, `PacketAnim`, `SimState`
3. Free functions (above `main()`): `GetFirstValidIp`, `FindCable`, `EvaluateCubicBezier`, `BuildPathStr`, `UpdatePacketAnim`, `DrawPacketAnim`, `DrawLogConsole`
4. Modified functions: `DrawDotGrid`, `UpdateContextMenuHover`, `DrawContextMenu`, `ExecuteMenuAction`
5. `main()`: `simState`, `logEntries`, ESC handler, spawn guard, LMB dest-click block, `UpdatePacketAnim` call, idle-transition, draw wiring

---

## Task 1: M3b.1 — Layout Constants + Log Console Strip

**Files:**
- Modify: `src/main.cpp`

**Context:** The log strip is 90px tall at the bottom of the window. `CANVAS_H = SCREEN_H - LOG_H = 630`. Everything currently using `SCREEN_H` for canvas-area calculations must switch to `CANVAS_H`. The log strip is drawn last and overdraw handles the panel bleed — no scissor mode needed.

- [ ] **Step 1: Add `LOG_H` and `CANVAS_H` constants**

Find the constants block (lines 10–25). After the `CONTEXT_MENU_W` line, add:

```cpp
static const int   LOG_H          = 90;
static const int   CANVAS_H       = SCREEN_H - LOG_H;  // 630
```

- [ ] **Step 2: Fix `DrawDotGrid` to clip to `CANVAS_H`**

Find `DrawDotGrid` at line ~120. Change the `botRight` line from:

```cpp
    Vector2 botRight = GetScreenToWorld2D({(float)CANVAS_W, (float)SCREEN_H}, cam);
```

to:

```cpp
    Vector2 botRight = GetScreenToWorld2D({(float)CANVAS_W, (float)CANVAS_H}, cam);
```

- [ ] **Step 3: Fix context menu Y clamping in `UpdateContextMenuHover` and `DrawContextMenu`**

Both functions contain this line (at ~line 691 and ~line 721):

```cpp
    float y = std::min(menu.screenPos.y, (float)(SCREEN_H - (int)h - 4));
```

Change both occurrences to:

```cpp
    float y = std::min(menu.screenPos.y, (float)(CANVAS_H - (int)h - 4));
```

- [ ] **Step 4: Fix `inCanvas` to exclude the log strip area**

Find in `main()` (line ~798):

```cpp
        bool inCanvas = (screenMouse.x < (float)CANVAS_W);
```

Change to:

```cpp
        bool inCanvas = (screenMouse.x < (float)CANVAS_W &&
                         screenMouse.y < (float)CANVAS_H);
```

- [ ] **Step 5: Fix camera offset and HUD hint text**

Find the camera initialization in `main()` (line ~776):

```cpp
    camera.offset   = {CANVAS_W / 2.0f, SCREEN_H / 2.0f};
```

Change to:

```cpp
    camera.offset   = {CANVAS_W / 2.0f, CANVAS_H / 2.0f};
```

Find the HUD hint text near end of draw loop (line ~1094–1095):

```cpp
            DrawText("P=PC  R=Router  S=Switch  Del=Delete  MMB=Pan  Scroll=Zoom  Drag-port=Cable  Esc=Cancel",
                     10, SCREEN_H - 24, 12, Color{100, 116, 139, 255});
```

Change `SCREEN_H - 24` to `CANVAS_H - 24`:

```cpp
            DrawText("P=PC  R=Router  S=Switch  Del=Delete  MMB=Pan  Scroll=Zoom  Drag-port=Cable  Esc=Cancel",
                     10, CANVAS_H - 24, 12, Color{100, 116, 139, 255});
```

- [ ] **Step 6: Add `LogEntry` struct**

Find `struct ForwardResult` (line ~52). Add `LogEntry` immediately after it (after the closing `};`):

```cpp
struct LogEntry {
    bool        success;
    std::string pathStr;   // e.g. "PC1 → R1 → PC2"
    std::string reason;    // "delivered" | "no route to X" | etc.
    float       timestamp; // GetTime() at moment of trigger
};
```

- [ ] **Step 7: Add `DrawLogConsole` free function**

Add this function immediately before `// ── Spawn helper ──` (just before `SpawnNode`):

```cpp
// ── Log console (drawn outside BeginMode2D, full-width bottom strip) ─────
void DrawLogConsole(const std::vector<LogEntry>& entries) {
    DrawRectangle(0, CANVAS_H, SCREEN_W, LOG_H, Color{10, 15, 28, 255});
    DrawLineEx({0.f, (float)CANVAS_H}, {(float)SCREEN_W, (float)CANVAS_H},
               1.f, Color{51, 65, 85, 255});
    DrawText("LOG", 12, CANVAS_H + 8, 9, Color{71, 85, 105, 255});

    if (entries.empty()) {
        DrawText("No simulations run yet", 36, CANVAS_H + 36, 10,
                 Color{51, 65, 85, 255});
        return;
    }

    int maxLines = 3;
    int startIdx = std::max(0, (int)entries.size() - maxLines);
    int shown    = std::min(maxLines, (int)entries.size());
    for (int i = 0; i < shown; ++i) {
        const auto& e = entries[startIdx + i];
        int lineY = CANVAS_H + 8 + (shown - 1 - i) * 24;  // newest at top

        int   secs = (int)e.timestamp;
        int   mins = secs / 60; secs %= 60;
        char  tsbuf[16];
        std::snprintf(tsbuf, sizeof(tsbuf), "[%02d:%02d]", mins, secs);
        DrawText(tsbuf, 36, lineY, 10, Color{71, 85, 105, 255});

        const char* icon    = e.success ? "\xe2\x9c\x93" : "\xe2\x9c\x97";
        Color       icColor = e.success ? Color{34, 197, 94, 255}
                                        : Color{239, 68, 68, 255};
        DrawText(icon, 90, lineY, 10, icColor);

        std::string msg = e.pathStr + "  \xe2\x80\x94  " + e.reason;
        DrawText(msg.c_str(), 108, lineY, 10, icColor);
    }
}
```

- [ ] **Step 8: Declare `logEntries` in `main()` and wire `DrawLogConsole` into the draw loop**

In `main()`, after the `ContextMenu contextMenu;` declaration (line ~793), add:

```cpp
    std::vector<LogEntry> logEntries;
```

In the draw loop, find:

```cpp
            DrawContextMenu(contextMenu, screenMouse);
```

Add `DrawLogConsole` immediately after it:

```cpp
            DrawContextMenu(contextMenu, screenMouse);
            DrawLogConsole(logEntries);
```

- [ ] **Step 9: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make
```

Expected: zero warnings, zero errors.

- [ ] **Step 10: Run and verify layout**

```bash
./packet-path
```

Verify:
- Dark log strip visible at bottom 90px, full width
- "LOG" label and "No simulations run yet" placeholder visible
- Dot grid stops at the log strip boundary (no dots bleeding into the strip)
- Context menu never opens inside the log strip (right-click near bottom)
- HUD hint text now appears just above the log strip, not inside it

- [ ] **Step 11: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m3b.1): log console strip, CANVAS_H layout, dotgrid clip, inCanvas Y guard"
```

---

## Task 2: M3b.2 — SimState + "Send Packet To…" + SIM_SELECTING_DST

**Files:**
- Modify: `src/main.cpp`

**Context:** Add the simulation mode state machine and wire the "Send Packet To…" context menu item. When selected, the canvas enters `SIM_SELECTING_DST` mode: nodes glow with a blue ring and a status label appears at the top of the canvas. ESC cancels.

- [ ] **Step 1: Add `SimMode`, `PacketAnim`, and `SimState` structs**

Find `struct LogEntry` added in Task 1. Add the following immediately after it:

```cpp
enum SimMode { SIM_IDLE, SIM_SELECTING_DST, SIM_ANIMATING };

struct PacketAnim {
    ForwardResult result;
    int   hop       = 0;     // current hop segment index (path[hop] → path[hop+1])
    float t         = 0.f;   // interpolation within current hop [0..1]
    bool  done      = false;
    float failPulse = 0.f;   // countdown for red-pulse effect (seconds)
};

struct SimState {
    SimMode    mode  = SIM_IDLE;
    int        srcId = -1;
    PacketAnim anim;
};
```

- [ ] **Step 2: Add "Send Packet To…" to `nodeItems` in `UpdateContextMenuHover`**

Find in `UpdateContextMenuHover` (line ~676):

```cpp
    static const char* nodeItems[]   = {"Rename", "Delete", nullptr};
```

Change to:

```cpp
    static const char* nodeItems[]   = {"Rename", "Delete", "Send Packet To\xe2\x80\xa6", nullptr};
```

(`\xe2\x80\xa6` is the UTF-8 ellipsis character `…`)

- [ ] **Step 3: Add "Send Packet To…" to `nodeItems` in `DrawContextMenu`**

Find in `DrawContextMenu` (line ~705):

```cpp
    static const char* nodeItems[]   = {"Rename", "Delete", nullptr};
```

Change to:

```cpp
    static const char* nodeItems[]   = {"Rename", "Delete", "Send Packet To\xe2\x80\xa6", nullptr};
```

- [ ] **Step 4: Add `SimState&` parameter to `ExecuteMenuAction` and handle item 2**

Find the `ExecuteMenuAction` signature (line ~735):

```cpp
void ExecuteMenuAction(ContextMenu& menu, std::vector<DeviceNode>& nodes,
                       std::vector<Cable>& cables, int& selectedId,
                       PanelState& ps, Camera2D& camera)
```

Change to:

```cpp
void ExecuteMenuAction(ContextMenu& menu, std::vector<DeviceNode>& nodes,
                       std::vector<Cable>& cables, int& selectedId,
                       PanelState& ps, Camera2D& camera, SimState& simState)
```

Find the `CTX_NODE` block inside `ExecuteMenuAction`:

```cpp
    if (menu.ctx == CTX_NODE) {
        if (item == 0) {  // Rename — select node and focus hostname field
            selectedId = menu.targetId;
            for (auto& n : nodes) n.selected = (n.id == selectedId);
            ps.activeTab   = TAB_CONFIG;
            ps.activeField = 0;
        } else if (item == 1) {  // Delete
```

Add item 2 handling after item 1's closing brace:

```cpp
    if (menu.ctx == CTX_NODE) {
        if (item == 0) {  // Rename — select node and focus hostname field
            selectedId = menu.targetId;
            for (auto& n : nodes) n.selected = (n.id == selectedId);
            ps.activeTab   = TAB_CONFIG;
            ps.activeField = 0;
        } else if (item == 1) {  // Delete
            cables.erase(std::remove_if(cables.begin(), cables.end(),
                [&](const Cable& c){
                    return c.fromId == menu.targetId || c.toId == menu.targetId;
                }), cables.end());
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const DeviceNode& n){ return n.id == menu.targetId; }),
                nodes.end());
            if (selectedId == menu.targetId) { selectedId = -1; ps.activeField = -1; }
        } else if (item == 2) {  // Send Packet To…
            simState.mode  = SIM_SELECTING_DST;
            simState.srcId = menu.targetId;
        }
    } else if (menu.ctx == CTX_CABLE) {
```

- [ ] **Step 5: Update the `ExecuteMenuAction` call site in `main()`**

Find (line ~856):

```cpp
                    ExecuteMenuAction(contextMenu, nodes, cables, selectedId, ps, camera);
```

Change to:

```cpp
                    ExecuteMenuAction(contextMenu, nodes, cables, selectedId, ps, camera, simState);
```

- [ ] **Step 6: Declare `SimState simState` in `main()`**

In `main()`, after `std::vector<LogEntry> logEntries;` (added in Task 1), add:

```cpp
    SimState simState;
```

- [ ] **Step 7: Update spawn key guard and ESC handler for `SIM_SELECTING_DST`**

Find (line ~801):

```cpp
        if (inCanvas && ps.activeField == -1 && ps.activeRouteField == -1) {
```

Change to:

```cpp
        if (inCanvas && ps.activeField == -1 && ps.activeRouteField == -1 &&
            simState.mode == SIM_IDLE) {
```

Find the ESC handler (line ~807). After the existing `else if (connecting)` branch, add:

```cpp
            } else if (simState.mode == SIM_SELECTING_DST) {
                simState.mode  = SIM_IDLE;
                simState.srcId = -1;
```

The full ESC block becomes:

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
            } else if (simState.mode == SIM_SELECTING_DST) {
                simState.mode  = SIM_IDLE;
                simState.srcId = -1;
            }
        }
```

- [ ] **Step 8: Draw SIM_SELECTING_DST visuals in the draw loop**

Inside `BeginMode2D`, after the `for (const auto& n : nodes) DrawDeviceNode(n);` loop, add:

```cpp
                // SIM_SELECTING_DST — ring on all eligible destination nodes
                if (simState.mode == SIM_SELECTING_DST) {
                    for (const auto& n : nodes) {
                        if (n.id == simState.srcId) {
                            // Bright ring on source
                            DrawCircleLinesV(n.position, NODE_W * 0.6f,
                                             Color{34, 197, 94, 200});
                        } else {
                            // Faint blue ring on valid destinations
                            DrawCircleLinesV(n.position, NODE_W * 0.6f,
                                             Color{96, 165, 250, 100});
                        }
                    }
                }
```

Outside `EndMode2D` (after it, before `DrawPanel`), add the status label:

```cpp
            if (simState.mode == SIM_SELECTING_DST) {
                const char* hint = "Click destination node  \xe2\x80\x94  ESC to cancel";
                int tw = MeasureText(hint, 12);
                DrawText(hint, (CANVAS_W - tw) / 2, 12, 12,
                         Color{148, 163, 184, 255});
            }
```

- [ ] **Step 9: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make
```

Expected: zero warnings.

- [ ] **Step 10: Run and verify**

```bash
./packet-path
```

- Add a PC and a Router. Right-click the PC → menu shows "Rename", "Delete", "Send Packet To…".
- Click "Send Packet To…" → all nodes get rings (green on PC, blue on Router), status hint appears at top.
- Press ESC → rings disappear, mode returns to idle.
- Verify P/R/S spawn keys don't fire while in selecting mode.

- [ ] **Step 11: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m3b.2): SimState, Send Packet To context menu, SIM_SELECTING_DST visual"
```

---

## Task 3: M3b.3 — Helper Functions + UpdatePacketAnim + Destination Click

**Files:**
- Modify: `src/main.cpp`

**Context:** Add the pure-logic helpers and wire the destination click. When the user clicks a node in `SIM_SELECTING_DST` mode, `SimulateForward` is called, a `LogEntry` is pushed, and `simState.mode` becomes `SIM_ANIMATING`. `UpdatePacketAnim` advances `t` and `hop` each frame.

- [ ] **Step 1: Add `HOP_DURATION` constant**

In the constants block (after `CANVAS_H`), add:

```cpp
static const float HOP_DURATION   = 0.4f;  // seconds per hop segment
```

- [ ] **Step 2: Add `GetFirstValidIp` free function**

Add immediately before `DrawLogConsole` (added in Task 1):

```cpp
// Returns first valid plain IP (no prefix) from a node's interfaces.
std::string GetFirstValidIp(const DeviceNode& n) {
    for (int i = 0; i < PORTS_PER_NODE; ++i) {
        const auto& ip = n.portIp[i];
        auto slash = ip.find('/');
        std::string plain = (slash != std::string::npos) ? ip.substr(0, slash) : ip;
        if (ValidateIPOnly(plain)) return plain;
    }
    auto slash = n.mgmtIp.find('/');
    std::string plain = (slash != std::string::npos)
                      ? n.mgmtIp.substr(0, slash) : n.mgmtIp;
    if (ValidateIPOnly(plain)) return plain;
    return "";
}
```

- [ ] **Step 3: Add `FindCable` free function**

Add immediately after `GetFirstValidIp`:

```cpp
// Returns first cable connecting node a to node b (either direction).
const Cable* FindCable(const std::vector<Cable>& cables, int a, int b) {
    for (const auto& c : cables)
        if ((c.fromId == a && c.toId == b) || (c.fromId == b && c.toId == a))
            return &c;
    return nullptr;
}
```

- [ ] **Step 4: Add `EvaluateCubicBezier` free function**

Add immediately after `FindCable`:

```cpp
Vector2 EvaluateCubicBezier(Vector2 p0, Vector2 c1, Vector2 c2, Vector2 p3, float t) {
    float it = 1.f - t;
    return {
        it*it*it*p0.x + 3*it*it*t*c1.x + 3*it*t*t*c2.x + t*t*t*p3.x,
        it*it*it*p0.y + 3*it*it*t*c1.y + 3*it*t*t*c2.y + t*t*t*p3.y
    };
}
```

- [ ] **Step 5: Add `BuildPathStr` free function**

Add immediately after `EvaluateCubicBezier`:

```cpp
std::string BuildPathStr(const std::vector<int>& path,
                         const std::vector<DeviceNode>& nodes) {
    std::string s;
    for (int i = 0; i < (int)path.size(); ++i) {
        if (i > 0) s += " \xe2\x86\x92 ";   // UTF-8 →
        const DeviceNode* n = FindNode(nodes, path[i]);
        s += n ? n->label : "?";
    }
    return s;
}
```

- [ ] **Step 6: Add `UpdatePacketAnim` free function**

Add immediately after `BuildPathStr`:

```cpp
void UpdatePacketAnim(PacketAnim& anim, float dt,
                      const std::vector<DeviceNode>& nodes,
                      const std::vector<Cable>& cables)
{
    (void)nodes; (void)cables;
    if (anim.done) {
        anim.failPulse = std::max(0.f, anim.failPulse - dt);
        return;
    }

    const auto& path = anim.result.path;
    if ((int)path.size() <= 1) { anim.done = true; return; }

    anim.t += dt / HOP_DURATION;
    if (anim.t >= 1.f) {
        anim.t = 0.f;
        anim.hop++;
        if (anim.hop >= (int)path.size() - 1) {
            anim.done = true;
            if (!anim.result.success)
                anim.failPulse = 0.5f;
        }
    }
}
```

- [ ] **Step 7: Add destination click handler in the LMB pressed block**

Find the LMB pressed block (line ~852):

```cpp
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (contextMenu.visible) {
                if (contextMenu.hoverItem != -1)
                    ExecuteMenuAction(contextMenu, nodes, cables, selectedId, ps, camera, simState);
                contextMenu.visible = false;
            } else if (inCanvas) {
```

Add a new `else if` branch for `SIM_SELECTING_DST` **before** the existing `else if (inCanvas)`:

```cpp
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (contextMenu.visible) {
                if (contextMenu.hoverItem != -1)
                    ExecuteMenuAction(contextMenu, nodes, cables, selectedId, ps, camera, simState);
                contextMenu.visible = false;
            } else if (simState.mode == SIM_SELECTING_DST && inCanvas) {
                // Destination selection — find clicked node
                int dstId = -1;
                for (int i = (int)nodes.size() - 1; i >= 0; --i) {
                    if (CheckCollisionPointRec(worldMouse, GetNodeRect(nodes[i]))) {
                        dstId = nodes[i].id;
                        break;
                    }
                }
                // Clicking empty canvas or src node is a no-op
                if (dstId != -1 && dstId != simState.srcId) {
                    const DeviceNode* dst = FindNode(nodes, dstId);
                    std::string destIp = dst ? GetFirstValidIp(*dst) : "";
                    LogEntry le;
                    if (destIp.empty()) {
                        le.success   = false;
                        const DeviceNode* src = FindNode(nodes, simState.srcId);
                        le.pathStr   = (src ? src->label : "?") + " \xe2\x86\x92 " +
                                       (dst ? dst->label : "?");
                        le.reason    = "destination has no configured IP";
                        le.timestamp = GetTime();
                    } else {
                        ForwardResult fr = SimulateForward(simState.srcId, destIp,
                                                           nodes, cables);
                        simState.anim = PacketAnim{fr, 0, 0.f, false, 0.f};
                        le.success    = fr.success;
                        le.pathStr    = BuildPathStr(fr.path, nodes);
                        le.reason     = fr.reason;
                        le.timestamp  = GetTime();
                        simState.mode = SIM_ANIMATING;
                    }
                    if (logEntries.size() >= 50)
                        logEntries.erase(logEntries.begin());
                    logEntries.push_back(le);
                    if (simState.mode != SIM_ANIMATING) {
                        simState.mode  = SIM_IDLE;
                        simState.srcId = -1;
                    }
                }
                // else: no-op, stay in SIM_SELECTING_DST
            } else if (inCanvas) {
```

- [ ] **Step 8: Call `UpdatePacketAnim` and handle animation→idle transition**

Find the selection-reset block near the end of the main loop (just before `// ── Draw ──`). Add after it:

```cpp
        // ── Packet animation update ───────────────────────────────────────
        if (simState.mode == SIM_ANIMATING) {
            UpdatePacketAnim(simState.anim, GetFrameTime(), nodes, cables);
            if (simState.anim.done && simState.anim.failPulse <= 0.f) {
                simState.mode  = SIM_IDLE;
                simState.srcId = -1;
            }
        }
```

- [ ] **Step 9: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make
```

Expected: zero warnings.

- [ ] **Step 10: Run and verify**

```bash
./packet-path
```

- Add PC1 (`portIp[0] = 10.0.0.2/24`, static route `0.0.0.0/0 → 10.0.0.1`) and Router1 (`portIp[0] = 10.0.0.1/24, portIp[1] = 10.0.1.1/24`). Connect PC1 port0 ↔ Router1 port0.
- Right-click PC1 → "Send Packet To…" → click Router1.
- Verify: log strip gains a new entry (green ✓ or red ✗). No crash.
- Verify: trying to right-click during `SIM_ANIMATING` is fine (animating state transitions to idle after ~0.4s × hops).

- [ ] **Step 11: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m3b.3): helpers, UpdatePacketAnim, destination click handler, log entry push"
```

---

## Task 4: M3b.4 — DrawPacketAnim

**Files:**
- Modify: `src/main.cpp`

**Context:** The glowing dot travels along the bezier cable from node to node. The dot is always green during travel. On failure, after the last hop completes, the src's failPulse countdown draws an expanding red ring on the failed node. `DrawPacketAnim` is called inside `BeginMode2D`, after `DrawAllCables` and before the node-drawing loop.

- [ ] **Step 1: Add `DrawPacketAnim` free function**

Add immediately after `UpdatePacketAnim`:

```cpp
void DrawPacketAnim(const PacketAnim& anim,
                    const std::vector<DeviceNode>& nodes,
                    const std::vector<Cable>& cables)
{
    const auto& path = anim.result.path;
    if (path.empty()) return;

    // Failure pulse — red ring expanding on the last node
    if (anim.failPulse > 0.f) {
        const DeviceNode* failNode = FindNode(nodes, path.back());
        if (failNode) {
            float frac = anim.failPulse / 0.5f;   // 1..0 as pulse fades
            float r    = 30.f + 20.f * (1.f - frac);
            DrawCircleV(failNode->position, r,
                        Color{239, 68, 68, (unsigned char)(frac * 80.f)});
            DrawCircleLinesV(failNode->position, r, Color{239, 68, 68, 180});
        }
        return;
    }

    if (anim.done) return;
    if (anim.hop >= (int)path.size() - 1) return;

    int fromId = path[anim.hop];
    int toId   = path[anim.hop + 1];

    const DeviceNode* fromNode = FindNode(nodes, fromId);
    const DeviceNode* toNode   = FindNode(nodes, toId);
    const Cable*      cable    = FindCable(cables, fromId, toId);
    if (!fromNode || !toNode || !cable) return;

    // Resolve port indices (cable can be stored in either direction)
    int fromPort = (cable->fromId == fromId) ? cable->fromPort : cable->toPort;
    int toPort   = (cable->fromId == toId)   ? cable->fromPort : cable->toPort;

    Vector2 p0 = GetPortPosition(*fromNode, fromPort);
    Vector2 p3 = GetPortPosition(*toNode,   toPort);
    Vector2 c1 = BezierCtrl(p0, fromPort);
    Vector2 c2 = BezierCtrl(p3, toPort);

    Vector2 pos = EvaluateCubicBezier(p0, c1, c2, p3, anim.t);

    // Green glow (outer) + core dot — always green during travel
    DrawCircleV(pos, 14.f, Color{34, 197, 94, 55});
    DrawCircleV(pos, 7.f,  Color{34, 197, 94, 255});
}
```

- [ ] **Step 2: Wire `DrawPacketAnim` into the draw loop**

Find inside `BeginMode2D` (line ~1066–1068):

```cpp
                DrawDotGrid(camera);
                DrawAllCables(cables, nodes);
```

Add `DrawPacketAnim` immediately after `DrawAllCables`:

```cpp
                DrawDotGrid(camera);
                DrawAllCables(cables, nodes);
                DrawPacketAnim(simState.anim, nodes, cables);
```

- [ ] **Step 3: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make
```

Expected: zero warnings.

- [ ] **Step 4: Run and verify**

```bash
./packet-path
```

Set up topology: PC1 (portIp[0] = `10.0.0.2/24`, static route `0.0.0.0/0` → `10.0.0.1`) ↔ Router1 (portIp[0] = `10.0.0.1/24`, portIp[1] = `10.0.1.1/24`) ↔ PC2 (portIp[0] = `10.0.1.2/24`).

- Right-click PC1 → "Send Packet To…" → click PC2.
- **Expected:** Green glowing dot travels from PC1 to Router1 (hop 1, 0.4s), then Router1 to PC2 (hop 2, 0.4s). Reaches PC2, animation ends.
- Log strip shows: `✓  PC1 → Router1 → PC2  —  delivered`

- Right-click PC1 → "Send Packet To…" → click a node with no configured IP.
- **Expected:** Log entry `✗  PC1 → SomeNode  —  destination has no configured IP`. No animation dot.

- Right-click PC1 → "Send Packet To…" → click Router1 (Router1 has no route back, and PC1 has default route but Router1 may not have a route to an external IP).
- Add a PC3 with no routes reachable from PC1. Click PC3 as destination.
- **Expected:** Red dot stops at last reachable node; red pulse ring expands on that node.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m3b.4): DrawPacketAnim — glowing dot and failure pulse"
```

---

## Task 5: M3b.5 — Integration, Polish, and Edge Cases

**Files:**
- Modify: `src/main.cpp`

**Context:** Verify the full end-to-end flow, add remaining guards, and ensure all Phase 2 and Phase 3a features still work correctly. This task is mostly run-and-verify with targeted small fixes.

- [ ] **Step 1: Guard KEY_DELETE during SIM_ANIMATING**

Find (line ~820):

```cpp
        if (ps.activeField == -1 && ps.activeRouteField == -1 &&
            IsKeyPressed(KEY_DELETE) && selectedId != -1) {
```

Change to:

```cpp
        if (ps.activeField == -1 && ps.activeRouteField == -1 &&
            simState.mode == SIM_IDLE &&
            IsKeyPressed(KEY_DELETE) && selectedId != -1) {
```

This prevents deleting a node while a packet is animating through it.

- [ ] **Step 2: Cancel SIM_SELECTING_DST when RMB is pressed**

Find the RMB handler (line ~925):

```cpp
        if (inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            connecting  = false;
            hoverNodeId = -1;
            hoverPort   = -1;
            contextMenu.screenPos = screenMouse;
```

Add cancellation at the top of this block:

```cpp
        if (inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            if (simState.mode == SIM_SELECTING_DST) {
                simState.mode  = SIM_IDLE;
                simState.srcId = -1;
                // Don't open a context menu while cancelling
            } else {
            connecting  = false;
            hoverNodeId = -1;
            hoverPort   = -1;
            contextMenu.screenPos = screenMouse;
            contextMenu.worldPos  = worldMouse;
            contextMenu.hoverItem = -1;
            ps.activeField        = -1;
            ps.activeRouteField   = -1;

            // Priority: node body > cable > canvas
            int hitIdx = -1;
            for (int i = (int)nodes.size() - 1; i >= 0; --i) {
                if (CheckCollisionPointRec(worldMouse, GetNodeRect(nodes[i]))) {
                    hitIdx = i;
                    break;
                }
            }
            if (hitIdx != -1) {
                contextMenu.visible  = true;
                contextMenu.ctx      = CTX_NODE;
                contextMenu.targetId = nodes[hitIdx].id;
            } else {
                int ci = HitTestCable(cables, nodes, worldMouse, 6.0f);
                if (ci != -1) {
                    contextMenu.visible  = true;
                    contextMenu.ctx      = CTX_CABLE;
                    contextMenu.targetId = ci;
                } else {
                    contextMenu.visible  = true;
                    contextMenu.ctx      = CTX_CANVAS;
                    contextMenu.targetId = -1;
                }
            }
            }  // closes else
        }
```

- [ ] **Step 3: Block new "Send Packet To…" while animating**

In `ExecuteMenuAction`, the `item == 2` handler added in Task 2:

```cpp
        } else if (item == 2) {  // Send Packet To…
            simState.mode  = SIM_SELECTING_DST;
            simState.srcId = menu.targetId;
        }
```

Change to:

```cpp
        } else if (item == 2) {  // Send Packet To…
            if (simState.mode == SIM_IDLE) {
                simState.mode  = SIM_SELECTING_DST;
                simState.srcId = menu.targetId;
            }
        }
```

- [ ] **Step 4: Dim "Send Packet To…" menu item when animating**

In `DrawContextMenu`, find the item rendering loop:

```cpp
    for (int i = 0; i < count; ++i) {
        Rectangle ir = {x + 4, y + 4 + (float)(i * MENU_ITEM_H),
                        (float)(CONTEXT_MENU_W - 8), (float)MENU_ITEM_H};
        if (menu.hoverItem == i)
            DrawRectangleRounded(ir, 0.08f, 4, Color{51, 65, 85, 255});
        DrawText(items[i], (int)ir.x + 8, (int)ir.y + 7, 13, WHITE);
    }
```

The `DrawContextMenu` function doesn't have access to `simState` (it's a draw-only function). The simplest approach is to just let the guard in `ExecuteMenuAction` silently absorb the click. No visual dimming needed for Phase 3b — leave this as-is.

- [ ] **Step 5: Build clean**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make
```

Expected: zero warnings, zero errors.

- [ ] **Step 6: Full end-to-end verification**

```bash
./packet-path
```

Run through the complete acceptance criteria:

**Layout:**
- [ ] Log strip visible at bottom 90px, full width, dark background, "LOG" label
- [ ] Dot grid stops at CANVAS_H boundary
- [ ] Context menus never open inside log strip (right-click near bottom edge)
- [ ] HUD hint text appears above log strip

**Simulation trigger:**
- [ ] Right-click a node → context menu shows "Rename", "Delete", "Send Packet To…"
- [ ] Click "Send Packet To…" → blue rings on all nodes, green ring on src, hint label at top
- [ ] ESC cancels → rings disappear
- [ ] RMB while selecting → cancels (no context menu opens)
- [ ] P/R/S spawn keys blocked during selecting mode
- [ ] DEL key blocked during animating mode

**Packet animation (requires configured topology: PC1 ↔ Router1 ↔ PC2):**
- [ ] PC1 → PC2: green dot travels PC1 → R1 → PC2, completes, log shows ✓ delivered
- [ ] PC1 → unconfigured node: log shows ✗ destination has no configured IP, no dot
- [ ] PC1 → unreachable node (no route): red dot stops at last node, red pulse, log shows ✗
- [ ] path.size() == 1 (connected at source, e.g. PC1 → own subnet): delivers immediately
- [ ] One sim at a time: cannot trigger new sim while animating

**Log console:**
- [ ] Each simulation pushes one entry (newest at top, green/red coloring)
- [ ] "No simulations run yet" when no sims run
- [ ] Multiple entries stack correctly (max 3 shown, newest at top)

**Phase 2 regression:**
- [ ] Canvas drag, cable connect, context menu, hostname/IP editing all still work
- [ ] Routes tab add/delete still works
- [ ] Context menus clamp to CANVAS_H (not SCREEN_H)

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m3b.5): KEY_DELETE guard, RMB cancel, SIM_IDLE block, integration verified"
```

---

## Phase 3b Exit

After all five tasks pass:

```bash
git log --oneline
```

Expected history (most recent first):
```
feat(m3b.5): KEY_DELETE guard, RMB cancel, SIM_IDLE block, integration verified
feat(m3b.4): DrawPacketAnim — glowing dot and failure pulse
feat(m3b.3): helpers, UpdatePacketAnim, destination click handler, log entry push
feat(m3b.2): SimState, Send Packet To context menu, SIM_SELECTING_DST visual
feat(m3b.1): log console strip, CANVAS_H layout, dotgrid clip, inCanvas Y guard
```

Final build:

```bash
make && ./packet-path
```

Expected: zero warnings, game runs cleanly, packet animation fully functional, log console populates with each simulation.

---

## Acceptance Criteria Summary

| Feature | Verified in |
|---|---|
| `LOG_H = 90`, `CANVAS_H = 630` | Task 1 |
| Dot grid clips to `CANVAS_H` | Task 1 |
| `inCanvas` excludes log strip | Task 1 |
| Context menus clamp to `CANVAS_H` | Task 1 |
| `LogEntry` struct + `DrawLogConsole` | Task 1 |
| `SimMode` / `PacketAnim` / `SimState` | Task 2 |
| "Send Packet To…" menu item | Task 2 |
| `SIM_SELECTING_DST` visual (rings + hint) | Task 2 |
| ESC cancels selecting mode | Task 2 |
| `GetFirstValidIp`, `FindCable`, `EvaluateCubicBezier`, `BuildPathStr` | Task 3 |
| `UpdatePacketAnim` | Task 3 |
| Destination click → `SimulateForward` → log entry | Task 3 |
| Animation → idle transition | Task 3 |
| Green dot travels along bezier | Task 4 |
| Failure red pulse on last node | Task 4 |
| KEY_DELETE blocked during animation | Task 5 |
| RMB cancels selecting mode | Task 5 |
| One sim at a time enforced | Task 5 |
| Phase 2 + 3a regression clean | Task 5 |
