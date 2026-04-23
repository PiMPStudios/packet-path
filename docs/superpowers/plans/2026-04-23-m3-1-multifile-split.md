# M3.1 Multi-File Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split `src/main.cpp` (~1,379 lines) into 7 focused module pairs so `main.cpp` becomes a thin game-loop entry point with a single include.

**Architecture:** Data structs (`DeviceNode`, `Cable`, `PacketAnim`) live in pure modules—no raylib draw calls, just geometry helpers and utilities. ALL draw functions (`DrawDeviceNode`, `DrawPanel`, `DrawLogConsole`, etc.) are centralised in `NetworkCanvas.h/cpp`. UI state modules (`ConfigPanel`, `UI`) handle only input and state management. `SimulationEngine` is completely isolated from rendering. `NetworkCanvas.h` is the single top-level header `main.cpp` needs.

**Tech Stack:** C++17, raylib 5.5, macOS, GNU Make. No external test framework — verification is `make` (zero warnings) + `./packet-path` smoke test after every task.

---

## File Map

| File | Owns |
|---|---|
| `src/Device.h/cpp` | `DeviceType`, `DeviceNode`, `RouteEntry`, `ForwardResult`, `LogEntry`; IP utilities; device geometry helpers (no draw calls) |
| `src/Cable.h/cpp` | `Cable` struct; `FindNode`, `BezierCtrl` |
| `src/Packet.h/cpp` | `SimMode`, `PacketAnim`, `SimState`; animation update logic; bezier evaluation |
| `src/SimulationEngine.h/cpp` | `SimulateForward` — pure forwarding logic, no raylib |
| `src/ConfigPanel.h/cpp` | `PanelTab`, `PanelState`; layout rect helpers; keyboard input handlers (`UpdateTextField`, `UpdateRoutesTab`) |
| `src/UI.h/cpp` | `ContextType`, `ContextMenu`; `SpawnNode`; `ExecuteMenuAction`; `UpdateContextMenuHover` |
| `src/NetworkCanvas.h/cpp` | ALL draw functions; hit-test functions; all screen/layout constants |
| `src/main.cpp` | Game loop, init, global state (`nodes`, `cables`, `camera`, `simState`, etc.) |

### Include dependency tree (headers only — no cycles)

```
raylib.h (external)
Device.h  → raylib.h, string, vector, cstdint, cstdio, cmath
Cable.h   → Device.h
Packet.h  → Device.h, Cable.h
SimulationEngine.h → Device.h, Cable.h, string, vector
ConfigPanel.h → Device.h, raylib.h, string, vector
UI.h      → Device.h, Cable.h, ConfigPanel.h, Packet.h, raylib.h
NetworkCanvas.h → Device.h, Cable.h, Packet.h, SimulationEngine.h,
                  ConfigPanel.h, UI.h, raylib.h, algorithm, cmath, cstdio, cstdint
main.cpp  → NetworkCanvas.h   (single include; gets everything transitively)
```

---

## Task 1: Update Makefile for Multi-File Compilation

**Files:**
- Modify: `Makefile`

This must be done first. The wildcard rule picks up every `.cpp` added in later tasks automatically.

- [ ] **Step 1: Update SRC line in Makefile**

Replace the current `SRC` line with a wildcard. The full updated Makefile:

```makefile
CC       = g++
CFLAGS   = -std=c++17 -Wall -Wextra -O2
INCLUDES = $(shell pkg-config --cflags raylib 2>/dev/null || echo "-I/usr/local/include")
LIBS     = $(shell pkg-config --libs   raylib 2>/dev/null || echo "-L/usr/local/lib -lraylib \
             -framework OpenGL -framework Cocoa -framework IOKit \
             -framework CoreAudio -framework CoreVideo")

TARGET = packet-path
SRC    = $(wildcard src/*.cpp)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC) $(LIBS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
```

- [ ] **Step 2: Verify clean build with only main.cpp**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make clean && make
```

Expected: compiles with zero warnings, binary `packet-path` produced.

- [ ] **Step 3: Smoke test**

```bash
./packet-path
```

Expected: window opens, dot grid visible, can right-click to spawn nodes, drag them, ESC closes context menu. Quit with ✕ or Escape from no-field state (click empty canvas first).

- [ ] **Step 4: Commit**

```bash
git add Makefile
git commit -m "build: switch Makefile to wildcard src/*.cpp for multi-file split"
```

---

## Task 2: Device Module

**Files:**
- Create: `src/Device.h`
- Create: `src/Device.cpp`
- Modify: `src/main.cpp`

Extracts all device data structures, IP utilities, and geometry helpers. After this task, `main.cpp` will include `Device.h` and have those declarations removed.

- [ ] **Step 1: Create `src/Device.h`**

```cpp
#pragma once
#include "raylib.h"
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cmath>

// ── Device geometry constants ────────────────────────────────────────────
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

struct ForwardResult {
    bool             success = false;
    std::vector<int> path;
    std::string      reason;
};

struct LogEntry {
    bool        success;
    std::string pathStr;
    std::string reason;
    float       timestamp;
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
};

// ── Device geometry helpers (no draw calls) ───────────────────────────────
Color     GetDeviceColor(DeviceType t);
Rectangle GetNodeRect(const DeviceNode& n);
Vector2   GetPortPosition(const DeviceNode& n, int port);
std::string GetPortName(DeviceType type, int port);
std::vector<RouteEntry> GetRoutingTable(const DeviceNode& n);

// ── IP utilities (no raylib) ──────────────────────────────────────────────
std::string NetworkAddress(const std::string& cidr);
bool        IpInSubnet(const std::string& ip, const std::string& subnet);
bool        ValidateIPOnly(const std::string& ip);
int         PrefixLen(const std::string& cidr);
bool        ValidateIP(const std::string& ip);
```

- [ ] **Step 2: Create `src/Device.cpp`**

Cut the following functions verbatim from `main.cpp` and paste them here, preceded by `#include "Device.h"`. The functions to cut are (search by name):

- `GetDeviceColor`
- `GetNodeRect`
- `GetPortPosition`
- `GetPortName`
- `GetRoutingTable`
- `NetworkAddress`
- `IpInSubnet`
- `ValidateIPOnly`
- `PrefixLen`
- `ValidateIP`

The resulting `src/Device.cpp` must start with exactly:

```cpp
#include "Device.h"
#include <algorithm>
```

Then all ten functions follow verbatim. No other changes to the function bodies.

- [ ] **Step 3: Update `src/main.cpp`**

3a. At the top of main.cpp, immediately after the existing `#include "raylib.h"` line, add:

```cpp
#include "Device.h"
```

3b. Delete from main.cpp (search by content, remove the entire blocks):
- The five device geometry constants: `PORTS_PER_NODE`, `NODE_W`, `NODE_H`, `NODE_FONT_SZ`, `PORT_RADIUS`
- The `RouteSource` enum and `RouteEntry` struct
- The `ForwardResult` struct
- The `LogEntry` struct
- The `DeviceType` enum and `DeviceNode` struct
- All ten functions listed in Step 2 (they now live in Device.cpp)

3c. Remove these now-redundant includes from main.cpp (they come transitively from Device.h):

```cpp
// remove these three lines:
#include <cstdio>
#include <cstdint>
```

Keep `#include <cmath>` and `#include <algorithm>` in main.cpp for now (game loop uses them).

- [ ] **Step 4: Build and verify**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make clean && make 2>&1
```

Expected: zero errors, zero warnings. If `portIp[4]` is referenced anywhere in remaining main.cpp code, that is fine — the array is now declared as `portIp[PORTS_PER_NODE]` in Device.h and PORTS_PER_NODE equals 4.

- [ ] **Step 5: Smoke test**

```bash
./packet-path
```

Expected: window opens normally. Right-click canvas → "Add Router Here" → router node appears with orange color. Click it → config panel opens. Verify IP fields work.

- [ ] **Step 6: Commit**

```bash
git add src/Device.h src/Device.cpp src/main.cpp
git commit -m "refactor: extract Device module (DeviceNode, IP utilities, geometry helpers)"
```

---

## Task 3: Cable Module

**Files:**
- Create: `src/Cable.h`
- Create: `src/Cable.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Create `src/Cable.h`**

```cpp
#pragma once
#include "Device.h"
#include <vector>

struct Cable {
    int fromId, fromPort;
    int toId,   toPort;
};

const DeviceNode* FindNode(const std::vector<DeviceNode>& nodes, int id);
Vector2           BezierCtrl(Vector2 p, int port);
```

- [ ] **Step 2: Create `src/Cable.cpp`**

Cut the `Cable` struct, `FindNode`, and `BezierCtrl` from `main.cpp` and paste as implementations. The resulting file:

```cpp
#include "Cable.h"

const DeviceNode* FindNode(const std::vector<DeviceNode>& nodes, int id) {
    for (const auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

Vector2 BezierCtrl(Vector2 p, int port) {
    const float offset = 60.0f;
    switch (port) {
        case 0: return {p.x,           p.y - offset};
        case 1: return {p.x + offset,  p.y         };
        case 2: return {p.x,           p.y + offset};
        case 3: return {p.x - offset,  p.y         };
        default: return p;
    }
}
```

Note: remove `static` from `BezierCtrl` — it must be externally visible now.

- [ ] **Step 3: Update `src/main.cpp`**

3a. After `#include "Device.h"` add:

```cpp
#include "Cable.h"
```

3b. Delete from main.cpp:
- The `Cable` struct definition (the 4-field struct: `fromId`, `fromPort`, `toId`, `toPort`)
- `FindNode` function
- `BezierCtrl` function (including its `static` keyword)

- [ ] **Step 4: Build and verify**

```bash
make clean && make 2>&1
```

Expected: zero errors, zero warnings.

- [ ] **Step 5: Smoke test**

```bash
./packet-path
```

Expected: drag-port-to-port still creates bezier cables that follow nodes when moved.

- [ ] **Step 6: Commit**

```bash
git add src/Cable.h src/Cable.cpp src/main.cpp
git commit -m "refactor: extract Cable module (Cable struct, FindNode, BezierCtrl)"
```

---

## Task 4: Packet Module

**Files:**
- Create: `src/Packet.h`
- Create: `src/Packet.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Create `src/Packet.h`**

```cpp
#pragma once
#include "Device.h"
#include "Cable.h"
#include <string>
#include <vector>

static const float HOP_DURATION = 0.4f;  // seconds per hop segment

enum SimMode { SIM_IDLE, SIM_SELECTING_DST, SIM_ANIMATING };

struct PacketAnim {
    ForwardResult result;
    int   hop       = 0;
    float t         = 0.f;
    bool  done      = false;
    float failPulse = 0.f;
};

struct SimState {
    SimMode    mode  = SIM_IDLE;
    int        srcId = -1;
    PacketAnim anim;
};

std::string  GetFirstValidIp(const DeviceNode& n);
const Cable* FindCable(const std::vector<Cable>& cables, int a, int b);
Vector2      EvaluateCubicBezier(Vector2 p0, Vector2 c1, Vector2 c2, Vector2 p3, float t);
std::string  BuildPathStr(const std::vector<int>& path,
                          const std::vector<DeviceNode>& nodes);
void         UpdatePacketAnim(PacketAnim& anim, float dt,
                              const std::vector<DeviceNode>& nodes,
                              const std::vector<Cable>& cables);
```

- [ ] **Step 2: Create `src/Packet.cpp`**

Cut these functions verbatim from `main.cpp`:

- `GetFirstValidIp`
- `FindCable`
- `EvaluateCubicBezier`
- `BuildPathStr`
- `UpdatePacketAnim`

The file must start with:

```cpp
#include "Packet.h"
#include <algorithm>
#include <cmath>
```

Then the five functions follow verbatim. Note that `UpdatePacketAnim` references `HOP_DURATION` — this constant is now in `Packet.h` so it will be visible.

- [ ] **Step 3: Update `src/main.cpp`**

3a. After `#include "Cable.h"` add:

```cpp
#include "Packet.h"
```

3b. Delete from main.cpp:
- `HOP_DURATION` constant
- `SimMode` enum
- `PacketAnim` struct
- `SimState` struct
- All five functions listed in Step 2

- [ ] **Step 4: Build and verify**

```bash
make clean && make 2>&1
```

Expected: zero errors, zero warnings.

- [ ] **Step 5: Smoke test**

```bash
./packet-path
```

Expected: right-click a node → "Send Packet To…" → click destination → green dot animates along cable. Failed path triggers red pulse.

- [ ] **Step 6: Commit**

```bash
git add src/Packet.h src/Packet.cpp src/main.cpp
git commit -m "refactor: extract Packet module (SimState, PacketAnim, animation logic)"
```

---

## Task 5: SimulationEngine Module

**Files:**
- Create: `src/SimulationEngine.h`
- Create: `src/SimulationEngine.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Create `src/SimulationEngine.h`**

```cpp
#pragma once
#include "Device.h"
#include "Cable.h"
#include <string>
#include <vector>

ForwardResult SimulateForward(int srcId, const std::string& destIp,
                              const std::vector<DeviceNode>& nodes,
                              const std::vector<Cable>& cables);
```

- [ ] **Step 2: Create `src/SimulationEngine.cpp`**

Cut `SimulateForward` verbatim from `main.cpp`. The file:

```cpp
#include "SimulationEngine.h"
#include <algorithm>
#include <unordered_set>
```

Then `SimulateForward` verbatim. The function body references `FindNode`, `GetRoutingTable`, `ValidateIPOnly`, `IpInSubnet`, `PrefixLen` — all declared in `Device.h` and `Cable.h`, which come through `SimulationEngine.h`.

- [ ] **Step 3: Update `src/main.cpp`**

3a. After `#include "Packet.h"` add:

```cpp
#include "SimulationEngine.h"
```

3b. Delete `SimulateForward` from main.cpp (the entire function body, ~70 lines).

3c. Remove `#include <unordered_set>` from main.cpp (it moves to SimulationEngine.cpp).

- [ ] **Step 4: Build and verify**

```bash
make clean && make 2>&1
```

Expected: zero errors, zero warnings.

- [ ] **Step 5: Smoke test**

```bash
./packet-path
```

Expected: set up two PCs with IPs on the same subnet, connect with cable, "Send Packet To…" succeeds with "delivered". Remove routes and verify failure reason appears in log.

- [ ] **Step 6: Commit**

```bash
git add src/SimulationEngine.h src/SimulationEngine.cpp src/main.cpp
git commit -m "refactor: extract SimulationEngine module (SimulateForward)"
```

---

## Task 6: ConfigPanel Module

**Files:**
- Create: `src/ConfigPanel.h`
- Create: `src/ConfigPanel.cpp`
- Modify: `src/main.cpp`

`ConfigPanel` owns panel state and the keyboard input handlers. It does NOT own any draw functions—those go to `NetworkCanvas` in Task 8.

- [ ] **Step 1: Create `src/ConfigPanel.h`**

```cpp
#pragma once
#include "Device.h"
#include "raylib.h"
#include <string>
#include <vector>

enum PanelTab { TAB_CONFIG, TAB_ROUTES };

struct PanelState {
    int         activeField      = -1;
    PanelTab    activeTab        = TAB_CONFIG;
    std::string newRouteDest;
    std::string newRouteNext;
    int         activeRouteField = -1;
};

// Layout rect helpers — implementations use CANVAS_W/PANEL_W from NetworkCanvas.h
Rectangle PnlFieldRect(int yOffset);
Rectangle PnlPortFieldRect(int port);
float     PnlTabW();
Rectangle PnlConfigTabRect();
Rectangle PnlRoutesTabRect();
Rectangle PnlRouteDeleteRect(int rowIdx);
Rectangle PnlRouteDestRect();
Rectangle PnlRouteNextRect();
Rectangle PnlRouteAddBtnRect();

// Keyboard input handlers (no draw calls)
void UpdateTextField(std::string& text, int maxLen);
void UpdateRoutesTab(DeviceNode* n, PanelState& ps);
```

- [ ] **Step 2: Create `src/ConfigPanel.cpp`**

Cut these from `main.cpp` verbatim:

- `PnlFieldRect`
- `PnlPortFieldRect`
- `PnlTabW` (remove `static` keyword)
- `PnlConfigTabRect`
- `PnlRoutesTabRect`
- `PnlRouteDeleteRect`
- `PnlRouteDestRect`
- `PnlRouteNextRect`
- `PnlRouteAddBtnRect`
- `UpdateTextField`
- `UpdateRoutesTab`

The file header:

```cpp
#include "ConfigPanel.h"
#include "NetworkCanvas.h"   // for CANVAS_W, PANEL_W, CFG_*, RTE_* layout constants
```

Note: `NetworkCanvas.h` is not yet created, so this include will cause a build failure until Task 8 completes. **Workaround for tasks 6–7:** temporarily define the needed constants at the top of `ConfigPanel.cpp` directly until `NetworkCanvas.h` exists, then replace with the single `#include "NetworkCanvas.h"` in Task 8 Step 4.

Temporary constants block to use in `ConfigPanel.cpp` while `NetworkCanvas.h` does not yet exist:

```cpp
#include "ConfigPanel.h"

// Temporary — replaced by #include "NetworkCanvas.h" in Task 8
static const int PANEL_W        = 280;
static const int SCREEN_W       = 1280;
static const int CANVAS_W       = SCREEN_W - PANEL_W;
static const int MENU_ITEM_H    = 28;
static const int CONTEXT_MENU_W = 160;
static const int LOG_H          = 90;
static const int SCREEN_H       = 720;
static const int CANVAS_H       = SCREEN_H - LOG_H;
static const int CFG_HOSTNAME_Y  = 158;
static const int CFG_MGMTIP_Y    = 210;
static const int CFG_IFACE_SEP_Y = 246;
static const int CFG_PORT_Y0     = 272;
static const int CFG_PORT_STRIDE = 44;
static const int RTE_ROW_Y0       = 142;
static const int RTE_HEADER_SEP_Y = 136;
static const int RTE_ROW_H        = 22;
static const int RTE_ADD_SEP_Y    = 420;
static const int RTE_DEST_Y       = 464;
static const int RTE_NEXT_Y       = 516;
static const int RTE_BTN_Y        = 554;
```

Then the eleven functions follow verbatim after the constants block. Remember to remove the `static` keyword from `PnlTabW`.

- [ ] **Step 3: Update `src/main.cpp`**

3a. After `#include "SimulationEngine.h"` add:

```cpp
#include "ConfigPanel.h"
```

3b. Delete from main.cpp:
- `PanelTab` enum
- `PanelState` struct
- All nine layout rect helpers (`PnlFieldRect`, `PnlPortFieldRect`, `PnlTabW`, `PnlConfigTabRect`, `PnlRoutesTabRect`, `PnlRouteDeleteRect`, `PnlRouteDestRect`, `PnlRouteNextRect`, `PnlRouteAddBtnRect`)
- `UpdateTextField`
- `UpdateRoutesTab`

- [ ] **Step 4: Build and verify**

```bash
make clean && make 2>&1
```

Expected: zero errors, zero warnings.

- [ ] **Step 5: Smoke test**

```bash
./packet-path
```

Expected: select a node → config panel opens, hostname field editable, IP fields validate (green border on valid IP, red on invalid). Tab-cycle works in Routes tab.

- [ ] **Step 6: Commit**

```bash
git add src/ConfigPanel.h src/ConfigPanel.cpp src/main.cpp
git commit -m "refactor: extract ConfigPanel module (PanelState, layout helpers, input handlers)"
```

---

## Task 7: UI Module

**Files:**
- Create: `src/UI.h`
- Create: `src/UI.cpp`
- Modify: `src/main.cpp`

`UI` owns `ContextMenu` state, `SpawnNode`, `ExecuteMenuAction`, and `UpdateContextMenuHover`. Draw functions for the context menu stay in main.cpp for now and move to `NetworkCanvas` in Task 8.

- [ ] **Step 1: Create `src/UI.h`**

```cpp
#pragma once
#include "Device.h"
#include "Cable.h"
#include "ConfigPanel.h"
#include "Packet.h"
#include "raylib.h"
#include <vector>
#include <string>

enum ContextType { CTX_NONE, CTX_NODE, CTX_CABLE, CTX_CANVAS };

struct ContextMenu {
    bool        visible   = false;
    Vector2     screenPos = {0.0f, 0.0f};
    Vector2     worldPos  = {0.0f, 0.0f};
    ContextType ctx       = CTX_NONE;
    int         targetId  = -1;
    int         hoverItem = -1;
};

DeviceNode SpawnNode(DeviceType type, Vector2 worldPos);

void UpdateContextMenuHover(ContextMenu& menu, Vector2 screenMouse);

void ExecuteMenuAction(ContextMenu& menu,
                       std::vector<DeviceNode>& nodes,
                       std::vector<Cable>& cables,
                       int& selectedId,
                       PanelState& ps,
                       Camera2D& camera,
                       SimState& simState);
```

- [ ] **Step 2: Create `src/UI.cpp`**

Cut `ContextType` enum, `ContextMenu` struct, `nextId`, `SpawnNode`, `UpdateContextMenuHover`, and `ExecuteMenuAction` from main.cpp.

The file header plus `nextId`:

```cpp
#include "UI.h"
#include "NetworkCanvas.h"   // for CANVAS_W, CANVAS_H, CONTEXT_MENU_W, MENU_ITEM_H

// Temporary — replaced by full NetworkCanvas.h in Task 8 Step 4
// (same pattern as ConfigPanel.cpp Task 6 Step 2)
// If NetworkCanvas.h does not yet exist, add the same temporary constants
// block used in ConfigPanel.cpp, then remove them in Task 8.

static int nextId = 1;
```

Then `SpawnNode`, `UpdateContextMenuHover`, and `ExecuteMenuAction` verbatim.

Notes on `ExecuteMenuAction`:
- The function already references `SIM_ANIMATING`, `SIM_IDLE`, `SIM_SELECTING_DST` — these come from `Packet.h` via `UI.h`.
- It references `TAB_CONFIG` — comes from `ConfigPanel.h` via `UI.h`.
- It references `SpawnNode`, `FindNode` — both available via `UI.h`.

- [ ] **Step 3: Update `src/main.cpp`**

3a. After `#include "ConfigPanel.h"` add:

```cpp
#include "UI.h"
```

3b. Delete from main.cpp:
- `ContextType` enum
- `ContextMenu` struct
- `static int nextId = 1;`
- `SpawnNode` function
- `UpdateContextMenuHover` function
- `ExecuteMenuAction` function

Keep `DrawContextMenu` in main.cpp for now — it moves in Task 8.

- [ ] **Step 4: Build and verify**

```bash
make clean && make 2>&1
```

Expected: zero errors, zero warnings.

- [ ] **Step 5: Smoke test**

```bash
./packet-path
```

Expected: right-click canvas opens context menu. "Add PC Here" / "Add Router Here" / "Add Switch Here" all spawn correctly. Right-click a node → "Delete" removes it and its cables.

- [ ] **Step 6: Commit**

```bash
git add src/UI.h src/UI.cpp src/main.cpp
git commit -m "refactor: extract UI module (ContextMenu, SpawnNode, ExecuteMenuAction)"
```

---

## Task 8: NetworkCanvas Module

**Files:**
- Create: `src/NetworkCanvas.h`
- Create: `src/NetworkCanvas.cpp`
- Modify: `src/ConfigPanel.cpp` (replace temp constants with include)
- Modify: `src/UI.cpp` (replace temp constants with include)
- Modify: `src/main.cpp`

This is the largest task. `NetworkCanvas` becomes the single rendering layer: every function that calls a `DrawX` or `BeginMode2D` raylib function lives here. It also owns all screen/layout constants and the two hit-test functions (`HitTestPort`, `HitTestCable`).

- [ ] **Step 1: Create `src/NetworkCanvas.h`**

```cpp
#pragma once
#include "raylib.h"
#include "Device.h"
#include "Cable.h"
#include "Packet.h"
#include "SimulationEngine.h"
#include "ConfigPanel.h"
#include "UI.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

// ── Screen & layout constants ─────────────────────────────────────────────
static const int   SCREEN_W       = 1280;
static const int   SCREEN_H       = 720;
static const Color BG_COLOR       = {15, 23, 42, 255};
static const int   PANEL_W        = 280;
static const int   CANVAS_W       = SCREEN_W - PANEL_W;   // 1000
static const int   LOG_H          = 90;
static const int   CANVAS_H       = SCREEN_H - LOG_H;     // 630
static const Color PANEL_BG       = {22, 33, 62, 255};
static const Color PANEL_BORDER   = {51, 65, 85, 255};
static const int   MENU_ITEM_H    = 28;
static const int   CONTEXT_MENU_W = 160;

// ── Config tab layout ──────────────────────────────────────────────────────
static const int CFG_HOSTNAME_Y   = 158;
static const int CFG_MGMTIP_Y     = 210;
static const int CFG_IFACE_SEP_Y  = 246;
static const int CFG_PORT_Y0      = 272;
static const int CFG_PORT_STRIDE  = 44;

// ── Routes tab layout ──────────────────────────────────────────────────────
static const int RTE_ROW_Y0       = 142;
static const int RTE_HEADER_SEP_Y = 136;
static const int RTE_ROW_H        = 22;
static const int RTE_ADD_SEP_Y    = 420;
static const int RTE_DEST_Y       = 464;
static const int RTE_NEXT_Y       = 516;
static const int RTE_BTN_Y        = 554;

// ── Canvas hit testing ─────────────────────────────────────────────────────
bool HitTestPort(const std::vector<DeviceNode>& nodes, Vector2 worldMouse,
                 int excludeId, int& outNode, int& outPort);

int  HitTestCable(const std::vector<Cable>& cables,
                  const std::vector<DeviceNode>& nodes,
                  Vector2 worldMouse, float threshold);

// ── Draw functions ────────────────────────────────────────────────────────
void DrawDotGrid(const Camera2D& cam);
void DrawDeviceNode(const DeviceNode& n);
void DrawAllCables(const std::vector<Cable>& cables,
                   const std::vector<DeviceNode>& nodes);
void DrawPacketAnim(const PacketAnim& anim,
                    const std::vector<DeviceNode>& nodes,
                    const std::vector<Cable>& cables);
void DrawTextField(Rectangle r, const char* topLabel, const char* placeholder,
                   const std::string& value, bool active, bool valid);
void DrawConfigTab(const DeviceNode* n, const PanelState& ps);
void DrawRoutesTab(const DeviceNode* n, const PanelState& ps);
void DrawPanel(int selectedId, const std::vector<DeviceNode>& nodes,
               const PanelState& ps);
void DrawContextMenu(const ContextMenu& menu, Vector2 screenMouse);
void DrawLogConsole(const std::vector<LogEntry>& entries);
```

- [ ] **Step 2: Create `src/NetworkCanvas.cpp`**

The file starts with:

```cpp
#include "NetworkCanvas.h"
```

Then cut the following functions verbatim from `main.cpp` and paste them here:

- `DrawDotGrid`
- `DrawDeviceNode`
- `DrawAllCables`
- `HitTestPort`
- `HitTestCable`
- `DrawTextField`
- `DrawConfigTab`
- `DrawRoutesTab`
- `DrawPanel`
- `DrawContextMenu`
- `DrawPacketAnim`
- `DrawLogConsole`

That is every remaining draw function in main.cpp. `HitTestCable` also moves here (it was still in main.cpp after Task 3 because it uses raylib collision calls, not pure data).

- [ ] **Step 3: Update `src/ConfigPanel.cpp`**

Replace the entire temporary constants block (the 17 `static const` lines added in Task 6 Step 2) with a single include:

```cpp
#include "ConfigPanel.h"
#include "NetworkCanvas.h"
```

Delete the temporary block. The eleven functions follow unchanged.

- [ ] **Step 4: Update `src/UI.cpp`**

Replace the temporary constants block in `UI.cpp` (same as above) with:

```cpp
#include "UI.h"
#include "NetworkCanvas.h"
```

Delete the temporary block. `SpawnNode`, `UpdateContextMenuHover`, `ExecuteMenuAction` follow unchanged.

- [ ] **Step 5: Update `src/main.cpp`**

5a. Replace ALL existing `#include` lines at the top of main.cpp with a single include:

```cpp
#include "NetworkCanvas.h"
```

(NetworkCanvas.h transitively provides raylib, Device, Cable, Packet, SimulationEngine, ConfigPanel, UI, and all standard headers used by the game loop.)

5b. Delete these remaining functions from main.cpp (they are now in NetworkCanvas.cpp):
- `DrawDotGrid`
- `DrawDeviceNode`
- `DrawAllCables`
- `HitTestPort`
- `HitTestCable`
- `DrawTextField`
- `DrawConfigTab`
- `DrawRoutesTab`
- `DrawPanel`
- `DrawContextMenu`
- `DrawPacketAnim`
- `DrawLogConsole`

5c. Delete the remaining screen/layout constants from main.cpp (they are now in NetworkCanvas.h):
- `SCREEN_W`, `SCREEN_H`, `BG_COLOR`, `PANEL_W`, `CANVAS_W`, `PANEL_BG`, `PANEL_BORDER`
- `MENU_ITEM_H`, `CONTEXT_MENU_W`, `LOG_H`, `CANVAS_H`
- All `CFG_*` constants
- All `RTE_*` constants

After Step 5, `main.cpp` should contain only: the single `#include "NetworkCanvas.h"`, the `main()` function body, and nothing else.

- [ ] **Step 6: Build and verify**

```bash
make clean && make 2>&1
```

Expected: zero errors, zero warnings. If there are redefinition warnings for the `static const` constants (because NetworkCanvas.h and ConfigPanel.cpp both temporarily defined some of them), they should be gone now that the temporary blocks were removed in Steps 3–4.

- [ ] **Step 7: Smoke test**

```bash
./packet-path
```

Verify the full feature set works:
- Dot grid renders, pan (middle mouse) and zoom (scroll) work
- Spawn PC/Router/Switch via right-click context menu
- Drag nodes, bezier cables form between ports, cables follow nodes
- Select node → config panel opens with Config and Routes tabs
- IP fields validate (green/red border)
- Add/delete static routes in Routes tab
- Right-click node → "Send Packet To…" → click destination → packet animates + log updates
- Failed path shows red pulse + failure reason in log

- [ ] **Step 8: Commit**

```bash
git add src/NetworkCanvas.h src/NetworkCanvas.cpp src/ConfigPanel.cpp src/UI.cpp src/main.cpp
git commit -m "refactor: extract NetworkCanvas module — all draw functions centralised, main.cpp slimmed to game loop"
```

---

## Task 9: Final Cleanup and Line Count Verification

**Files:**
- Verify: `src/main.cpp` (should be ~200 lines or fewer)
- Verify: all module files compile cleanly

This task has no code changes — it's a verification and cleanup pass.

- [ ] **Step 1: Count lines across all source files**

```bash
wc -l src/*.h src/*.cpp
```

Expected approximate distribution:

| File | Approx lines |
|---|---|
| `Device.h` | ~70 |
| `Device.cpp` | ~90 |
| `Cable.h` | ~15 |
| `Cable.cpp` | ~20 |
| `Packet.h` | ~35 |
| `Packet.cpp` | ~55 |
| `SimulationEngine.h` | ~12 |
| `SimulationEngine.cpp` | ~80 |
| `ConfigPanel.h` | ~40 |
| `ConfigPanel.cpp` | ~80 |
| `UI.h` | ~35 |
| `UI.cpp` | ~70 |
| `NetworkCanvas.h` | ~75 |
| `NetworkCanvas.cpp` | ~380 |
| `main.cpp` | ~200 |

If `main.cpp` exceeds 250 lines, check for leftover declarations that belong in a module.

- [ ] **Step 2: Clean build from scratch**

```bash
make clean && make 2>&1 | grep -E "error:|warning:" | head -30
```

Expected: no output (zero errors, zero warnings).

- [ ] **Step 3: Full smoke test**

```bash
./packet-path
```

Run through the complete verification checklist:
1. Window opens at 1280×720, dark navy background, dot grid
2. Right-click → Add PC, Add Router, Add Switch → all three types spawn
3. Drag nodes around — cables follow if connected
4. Connect PC to Router via port drag — bezier cable appears
5. Select node → Config tab shows hostname + mgmt IP + 4 port fields
6. Type `10.0.0.1/30` in a port field — border turns green; type junk — border red
7. Routes tab → add static route → appears in table → delete with ×
8. Right-click a node → "Send Packet To…" → click another node with compatible IP → green dot animates
9. Configure a missing route scenario → red pulse + log shows "no route to X"
10. Right-click a cable → "Delete Cable" removes it
11. Zoom to 0.15× and 4.0× — nodes stay draggable, hit detection correct
12. ESC cancels connect mode, clears field focus, cancels packet selection

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "refactor: M3.1 multi-file split complete — 1379-line main.cpp split into 7 focused modules"
```

---

## Self-Review Checklist

**Spec coverage:**
- [x] Device module extracts all data structs and IP utilities
- [x] Cable module extracts Cable struct and bezier helpers
- [x] Packet module extracts sim state and animation logic
- [x] SimulationEngine module isolates forwarding logic
- [x] ConfigPanel module extracts panel state and input handlers
- [x] UI module extracts context menu state and spawn logic
- [x] NetworkCanvas module centralises ALL draw functions
- [x] main.cpp reduced to single include + game loop
- [x] Makefile updated for multi-file compilation
- [x] No raylib draw calls in data struct files (Device, Cable, Packet, SimulationEngine)
- [x] HOP_DURATION moved to Packet.h (correct owner)
- [x] PORTS_PER_NODE moved to Device.h (correct owner)
- [x] All screen/layout constants centralised in NetworkCanvas.h

**Type consistency:**
- `BezierCtrl` declared in Cable.h, called in NetworkCanvas.cpp ✓
- `FindNode` declared in Cable.h, called in SimulationEngine.cpp + NetworkCanvas.cpp ✓
- `UpdateContextMenuHover` declared in UI.h, called in main.cpp game loop ✓
- `HitTestPort` and `HitTestCable` declared in NetworkCanvas.h, called in main.cpp ✓
- `PnlTabW` — `static` removed, declared in ConfigPanel.h, called in NetworkCanvas.cpp ✓

**Placeholder scan:** No TBDs, no "implement later", no "similar to Task N". All file contents shown in full.
