# Phase 3b: Packet Animation + Log Console — Design Spec

**Date:** 2026-04-23  
**Branch:** `phase-3b-packet-animation` (to be created from `main`)  
**Builds on:** Phase 3a (`main` @ `5ecff90`, ~1101 lines, `src/main.cpp`)

---

## Goal

Animate packets along the forwarding paths computed by `SimulateForward`. Add a right-click "Send Packet To…" trigger, a glowing dot that travels hop-by-hop along bezier cables, failure highlighting at the last reachable node, and a full-width log console strip at the bottom of the window.

---

## Design Decisions (locked)

| Decision | Choice | Rationale |
|---|---|---|
| Simulation trigger | Right-click node → "Send Packet To…" → click destination | Spatial, consistent with existing right-click UX |
| Packet appearance | Glowing dot (green = success, red = failure) | Clean, immediately readable |
| Failure behavior | Stop at last reachable node + red pulse on that node | Shows exactly where forwarding broke |
| Log console placement | Bottom strip, full width, 90px, always visible | Familiar simulation-tool pattern |
| Animation speed | 0.4s per hop | Readable without feeling slow |
| Concurrent simulations | One at a time | Avoids visual clutter |
| File structure | Single `src/main.cpp` | Consistent with Phases 1–3a |

---

## Layout Change

Two new constants:

```cpp
static const int LOG_H    = 90;
static const int CANVAS_H = SCREEN_H - LOG_H;   // 630
```

The canvas viewport shrinks from 720px to 630px tall. The log strip occupies the full `SCREEN_W × LOG_H` band at `y = CANVAS_H`. It is drawn last and overdrows any canvas or panel content that bleeds into the bottom 90px — no scissor mode required.

`DrawDotGrid` must use `CANVAS_H` (not `SCREEN_H`) for its `botRight` Y coordinate so the grid stops above the log strip.

`UpdateContextMenuHover` and `DrawContextMenu` must clamp menu Y to `CANVAS_H - h - 4` (not `SCREEN_H - h - 4`) so context menus never open inside the log strip.

---

## Architecture

### What changes

| Symbol | Change |
|---|---|
| `LOG_H` / `CANVAS_H` | **New constants** — layout |
| `LogEntry` | **New struct** — one entry per simulation result |
| `SimMode` | **New enum** — `SIM_IDLE`, `SIM_SELECTING_DST`, `SIM_ANIMATING` |
| `PacketAnim` | **New struct** — in-flight animation state |
| `SimState` | **New struct** — wraps `SimMode + srcId + PacketAnim` |
| `nodeItems[]` | **Extended** — adds "Send Packet To…" as item 2 |
| `UpdateContextMenuHover` | **Modified** — CTX_NODE now counts 3 items; Y clamp uses `CANVAS_H` |
| `DrawContextMenu` | **Modified** — CTX_NODE now draws 3 items; Y clamp uses `CANVAS_H` |
| `ExecuteMenuAction` | **Modified** — item 2 sets `simState` into selecting mode |
| `DrawDotGrid` | **Modified** — `botRight` Y uses `CANVAS_H` |
| `FindCable` | **New** — returns first cable connecting two node IDs |
| `EvaluateCubicBezier` | **New** — standard cubic bezier point at `t` |
| `GetFirstValidIp` | **New** — returns first valid portIp or mgmtIp from a node |
| `UpdatePacketAnim` | **New** — advances hop timer, detects completion |
| `DrawPacketAnim` | **New** — draws glowing dot + failure pulse (inside BeginMode2D) |
| `DrawLogConsole` | **New** — draws the bottom log strip (outside BeginMode2D) |
| `main()` | **Extended** — `SimState simState`, `logEntries` vector, ESC cancel, dest click, draw wiring |

### Estimated size

1101 lines (Phase 3a) + ~220 new lines = **~1320 lines** after Phase 3b.

---

## Data Model

### LogEntry

```cpp
struct LogEntry {
    bool        success;
    std::string pathStr;   // "PC1 → R1 → PC2" (labels, pre-built at sim time)
    std::string reason;    // "delivered" | "no route to X" | "next-hop unreachable: X" | etc.
    float       timestamp; // GetTime() at moment simulation was triggered
};
```

### SimMode

```cpp
enum SimMode { SIM_IDLE, SIM_SELECTING_DST, SIM_ANIMATING };
```

### PacketAnim

```cpp
struct PacketAnim {
    ForwardResult result;          // from SimulateForward
    int           hop      = 0;    // current hop index (0 = path[0]→path[1])
    float         t        = 0.f;  // interpolation within current hop [0..1]
    bool          done     = false;
    float         failPulse = 0.f; // countdown for red-pulse effect (seconds)
};
```

`hop` ranges from `0` to `result.path.size() - 2` (inclusive). When `hop == path.size() - 1`, the packet has completed all segments.

Special case: `path.size() == 1` means the destination is reachable via a connected route on the source node itself. The packet delivers immediately with no hop animation.

### SimState

```cpp
struct SimState {
    SimMode    mode  = SIM_IDLE;
    int        srcId = -1;
    PacketAnim anim;
};
```

`SimState simState` is declared in `main()` alongside `nodes`, `cables`, `contextMenu`, `ps`.

---

## New Free Functions (above `main()`)

### GetFirstValidIp

Returns the first valid plain IP (no prefix) from a node's interfaces. Used to pick the destination IP when the user clicks a target node.

```cpp
std::string GetFirstValidIp(const DeviceNode& n) {
    for (int i = 0; i < PORTS_PER_NODE; ++i) {
        const auto& ip = n.portIp[i];
        // Strip prefix if present, then validate
        auto slash = ip.find('/');
        std::string plain = (slash != std::string::npos) ? ip.substr(0, slash) : ip;
        if (ValidateIPOnly(plain)) return plain;
    }
    auto slash = n.mgmtIp.find('/');
    std::string plain = (slash != std::string::npos) ? n.mgmtIp.substr(0, slash) : n.mgmtIp;
    if (ValidateIPOnly(plain)) return plain;
    return "";
}
```

### FindCable

Returns a pointer to the first cable connecting node `a` to node `b` (either direction), or `nullptr` if none exists.

```cpp
const Cable* FindCable(const std::vector<Cable>& cables, int a, int b) {
    for (const auto& c : cables)
        if ((c.fromId == a && c.toId == b) || (c.fromId == b && c.toId == a))
            return &c;
    return nullptr;
}
```

### EvaluateCubicBezier

Standard cubic bezier formula.

```cpp
Vector2 EvaluateCubicBezier(Vector2 p0, Vector2 c1, Vector2 c2, Vector2 p3, float t) {
    float it = 1.f - t;
    return {
        it*it*it*p0.x + 3*it*it*t*c1.x + 3*it*t*t*c2.x + t*t*t*p3.x,
        it*it*it*p0.y + 3*it*it*t*c1.y + 3*it*t*t*c2.y + t*t*t*p3.y
    };
}
```

### BuildPathStr

Builds a human-readable path string from node IDs. Called once when a `LogEntry` is pushed.

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

### UpdatePacketAnim

Advances the animation by `dt` seconds. Called every frame when `simState.mode == SIM_ANIMATING`.

```cpp
static const float HOP_DURATION = 0.4f;

void UpdatePacketAnim(PacketAnim& anim, float dt,
                      const std::vector<DeviceNode>& nodes,
                      const std::vector<Cable>& cables)
{
    if (anim.done) {
        anim.failPulse = std::max(0.f, anim.failPulse - dt);
        return;
    }

    const auto& path = anim.result.path;

    // Immediate delivery (connected at source)
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

### DrawPacketAnim

Draws the glowing dot and failure pulse. Called **inside `BeginMode2D`**, after `DrawAllCables`, before `DrawDeviceNode` loop.

```cpp
void DrawPacketAnim(const PacketAnim& anim,
                    const std::vector<DeviceNode>& nodes,
                    const std::vector<Cable>& cables)
{
    const auto& path = anim.result.path;
    if (path.empty()) return;

    // Failure pulse on last node
    if (anim.failPulse > 0.f) {
        const DeviceNode* failNode = FindNode(nodes, path.back());
        if (failNode) {
            float r = 30.f + 20.f * (1.f - anim.failPulse / 0.5f);
            DrawCircleV(failNode->position, r,
                        Color{239, 68, 68, (unsigned char)(anim.failPulse / 0.5f * 80)});
        }
        return;
    }

    if (anim.done) return;

    // Current hop segment
    if (anim.hop >= (int)path.size() - 1) return;
    int fromId = path[anim.hop];
    int toId   = path[anim.hop + 1];

    const DeviceNode* fromNode = FindNode(nodes, fromId);
    const DeviceNode* toNode   = FindNode(nodes, toId);
    const Cable*      cable    = FindCable(cables, fromId, toId);
    if (!fromNode || !toNode || !cable) return;

    // Determine port positions (handle cable direction)
    int fromPort = (cable->fromId == fromId) ? cable->fromPort : cable->toPort;
    int toPort   = (cable->fromId == toId)   ? cable->fromPort : cable->toPort;

    Vector2 p0 = GetPortPosition(*fromNode, fromPort);
    Vector2 p3 = GetPortPosition(*toNode,   toPort);
    Vector2 c1 = BezierCtrl(p0, fromPort);
    Vector2 c2 = BezierCtrl(p3, toPort);

    Vector2 pos = EvaluateCubicBezier(p0, c1, c2, p3, anim.t);

    // Dot is always green while in motion — turns red only at the failure stop
    DrawCircleV(pos, 14.f, Color{34, 197, 94, 60});
    DrawCircleV(pos, 7.f,  Color{34, 197, 94, 255});
}
```

### DrawLogConsole

Draws the bottom log strip. Called **outside `BeginMode2D`**, after `DrawContextMenu`, before `EndDrawing`.

```cpp
void DrawLogConsole(const std::vector<LogEntry>& entries) {
    int y0 = CANVAS_H;
    DrawRectangle(0, y0, SCREEN_W, LOG_H, Color{10, 15, 28, 255});
    DrawLineEx({0.f, (float)y0}, {(float)SCREEN_W, (float)y0}, 1.f,
               Color{51, 65, 85, 255});
    DrawText("LOG", 12, y0 + 8, 9, Color{71, 85, 105, 255});

    int maxLines = 3;
    int startIdx = std::max(0, (int)entries.size() - maxLines);
    for (int i = 0; i < std::min(maxLines, (int)entries.size()); ++i) {
        const auto& e = entries[startIdx + i];
        int lineY = y0 + 8 + (maxLines - 1 - i) * 24;   // newest at top

        // Timestamp
        int secs = (int)e.timestamp;
        int mins  = secs / 60; secs %= 60;
        char tsbuf[16];
        std::snprintf(tsbuf, sizeof(tsbuf), "[%02d:%02d]", mins, secs);
        DrawText(tsbuf, 36, lineY, 10, Color{71, 85, 105, 255});

        // Icon
        const char* icon = e.success ? "\xe2\x9c\x93" : "\xe2\x9c\x97";
        Color iconColor  = e.success ? Color{34, 197, 94, 255} : Color{239, 68, 68, 255};
        DrawText(icon, 90, lineY, 10, iconColor);

        // Path + reason
        std::string msg = e.pathStr + "  \xe2\x80\x94  " + e.reason;
        DrawText(msg.c_str(), 108, lineY, 10, iconColor);
    }

    if (entries.empty())
        DrawText("No simulations run yet", 36, y0 + 36, 10, Color{51, 65, 85, 255});
}
```

---

## Simulation Flow

### SIM_SELECTING_DST visual indicators

While in `SIM_SELECTING_DST` mode, draw inside `BeginMode2D` (after all nodes):

1. A faint blue ring (`DrawCircleLinesV`) around every node except `simState.srcId`
2. A bright ring around `simState.srcId` to confirm it's the source

In screen space (outside `EndMode2D`), draw a status label:
```
"Click destination node  —  ESC to cancel"
```
at the top-center of the canvas in dim text.

### Destination click

In the LMB pressed block, **before** the existing node-selection logic, check:

```cpp
if (simState.mode == SIM_SELECTING_DST && !contextMenu.visible) {
    // Check if user clicked a node
    int clickedId = -1;
    for (int i = (int)nodes.size() - 1; i >= 0; --i) {
        if (CheckCollisionPointRec(worldMouse, GetNodeRect(nodes[i]))) {
            clickedId = nodes[i].id;
            break;
        }
    }
    // Clicking empty canvas (no node hit) is a no-op — don't cancel, don't proceed
    if (clickedId != -1 && clickedId != simState.srcId) {
        // Find destination IP
        const DeviceNode* dst = FindNode(nodes, clickedId);
        std::string destIp = dst ? GetFirstValidIp(*dst) : "";
        if (destIp.empty()) {
            // No configured IP — log error, stay idle
            logEntries.push_back({false, FindNode(nodes, simState.srcId)->label +
                                  " \xe2\x86\x92 " + dst->label,
                                  "destination has no configured IP",
                                  GetTime()});
        } else {
            ForwardResult fr = SimulateForward(simState.srcId, destIp, nodes, cables);
            simState.anim = PacketAnim{fr, 0, 0.f, false, 0.f};
            LogEntry le;
            le.success   = fr.success;
            le.pathStr   = BuildPathStr(fr.path, nodes);
            le.reason    = fr.reason;
            le.timestamp = GetTime();
            if (logEntries.size() >= 50) logEntries.erase(logEntries.begin());
            logEntries.push_back(le);
            simState.mode = SIM_ANIMATING;
        }
        simState.srcId = -1;
        if (simState.mode != SIM_ANIMATING) simState.mode = SIM_IDLE;
    }
    // Consume the click — handled flag prevents fall-through to node selection
}
```

**Click consumption:** The destination-click block is checked first inside the LMB-pressed handler. After handling a destination click (whether it ran a sim or was a no-op), set a `bool simClickConsumed = true` flag and wrap the remainder of the LMB-pressed block in `if (!simClickConsumed)`. This avoids the destination node also getting selected.

### ESC cancels simulation mode

Add to the ESC handler (after existing `activeField` / `activeRouteField` checks):

```cpp
else if (simState.mode == SIM_SELECTING_DST) {
    simState.mode  = SIM_IDLE;
    simState.srcId = -1;
}
```

### Animation → Idle transition

In the main loop update section, after `UpdatePacketAnim`:

```cpp
if (simState.mode == SIM_ANIMATING &&
    simState.anim.done && simState.anim.failPulse <= 0.f)
    simState.mode = SIM_IDLE;
```

---

## Draw Order

```
BeginDrawing()
  ClearBackground(BG_COLOR)
  BeginMode2D(camera)
    DrawDotGrid(camera)           // clipped to CANVAS_H
    DrawAllCables(cables, nodes)
    DrawPacketAnim(...)           // glowing dot on cable
    for each node: DrawDeviceNode(n)
    [SIM_SELECTING_DST rings]     // node rings when selecting
  EndMode2D()
  [SIM_SELECTING_DST label]       // "Click destination…" screen-space text
  DrawPanel(...)
  DrawContextMenu(...)
  DrawLogConsole(logEntries)      // bottom strip, drawn last
EndDrawing()
```

---

## Acceptance Criteria

### Layout
- Log strip visible at bottom 90px, full width, dark background, "LOG" label
- Dot grid stops at y=630 (does not bleed into log strip)
- Context menus clamp to `CANVAS_H` — never open inside the log strip

### Simulation trigger
- Right-click a node → context menu shows "Rename", "Delete", "Send Packet To…"
- Click "Send Packet To…" → nodes get blue rings, src node gets bright ring, status label appears
- ESC cancels selecting mode, returns to idle
- Click a different node → simulation runs

### Packet animation
- Green glowing dot travels from node to node along bezier curves at 0.4s per hop
- Reaching destination: dot arrives, animation completes, log entry pushed
- Failure: red dot stops at last reachable node, red pulse ring expands on that node, animation completes
- No animation if path.size() == 1 (connected at source, delivers immediately)

### Log console
- Each simulation pushes one entry: timestamp, ✓/✗ icon, path string, reason
- Up to 3 entries visible, newest at top
- Green text for success, red for failure
- "No simulations run yet" placeholder when empty

### Forwarding engine
- Existing `SimulateForward` behavior unchanged (tested in Phase 3a)

---

## Out of Scope for Phase 3b

- Slow-motion replay button
- Multiple concurrent packet animations
- ICMP, ARP, or protocol-specific packet types
- Packet labeling / badge display
- Multi-file split (deferred)
- Per-hop delay differentiation
