# Phase 1: Skeleton — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a zoomable, pannable 2D canvas with draggable PC/Router/Switch nodes that can be wired together with bezier cables — all in a single `src/main.cpp`.

**Architecture:** Single-file era. All structs (`DeviceNode`, `Cable`) are plain C++ with no raylib calls inside them. All raylib drawing lives in standalone functions (`DrawDeviceNode`, `DrawAllCables`). The camera transforms are applied once per frame around the draw block; all hit detection uses `GetScreenToWorld2D` to convert mouse position into world space before any collision checks.

**Tech Stack:** C++17, raylib 5.x, macOS (Makefile with `pkg-config` fallback for raylib)

---

## File Map

| File | Status | Responsibility |
| --- | --- | --- |
| `Makefile` | Create | Compile `src/main.cpp` → `./packet-path`, link raylib |
| `src/main.cpp` | Create | Everything: structs, draw functions, input, game loop |

---

## Task 0: Pre-Phase — Makefile + raylib Window

**Files:**

- Create: `Makefile`
- Create: `src/main.cpp`

- [ ] **Step 1: Create the Makefile**

```makefile
CC       = g++
CFLAGS   = -std=c++17 -Wall -Wextra -O2
INCLUDES = $(shell pkg-config --cflags raylib 2>/dev/null || echo "-I/usr/local/include")
LIBS     = $(shell pkg-config --libs   raylib 2>/dev/null || echo "-L/usr/local/lib -lraylib \
             -framework OpenGL -framework Cocoa -framework IOKit \
             -framework CoreAudio -framework CoreVideo")

TARGET = packet-path
SRC    = src/main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
 $(CC) $(CFLAGS) $(INCLUDES) $(SRC) $(LIBS) -o $(TARGET)

clean:
 rm -f $(TARGET)

.PHONY: all clean
```

> Note: the `$(CC)` recipe line must be indented with a **tab**, not spaces.

- [ ] **Step 2: Create `src/main.cpp` — window only**

```cpp
#include "raylib.h"

static const int   SCREEN_W = 1280;
static const int   SCREEN_H = 720;
static const Color BG_COLOR = {15, 23, 42, 255};  // dark navy

int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Packet Path");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
            ClearBackground(BG_COLOR);
            DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
```

- [ ] **Step 3: Build and verify**

```bash
make
```

Expected: compiles with zero warnings, produces `./packet-path`.

- [ ] **Step 4: Run and verify acceptance criteria**

```bash
./packet-path
```

Expected:

- 1280×720 dark navy window opens
- FPS counter visible in top-left, steady at 60
- Window closes cleanly on ✕ or Escape key

- [ ] **Step 5: Commit**

```bash
git add Makefile src/main.cpp
git commit -m "feat(pre): raylib window + Makefile build system"
```

---

## Task 1: M1.1 — Single Draggable DeviceNode

**Files:**

- Modify: `src/main.cpp`

- [ ] **Step 1: Add constants, enum, and struct above `main()`**

Replace the contents of `src/main.cpp` with:

```cpp
#include "raylib.h"
#include <string>

// ── Constants ─────────────────────────────────────────────────────────────
static const int   SCREEN_W = 1280;
static const int   SCREEN_H = 720;
static const float NODE_W   = 120.0f;
static const float NODE_H   =  60.0f;
static const Color BG_COLOR = {15, 23, 42, 255};

// ── Device types & node struct ─────────────────────────────────────────────
enum DeviceType { PC, ROUTER, SWITCH };

struct DeviceNode {
    int         id;
    DeviceType  type;
    Vector2     position;    // world-space centre
    std::string label;
    bool        selected = false;
};

// ── Helper functions ───────────────────────────────────────────────────────
Color GetDeviceColor(DeviceType t) {
    switch (t) {
        case PC:     return {59,  130, 246, 255};  // blue
        case ROUTER: return {249, 115,  22, 255};  // orange
        case SWITCH: return {34,  197,  94, 255};  // green
    }
    return WHITE;
}

Rectangle GetNodeRect(const DeviceNode& n) {
    return {n.position.x - NODE_W / 2.0f,
            n.position.y - NODE_H / 2.0f,
            NODE_W, NODE_H};
}

void DrawDeviceNode(const DeviceNode& n) {
    Rectangle r = GetNodeRect(n);
    Color     c = GetDeviceColor(n.type);

    // drop shadow
    DrawRectangleRounded({r.x + 3, r.y + 3, r.width, r.height}, 0.3f, 8,
                         Color{0, 0, 0, 80});
    // body
    DrawRectangleRounded(r, 0.3f, 8, c);
    // selection ring
    if (n.selected)
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, 2.5f, WHITE);

    // centred label
    int fsz = 14;
    int tw  = MeasureText(n.label.c_str(), fsz);
    DrawText(n.label.c_str(),
             (int)(n.position.x - tw / 2.0f),
             (int)(n.position.y - fsz / 2.0f),
             fsz, WHITE);
}

// ── Main ──────────────────────────────────────────────────────────────────
int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Packet Path");
    SetTargetFPS(60);

    DeviceNode node;
    node.id       = 1;
    node.type     = PC;
    node.position = {SCREEN_W / 2.0f, SCREEN_H / 2.0f};
    node.label    = "PC-1";

    bool    dragging   = false;
    Vector2 dragOffset = {0, 0};

    while (!WindowShouldClose()) {
        // ── Input ──────────────────────────────────────────────────────
        Vector2 mouse = GetMousePosition();

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mouse, GetNodeRect(node))) {
                dragging      = true;
                node.selected = true;
                dragOffset    = {mouse.x - node.position.x,
                                 mouse.y - node.position.y};
            } else {
                node.selected = false;
            }
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragging)
            node.position = {mouse.x - dragOffset.x, mouse.y - dragOffset.y};
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            dragging = false;

        // ── Draw ───────────────────────────────────────────────────────
        BeginDrawing();
            ClearBackground(BG_COLOR);
            DrawDeviceNode(node);
            DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
```

- [ ] **Step 2: Build**

```bash
make
```

Expected: zero warnings.

- [ ] **Step 3: Run and verify acceptance criteria**

```bash
./packet-path
```

Expected:

- Blue rounded rectangle labelled "PC-1" at screen centre
- Click and hold the node body → node follows mouse
- Release → node stays at new position
- Click empty canvas area → node deselects (no white ring)
- Releasing mouse anywhere causes no crash or position jump

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m1.1): single draggable DeviceNode with selection"
```

---

## Task 2: M1.2 — Multiple Nodes + Device Types + Selection

**Files:**

- Modify: `src/main.cpp`

- [ ] **Step 1: Replace the data section and main() with multi-node support**

Replace `src/main.cpp` with the following (keep all the top-of-file constants, enum, struct, and helper functions from Task 1 unchanged — only `main()` changes):

```cpp
#include "raylib.h"
#include <string>
#include <vector>

// ── Constants ─────────────────────────────────────────────────────────────
static const int   SCREEN_W = 1280;
static const int   SCREEN_H = 720;
static const float NODE_W   = 120.0f;
static const float NODE_H   =  60.0f;
static const Color BG_COLOR = {15, 23, 42, 255};

// ── Device types & node struct ─────────────────────────────────────────────
enum DeviceType { PC, ROUTER, SWITCH };

struct DeviceNode {
    int         id;
    DeviceType  type;
    Vector2     position;
    std::string label;
    bool        selected = false;
};

// ── Helpers (unchanged from Task 1) ───────────────────────────────────────
Color GetDeviceColor(DeviceType t) {
    switch (t) {
        case PC:     return {59,  130, 246, 255};
        case ROUTER: return {249, 115,  22, 255};
        case SWITCH: return {34,  197,  94, 255};
    }
    return WHITE;
}

Rectangle GetNodeRect(const DeviceNode& n) {
    return {n.position.x - NODE_W / 2.0f,
            n.position.y - NODE_H / 2.0f,
            NODE_W, NODE_H};
}

void DrawDeviceNode(const DeviceNode& n) {
    Rectangle r = GetNodeRect(n);
    Color     c = GetDeviceColor(n.type);
    DrawRectangleRounded({r.x + 3, r.y + 3, r.width, r.height}, 0.3f, 8,
                         Color{0, 0, 0, 80});
    DrawRectangleRounded(r, 0.3f, 8, c);
    if (n.selected)
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, 2.5f, WHITE);
    int fsz = 14;
    int tw  = MeasureText(n.label.c_str(), fsz);
    DrawText(n.label.c_str(),
             (int)(n.position.x - tw / 2.0f),
             (int)(n.position.y - fsz / 2.0f),
             fsz, WHITE);
}

// ── Spawn helper ──────────────────────────────────────────────────────────
static int nextId = 1;

DeviceNode SpawnNode(DeviceType type) {
    static const char* names[] = {"PC", "RTR", "SW"};
    DeviceNode n;
    n.id       = nextId++;
    n.type     = type;
    n.position = {SCREEN_W / 2.0f + (float)(GetRandomValue(-40, 40)),
                  SCREEN_H / 2.0f + (float)(GetRandomValue(-40, 40))};
    n.label    = std::string(names[(int)type]) + "-" + std::to_string(n.id);
    return n;
}

// ── Main ──────────────────────────────────────────────────────────────────
int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Packet Path");
    SetTargetFPS(60);

    std::vector<DeviceNode> nodes;
    nodes.push_back(SpawnNode(PC));  // start with one PC

    int     selectedId  = -1;
    bool    dragging    = false;
    Vector2 dragOffset  = {0, 0};

    while (!WindowShouldClose()) {
        // ── Input ──────────────────────────────────────────────────────
        Vector2 mouse = GetMousePosition();

        // Spawn keys
        if (IsKeyPressed(KEY_P)) nodes.push_back(SpawnNode(PC));
        if (IsKeyPressed(KEY_R)) nodes.push_back(SpawnNode(ROUTER));
        if (IsKeyPressed(KEY_S)) nodes.push_back(SpawnNode(SWITCH));

        // Delete selected
        if (IsKeyPressed(KEY_DELETE) && selectedId != -1) {
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const DeviceNode& n){ return n.id == selectedId; }),
                nodes.end());
            selectedId = -1;
        }

        // LMB: select + start drag (iterate back-to-front for correct z-order)
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int hitId = -1;
            for (int i = (int)nodes.size() - 1; i >= 0; --i) {
                if (CheckCollisionPointRec(mouse, GetNodeRect(nodes[i]))) {
                    hitId = nodes[i].id;
                    break;
                }
            }
            // update selection state on all nodes
            for (auto& n : nodes) n.selected = (n.id == hitId);
            selectedId = hitId;

            if (selectedId != -1) {
                dragging = true;
                for (auto& n : nodes) {
                    if (n.id == selectedId) {
                        dragOffset = {mouse.x - n.position.x,
                                      mouse.y - n.position.y};
                        break;
                    }
                }
            }
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragging) {
            for (auto& n : nodes) {
                if (n.id == selectedId) {
                    n.position = {mouse.x - dragOffset.x,
                                  mouse.y - dragOffset.y};
                    break;
                }
            }
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            dragging = false;

        // ── Draw ───────────────────────────────────────────────────────
        BeginDrawing();
            ClearBackground(BG_COLOR);
            for (const auto& n : nodes) DrawDeviceNode(n);
            DrawFPS(10, 10);
            // hint text
            DrawText("P=PC  R=Router  S=Switch  Del=Delete selected",
                     10, SCREEN_H - 24, 12, Color{100, 116, 139, 255});
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
```

- [ ] **Step 2: Build**

```bash
make
```

Expected: zero warnings.

- [ ] **Step 3: Run and verify acceptance criteria**

```bash
./packet-path
```

Expected:

- One blue PC node at start
- Press `P` → new blue PC spawns near centre
- Press `R` → orange Router spawns near centre
- Press `S` → green Switch spawns near centre
- Click any node → white ring appears, others deselect
- Click canvas background → all deselect
- Drag a node independently — others don't move
- Select a node, press Delete → it disappears, others intact
- Spawn 10+ nodes — smooth 60 FPS, no stutters
- Hint bar visible at bottom of screen

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m1.2): multiple nodes, device types, P/R/S spawn, Delete key"
```

---

## Task 3: M1.3 — Camera 2D — Pan & Zoom

**Files:**

- Modify: `src/main.cpp`

- [ ] **Step 1: Add dot-grid draw function above `main()`**

Add this function after `DrawDeviceNode()`:

```cpp
void DrawDotGrid(Camera2D cam) {
    float spacing = 40.0f;
    Color dot     = {30, 41, 59, 255};

    // visible world rect
    Vector2 topLeft  = GetScreenToWorld2D({0, 0}, cam);
    Vector2 botRight = GetScreenToWorld2D({(float)SCREEN_W, (float)SCREEN_H}, cam);

    float startX = floorf(topLeft.x / spacing) * spacing;
    float startY = floorf(topLeft.y / spacing) * spacing;

    for (float x = startX; x <= botRight.x; x += spacing)
        for (float y = startY; y <= botRight.y; y += spacing)
            DrawCircleV({x, y}, 1.5f / cam.zoom, dot);
}
```

Also add `#include <cmath>` at the top.

- [ ] **Step 2: Add Camera2D and update main() input + draw**

Replace `main()` with the following (keep all helpers above it untouched):

```cpp
int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Packet Path");
    SetTargetFPS(60);

    std::vector<DeviceNode> nodes;
    nodes.push_back(SpawnNode(PC));

    Camera2D camera   = {};
    camera.offset     = {SCREEN_W / 2.0f, SCREEN_H / 2.0f};
    camera.target     = {0, 0};
    camera.zoom       = 1.0f;

    int     selectedId = -1;
    bool    dragging   = false;
    Vector2 dragOffset = {0, 0};

    while (!WindowShouldClose()) {
        Vector2 screenMouse = GetMousePosition();
        Vector2 worldMouse  = GetScreenToWorld2D(screenMouse, camera);

        // ── Spawn / delete ─────────────────────────────────────────────
        if (IsKeyPressed(KEY_P)) nodes.push_back(SpawnNode(PC));
        if (IsKeyPressed(KEY_R)) nodes.push_back(SpawnNode(ROUTER));
        if (IsKeyPressed(KEY_S)) nodes.push_back(SpawnNode(SWITCH));

        if (IsKeyPressed(KEY_DELETE) && selectedId != -1) {
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const DeviceNode& n){ return n.id == selectedId; }),
                nodes.end());
            selectedId = -1;
        }

        // ── Camera pan (middle mouse) ──────────────────────────────────
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            Vector2 delta = GetMouseDelta();
            camera.target.x -= delta.x / camera.zoom;
            camera.target.y -= delta.y / camera.zoom;
        }

        // ── Camera zoom (scroll wheel, anchor on cursor) ───────────────
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            Vector2 beforeZoom = GetScreenToWorld2D(screenMouse, camera);
            camera.zoom *= (1.0f + wheel * 0.1f);
            camera.zoom  = Clamp(camera.zoom, 0.15f, 4.0f);
            Vector2 afterZoom  = GetScreenToWorld2D(screenMouse, camera);
            camera.target.x   += beforeZoom.x - afterZoom.x;
            camera.target.y   += beforeZoom.y - afterZoom.y;
        }

        // ── Node select + drag (world-space mouse) ─────────────────────
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int hitId = -1;
            for (int i = (int)nodes.size() - 1; i >= 0; --i) {
                if (CheckCollisionPointRec(worldMouse, GetNodeRect(nodes[i]))) {
                    hitId = nodes[i].id;
                    break;
                }
            }
            for (auto& n : nodes) n.selected = (n.id == hitId);
            selectedId = hitId;

            if (selectedId != -1) {
                dragging = true;
                for (auto& n : nodes) {
                    if (n.id == selectedId) {
                        dragOffset = {worldMouse.x - n.position.x,
                                      worldMouse.y - n.position.y};
                        break;
                    }
                }
            }
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragging) {
            for (auto& n : nodes) {
                if (n.id == selectedId) {
                    n.position = {worldMouse.x - dragOffset.x,
                                  worldMouse.y - dragOffset.y};
                    break;
                }
            }
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            dragging = false;

        // ── Draw ───────────────────────────────────────────────────────
        BeginDrawing();
            ClearBackground(BG_COLOR);

            BeginMode2D(camera);
                DrawDotGrid(camera);
                for (const auto& n : nodes) DrawDeviceNode(n);
            EndMode2D();

            // HUD (screen-space, outside camera)
            DrawFPS(10, 10);
            DrawText("P=PC  R=Router  S=Switch  Del=Delete  MMB=Pan  Scroll=Zoom",
                     10, SCREEN_H - 24, 12, Color{100, 116, 139, 255});
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
```

- [ ] **Step 3: Build**

```bash
make
```

Expected: zero warnings.

- [ ] **Step 4: Run and verify acceptance criteria**

```bash
./packet-path
```

Expected:

- Dot grid visible across entire canvas
- Hold middle mouse button + drag → canvas pans smoothly
- Scroll wheel up/down → zooms in/out, anchored on cursor position (nodes don't jump)
- After zooming, click and drag nodes — hit detection still correct at any zoom level
- Zoom in far (4×) and out far (0.15×) — no inversion, no crash
- Dot grid spacing stays consistent (dots scale with zoom)
- Spawn + Delete still work correctly

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m1.3): Camera2D pan/zoom with cursor-anchored scroll and dot grid"
```

---

## Task 4: M1.4 — Cable Draw + Port Snap

**Files:**

- Modify: `src/main.cpp`

- [ ] **Step 1: Add port constants and helper above `DrawDeviceNode()`**

Add after the constants block:

```cpp
static const float PORT_RADIUS  = 6.0f;
static const int   PORTS_PER_NODE = 4;  // top=0, right=1, bottom=2, left=3

Vector2 GetPortPosition(const DeviceNode& n, int port) {
    float hw = NODE_W / 2.0f, hh = NODE_H / 2.0f;
    switch (port) {
        case 0: return {n.position.x,       n.position.y - hh};  // top
        case 1: return {n.position.x + hw,  n.position.y      };  // right
        case 2: return {n.position.x,       n.position.y + hh};  // bottom
        case 3: return {n.position.x - hw,  n.position.y      };  // left
    }
    return n.position;
}
```

- [ ] **Step 2: Add Cable struct and DrawAllCables() above `main()`**

Add after `GetPortPosition()`:

```cpp
struct Cable {
    int fromId, fromPort;
    int toId,   toPort;
};

// Returns pointer to node with given id, or nullptr
DeviceNode* FindNode(std::vector<DeviceNode>& nodes, int id) {
    for (auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

void DrawAllCables(const std::vector<Cable>& cables,
                   std::vector<DeviceNode>& nodes)
{
    for (const auto& c : cables) {
        DeviceNode* from = FindNode(nodes, c.fromId);
        DeviceNode* to   = FindNode(nodes, c.toId);
        if (!from || !to) continue;

        Vector2 p0 = GetPortPosition(*from, c.fromPort);
        Vector2 p3 = GetPortPosition(*to,   c.toPort);

        // control points: offset perpendicular to the port exit direction
        float offset = 60.0f;
        auto ctrl = [&](Vector2 p, int port) -> Vector2 {
            switch (port) {
                case 0: return {p.x,           p.y - offset};
                case 1: return {p.x + offset,  p.y         };
                case 2: return {p.x,           p.y + offset};
                case 3: return {p.x - offset,  p.y         };
            }
            return p;
        };

        DrawLineBezierCubic(p0, p3, ctrl(p0, c.fromPort), ctrl(p3, c.toPort),
                            2.0f, Color{148, 163, 184, 255});
    }
}
```

- [ ] **Step 3: Add port drawing to `DrawDeviceNode()`**

Inside `DrawDeviceNode()`, add at the very end of the function (after the label draw):

```cpp
    // draw ports
    for (int i = 0; i < PORTS_PER_NODE; ++i) {
        Vector2 pos = GetPortPosition(n, i);
        DrawCircleV(pos, PORT_RADIUS, Color{51, 65, 85, 255});
        DrawCircleV(pos, PORT_RADIUS - 2.0f, Color{100, 116, 139, 255});
    }
```

- [ ] **Step 4: Add connection state and update main()**

Add these variables to the top of `main()` (alongside `selectedId`, `dragging`, etc.):

```cpp
    std::vector<Cable> cables;

    // cable connection state
    bool    connecting     = false;
    int     connectFromId  = -1;
    int     connectFromPort= -1;
    int     hoverNodeId    = -1;
    int     hoverPort      = -1;
```

- [ ] **Step 5: Add port hit detection helper above `main()`**

```cpp
// Returns true and sets outNode/outPort if mouse is within PORT_RADIUS of any port
bool HitTestPort(const std::vector<DeviceNode>& nodes, Vector2 worldMouse,
                 int excludeId, int& outNode, int& outPort)
{
    for (int i = (int)nodes.size() - 1; i >= 0; --i) {
        if (nodes[i].id == excludeId) continue;
        for (int p = 0; p < PORTS_PER_NODE; ++p) {
            Vector2 pp = GetPortPosition(nodes[i], p);
            if (CheckCollisionPointCircle(worldMouse, pp, PORT_RADIUS * 1.5f)) {
                outNode = nodes[i].id;
                outPort = p;
                return true;
            }
        }
    }
    return false;
}
```

- [ ] **Step 6: Replace the LMB input block in `main()` with port-aware version**

Replace the entire `if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))` block and drag blocks with:

```cpp
        // ── LMB pressed ───────────────────────────────────────────────
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            // Check port hit first
            int pNode = -1, pPort = -1;
            if (HitTestPort(nodes, worldMouse, -1, pNode, pPort)) {
                // start cable connection
                connecting      = true;
                connectFromId   = pNode;
                connectFromPort = pPort;
                dragging        = false;
            } else {
                // check node body hit
                int hitId = -1;
                for (int i = (int)nodes.size() - 1; i >= 0; --i) {
                    if (CheckCollisionPointRec(worldMouse, GetNodeRect(nodes[i]))) {
                        hitId = nodes[i].id;
                        break;
                    }
                }
                for (auto& n : nodes) n.selected = (n.id == hitId);
                selectedId = hitId;

                if (selectedId != -1) {
                    dragging = true;
                    for (auto& n : nodes) {
                        if (n.id == selectedId) {
                            dragOffset = {worldMouse.x - n.position.x,
                                          worldMouse.y - n.position.y};
                            break;
                        }
                    }
                }
            }
        }

        // ── LMB held ──────────────────────────────────────────────────
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            if (dragging) {
                for (auto& n : nodes) {
                    if (n.id == selectedId) {
                        n.position = {worldMouse.x - dragOffset.x,
                                      worldMouse.y - dragOffset.y};
                        break;
                    }
                }
            }
            if (connecting) {
                // update hover target
                hoverNodeId = -1; hoverPort = -1;
                HitTestPort(nodes, worldMouse, connectFromId, hoverNodeId, hoverPort);
            }
        }

        // ── LMB released ──────────────────────────────────────────────
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (connecting && hoverNodeId != -1) {
                // commit cable (avoid duplicates on same port pair)
                bool exists = false;
                for (const auto& c : cables) {
                    if ((c.fromId == connectFromId && c.fromPort == connectFromPort &&
                         c.toId   == hoverNodeId   && c.toPort   == hoverPort) ||
                        (c.fromId == hoverNodeId   && c.fromPort == hoverPort &&
                         c.toId   == connectFromId && c.toPort   == connectFromPort))
                    { exists = true; break; }
                }
                if (!exists)
                    cables.push_back({connectFromId, connectFromPort,
                                      hoverNodeId,   hoverPort});
            }
            connecting  = false;
            dragging    = false;
            hoverNodeId = -1;
        }
```

- [ ] **Step 7: Update the draw block to render cables and live connection line**

Inside `BeginMode2D(camera)` / `EndMode2D()`, update to:

```cpp
            BeginMode2D(camera);
                DrawDotGrid(camera);
                DrawAllCables(cables, nodes);

                // live connection line while dragging from a port
                if (connecting) {
                    DeviceNode* fromNode = FindNode(nodes, connectFromId);
                    if (fromNode) {
                        Vector2 p0 = GetPortPosition(*fromNode, connectFromPort);
                        DrawLineEx(p0, worldMouse, 2.0f, Color{148, 163, 184, 180});
                        DrawCircleV(worldMouse, 4.0f, WHITE);
                    }
                }

                // highlight hover port green
                if (hoverNodeId != -1) {
                    DeviceNode* hNode = FindNode(nodes, hoverNodeId);
                    if (hNode) {
                        Vector2 pp = GetPortPosition(*hNode, hoverPort);
                        DrawCircleV(pp, PORT_RADIUS + 3.0f, Color{34, 197, 94, 200});
                    }
                }

                for (const auto& n : nodes) DrawDeviceNode(n);
            EndMode2D();
```

- [ ] **Step 8: Build**

```bash
make
```

Expected: zero warnings.

- [ ] **Step 9: Run and verify acceptance criteria**

```bash
./packet-path
```

Expected:

- Small grey circles visible at all 4 port positions (top/right/bottom/left) on every node
- Click and drag from any port → a live white line follows the mouse
- Hovering over a valid target port on a different node → green circle highlight appears
- Release on highlighted port → permanent bezier cable drawn between the two ports
- Move either connected node → cable redraws correctly, following the nodes
- Cannot connect a port to another port on the same device
- Connecting the same port pair twice is silently ignored (no duplicate cables)
- Node dragging and camera pan/zoom still work correctly alongside cables

- [ ] **Step 10: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m1.4): cable system with port snap, bezier draw, live connection preview"
```

---

## Phase 1 Exit Commit

After all four milestones pass:

```bash
git push
```

Verify on GitHub: `PiMPStudios/packet-path` → all commits visible on `main`.

**What you have:** An infinite pan/zoom canvas with spawnable PC/Router/Switch nodes, independent drag, selection, deletion, and bezier cable wiring. All ~320 lines in `src/main.cpp`. Ready for Phase 2: Canvas Engine.

---

## Self-Review

**Spec coverage:**

- ✅ M0: Makefile + window (Task 0)
- ✅ M1.1: Single draggable DeviceNode (Task 1)
- ✅ M1.2: Multiple nodes + types + selection + spawn keys + delete (Task 2)
- ✅ M1.3: Camera2D pan/zoom with cursor anchor + dot grid (Task 3)
- ✅ M1.4: Cable system — port positions, connection FSM, bezier, live preview, hover highlight (Task 4)

**Placeholder scan:** No TBDs, TODOs, or vague steps. Every step has complete code or an exact command with expected output.

**Type consistency:**

- `DeviceNode` — consistent across all tasks
- `GetNodeRect()`, `GetDeviceColor()`, `DrawDeviceNode()` — defined Task 1, used consistently
- `GetPortPosition()` — defined Task 4 Step 1, used in Steps 2, 5, 6, 7
- `FindNode()` — defined Task 4 Step 2, used in Steps 6, 7
- `HitTestPort()` — defined Task 4 Step 5, used in Step 6
- `Cable` struct — defined Task 4 Step 2, used in Steps 4, 6, 7
- `cables` vector — declared Step 4, populated Step 6, drawn Step 7
