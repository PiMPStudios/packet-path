# Packet Path — Development Plan

## Philosophy

**Vertical slice first.** Every phase produces a working, playable (or at minimum testable) game state — never a pile of infrastructure with no visible result. Phase 3 ("First Playable") is the first real "aha" moment; everything before it is scaffolding to get there fast.

**Single-file era.** All code lives in `src/main.cpp` through Phase 2. Multi-file structure is introduced at Phase 3 — not before. The architecture will be clearer once the core loop is working and the right boundaries are obvious.

**Each milestone = one focused Claude session.** Milestones are scoped to produce a concrete, testable result in roughly one sitting. If something grows too large mid-session, split it — don't pile on.

**No raylib in data structures.** `DeviceNode`, `Cable`, `Packet`, etc. are plain C++ structs. All raylib calls live in draw functions only. This keeps the simulation logic testable and the rendering layer swappable.

---

## Locked Phase Map

```
PRE  →  P1  →  P2  →  P3 ⭐  →  P4  →  P5  →  P6
Setup   Skel  Canvas  Playable  Routing  Game   Polish
```

| Phase | Name | Key Output | Milestones |
|---|---|---|---|
| PRE | Setup | Window opens, Makefile works | 1 |
| 1 | Skeleton | Draggable nodes + cables on canvas | 4 |
| 2 | Canvas Engine | Config panel + IP input + delete | 4 |
| 3 | First Playable ⭐ | A ping works end-to-end | 4 |
| 4 | Routing Core | OSPF single-area + routing table | 5 |
| 5 | Game Mechanics | 4 playable CCNA scenarios + star rating | 4 |
| 6 | MVP Polish | Packet trace, log console, SFX | 4 |

**Total: ~26 milestones**

---

## Project Folder Structure

### Phases 1–2: Single-File Era

```
packet-path/
├── src/
│   └── main.cpp          ← everything lives here (~300–350 lines at Phase 1 end)
├── assets/               ← empty until Phase 6 (fonts, icons, sounds)
├── docs/
│   ├── packet-path-game-roadmap.md
│   ├── Development_Plan.md
│   └── RFCs/             ← 27 RFCs + IEEE 802.1Q + ITU-T G.694.1 + SD-WAN drafts
├── Makefile
├── CLAUDE.md
├── .gitignore
└── README.md
```

### Phase 3+: Multi-File Era

```
packet-path/
├── src/
│   ├── main.cpp
│   ├── Device.h / Device.cpp
│   ├── Packet.h / Packet.cpp
│   ├── Cable.h / Cable.cpp
│   ├── SimulationEngine.h / SimulationEngine.cpp
│   ├── NetworkCanvas.h / NetworkCanvas.cpp
│   ├── ConfigPanel.h / ConfigPanel.cpp
│   └── UI.h / UI.cpp
├── include/
├── assets/
├── docs/
└── Makefile
```

**Trigger for split:** When `main.cpp` exceeds ~500 lines OR when adding SimulationEngine makes responsibilities unclear — whichever comes first in Phase 3.

---

## Pre-Phase: Project Setup

### M0 — Project Setup & raylib Window

**Goal:** The project compiles and a window opens.

**Files created:** `Makefile`, `src/main.cpp`

**What to build:**
- `Makefile` — compiles `src/main.cpp`, links raylib, `-std=c++17 -Wall -O2`
- `src/main.cpp` — `InitWindow(1280, 720, "Packet Path")`, `SetTargetFPS(60)`, game loop, `BeginDrawing` / `EndDrawing` / `CloseWindow`
- `DrawFPS(10, 10)` in top-left corner
- `ClearBackground(Color{15, 23, 42, 255})` — dark navy background

**Acceptance criteria:**
- `make` compiles with zero warnings
- `./packet-path` opens a 1280×720 dark window
- FPS counter visible and stable at 60
- Window closes cleanly on ✕ or Escape

---

## Phase 1: Skeleton

**Exit state:** An infinite, zoomable canvas with draggable PC/Router/Switch nodes that can be wired together with bezier cables. No networking logic — just a canvas that feels good to use.

---

### M1.1 — Single Draggable DeviceNode

**Goal:** One node on screen that you can grab and move.

**Files touched:** `src/main.cpp`

**What to build:**

```cpp
enum DeviceType { PC, ROUTER, SWITCH };

struct DeviceNode {
    int id;
    DeviceType type;
    Vector2 position;   // world-space center
    std::string label;
    bool selected = false;
};

Color GetDeviceColor(DeviceType type);          // PC=blue, Router=orange, Switch=green
Rectangle GetNodeRect(const DeviceNode& node);  // NODE_W=120, NODE_H=60
void DrawDeviceNode(const DeviceNode& node);    // rounded rect + shadow + label
```

**Drag logic:** On LMB press over node rect → record `dragOffset = mouse - node.position`. While LMB held → `node.position = mouse - dragOffset`. On LMB release → clear drag state.

**Acceptance criteria:**
- Node renders as a labeled rounded rectangle with drop shadow
- Click and hold on the node body to drag it
- Node stays where released
- Releasing outside the node causes no crash or jump

---

### M1.2 — Multiple Nodes + Device Types + Selection

**Goal:** Spawn any number of nodes, select and drag them independently.

**Files touched:** `src/main.cpp`

**What to build:**
- `std::vector<DeviceNode> nodes` with auto-incrementing IDs
- Color per type: PC = `#3b82f6` (blue) · Router = `#f97316` (orange) · Switch = `#22c55e` (green)
- Hit detection: iterate `nodes` back-to-front, `CheckCollisionPointRec`
- `int selectedNodeId = -1` — selected node gets a white highlight border
- Spawn keys: `P` = PC (blue), `R` = Router (orange), `S` = Switch (green) — spawns near canvas center
- `Delete` key removes selected node; erases from vector

**Acceptance criteria:**
- Press P/R/S to spawn nodes of each type with correct colors
- Click a node to select it (highlight border appears)
- Click canvas background to deselect
- Drag any node independently without moving others
- Delete key removes selected node; others unaffected
- Spawn 10+ nodes — no performance issues

---

### M1.3 — Camera 2D — Pan & Zoom

**Goal:** Infinite scrollable, zoomable canvas.

**Files touched:** `src/main.cpp`

**What to build:**
```cpp
Camera2D camera = {
    .offset = { SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f },
    .target = { 0, 0 },
    .rotation = 0,
    .zoom = 1.0f
};
```

- **Pan:** Middle-mouse held → `camera.target -= GetMouseDelta() / camera.zoom`
- **Zoom:** Scroll wheel → scale zoom, clamp to [0.15, 4.0], anchor on cursor:
  ```cpp
  Vector2 worldBefore = GetScreenToWorld2D(GetMousePosition(), camera);
  camera.zoom *= (1.0f + wheel * 0.1f);
  camera.zoom = Clamp(camera.zoom, 0.15f, 4.0f);
  Vector2 worldAfter = GetScreenToWorld2D(GetMousePosition(), camera);
  camera.target += worldBefore - worldAfter;  // re-anchor
  ```
- All nodes drawn inside `BeginMode2D(camera)` / `EndMode2D()`
- All mouse hit detection uses `GetScreenToWorld2D(GetMousePosition(), camera)`
- Subtle dot-grid background drawn inside camera to convey infinite canvas

**Acceptance criteria:**
- Middle-mouse drag pans smoothly
- Scroll wheel zooms anchored on cursor — nodes don't jump
- Nodes draggable at any zoom level (hit detection correct in world space)
- Zoom clamped — cannot invert or hit zero

---

### M1.4 — Cable Draw + Port Snap

**Goal:** Click-drag from one node's port to another to wire them with a bezier cable.

**Files touched:** `src/main.cpp`

**What to build:**

```cpp
// 4 ports per device: top, right, bottom, left (indices 0-3)
Vector2 GetPortPosition(const DeviceNode& node, int portIndex);

struct Cable {
    int fromId, fromPort;
    int toId,   toPort;
};
std::vector<Cable> cables;
```

**Connection state machine:**
1. LMB press near a port circle → enter `CONNECTING` state, record `connectFrom`
2. While dragging → draw a live bezier from start port to mouse position
3. Hover near valid target port on a different device → highlight it green
4. LMB release on valid port → push `Cable` to `cables`, exit `CONNECTING`
5. LMB release on nothing → cancel

**Drawing cables:** `DrawLineBezierCubic` with control points offset perpendicular to the port side. Cables recompute endpoints every frame from current port positions — no cached positions.

**Guards:** Cannot connect a port to itself. Cannot connect a device to itself.

**Acceptance criteria:**
- Small circles visible at all 4 port positions on each device
- Click-drag from port → live bezier follows mouse
- Green highlight appears on valid target port on hover
- Release on valid port creates a permanent bezier cable
- Moving either connected device — cable follows correctly
- Self-connect is silently ignored

---

### Phase 1 Exit Criteria

After M1.4 you have:
- An infinite pan/zoom canvas with a dark navy + dot-grid background
- Spawnable PC, Router, Switch nodes in distinct colors, draggable anywhere
- Click-to-select with Delete to remove
- Wired bezier cables that follow devices when moved
- Everything in `src/main.cpp` (~300–350 lines)

**Commit message:** `feat: Phase 1 complete — draggable canvas with bezier cable connections`

---

## Phase 2: Canvas Engine

**Goal:** The canvas becomes configurable — click a device to open a side panel and set its IP address, interface, and label. Right-click to delete. Foundation for Phase 3's simulation.

**Exit state:** A fully interactive canvas where devices have IP addresses and the UI feels like a real network diagram tool.

### Milestone overview

| ID | Title | Key addition |
|---|---|---|
| M2.1 | Side Config Panel | Right-side panel opens on node click, shows device info |
| M2.2 | IP Address Input | Text field in panel accepts IP/mask; stored on DeviceNode |
| M2.3 | Interface Config | Per-port interface name + IP (e.g. `Gi0/0 · 10.0.0.1/30`) |
| M2.4 | Right-Click & Polish | Context menu (delete, rename), node labels editable |

---

## Phase 3: First Playable ⭐

**Goal:** A packet actually travels from PC to PC. This is the first "aha" moment.

**Exit state:** Press "Test Network" — an animated packet (green dot) travels from source to destination, resolving ARP and forwarding across the link. Success = green flash. Failure = red drop with reason.

### Milestone overview

| ID | Title | Key addition |
|---|---|---|
| M3.1 | SimulationEngine skeleton | `SimulationEngine` class, `RunSimulation()` entry point, split to multi-file |
| M3.2 | ARP resolution | ARP table per device, broadcast → reply → cache |
| M3.3 | Animated Packet object | `Packet` struct, waypoint movement, `GetFrameTime()` delta |
| M3.4 | Success / drop states | Green arrival flash, red drop with reason string, bottom log line |

> **Multi-file split happens here.** `SimulationEngine`, `Device`, `Packet`, `Cable` each get their own `.h/.cpp`. `main.cpp` becomes the render + input loop only.

---

## Phase 4: Routing Core

**Goal:** Packets route intelligently. Static routes first, then OSPF single-area with real SPF.

**Exit state:** Configure OSPF on routers → adjacencies form → SPF runs → routing tables populate → packets take the correct path automatically.

### Milestone overview

| ID | Title | Key addition |
|---|---|---|
| M4.1 | RIB + FIB + static routes | `RoutingTable` class, `ip route` config, longest-prefix match |
| M4.2 | Packet forwarding walk | Hop-by-hop forwarding using FIB, TTL decrement, loop guard |
| M4.3 | OSPF Hello + adjacency FSM | Hello timers, `Down→Init→2-Way→Full` state machine |
| M4.4 | OSPF LSDB + SPF | Link-state database, Dijkstra SPF, populate RIB from SPF tree |
| M4.5 | Routing table panel | UI panel showing per-device RIB (`O 10.0.0.0/24 via 10.0.1.1`) |

---

## Phase 5: Game Mechanics

**Goal:** Turn the simulator into a game. Levels, win conditions, star ratings.

**Exit state:** 4 beginner CCNA scenarios are playable with clear win conditions and 1–3 star scoring.

### Milestone overview

| ID | Title | Key addition |
|---|---|---|
| M5.1 | Level JSON format + loader | `levels/level_01.json` schema, `LoadLevel()`, pre-placed devices |
| M5.2 | Win condition checker | `WinCondition` struct — "all PCs can ping gateway" — checked after simulation |
| M5.3 | Star rating UI | 1–3 stars based on efficiency (unused devices, extra hops); end-of-level overlay |
| M5.4 | 4 CCNA scenarios | Level 1: basic ping · Level 2: VLANs · Level 3: static routing · Level 4: OSPF |

---

## Phase 6: MVP Polish

**Goal:** The game feels finished. Feedback is clear, errors are informative, and it sounds alive.

**Exit state:** MVP ready to show to real users. Packet trace explains every decision. Log console shows live events. Broken paths highlight red. Basic SFX on key events.

### Milestone overview

| ID | Title | Key addition |
|---|---|---|
| M6.1 | Packet trace viewer | Click any packet (or post-run) → step-by-step trace of every hop decision |
| M6.2 | Bottom log console | Scrollable log: "ARP request sent", "Label 24000 swapped", "Packet dropped — no route" |
| M6.3 | Broken path highlight | Failed packets turn links red; `Troubleshoot` mode shows exactly where the break is |
| M6.4 | SFX | `InitAudioDevice`, sounds for: packet-send, packet-arrive, packet-drop, route-updated, link-fail |

---

## Working with Claude — Session Workflow

Each milestone session should follow this pattern:

1. **Start:** Paste the milestone section from this doc as context
2. **Load skills:** `network-sim`, `raylib-development`, `educational-game-design` as relevant
3. **Implement:** Code-writer + TDD workflow
4. **Test:** Run acceptance criteria manually
5. **Commit:** One commit per milestone with descriptive message
6. **Update this doc:** Mark milestone complete, note any decisions or deviations

### Skill triggers by phase

| Phase | Primary skills |
|---|---|
| PRE, P1, P2 | `raylib-development` |
| P3 | `raylib-development` + `network-sim` (forwarding plane) |
| P4 | `network-sim` (routing algorithms, protocol state machines) |
| P5 | `educational-game-design` (level sequencing, feedback systems) |
| P6 | `game-feel` + `sound-design` |

---

*Last updated: 2026-04-23 — Phase 1 detail locked, Pre-Phase + M1.1–M1.4 fully specified.*
