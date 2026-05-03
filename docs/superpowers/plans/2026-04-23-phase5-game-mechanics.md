# Phase 5: Game Mechanics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the network simulator into a 4-level game with pre-configured topologies, win conditions verified by SimulateForward, and a star-rating overlay when all conditions pass.

**Architecture:** Two new source modules — `Level.h/.cpp` (data loading, win checking) and `GameUI.h/.cpp` (draw primitives) — plus targeted changes to `main.cpp` for a 3-state game mode machine (GAME_SANDBOX / GAME_PLAYING / GAME_WIN). Levels are JSON files in `levels/`, parsed with nlohmann/json (single-header, placed in `include/nlohmann/`). Win conditions call `SimulateForward` programmatically after each player-triggered simulation. `ApplyLevel` replaces nodes/cables wholesale, which resets all OSPF runtime state automatically because the JSON-loaded DeviceNodes have empty neighbor/LSDB vectors.

**Tech Stack:** C++17, raylib 5.5, nlohmann/json 3.11.3 (single-header), GNU Make, macOS

---

## File Map

| Action | File | Responsibility |
| -------- | ------ | ---------------- |
| Create | `include/nlohmann/json.hpp` | Single-header JSON parser (downloaded) |
| Create | `src/Level.h` | `WinCondition`, `LevelDef`, `LoadLevel`, `ApplyLevel`, `CheckWinConditions` |
| Create | `src/Level.cpp` | JSON parsing, level application, win-condition runner |
| Create | `src/GameUI.h` | `GameMode` enum, `DrawLevelHUD`, `DrawWinOverlay`, overlay rect helpers |
| Create | `src/GameUI.cpp` | Draw implementations |
| Create | `levels/level_01.json` | Basic Ping — 1 router, static routes, pre-configured |
| Create | `levels/level_02.json` | Multi-Hop Routing — 2 routers, static routes |
| Create | `levels/level_03.json` | OSPF Single-Area — OSPF replaces static routes between routers |
| Create | `levels/level_04.json` | Multi-Area OSPF — ABR, O IA routes |
| Modify | `Makefile` | Add `-isystem ./include` to suppress third-party header warnings |
| Modify | `src/UI.h` | Declare `SetNextId(int)` |
| Modify | `src/UI.cpp` | Implement `SetNextId(int)` |
| Modify | `src/main.cpp` | Game mode state, level key shortcuts, win-check hook, HUD and overlay draw calls |

---

## Task 1: Level data model, JSON loader, win-condition checker (M5.1 + M5.2)

**Files:**

- Create: `include/nlohmann/json.hpp` (downloaded)
- Create: `src/Level.h`
- Create: `src/Level.cpp`
- Modify: `Makefile`
- Modify: `src/UI.h`
- Modify: `src/UI.cpp`

---

- [ ] **Step 1: Download nlohmann/json single header**

```bash
mkdir -p include/nlohmann
curl -L -o include/nlohmann/json.hpp \
  "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp"
wc -l include/nlohmann/json.hpp
```

Expected: 25000+ lines printed.

---

- [ ] **Step 2: Update Makefile — add system include path**

Open `Makefile`. Change line 2 from:

```makefile
CFLAGS   = -std=c++17 -Wall -Wextra -O2
```

to:

```makefile
CFLAGS   = -std=c++17 -Wall -Wextra -O2 -isystem ./include
```

(`-isystem` treats `./include` as a system header directory, suppressing `-Wextra` warnings emitted by nlohmann/json.)

---

- [ ] **Step 3: Add `SetNextId` to UI.h**

In `src/UI.h`, after the `DeviceNode SpawnNode(...)` declaration, add:

```cpp
void SetNextId(int n);  // called by ApplyLevel to avoid ID collisions after level load
```

---

- [ ] **Step 4: Add `SetNextId` to UI.cpp**

In `src/UI.cpp`, after `static int nextId = 1;` on line 4, add:

```cpp
void SetNextId(int n) { nextId = n; }
```

---

- [ ] **Step 5: Write `src/Level.h`**

Create the file with this exact content:

```cpp
#pragma once
#include "Device.h"
#include "Cable.h"
#include <string>
#include <vector>

struct WinCondition {
    std::string srcLabel;    // label of the source device
    std::string dstLabel;    // label of the destination device
    std::string description; // shown in win overlay: "PC-A can reach PC-B"
};

struct LevelDef {
    int                       id = 0;
    std::string               title;
    std::string               briefing;        // instructional text (stored; shown in Phase 6)
    std::vector<DeviceNode>   devices;
    std::vector<Cable>        cables;
    std::vector<WinCondition> winConditions;
};

// Load a level from JSON file. Returns false on file-not-found or parse error.
// On success, also calls SetNextId(maxId + 1) so SpawnNode won't conflict.
bool LoadLevel(const std::string& path, LevelDef& out);

// Replace nodes/cables with the level's pre-configured topology.
// Resets selectedId. OSPF runtime state (neighbors, LSDbs, routes) is reset
// implicitly because the JSON-loaded DeviceNodes have empty containers.
void ApplyLevel(const LevelDef& def,
                std::vector<DeviceNode>& nodes,
                std::vector<Cable>& cables,
                int& selectedId);

// Run SimulateForward for every win condition.
// Returns the number of conditions that passed (0 .. winConditions.size()).
int CheckWinConditions(const LevelDef& def,
                       const std::vector<DeviceNode>& nodes,
                       const std::vector<Cable>& cables);
```

---

- [ ] **Step 6: Write `src/Level.cpp`**

Create the file with this exact content:

```cpp
#include "Level.h"
#include "Packet.h"
#include "SimulationEngine.h"
#include "UI.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

using json = nlohmann::json;

bool LoadLevel(const std::string& path, LevelDef& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    json j;
    try { f >> j; } catch (...) { return false; }

    out          = LevelDef{};
    out.id       = j.value("id",       0);
    out.title    = j.value("title",    "");
    out.briefing = j.value("briefing", "");

    int maxId = 0;
    for (const auto& d : j.value("devices", json::array())) {
        DeviceNode n;
        n.id    = d.value("id",    0);
        n.label = d.value("label", "");
        std::string typeStr = d.value("type", "PC");
        if      (typeStr == "ROUTER") n.type = ROUTER;
        else if (typeStr == "SWITCH") n.type = SWITCH;
        else                          n.type = PC;
        n.position = {d.value("x", 0.0f), d.value("y", 0.0f)};

        for (int i = 0; i < PORTS_PER_NODE; ++i) {
            std::string key = "portIp" + std::to_string(i);
            n.portIp[i] = d.value(key, "");
        }

        n.ospfEnabled = d.value("ospfEnabled", false);
        for (int i = 0; i < PORTS_PER_NODE; ++i) {
            std::string key = "ospfArea" + std::to_string(i);
            n.ospfPortArea[i] = (uint32_t)d.value(key, 0);
        }

        for (const auto& sr : d.value("staticRoutes", json::array())) {
            RouteEntry re;
            re.dest    = sr.value("dest",    "");
            re.nextHop = sr.value("nextHop", "");
            re.outPort = -1;
            re.src     = ROUTE_STATIC;
            n.staticRoutes.push_back(re);
        }

        maxId = std::max(maxId, n.id);
        out.devices.push_back(n);
    }

    for (const auto& c : j.value("cables", json::array())) {
        Cable cable;
        cable.fromId   = c.value("from",     0);
        cable.fromPort = c.value("fromPort", 0);
        cable.toId     = c.value("to",       0);
        cable.toPort   = c.value("toPort",   0);
        out.cables.push_back(cable);
    }

    for (const auto& wc : j.value("winConditions", json::array())) {
        WinCondition w;
        w.srcLabel    = wc.value("src",         "");
        w.dstLabel    = wc.value("dst",         "");
        w.description = wc.value("description", "");
        out.winConditions.push_back(w);
    }

    SetNextId(maxId + 1);
    return true;
}

void ApplyLevel(const LevelDef& def,
                std::vector<DeviceNode>& nodes,
                std::vector<Cable>& cables,
                int& selectedId) {
    nodes      = def.devices;
    cables     = def.cables;
    selectedId = -1;
    int maxId  = 0;
    for (const auto& n : nodes) maxId = std::max(maxId, n.id);
    SetNextId(maxId + 1);
}

int CheckWinConditions(const LevelDef& def,
                       const std::vector<DeviceNode>& nodes,
                       const std::vector<Cable>& cables) {
    int passed = 0;
    for (const auto& wc : def.winConditions) {
        const DeviceNode* src = nullptr;
        const DeviceNode* dst = nullptr;
        for (const auto& n : nodes) {
            if (n.label == wc.srcLabel) src = &n;
            if (n.label == wc.dstLabel) dst = &n;
        }
        if (!src || !dst) continue;
        std::string dstIp = GetFirstValidIp(*dst);
        if (dstIp.empty()) continue;
        ForwardResult fr = SimulateForward(src->id, dstIp, nodes, cables);
        if (fr.success) ++passed;
    }
    return passed;
}
```

---

- [ ] **Step 7: Build and verify**

```bash
make clean && make 2>&1
```

Expected: compiles to `packet-path` with zero errors and zero warnings.

---

- [ ] **Step 8: Commit**

```bash
git add include/nlohmann/json.hpp src/Level.h src/Level.cpp \
        src/UI.h src/UI.cpp Makefile
git commit -m "feat(m5.1-m5.2): Level data model, JSON loader, win-condition checker"
```

---

## Task 2: Level HUD and win overlay UI (M5.3)

**Files:**

- Create: `src/GameUI.h`
- Create: `src/GameUI.cpp`

The Makefile already uses `$(wildcard src/*.cpp)`, so these files are picked up automatically.

---

- [ ] **Step 1: Write `src/GameUI.h`**

Create the file with this exact content:

```cpp
#pragma once
#include "Level.h"
#include "raylib.h"
#include <string>

enum GameMode { GAME_SANDBOX, GAME_PLAYING, GAME_WIN };

// Overlay layout (centered on the canvas — requires CANVAS_W/CANVAS_H from NetworkCanvas.h,
// so implementations live in GameUI.cpp which includes NetworkCanvas.h).
Rectangle WinOverlayRect();
Rectangle WinRetryBtnRect();
Rectangle WinNextBtnRect();

// Top-left badge showing current level, title, and win-condition progress.
void DrawLevelHUD(int levelId, const std::string& title,
                  int conditionsPassed, int conditionsTotal);

// Semi-transparent win overlay with "LEVEL COMPLETE!", stars, checklist, and buttons.
// hasNextLevel: if false, the Next Level button is greyed out.
void DrawWinOverlay(const LevelDef& def, bool hasNextLevel);
```

---

- [ ] **Step 2: Write `src/GameUI.cpp`**

Create the file with this exact content:

```cpp
#include "GameUI.h"
#include "NetworkCanvas.h"   // CANVAS_W, CANVAS_H
#include <cstdio>

Rectangle WinOverlayRect() {
    return {(float)(CANVAS_W - 320) / 2.0f,
            (float)(CANVAS_H - 240) / 2.0f,
            320.0f, 240.0f};
}

Rectangle WinRetryBtnRect() {
    Rectangle r = WinOverlayRect();
    return {r.x + 20, r.y + 186, 120.0f, 36.0f};
}

Rectangle WinNextBtnRect() {
    Rectangle r = WinOverlayRect();
    return {r.x + 180, r.y + 186, 120.0f, 36.0f};
}

void DrawLevelHUD(int levelId, const std::string& title,
                  int conditionsPassed, int conditionsTotal) {
    // Badge: top-left of canvas, 8px inset
    DrawRectangle(8, 8, 224, 22, Color{10, 15, 28, 210});
    DrawRectangleLinesEx({8, 8, 224, 22}, 1.0f, Color{51, 65, 85, 255});

    char buf[80];
    std::snprintf(buf, sizeof(buf), "LVL %d  %s", levelId, title.c_str());
    DrawText(buf, 14, 13, 10, Color{148, 163, 184, 255});

    // Condition counter (right-aligned inside badge)
    char prog[8];
    std::snprintf(prog, sizeof(prog), "%d/%d", conditionsPassed, conditionsTotal);
    Color progColor = (conditionsPassed == conditionsTotal && conditionsTotal > 0)
                    ? Color{34, 197, 94, 255}
                    : Color{234, 179, 8, 255};
    int pw = MeasureText(prog, 10);
    DrawText(prog, 8 + 224 - pw - 8, 13, 10, progColor);
}

void DrawWinOverlay(const LevelDef& def, bool hasNextLevel) {
    // Canvas dim
    DrawRectangle(0, 0, CANVAS_W, CANVAS_H, Color{0, 0, 0, 150});

    Rectangle r = WinOverlayRect();
    DrawRectangleRounded(r, 0.12f, 8, Color{22, 33, 62, 255});
    DrawRectangleRoundedLinesEx(r, 0.12f, 8, 2.0f, Color{59, 130, 246, 255});

    // "LEVEL COMPLETE!"
    const char* done = "LEVEL COMPLETE!";
    int tw = MeasureText(done, 20);
    DrawText(done, (int)(r.x + (r.width - tw) / 2.0f), (int)r.y + 20, 20, WHITE);

    // Level title
    int ttw = MeasureText(def.title.c_str(), 12);
    DrawText(def.title.c_str(),
             (int)(r.x + (r.width - ttw) / 2.0f), (int)r.y + 50, 12,
             Color{148, 163, 184, 255});

    // Three gold stars (UTF-8 filled star ★ = \xe2\x98\x85)
    const char* star   = "\xe2\x98\x85";
    Color       starC  = Color{234, 179, 8, 255};
    int         sx     = (int)(r.x + (r.width - 72) / 2.0f);
    DrawText(star, sx,      (int)r.y + 76, 24, starC);
    DrawText(star, sx + 24, (int)r.y + 76, 24, starC);
    DrawText(star, sx + 48, (int)r.y + 76, 24, starC);

    // Win conditions checklist
    int cy = (int)r.y + 116;
    for (const auto& wc : def.winConditions) {
        std::string line = "\xe2\x9c\x93 " + wc.description;  // UTF-8 ✓
        DrawText(line.c_str(), (int)(r.x + 20), cy, 11, Color{34, 197, 94, 255});
        cy += 18;
    }

    // Retry button
    Rectangle retry = WinRetryBtnRect();
    DrawRectangleRounded(retry, 0.3f, 4, Color{30, 41, 59, 255});
    DrawRectangleLinesEx(retry, 1.0f, Color{51, 65, 85, 255});
    {
        int rtw = MeasureText("Retry", 12);
        DrawText("Retry", (int)(retry.x + (retry.width - rtw) / 2.0f),
                 (int)(retry.y + 12), 12, Color{148, 163, 184, 255});
    }

    // Next Level button
    Rectangle next   = WinNextBtnRect();
    Color     nextBg = hasNextLevel ? Color{30, 58, 138, 255} : Color{22, 33, 62, 255};
    Color     nextBr = hasNextLevel ? Color{59, 130, 246, 255} : Color{51, 65, 85, 255};
    Color     nextTx = hasNextLevel ? WHITE : Color{51, 65, 85, 255};
    DrawRectangleRounded(next, 0.3f, 4, nextBg);
    DrawRectangleLinesEx(next, 1.0f, nextBr);
    {
        const char* nlabel = "Next Level";
        int ntw = MeasureText(nlabel, 12);
        DrawText(nlabel, (int)(next.x + (next.width - ntw) / 2.0f),
                 (int)(next.y + 12), 12, nextTx);
    }
}
```

---

- [ ] **Step 3: Build and verify**

```bash
make clean && make 2>&1
```

Expected: zero errors, zero warnings. `packet-path` produced.

---

- [ ] **Step 4: Commit**

```bash
git add src/GameUI.h src/GameUI.cpp
git commit -m "feat(m5.3): GameUI — level HUD badge and win overlay with star rating"
```

---

## Task 3: Wire levels into main.cpp (M5.2 + M5.3 game loop integration)

**Files:**

- Modify: `src/main.cpp`

This task makes five precise changes to `main.cpp`. Read the full file before editing to confirm line numbers match (the file is ~565 lines as of this plan).

---

- [ ] **Step 1: Add includes at top of main.cpp**

After line 2 (`#include "OspfEngine.h"`), add:

```cpp
#include "Level.h"
#include "GameUI.h"
```

---

- [ ] **Step 2: Add game-mode state variables inside `main()`**

After the line `SimState simState;` (currently around line 32), add:

```cpp
GameMode    gameMode             = GAME_SANDBOX;
int         currentLevel         = 0;   // 0 = sandbox, 1-4 = levels
LevelDef    activeLevelDef;
int         lastConditionsPassed = 0;
```

---

- [ ] **Step 3: Add level key shortcuts (keys 1–4 load levels, 0 returns to sandbox)**

In the spawn/delete block — the `if (inCanvas && ps.activeField == -1 && ps.activeRouteField == -1 && simState.mode == SIM_IDLE)` block — add AFTER the existing P/R/S spawn lines and BEFORE the closing `}` of that block:

```cpp
            // Level shortcuts: 1–4 load JSON levels, 0 returns to sandbox
            if (ps.activePortAreaField == -1) {
                for (int k = 1; k <= 4; ++k) {
                    if (IsKeyPressed(KEY_ONE + (k - 1))) {
                        char path[64];
                        std::snprintf(path, sizeof(path), "levels/level_%02d.json", k);
                        LevelDef def;
                        if (LoadLevel(path, def)) {
                            activeLevelDef       = def;
                            ApplyLevel(def, nodes, cables, selectedId);
                            ps                   = PanelState{};
                            simState             = SimState{};
                            logEntries.clear();
                            lastConditionsPassed = 0;
                            gameMode             = GAME_PLAYING;
                            currentLevel         = k;
                        }
                    }
                }
                if (IsKeyPressed(KEY_ZERO)) {
                    gameMode     = GAME_SANDBOX;
                    currentLevel = 0;
                }
            }
```

---

- [ ] **Step 4: Wrap the LMB handler — win overlay clicks consume the event**

The LMB pressed section currently begins with:

```cpp
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (contextMenu.visible) {
```

Replace the entire `if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { ... }` block with this structure (keep all the existing interior code verbatim — only wrap it):

```cpp
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (gameMode == GAME_WIN) {
                // Win overlay clicks — consume event; don't fall through to canvas
                if (CheckCollisionPointRec(screenMouse, WinRetryBtnRect())) {
                    ApplyLevel(activeLevelDef, nodes, cables, selectedId);
                    ps                   = PanelState{};
                    simState             = SimState{};
                    logEntries.clear();
                    lastConditionsPassed = 0;
                    gameMode             = GAME_PLAYING;
                } else if (CheckCollisionPointRec(screenMouse, WinNextBtnRect()) &&
                           currentLevel < 4) {
                    ++currentLevel;
                    char path[64];
                    std::snprintf(path, sizeof(path), "levels/level_%02d.json", currentLevel);
                    LevelDef def;
                    if (LoadLevel(path, def)) {
                        activeLevelDef       = def;
                        ApplyLevel(def, nodes, cables, selectedId);
                        ps                   = PanelState{};
                        simState             = SimState{};
                        logEntries.clear();
                        lastConditionsPassed = 0;
                        gameMode             = GAME_PLAYING;
                    }
                }
                // any other click on the WIN screen is silently consumed
            } else {
                // ── existing LMB code (verbatim, indented one level) ──────────
                if (contextMenu.visible) {
                    /* ... all existing code until the final closing brace ... */
                }
            }
        }
```

Keep every line of existing LMB code intact inside the `else` block. Only add the outer `if (gameMode == GAME_WIN) { ... } else {` wrapper and the matching closing `}`.

---

- [ ] **Step 5: Add win-condition check after each SimulateForward**

Inside the destination-click handler (still inside the LMB section), after the line:

```cpp
                    pushLog(le);
```

(this is the `pushLog` that adds the LOG_FORWARD entry after the simulation attempt), add:

```cpp
                    // Check all win conditions after every simulation attempt
                    if (gameMode == GAME_PLAYING &&
                        !activeLevelDef.winConditions.empty()) {
                        int passed = CheckWinConditions(
                                         activeLevelDef, nodes, cables);
                        lastConditionsPassed = passed;
                        if (passed == (int)activeLevelDef.winConditions.size())
                            gameMode = GAME_WIN;
                    }
```

---

- [ ] **Step 6: Add HUD and overlay draw calls in the draw section**

In the draw section, after the line `DrawLogConsole(logEntries);` and before `DrawFPS(...)`, add:

```cpp
            // Level HUD badge (top-left) and win overlay
            if (gameMode == GAME_PLAYING || gameMode == GAME_WIN) {
                DrawLevelHUD(currentLevel, activeLevelDef.title,
                             lastConditionsPassed,
                             (int)activeLevelDef.winConditions.size());
            }
            if (gameMode == GAME_WIN) {
                DrawWinOverlay(activeLevelDef, currentLevel < 4);
            }
```

---

- [ ] **Step 7: Update the bottom hint text**

Find the line:

```cpp
            DrawText("P=PC  R=Router  S=Switch  Del=Delete  MMB=Pan  Scroll=Zoom  Drag-port=Cable  Esc=Cancel",
                     10, CANVAS_H - 24, 12, Color{100, 116, 139, 255});
```

Replace with:

```cpp
            DrawText("P=PC  R=Router  S=Switch  Del=Delete  MMB=Pan  Scroll=Zoom  "
                     "Drag-port=Cable  Esc=Cancel  1-4=Level  0=Sandbox",
                     10, CANVAS_H - 24, 10, Color{100, 116, 139, 255});
```

(Font size reduced from 12 → 10 to fit the longer text.)

---

- [ ] **Step 8: Build and verify**

```bash
make clean && make 2>&1
```

Expected: zero errors, zero warnings.

---

- [ ] **Step 9: Smoke test — app opens, key 1 prints nothing (no JSON yet), no crash**

```bash
./packet-path &
```

Press key `1` — nothing happens (no level files yet). Press key `0` — nothing changes. App stays open without crashing. Kill the process with Ctrl-C.

---

- [ ] **Step 10: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m5.2-m5.3): wire level loading, win-condition check, and overlays into game loop"
```

---

## Task 4: Author 4 level JSON files (M5.4)

**Files:**

- Create: `levels/level_01.json`
- Create: `levels/level_02.json`
- Create: `levels/level_03.json`
- Create: `levels/level_04.json`

### JSON schema reference

```text
{
  "id":        int           — level number (1-4)
  "title":     string        — shown in HUD badge
  "briefing":  string        — stored for future display
  "devices": [
    {
      "id":         int       — node ID (used by cables to reference endpoints)
      "label":      string    — displayed on node; used by winConditions for lookup
      "type":       string    — "PC" | "ROUTER" | "SWITCH"
      "x":          float     — world-space position (canvas center = 0,0)
      "y":          float
      "portIp0"..3: string    — CIDR, e.g. "10.0.0.1/24"; omit or "" for unused ports
                                Port 0=top, 1=right, 2=bottom, 3=left
      "ospfEnabled": bool     — default false
      "ospfArea0"..3: int     — area ID per port; default 0
      "staticRoutes": [
        { "dest": "cidr", "nextHop": "ip" }
      ]
    }
  ],
  "cables": [
    { "from": nodeId, "fromPort": 0-3, "to": nodeId, "toPort": 0-3 }
  ],
  "winConditions": [
    { "src": "label", "dst": "label", "description": "..." }
  ]
}
```

Port directions: **0=top, 1=right, 2=bottom, 3=left**.

For all horizontal topologies, the convention is: **rightmost port (1) of the left device connects to leftmost port (3) of the right device.**

---

- [ ] **Step 1: Create `levels/` directory**

```bash
mkdir -p levels
```

---

- [ ] **Step 2: Create `levels/level_01.json` — Basic Ping**

Topology: `PC-A ←→ RTR-1 ←→ PC-B` (3 devices, 1 router hop)

Lesson: A packet crossing one router. Everything pre-configured — the player just sends a packet.

```json
{
  "id": 1,
  "title": "Basic Ping",
  "briefing": "All IPs and routes are pre-configured. Right-click PC-A, choose Send Packet To, then click PC-B.",
  "devices": [
    {
      "id": 1, "label": "PC-A", "type": "PC",
      "x": -200.0, "y": 0.0,
      "portIp1": "10.0.0.2/24",
      "staticRoutes": [{"dest": "0.0.0.0/0", "nextHop": "10.0.0.1"}]
    },
    {
      "id": 2, "label": "RTR-1", "type": "ROUTER",
      "x": 0.0, "y": 0.0,
      "portIp3": "10.0.0.1/24",
      "portIp1": "10.0.1.1/24"
    },
    {
      "id": 3, "label": "PC-B", "type": "PC",
      "x": 200.0, "y": 0.0,
      "portIp3": "10.0.1.2/24",
      "staticRoutes": [{"dest": "0.0.0.0/0", "nextHop": "10.0.1.1"}]
    }
  ],
  "cables": [
    {"from": 1, "fromPort": 1, "to": 2, "toPort": 3},
    {"from": 2, "fromPort": 1, "to": 3, "toPort": 3}
  ],
  "winConditions": [
    {"src": "PC-A", "dst": "PC-B", "description": "PC-A can reach PC-B"}
  ]
}
```

Static trace (expected forward path): PC-A → default via 10.0.0.1 → RTR-1 → C 10.0.1.0/24 → delivered.

---

- [ ] **Step 3: Create `levels/level_02.json` — Multi-Hop Static Routing**

Topology: `PC-A ←→ RTR-1 ←→ RTR-2 ←→ PC-B` (4 devices, 2 router hops)

Lesson: Explicit static routes on each router enable end-to-end reachability across 2 hops.

```json
{
  "id": 2,
  "title": "Multi-Hop Routing",
  "briefing": "Two routers chain PC-A to PC-B. Static routes on each router enable reachability. Send a packet from PC-A to PC-B.",
  "devices": [
    {
      "id": 1, "label": "PC-A", "type": "PC",
      "x": -300.0, "y": 0.0,
      "portIp1": "10.0.0.2/24",
      "staticRoutes": [{"dest": "0.0.0.0/0", "nextHop": "10.0.0.1"}]
    },
    {
      "id": 2, "label": "RTR-1", "type": "ROUTER",
      "x": -100.0, "y": 0.0,
      "portIp3": "10.0.0.1/24",
      "portIp1": "10.0.12.1/24",
      "staticRoutes": [{"dest": "10.0.1.0/24", "nextHop": "10.0.12.2"}]
    },
    {
      "id": 3, "label": "RTR-2", "type": "ROUTER",
      "x": 100.0, "y": 0.0,
      "portIp3": "10.0.12.2/24",
      "portIp1": "10.0.1.1/24",
      "staticRoutes": [{"dest": "10.0.0.0/24", "nextHop": "10.0.12.1"}]
    },
    {
      "id": 4, "label": "PC-B", "type": "PC",
      "x": 300.0, "y": 0.0,
      "portIp3": "10.0.1.2/24",
      "staticRoutes": [{"dest": "0.0.0.0/0", "nextHop": "10.0.1.1"}]
    }
  ],
  "cables": [
    {"from": 1, "fromPort": 1, "to": 2, "toPort": 3},
    {"from": 2, "fromPort": 1, "to": 3, "toPort": 3},
    {"from": 3, "fromPort": 1, "to": 4, "toPort": 3}
  ],
  "winConditions": [
    {"src": "PC-A", "dst": "PC-B", "description": "PC-A can reach PC-B across two hops"}
  ]
}
```

Static trace: PC-A → default via 10.0.0.1 → RTR-1 → S 10.0.1.0/24 via 10.0.12.2 → RTR-2 → C 10.0.1.0/24 → delivered.

---

- [ ] **Step 4: Create `levels/level_03.json` — OSPF Single-Area**

Topology: `PC-A ←→ RTR-1 ←→ RTR-2 ←→ PC-B` (same as Level 2, but routers run OSPF — no static routes between them)

Lesson: OSPF discovers neighbor networks automatically. Wait ~2 seconds for adjacencies to reach FULL before sending.

```json
{
  "id": 3,
  "title": "OSPF — Single Area",
  "briefing": "OSPF is enabled on both routers (all ports in Area 0). Wait a moment for adjacencies to form, then send a packet from PC-A to PC-B.",
  "devices": [
    {
      "id": 1, "label": "PC-A", "type": "PC",
      "x": -300.0, "y": 0.0,
      "portIp1": "10.0.0.2/24",
      "staticRoutes": [{"dest": "0.0.0.0/0", "nextHop": "10.0.0.1"}]
    },
    {
      "id": 2, "label": "RTR-1", "type": "ROUTER",
      "x": -100.0, "y": 0.0,
      "portIp3": "10.0.0.1/24",
      "portIp1": "10.0.12.1/24",
      "ospfEnabled": true
    },
    {
      "id": 3, "label": "RTR-2", "type": "ROUTER",
      "x": 100.0, "y": 0.0,
      "portIp3": "10.0.12.2/24",
      "portIp1": "10.0.1.1/24",
      "ospfEnabled": true
    },
    {
      "id": 4, "label": "PC-B", "type": "PC",
      "x": 300.0, "y": 0.0,
      "portIp3": "10.0.1.2/24",
      "staticRoutes": [{"dest": "0.0.0.0/0", "nextHop": "10.0.1.1"}]
    }
  ],
  "cables": [
    {"from": 1, "fromPort": 1, "to": 2, "toPort": 3},
    {"from": 2, "fromPort": 1, "to": 3, "toPort": 3},
    {"from": 3, "fromPort": 1, "to": 4, "toPort": 3}
  ],
  "winConditions": [
    {"src": "PC-A", "dst": "PC-B", "description": "PC-A can reach PC-B via OSPF"}
  ]
}
```

Static trace (after OSPF converges): PC-A → default via 10.0.0.1 → RTR-1 → O 10.0.1.0/24 via 10.0.12.2 → RTR-2 → C 10.0.1.0/24 → delivered.

OSPF behavior: RTR-1 and RTR-2 both have `ospfEnabled=true`. OSPF_HELLO_INTERVAL=2.0s, so adjacency reaches FULL after the first hello exchange (~2s). After `anyChange`, SPF runs and routes populate.

---

- [ ] **Step 5: Create `levels/level_04.json` — Multi-Area OSPF + ABR**

Topology: `PC-A ←→ RTR-1 ←→ RTR-2(ABR) ←→ RTR-3 ←→ PC-B` (5 devices)

Lesson: RTR-2 is an Area Border Router — its left port is in Area 0, right port in Area 1. After convergence, RTR-1 and RTR-3 get O IA (inter-area) routes via RTR-2. The O IA routes appear in orange in the Routes tab.

Area assignments:

- RTR-1: all ports → Area 0 (default)
- RTR-2: port 3 (left, facing RTR-1) → Area 0; port 1 (right, facing RTR-3) → Area 1
- RTR-3: all ports → Area 1

```json
{
  "id": 4,
  "title": "Multi-Area OSPF — ABR",
  "briefing": "RTR-2 bridges Area 0 (left) and Area 1 (right). After OSPF converges, O IA routes appear on RTR-1 and RTR-3. Send a packet from PC-A to PC-B.",
  "devices": [
    {
      "id": 1, "label": "PC-A", "type": "PC",
      "x": -350.0, "y": 0.0,
      "portIp1": "10.0.0.2/24",
      "staticRoutes": [{"dest": "0.0.0.0/0", "nextHop": "10.0.0.1"}]
    },
    {
      "id": 2, "label": "RTR-1", "type": "ROUTER",
      "x": -175.0, "y": 0.0,
      "portIp3": "10.0.0.1/24",
      "portIp1": "10.0.12.1/24",
      "ospfEnabled": true
    },
    {
      "id": 3, "label": "RTR-2", "type": "ROUTER",
      "x": 0.0, "y": 0.0,
      "portIp3": "10.0.12.2/24",
      "portIp1": "10.0.23.1/24",
      "ospfEnabled": true,
      "ospfArea3": 0,
      "ospfArea1": 1
    },
    {
      "id": 4, "label": "RTR-3", "type": "ROUTER",
      "x": 175.0, "y": 0.0,
      "portIp3": "10.0.23.2/24",
      "portIp1": "10.0.1.1/24",
      "ospfEnabled": true
    },
    {
      "id": 5, "label": "PC-B", "type": "PC",
      "x": 350.0, "y": 0.0,
      "portIp3": "10.0.1.2/24",
      "staticRoutes": [{"dest": "0.0.0.0/0", "nextHop": "10.0.1.1"}]
    }
  ],
  "cables": [
    {"from": 1, "fromPort": 1, "to": 2, "toPort": 3},
    {"from": 2, "fromPort": 1, "to": 3, "toPort": 3},
    {"from": 3, "fromPort": 1, "to": 4, "toPort": 3},
    {"from": 4, "fromPort": 1, "to": 5, "toPort": 3}
  ],
  "winConditions": [
    {"src": "PC-A", "dst": "PC-B", "description": "PC-A can reach PC-B across two OSPF areas"}
  ]
}
```

Static trace (after OSPF converges):

- PC-A → default via 10.0.0.1 → RTR-1
- RTR-1 → O IA 10.0.1.0/24 via 10.0.12.2 → RTR-2
- RTR-2 → O 10.0.1.0/24 via 10.0.23.2 → RTR-3
- RTR-3 → C 10.0.1.0/24 → delivered ✓

ABR detection: RTR-2 has FULL neighbors in two different areas (RTR-1 in area 0, RTR-3 in area 1), so `IsAbr(RTR-2)` returns true. `PropagateSummaryRoutes` copies RTR-2's area-1 routes (O 10.0.23.0/24, O 10.0.1.0/24) as ROUTE_OSPF_IA into RTR-1's routing table, and copies area-0 routes as ROUTE_OSPF_IA into RTR-3's routing table.

---

- [ ] **Step 6: Build and run the full 4-level suite**

```bash
make clean && make 2>&1
```

Expected: zero errors, zero warnings.

```bash
./packet-path
```

Manual verification checklist:

- [ ] Press **1**: Level 1 loads — PC-A, RTR-1, PC-B appear on canvas. Level badge shows "LVL 1  Basic Ping  0/1" in yellow.
- [ ] Right-click PC-A → Send Packet To → click PC-B → green success animation → win overlay appears with "LEVEL COMPLETE!", three gold stars, "✓ PC-A can reach PC-B".
- [ ] Click **Retry** → level reloads, overlay disappears, badge shows 0/1 again.
- [ ] Click through to win again → click **Next Level** → Level 2 loads.
- [ ] Press **2**: Level 2 loads — 4 devices. Send PC-A → PC-B → win.
- [ ] Press **3**: Level 3 loads — OSPF enabled. Immediately sending fails (no routes yet). Wait ~2 seconds → cables turn green (OSPF FULL) → send PC-A → PC-B → win.
- [ ] Press **4**: Level 4 loads — 5 devices. Wait ~2 seconds → select RTR-2, open Routes tab → verify O routes appear. Open RTR-1 Routes tab → verify O IA entries in orange. Send PC-A → PC-B → win. "Next Level" button is greyed out (no level 5).
- [ ] Press **0**: returns to sandbox mode, badge disappears.

---

- [ ] **Step 7: Commit**

```bash
git add levels/level_01.json levels/level_02.json \
        levels/level_03.json levels/level_04.json
git commit -m "feat(m5.4): 4 CCNA-style level JSON files — ping, static routing, OSPF, multi-area"
```

---

## Self-Review

### Spec coverage

| Requirement | Task |
| ------------- | ------ |
| M5.1 — Level JSON format + loader | Task 1 (Level.h/Level.cpp, nlohmann/json) |
| M5.1 — `levels/level_01.json` schema | Task 4 (all 4 files) |
| M5.2 — Win condition checker | Task 1 (`CheckWinConditions`) + Task 3 (hook in main) |
| M5.3 — Star rating UI | Task 2 (`DrawWinOverlay` with 3 stars) |
| M5.3 — End-of-level overlay | Task 2 + Task 3 (draw call + click handlers) |
| M5.4 — Level 1: basic ping | Task 4 Step 2 |
| M5.4 — Level 2: static routing | Task 4 Step 3 |
| M5.4 — Level 3: OSPF single-area | Task 4 Step 4 |
| M5.4 — Level 4: OSPF multi-area | Task 4 Step 5 |
| Retry + Next Level flow | Task 3 Step 4 |
| Level HUD badge | Task 2 + Task 3 Step 6 |
| Keyboard shortcuts 1–4, 0 | Task 3 Step 3 |

All requirements covered. No gaps.

### Placeholder scan

No "TBD", "TODO", or incomplete sections. All code blocks are complete and match the type signatures defined in the same plan.

### Type consistency

- `LevelDef` defined in Task 1 (Level.h), used in Tasks 2, 3 — consistent.
- `GameMode` defined in Task 2 (GameUI.h), used in Task 3 — consistent.
- `WinRetryBtnRect()` / `WinNextBtnRect()` declared in GameUI.h, defined in GameUI.cpp, called in Task 3 — consistent.
- `CheckWinConditions(def, nodes, cables)` signature matches declaration and call sites — consistent.
- `ApplyLevel(def, nodes, cables, selectedId)` signature matches declaration and call sites — consistent.
- `LoadLevel(path, def)` signature matches declaration and call sites — consistent.
- `DrawLevelHUD(levelId, title, passed, total)` signature matches declaration and call sites — consistent.
- `DrawWinOverlay(def, hasNextLevel)` signature matches declaration and call sites — consistent.
