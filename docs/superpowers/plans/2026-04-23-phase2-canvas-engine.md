# Phase 2: Canvas Engine — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a fixed 280px right sidebar config panel with per-node hostname + management IP + per-port interface IP fields (with validation), a right-click context menu, and all the plumbing to make panel focus, keyboard input, and canvas interactions work correctly — all remaining in `src/main.cpp`.

**Architecture:** A 280px fixed sidebar divides the 1280px screen into a 1000px canvas (left) and 280px config panel (right). Camera offset shifts to canvas center {500, 360}. All panel UI is drawn in screen space after `EndMode2D`. Config fields (mgmtIp, portIp[4]) live directly in `DeviceNode`. A `PanelState` struct tracks which field has keyboard focus. A `ContextMenu` struct drives right-click menus. Canvas input is guarded so clicks in the panel never trigger canvas actions.

**Tech Stack:** C++17, raylib 5.5, macOS (same Makefile, single `src/main.cpp`)

---

## File Map

| File | Status | Responsibility |
| --- | --- | --- |
| `src/main.cpp` | Modify | Add PANEL_W/CANVAS_W constants; extend DeviceNode; add DrawTextField, ValidateIP, UpdateTextField, GetPortName, DrawPanel, DrawContextMenu, HitTestCable, ExecuteMenuAction; update camera offset, mouse guard, ESC handling, spawn/delete key gating |

---

## Codebase Starting Point

Phase 2 builds on the final Phase 1 + carry-forward `src/main.cpp` (~340 lines) on branch `phase-2-canvas-engine`. Key facts the implementer must know:

- `std::clamp` is used throughout — **never** `Clamp()` from raymath.h (triggers 53 warnings on this system)
- Cables use `DrawSplineSegmentBezierCubic(p0, c1, c2, p3, thick, color)` — **not** `DrawLineBezierCubic`
- `FindNode` returns `const DeviceNode*` — do not change this signature
- SpawnNode signature: `DeviceNode SpawnNode(DeviceType type, Vector2 worldPos)`
- `DeviceNode` uses in-class defaults; all fields zero-initialised unless otherwise set

---

## Task 1: M2.1 — Fixed Right Sidebar (Visual Shell)

**Files:**

- Modify: `src/main.cpp`

Add the panel background, border, header, and placeholder. No text fields yet.
Camera offset shifts to canvas center. Mouse input is guarded to the left 1000px.

- [ ] **Step 1: Add four panel constants after the existing constants block**

Current constants block ends at:

```cpp
static const Color BG_COLOR     = {15, 23, 42, 255};
```

Add immediately after:

```cpp
static const int   PANEL_W      = 280;
static const int   CANVAS_W     = SCREEN_W - PANEL_W;   // 1000
static const Color PANEL_BG     = {22, 33, 62, 255};
static const Color PANEL_BORDER = {51, 65, 85, 255};
```

- [ ] **Step 2: Fix DrawDotGrid — canvas width, not screen width**

In `DrawDotGrid`, find this line:

```cpp
    Vector2 botRight = GetScreenToWorld2D({(float)SCREEN_W, (float)SCREEN_H}, cam);
```

Replace with:

```cpp
    Vector2 botRight = GetScreenToWorld2D({(float)CANVAS_W, (float)SCREEN_H}, cam);
```

- [ ] **Step 3: Add DrawPanel function above `main()`**

Add this complete function after `DrawAllCables()`:

```cpp
void DrawPanel(int selectedId, const std::vector<DeviceNode>& nodes) {
    // Background + left border
    DrawRectangle(CANVAS_W, 0, PANEL_W, SCREEN_H, PANEL_BG);
    DrawLineEx({(float)CANVAS_W, 0.0f}, {(float)CANVAS_W, (float)SCREEN_H},
               1.0f, PANEL_BORDER);

    // Panel header
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

    // Device type badge + label
    const char* typeNames[] = {"PC", "Router", "Switch"};
    Color typeColors[]      = {{59, 130, 246, 255}, {249, 115, 22, 255}, {34, 197, 94, 255}};
    int bw = MeasureText(typeNames[(int)n->type], 11) + 16;
    DrawRectangleRounded({(float)(CANVAS_W + 12), 50.0f, (float)bw, 22.0f},
                         0.5f, 4, typeColors[(int)n->type]);
    DrawText(typeNames[(int)n->type], CANVAS_W + 20, 56, 11, WHITE);
    DrawText(n->label.c_str(), CANVAS_W + 16 + bw, 56, 13, WHITE);

    DrawLineEx({(float)CANVAS_W, 84.0f}, {(float)(CANVAS_W + PANEL_W), 84.0f},
               1.0f, PANEL_BORDER);
    DrawText("GENERAL", CANVAS_W + 12, 94, 10, Color{100, 116, 139, 255});
}
```

- [ ] **Step 4: Shift camera offset to canvas centre in `main()`**

Find the camera initialization block in `main()`:

```cpp
    camera.offset   = {SCREEN_W / 2.0f, SCREEN_H / 2.0f};
```

Replace with:

```cpp
    camera.offset   = {CANVAS_W / 2.0f, SCREEN_H / 2.0f};
```

- [ ] **Step 5: Add canvas mouse guard in main() — gate all canvas input**

In `main()`, find:

```cpp
        Vector2 screenMouse = GetMousePosition();
        Vector2 worldMouse  = GetScreenToWorld2D(screenMouse, camera);
```

Add immediately after:

```cpp
        bool inCanvas = (screenMouse.x < (float)CANVAS_W);
```

Then wrap every canvas interaction that currently starts with `if (IsKey...` or `if (IsMouseButton...` (spawn keys, ESC, DELETE, camera pan, camera zoom, LMB pressed, LMB held, LMB released) so they only fire when in canvas. Specifically:

Replace:

```cpp
        // ── Spawn / delete / cancel ────────────────────────────────────
        if (IsKeyPressed(KEY_P)) nodes.push_back(SpawnNode(PC,     worldMouse));
        if (IsKeyPressed(KEY_R)) nodes.push_back(SpawnNode(ROUTER, worldMouse));
        if (IsKeyPressed(KEY_S)) nodes.push_back(SpawnNode(SWITCH, worldMouse));

        if (IsKeyPressed(KEY_ESCAPE) && connecting) {
```

With:

```cpp
        // ── Spawn / delete / cancel (canvas only) ─────────────────────
        if (inCanvas) {
            if (IsKeyPressed(KEY_P)) nodes.push_back(SpawnNode(PC,     worldMouse));
            if (IsKeyPressed(KEY_R)) nodes.push_back(SpawnNode(ROUTER, worldMouse));
            if (IsKeyPressed(KEY_S)) nodes.push_back(SpawnNode(SWITCH, worldMouse));
        }

        if (IsKeyPressed(KEY_ESCAPE) && connecting) {
```

Replace the camera pan guard:

```cpp
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
```

With:

```cpp
        if (inCanvas && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
```

Replace the zoom guard:

```cpp
        float wheel = std::clamp(GetMouseWheelMove(), -3.0f, 3.0f);
        if (wheel != 0.0f) {
```

With:

```cpp
        float wheel = inCanvas ? std::clamp(GetMouseWheelMove(), -3.0f, 3.0f) : 0.0f;
        if (wheel != 0.0f) {
```

Replace the LMB pressed guard:

```cpp
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
```

With:

```cpp
        if (inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
```

Replace the LMB held guard (both the drag and connecting blocks inside):

```cpp
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
```

With:

```cpp
        if (inCanvas && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
```

Replace the LMB released guard:

```cpp
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
```

With:

```cpp
        if (inCanvas && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
```

The DELETE key guard stays ungated by inCanvas (keyboard shortcut works from anywhere):

```cpp
        if (IsKeyPressed(KEY_DELETE) && selectedId != -1) {
```

This stays as-is.

- [ ] **Step 6: Call DrawPanel in the draw block**

In the draw block, find the HUD line drawn after `EndMode2D()`:

```cpp
            DrawFPS(SCREEN_W - 80, 10);
            DrawText("P=PC  R=Router ...",
```

Update to call DrawPanel before the FPS counter and narrow the hint bar to canvas width:

```cpp
            DrawPanel(selectedId, nodes);
            DrawFPS(SCREEN_W - 80, 10);
            DrawText("P=PC  R=Router  S=Switch  Del=Delete  MMB=Pan  Scroll=Zoom  Drag-port=Cable  Esc=Cancel",
                     10, SCREEN_H - 24, 12, Color{100, 116, 139, 255});
```

- [ ] **Step 7: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make
```

Expected: zero warnings, binary produced.

- [ ] **Step 8: Run and verify acceptance criteria**

```bash
./packet-path
```

Expected:

- Left 1000px is the canvas with dot grid; right 280px is a dark navy panel with subtle left border
- Panel header reads "CONFIGURATION" in grey at the top
- When no node selected: panel centre shows "<- Select a device" in muted grey
- When a node is selected: panel shows a coloured type badge ("PC"/"Router"/"Switch") and the node label beside it, plus "GENERAL" section label below a divider
- Clicking anywhere in the right panel does NOT trigger canvas interactions (no spawning, no wiring, no selection changes)
- Camera zoom and pan still work in the canvas area
- All Phase 1 features (spawn, drag, delete, cable) still work correctly

- [ ] **Step 9: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m2.1): fixed right sidebar panel shell with canvas mouse guard"
```

---

## Task 2: M2.2 — IP Address Input + Text Field System

**Files:**

- Modify: `src/main.cpp`

Add `mgmtIp` to `DeviceNode`. Implement `ValidateIP`, `UpdateTextField`, `DrawTextField`, `PanelState`. Wire up panel click-to-focus, typing, backspace, blinking cursor, and validation colour feedback.

- [ ] **Step 1: Add `#include <cstdio>` at the top of the file**

After the existing includes block:

```cpp
#include "raylib.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
```

Add:

```cpp
#include <cstdio>    // sscanf
```

- [ ] **Step 2: Extend DeviceNode with `mgmtIp`**

Find the `DeviceNode` struct. It currently ends with:

```cpp
    bool        selected = false;
};
```

Replace with:

```cpp
    bool        selected = false;
    std::string mgmtIp;     // "x.x.x.x/xx", empty = unconfigured
};
```

- [ ] **Step 3: Add PanelState struct above `main()`**

Add immediately before `static int nextId = 1;`:

```cpp
// ── Panel UI state ────────────────────────────────────────────────────────
struct PanelState {
    int activeField = -1;
    // -1=none  0=label(hostname)  1=mgmtIp
};

// Panel field layout helpers (screen space)
Rectangle PnlFieldRect(int yOffset) {
    return {(float)(CANVAS_W + 12), (float)yOffset, (float)(PANEL_W - 24), 26.0f};
}
```

- [ ] **Step 4: Add `ValidateIP` function above `main()`**

Add after the `PanelState` block:

```cpp
bool ValidateIP(const std::string& ip) {
    int a, b, c, d, prefix;
    return (std::sscanf(ip.c_str(), "%d.%d.%d.%d/%d", &a, &b, &c, &d, &prefix) == 5 &&
            a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
            c >= 0 && c <= 255 && d >= 0 && d <= 255 &&
            prefix >= 0 && prefix <= 32);
}
```

- [ ] **Step 5: Add `UpdateTextField` function above `main()`**

```cpp
void UpdateTextField(std::string& text, int maxLen) {
    int ch;
    while ((ch = GetCharPressed()) > 0)
        if ((int)text.size() < maxLen && ch >= 32 && ch < 127)
            text += (char)ch;
    if (IsKeyPressed(KEY_BACKSPACE) && !text.empty())
        text.pop_back();
}
```

- [ ] **Step 6: Add `DrawTextField` function above `main()`**

```cpp
void DrawTextField(Rectangle r, const char* topLabel, const char* placeholder,
                   const std::string& value, bool active, bool valid)
{
    if (topLabel && *topLabel)
        DrawText(topLabel, (int)r.x, (int)r.y - 16, 11, Color{100, 116, 139, 255});
    DrawRectangleRec(r, Color{15, 23, 42, 255});

    Color border;
    if (active)             border = WHITE;
    else if (value.empty()) border = Color{51, 65, 85, 255};
    else if (valid)         border = Color{34, 197, 94, 255};
    else                    border = Color{239, 68, 68, 255};
    DrawRectangleLinesEx(r, 1.5f, border);

    const int tx   = (int)r.x + 6;
    const int ty   = (int)r.y + (int)(r.height / 2) - 6;
    const int maxW = (int)r.width - 12;

    if (value.empty()) {
        if (placeholder && *placeholder && !active)
            DrawText(placeholder, tx, ty, 12, Color{51, 65, 85, 255});
        if (active && (int)(GetTime() * 2) % 2 == 0)
            DrawRectangle(tx, (int)r.y + 4, 2, (int)r.height - 8, WHITE);
    } else {
        int start = 0;
        while (start < (int)value.size() &&
               MeasureText(value.c_str() + start, 12) > maxW)
            ++start;
        DrawText(value.c_str() + start, tx, ty, 12, WHITE);
        if (active && (int)(GetTime() * 2) % 2 == 0) {
            int curX = tx + MeasureText(value.c_str() + start, 12);
            DrawRectangle(curX, (int)r.y + 4, 2, (int)r.height - 8, WHITE);
        }
    }
}
```

- [ ] **Step 7: Update `DrawPanel` to accept `PanelState` and draw text fields**

Replace the existing `DrawPanel` function signature and body with:

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

    const char* typeNames[] = {"PC", "Router", "Switch"};
    Color typeColors[]      = {{59,130,246,255},{249,115,22,255},{34,197,94,255}};
    int bw = MeasureText(typeNames[(int)n->type], 11) + 16;
    DrawRectangleRounded({(float)(CANVAS_W + 12), 50.0f, (float)bw, 22.0f},
                         0.5f, 4, typeColors[(int)n->type]);
    DrawText(typeNames[(int)n->type], CANVAS_W + 20, 56, 11, WHITE);
    DrawText(n->label.c_str(), CANVAS_W + 16 + bw, 56, 13, WHITE);

    DrawLineEx({(float)CANVAS_W, 84.0f}, {(float)(CANVAS_W + PANEL_W), 84.0f},
               1.0f, PANEL_BORDER);
    DrawText("GENERAL", CANVAS_W + 12, 94, 10, Color{100, 116, 139, 255});

    // Hostname (edits n->label)
    DrawTextField(PnlFieldRect(126), "Hostname", nullptr,
                  n->label, ps.activeField == 0, true);

    // Mgmt IP
    DrawTextField(PnlFieldRect(178), "Mgmt IP", "x.x.x.x/xx",
                  n->mgmtIp, ps.activeField == 1, ValidateIP(n->mgmtIp));
}
```

- [ ] **Step 8: Declare PanelState and prevSelectedId in `main()` vars block**

In `main()`, after the existing variable declarations (selectedId, dragging, etc.), add:

```cpp
    PanelState ps;
    int        prevSelectedId = -2;  // -2 forces a reset on first frame
```

- [ ] **Step 9: Add panel click-to-focus handling after the canvas LMB released block**

After the `// ── LMB released` block in main(), add a new block:

```cpp
        // ── Panel click-to-focus ───────────────────────────────────────
        if (!inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            ps.activeField = -1;
            if (selectedId != -1) {
                if (CheckCollisionPointRec(screenMouse, PnlFieldRect(126))) ps.activeField = 0;
                if (CheckCollisionPointRec(screenMouse, PnlFieldRect(178))) ps.activeField = 1;
            }
        }
```

- [ ] **Step 10: Add field update + char flush block**

Immediately after the panel click-to-focus block, add:

```cpp
        // ── Text field update ──────────────────────────────────────────
        if (ps.activeField != -1 && selectedId != -1) {
            DeviceNode* selNode = nullptr;
            for (auto& nd : nodes)
                if (nd.id == selectedId) { selNode = &nd; break; }
            if (selNode) {
                if (ps.activeField == 0) UpdateTextField(selNode->label,   32);
                if (ps.activeField == 1) UpdateTextField(selNode->mgmtIp, 18);
            }
        } else {
            while (GetCharPressed() > 0) {}  // flush char queue when no field active
        }

        // Reset active field when selection changes
        if (selectedId != prevSelectedId) {
            ps.activeField    = -1;
            prevSelectedId    = selectedId;
        }
```

- [ ] **Step 11: Gate spawn/delete keyboard shortcuts on no active field**

Find the spawn keys block (now inside `if (inCanvas)`):

```cpp
        if (inCanvas) {
            if (IsKeyPressed(KEY_P)) nodes.push_back(SpawnNode(PC,     worldMouse));
            if (IsKeyPressed(KEY_R)) nodes.push_back(SpawnNode(ROUTER, worldMouse));
            if (IsKeyPressed(KEY_S)) nodes.push_back(SpawnNode(SWITCH, worldMouse));
        }
```

Replace with:

```cpp
        if (inCanvas && ps.activeField == -1) {
            if (IsKeyPressed(KEY_P)) nodes.push_back(SpawnNode(PC,     worldMouse));
            if (IsKeyPressed(KEY_R)) nodes.push_back(SpawnNode(ROUTER, worldMouse));
            if (IsKeyPressed(KEY_S)) nodes.push_back(SpawnNode(SWITCH, worldMouse));
        }
```

Find the DELETE key block:

```cpp
        if (IsKeyPressed(KEY_DELETE) && selectedId != -1) {
```

Replace with:

```cpp
        if (ps.activeField == -1 && IsKeyPressed(KEY_DELETE) && selectedId != -1) {
```

- [ ] **Step 12: Update ESC key handling — field takes priority over wire cancel**

Find the current ESC block:

```cpp
        if (IsKeyPressed(KEY_ESCAPE) && connecting) {
            connecting  = false;
            hoverNodeId = -1;
            hoverPort   = -1;
        }
```

Replace with:

```cpp
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (ps.activeField != -1) {
                ps.activeField = -1;
            } else if (connecting) {
                connecting  = false;
                hoverNodeId = -1;
                hoverPort   = -1;
            }
        }
```

- [ ] **Step 13: Update DrawPanel call to pass PanelState**

Find the DrawPanel call in the draw block:

```cpp
            DrawPanel(selectedId, nodes);
```

Replace with:

```cpp
            DrawPanel(selectedId, nodes, ps);
```

- [ ] **Step 14: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make
```

Expected: zero warnings.

- [ ] **Step 15: Run and verify acceptance criteria**

```bash
./packet-path
```

Expected:

- Select a PC or Router → panel shows "Hostname" and "Mgmt IP" text fields below the GENERAL section label
- Click the Hostname field → white border appears, blinking cursor visible
- Type characters → they appear in the field AND update the node label on the canvas in real time
- Backspace → removes last character
- Click Mgmt IP field → it activates; Hostname field deactivates
- Type "192.168.1.1/24" → border stays red while typing partial value; turns green when valid CIDR entered
- Type "999.999.999.999/99" → border stays red (invalid IP)
- Type empty string → border goes to muted grey (unconfigured state)
- While a field is active, pressing P/R/S does NOT spawn devices — characters go to the field instead
- While a field is active, ESC deactivates the field (first press), then second ESC cancels wire if connecting
- Select a different node → activeField resets to -1
- All Phase 1 + M2.1 features still work

- [ ] **Step 16: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m2.2): IP address input fields with validation, PanelState focus management"
```

---

## Task 3: M2.3 — Per-Port Interface Config

**Files:**

- Modify: `src/main.cpp`

Add `portIp[4]` to DeviceNode and show four interface rows in the panel (one per port), each with a read-only port name and an editable IP field with the same validation as mgmtIp.

- [ ] **Step 1: Extend DeviceNode with `portIp[4]`**

Find the DeviceNode struct. It currently ends with:

```cpp
    std::string mgmtIp;     // "x.x.x.x/xx", empty = unconfigured
};
```

Replace with:

```cpp
    std::string mgmtIp;        // "x.x.x.x/xx", empty = unconfigured
    std::string portIp[4];     // per-port IPs, same format
};
```

- [ ] **Step 2: Add `PnlPortFieldRect` helper and update `PanelState` comment**

Find the `PnlFieldRect` helper:

```cpp
Rectangle PnlFieldRect(int yOffset) {
    return {(float)(CANVAS_W + 12), (float)yOffset, (float)(PANEL_W - 24), 26.0f};
}
```

Add immediately after it:

```cpp
Rectangle PnlPortFieldRect(int port) {
    return {(float)(CANVAS_W + 80), 240.0f + port * 44, (float)(PANEL_W - 92), 24.0f};
}
```

Also update the PanelState comment (for clarity — no code change needed):

```cpp
    // -1=none  0=label(hostname)  1=mgmtIp  2-5=portIp[0-3]
```

- [ ] **Step 3: Add `GetPortName` function above `main()`**

Add after `ValidateIP`:

```cpp
std::string GetPortName(DeviceType type, int port) {
    if (type == PC) {
        const char* names[] = {"eth0", "eth1", "eth2", "eth3"};
        return names[port];
    }
    // Router and Switch
    return "Gi0/" + std::to_string(port);
}
```

- [ ] **Step 4: Add the Interfaces section to `DrawPanel`**

In `DrawPanel`, after the mgmtIp `DrawTextField` call, add:

```cpp
    DrawLineEx({(float)CANVAS_W, 214.0f}, {(float)(CANVAS_W + PANEL_W), 214.0f},
               1.0f, PANEL_BORDER);
    DrawText("INTERFACES", CANVAS_W + 12, 224, 10, Color{100, 116, 139, 255});

    for (int i = 0; i < PORTS_PER_NODE; ++i) {
        std::string pname = GetPortName(n->type, i);
        int ry = 240 + i * 44;
        DrawText(pname.c_str(), CANVAS_W + 16, ry + 5, 11, Color{148, 163, 184, 255});
        DrawTextField(PnlPortFieldRect(i), "", "x.x.x.x/xx",
                      n->portIp[i], ps.activeField == 2 + i,
                      ValidateIP(n->portIp[i]));
    }
```

- [ ] **Step 5: Add port field click-to-focus**

In the `// ── Panel click-to-focus` block (added in Task 2), after the existing mgmtIp check:

```cpp
                if (CheckCollisionPointRec(screenMouse, PnlFieldRect(178))) ps.activeField = 1;
```

Add:

```cpp
                for (int i = 0; i < PORTS_PER_NODE; ++i)
                    if (CheckCollisionPointRec(screenMouse, PnlPortFieldRect(i)))
                        ps.activeField = 2 + i;
```

- [ ] **Step 6: Add port field update to the text field update block**

In the `// ── Text field update` block, after the existing mgmtIp UpdateTextField call:

```cpp
                if (ps.activeField == 1) UpdateTextField(selNode->mgmtIp, 18);
```

Add:

```cpp
                for (int i = 0; i < PORTS_PER_NODE; ++i)
                    if (ps.activeField == 2 + i) UpdateTextField(selNode->portIp[i], 18);
```

- [ ] **Step 7: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make
```

Expected: zero warnings.

- [ ] **Step 8: Run and verify acceptance criteria**

```bash
./packet-path
```

Expected:

- Select any node → panel now shows INTERFACES section with 4 rows below the GENERAL section
- PC shows "eth0", "eth1", "eth2", "eth3"; Router/Switch shows "Gi0/0" through "Gi0/3"
- Each row has a port name on the left and a clickable IP field on the right
- Click a port IP field → it activates (white border, blinking cursor)
- Type a valid IP → green border; type partial or invalid → red border
- Click a different field (hostname, mgmtIp, another port) → previous field deactivates
- ESC deactivates whichever field is active
- Hostname field, mgmtIp field, and all 4 port fields are independently focusable
- Typing in a field while another field holds data does not corrupt either field
- All M2.1 and M2.2 features still work

- [ ] **Step 9: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m2.3): per-port interface IP config rows in sidebar panel"
```

---

## Task 4: M2.4 — Right-Click Context Menu

**Files:**

- Modify: `src/main.cpp`

Add right-click context menus: node menu (Rename, Delete), cable menu (Delete Cable), canvas menu (Add PC/Router/Switch Here, Reset View). Bezier cables are hit-tested by sampling 20 points.

- [ ] **Step 1: Add ContextType enum and ContextMenu struct above `main()`**

Add after the `PanelState` / `PnlPortFieldRect` block:

```cpp
// ── Context menu ──────────────────────────────────────────────────────────
enum ContextType { CTX_NONE, CTX_NODE, CTX_CABLE, CTX_CANVAS };

struct ContextMenu {
    bool        visible   = false;
    Vector2     screenPos = {0.0f, 0.0f};
    Vector2     worldPos  = {0.0f, 0.0f};
    ContextType ctx       = CTX_NONE;
    int         targetId  = -1;    // nodeId for CTX_NODE; cable index for CTX_CABLE
    int         hoverItem = -1;
};
```

- [ ] **Step 2: Add `HitTestCable` function above `main()`**

Add after `HitTestPort`:

```cpp
// Returns cable index if worldMouse is within threshold of any cable bezier, else -1
int HitTestCable(const std::vector<Cable>& cables,
                 const std::vector<DeviceNode>& nodes,
                 Vector2 worldMouse, float threshold)
{
    for (int ci = 0; ci < (int)cables.size(); ++ci) {
        const Cable& c   = cables[ci];
        const DeviceNode* from = FindNode(nodes, c.fromId);
        const DeviceNode* to   = FindNode(nodes, c.toId);
        if (!from || !to) continue;

        Vector2 p0 = GetPortPosition(*from, c.fromPort);
        Vector2 p3 = GetPortPosition(*to,   c.toPort);

        const float offset = 60.0f;
        auto ctrl = [&](Vector2 p, int port) -> Vector2 {
            switch (port) {
                case 0: return {p.x,           p.y - offset};
                case 1: return {p.x + offset,  p.y         };
                case 2: return {p.x,           p.y + offset};
                case 3: return {p.x - offset,  p.y         };
            }
            return p;
        };
        Vector2 c1 = ctrl(p0, c.fromPort);
        Vector2 c2 = ctrl(p3, c.toPort);

        for (int s = 0; s <= 20; ++s) {
            float t  = (float)s / 20.0f;
            float it = 1.0f - t;
            Vector2 pt = {
                it*it*it*p0.x + 3*it*it*t*c1.x + 3*it*t*t*c2.x + t*t*t*p3.x,
                it*it*it*p0.y + 3*it*it*t*c1.y + 3*it*t*t*c2.y + t*t*t*p3.y
            };
            if (CheckCollisionPointCircle(worldMouse, pt, threshold))
                return ci;
        }
    }
    return -1;
}
```

- [ ] **Step 3: Add `DrawContextMenu` function above `main()`**

Add after `HitTestCable`:

```cpp
void DrawContextMenu(ContextMenu& menu, Vector2 screenMouse) {
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

    const int ITEM_H = 28, MENU_W = 160;
    float h = (float)(count * ITEM_H + 8);
    float x = std::min(menu.screenPos.x, (float)(CANVAS_W - MENU_W - 4));
    float y = std::min(menu.screenPos.y, (float)(SCREEN_H - (int)h - 4));

    DrawRectangleRounded({x, y, (float)MENU_W, h}, 0.08f, 4, Color{30, 41, 59, 255});
    DrawRectangleRoundedLinesEx({x, y, (float)MENU_W, h}, 0.08f, 4, 1.0f, PANEL_BORDER);

    menu.hoverItem = -1;
    for (int i = 0; i < count; ++i) {
        Rectangle ir = {x + 4, y + 4 + (float)(i * ITEM_H),
                        (float)(MENU_W - 8), (float)ITEM_H};
        if (CheckCollisionPointRec(screenMouse, ir)) {
            DrawRectangleRounded(ir, 0.08f, 4, Color{51, 65, 85, 255});
            menu.hoverItem = i;
        }
        DrawText(items[i], (int)ir.x + 8, (int)ir.y + 7, 13, WHITE);
    }
}
```

- [ ] **Step 4: Add `ExecuteMenuAction` function above `main()`**

Add after `DrawContextMenu`:

```cpp
void ExecuteMenuAction(ContextMenu& menu, std::vector<DeviceNode>& nodes,
                       std::vector<Cable>& cables, int& selectedId,
                       PanelState& ps, Camera2D& camera)
{
    int item = menu.hoverItem;
    if (menu.ctx == CTX_NODE) {
        if (item == 0) {  // Rename — select node and focus hostname field
            selectedId = menu.targetId;
            for (auto& n : nodes) n.selected = (n.id == selectedId);
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
        }
    } else if (menu.ctx == CTX_CABLE) {
        if (item == 0 && menu.targetId >= 0 && menu.targetId < (int)cables.size())
            cables.erase(cables.begin() + menu.targetId);
    } else if (menu.ctx == CTX_CANVAS) {
        if      (item == 0) nodes.push_back(SpawnNode(PC,     menu.worldPos));
        else if (item == 1) nodes.push_back(SpawnNode(ROUTER, menu.worldPos));
        else if (item == 2) nodes.push_back(SpawnNode(SWITCH, menu.worldPos));
        else if (item == 3) { camera.target = {0.0f, 0.0f}; camera.zoom = 1.0f; }
    }
}
```

- [ ] **Step 5: Declare ContextMenu in `main()` vars block**

In `main()`, after `PanelState ps;`, add:

```cpp
    ContextMenu contextMenu;
```

- [ ] **Step 6: Add RMB handling in main() — after the canvas LMB released block**

After the `// ── LMB released` block and before the panel click-to-focus block, add:

```cpp
        // ── RMB pressed — open context menu ───────────────────────────
        if (inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            contextMenu.screenPos = screenMouse;
            contextMenu.worldPos  = worldMouse;
            contextMenu.hoverItem = -1;
            ps.activeField        = -1;

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
        }
```

- [ ] **Step 7: Update LMB pressed handling — dismiss or execute context menu first**

Find the `// ── LMB pressed` block. It currently starts:

```cpp
        if (inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int pNode = -1, pPort = -1;
```

Replace the opening with:

```cpp
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (contextMenu.visible) {
                if (contextMenu.hoverItem != -1)
                    ExecuteMenuAction(contextMenu, nodes, cables, selectedId, ps, camera);
                contextMenu.visible = false;
            } else if (inCanvas) {
            int pNode = -1, pPort = -1;
```

Then add a closing `}` for the new `else if (inCanvas)` block. Find the end of the existing LMB pressed block — after:

```cpp
                    selectedId = -1;
                    dragging   = false;
                }
            }
        }
```

The outer `}` closes the original `if (IsMouseButtonPressed(...))`. You now need one extra closing brace for the `else if (inCanvas)`. The structure becomes:

```cpp
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (contextMenu.visible) {
                if (contextMenu.hoverItem != -1)
                    ExecuteMenuAction(contextMenu, nodes, cables, selectedId, ps, camera);
                contextMenu.visible = false;
            } else if (inCanvas) {
                // ... (all existing LMB pressed logic unchanged) ...
            }  // closes else if (inCanvas)
        }  // closes IsMouseButtonPressed
```

- [ ] **Step 8: Add ESC to dismiss context menu**

In the ESC handling block:

```cpp
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (ps.activeField != -1) {
                ps.activeField = -1;
            } else if (connecting) {
```

Add `contextMenu.visible = false;` at the start so any ESC always closes the menu:

```cpp
        if (IsKeyPressed(KEY_ESCAPE)) {
            contextMenu.visible = false;
            if (ps.activeField != -1) {
                ps.activeField = -1;
            } else if (connecting) {
                connecting  = false;
                hoverNodeId = -1;
                hoverPort   = -1;
            }
        }
```

- [ ] **Step 9: Call DrawContextMenu in the draw block**

In the draw block, after `DrawPanel(selectedId, nodes, ps);` and before `DrawFPS(...)`:

```cpp
            DrawContextMenu(contextMenu, screenMouse);
```

- [ ] **Step 10: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make
```

Expected: zero warnings.

- [ ] **Step 11: Run and verify acceptance criteria**

```bash
./packet-path
```

Expected:

- **Node context menu:** Right-click a device node → small dark popup appears with "Rename" and "Delete"; hover items highlight; click elsewhere dismisses
  - "Rename": node selects (if not already) and hostname field in panel activates (white border + cursor)
  - "Delete": node and its cables disappear (same as Delete key)
- **Canvas context menu:** Right-click empty canvas → popup shows "Add PC Here", "Add Router Here", "Add Switch Here", "Reset View"
  - "Add PC/Router/Switch Here": new device spawns exactly at right-click world position
  - "Reset View": camera returns to zoom=1.0, target={0,0} (origin centred in canvas)
- **Cable context menu:** Right-click directly on a bezier cable → popup shows "Delete Cable"
  - "Delete Cable": cable disappears
- **Dismissal:** Click anywhere outside menu → closes; ESC → closes; RMB again → replaces with new menu at new position
- Context menu stays within the canvas area (does not overlap the right panel)
- All M2.1, M2.2, M2.3 features still work

- [ ] **Step 12: Commit**

```bash
git add src/main.cpp
git commit -m "feat(m2.4): right-click context menu for nodes, cables, and canvas"
```

---

## Phase 2 Exit Commit

After all four milestones pass:

```bash
git push origin phase-2-canvas-engine
```

Then open a PR into `main` and merge after review.

**What you have:** A fully interactive canvas with a fixed config sidebar that shows per-node hostname, management IP, and four interface IPs (editable in-place with CIDR validation), plus right-click context menus for nodes, cables, and canvas — all in ~640 lines of `src/main.cpp`. Ready for Phase 3: Packet Animation.

---

## Self-Review

**Spec coverage:**

- ✅ M2.1: Fixed 280px right sidebar, canvas mouse guard, camera offset shift, placeholder/header/badge (Task 1)
- ✅ M2.2: IP text field system (UpdateTextField, ValidateIP, DrawTextField), hostname + mgmtIp fields, focus management, key gating, ESC priority (Task 2)
- ✅ M2.3: portIp[4], 4 interface rows with per-device port names, independent focus for each port field (Task 3)
- ✅ M2.4: ContextMenu struct, HitTestCable (bezier sample), DrawContextMenu, ExecuteMenuAction, Rename/Delete/Add Here/Reset View actions (Task 4)

**Placeholder scan:** No TBDs, TODOs, or vague steps. Every step has complete code or an exact command with expected output.

**Type consistency:**

- `PanelState.activeField`: 0=label, 1=mgmtIp, 2-5=portIp[0-3] — consistent across Tasks 2, 3 (panel click, update, draw)
- `PnlFieldRect(126)`, `PnlFieldRect(178)`, `PnlPortFieldRect(0-3)` — same helper used in both DrawPanel and panel click-to-focus
- `DrawTextField(r, topLabel, placeholder, value, active, valid)` — 6-arg signature consistent across all call sites
- `DrawPanel(selectedId, nodes, ps)` — 3-arg signature introduced Task 2, used in draw block from Task 2 onward
- `ContextMenu.targetId`: nodeId for CTX_NODE, cable index for CTX_CABLE — consistent in RMB handler and ExecuteMenuAction
- `ExecuteMenuAction` reads `menu.hoverItem` to determine which action — consistent with DrawContextMenu which sets `menu.hoverItem` during draw
- `HitTestCable` uses same `ctrl()` lambda and `offset=60.0f` as `DrawAllCables` — guaranteed consistent hit area and visual curve
