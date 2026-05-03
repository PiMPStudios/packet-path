# Resizable Window / Dynamic Resolution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Packet Path window fully resizable at runtime — all layout (panel, log strip, canvas, menus, overlays) adapts dynamically to `GetScreenWidth()`/`GetScreenHeight()` — while preserving pixel-perfect behavior at 1280×720.

**Architecture:** Replace the four compile-time layout constants that depend on screen size (`SCREEN_W`, `SCREEN_H`, `CANVAS_W`, `CANVAS_H`) with `inline int` functions that call `GetScreenWidth()`/`GetScreenHeight()` each frame. Four fixed constants (`PANEL_W`, `LOG_H`, `MENU_ITEM_H`, `CONTEXT_MENU_W`) remain as `inline constexpr int`. A new `src/Layout.h` is the single source of truth; `src/NetworkCanvas.h` drops its static constants and gains `#include "Layout.h"`. All call sites change from `CANVAS_W` to `CANVAS_W()` — a mechanical add-parens operation. On resize, the camera offset is updated inside the frame loop with `IsWindowResized()`.

**Tech Stack:** C++17, raylib 5.5, GNU Make.

---

## File Map

| Action | File | Responsibility |
| -------- | ------ | ---------------- |
| Create | `src/Layout.h` | Single source of truth: 4 `inline constexpr int` constants + 4 `inline int` functions wrapping GetScreenWidth/Height |
| Modify | `src/NetworkCanvas.h` | Remove 6 static const int layout constants; add `#include "Layout.h"` |
| Modify | `src/ConfigPanel.cpp` | `CANVAS_W` → `CANVAS_W()` at ~25 call sites (PANEL_W unchanged — it's a constant) |
| Modify | `src/GameUI.cpp` | `CANVAS_W` → `CANVAS_W()`, `CANVAS_H` → `CANVAS_H()` at 3 call sites |
| Modify | `src/TraceModal.cpp` | `CANVAS_H` → `CANVAS_H()`, `CANVAS_W` → `CANVAS_W()`, `SCREEN_W` → `SCREEN_W()`, `SCREEN_H` → `SCREEN_H()` at 7 call sites |
| Modify | `src/NetworkCanvas.cpp` | `CANVAS_W` → `CANVAS_W()`, `CANVAS_H` → `CANVAS_H()`, `SCREEN_W` → `SCREEN_W()`, `SCREEN_H` → `SCREEN_H()` at ~127 call sites |
| Modify | `src/main.cpp` | Add `SetWindowResizable`/`SetWindowMinSize`; `IsWindowResized` camera update; rename 10 constant uses to function calls |

---

## Task 1: Create `src/Layout.h` and Update `src/NetworkCanvas.h`

**Files:**

- Create: `src/Layout.h`
- Modify: `src/NetworkCanvas.h`

- [ ] **Step 1: Create `src/Layout.h`**

Create a new file `src/Layout.h` with this exact content:

```cpp
#pragma once
#include "raylib.h"

// Fixed layout constants — these never change with window size
inline constexpr int PANEL_W        = 280;
inline constexpr int LOG_H          = 90;
inline constexpr int MENU_ITEM_H    = 28;
inline constexpr int CONTEXT_MENU_W = 160;
inline constexpr int MIN_W          = 1024;
inline constexpr int MIN_H          = 600;

// Dynamic layout — queried fresh each use
inline int SCREEN_W() { return GetScreenWidth();              }
inline int SCREEN_H() { return GetScreenHeight();             }
inline int CANVAS_W() { return GetScreenWidth()  - PANEL_W;  }
inline int CANVAS_H() { return GetScreenHeight() - LOG_H;    }
```

- [ ] **Step 2: Update `src/NetworkCanvas.h`**

Open `src/NetworkCanvas.h`. Find lines 16–26 (the static const int layout block):

```cpp
// ── Screen & layout constants ─────────────────────────────────────────────
static const int   SCREEN_W       = 1280;
static const int   SCREEN_H       = 720;
static const Color BG_COLOR       = {15, 23, 42, 255};
static const int   PANEL_W        = 280;
static const int   CANVAS_W       = SCREEN_W - PANEL_W;
static const int   LOG_H          = 90;
static const int   CANVAS_H       = SCREEN_H - LOG_H;
static const Color PANEL_BG       = {22, 33, 62, 255};
static const Color PANEL_BORDER   = {51, 65, 85, 255};
static const int   MENU_ITEM_H    = 28;
static const int   CONTEXT_MENU_W = 160;
```

Replace with:

```cpp
// ── Screen & layout constants ─────────────────────────────────────────────
#include "Layout.h"
static const Color BG_COLOR     = {15, 23, 42, 255};
static const Color PANEL_BG     = {22, 33, 62, 255};
static const Color PANEL_BORDER = {51, 65, 85, 255};
```

This removes `SCREEN_W`, `SCREEN_H`, `PANEL_W`, `CANVAS_W`, `LOG_H`, `CANVAS_H`, `MENU_ITEM_H`, `CONTEXT_MENU_W` from NetworkCanvas.h and picks them up from Layout.h instead. All files that `#include "NetworkCanvas.h"` (ConfigPanel.cpp, GameUI.cpp, TraceModal.cpp, NetworkCanvas.cpp, main.cpp, UI.cpp) now have Layout.h transitively.

- [ ] **Step 3: Build to verify clean**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | tail -10
```

Expected: many errors about `CANVAS_W` and `SCREEN_W` not being callable without `()`. That is correct — Tasks 2–5 fix these. Zero linker errors; only "not a function" errors for the renamed symbols are acceptable at this step. If you see errors about PANEL_W, LOG_H, MENU_ITEM_H, or CONTEXT_MENU_W missing, stop and fix Layout.h before continuing.

- [ ] **Step 4: Commit**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
git add src/Layout.h src/NetworkCanvas.h
git commit -m "feat: add Layout.h with dynamic CANVAS_W/H functions, remove static constants from NetworkCanvas.h"
```

---

## Task 2: Update `src/ConfigPanel.cpp`

**Files:**

- Modify: `src/ConfigPanel.cpp`

This file has ~25 uses of `CANVAS_W` as an integer expression. `PANEL_W` is now a constexpr constant so it needs no parens. The change is purely mechanical: add `()` after every bare `CANVAS_W`.

- [ ] **Step 1: Rename all `CANVAS_W` to `CANVAS_W()` in ConfigPanel.cpp**

Run a sed replacement:

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
sed -i '' 's/CANVAS_W\b/CANVAS_W()/g' src/ConfigPanel.cpp
```

- [ ] **Step 2: Verify the result looks correct**

```bash
grep -n "CANVAS_W" src/ConfigPanel.cpp | head -20
```

Every occurrence should now read `CANVAS_W()`. You should see lines like:

```text
5:    return {(float)(CANVAS_W() + 12), (float)yOffset, (float)(PANEL_W - 24), 26.0f};
9:    return {(float)(CANVAS_W() + 80), ...
```

`PANEL_W` lines should be untouched (no parens added). If any line shows `CANVAS_W()()` (double parens), it means the file already had a call somewhere — fix those manually.

- [ ] **Step 3: Build to verify**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | grep "ConfigPanel" | head -10
```

Expected: no errors from ConfigPanel.cpp. Other files will still have errors from Tasks 3–5.

- [ ] **Step 4: Commit**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
git add src/ConfigPanel.cpp
git commit -m "refactor: CANVAS_W → CANVAS_W() in ConfigPanel.cpp for dynamic layout"
```

---

## Task 3: Update `src/GameUI.cpp` and `src/TraceModal.cpp`

**Files:**

- Modify: `src/GameUI.cpp`
- Modify: `src/TraceModal.cpp`

These two files are small. `GameUI.cpp` has 3 uses, `TraceModal.cpp` has 7.

- [ ] **Step 1: Update `src/GameUI.cpp`**

Open `src/GameUI.cpp`. The file currently has (lines 6–7 and 56):

```cpp
    return {(float)(CANVAS_W - 320) / 2.0f,
            (float)(CANVAS_H - 260) / 2.0f,
```

and

```cpp
    DrawRectangle(0, 0, CANVAS_W, CANVAS_H, Color{0, 0, 0, 150});
```

Replace all three uses:

```cpp
    return {(float)(CANVAS_W() - 320) / 2.0f,
            (float)(CANVAS_H() - 260) / 2.0f,
```

```cpp
    DrawRectangle(0, 0, CANVAS_W(), CANVAS_H(), Color{0, 0, 0, 150});
```

Or run:

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
sed -i '' 's/CANVAS_W\b/CANVAS_W()/g; s/CANVAS_H\b/CANVAS_H()/g' src/GameUI.cpp
```

- [ ] **Step 2: Update `src/TraceModal.cpp`**

The current uses in TraceModal.cpp are at lines 10–11, 18–19, 30, 33–34:

```cpp
    if (mouse.y < (float)CANVAS_H || mouse.y >= (float)SCREEN_H) return -1;
    if (mouse.x >= (float)CANVAS_W) return -1;
    ...
        int       lineY = CANVAS_H + 8 + (shown - 1 - i) * 24;
        Rectangle r     = {0.f, (float)(lineY - 2), (float)CANVAS_W, 22.f};
    ...
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, 140});
    ...
    const float MX = (SCREEN_W - MW) / 2.f;
    const float MY = (SCREEN_H - MH) / 2.f;
```

Run:

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
sed -i '' 's/CANVAS_W\b/CANVAS_W()/g; s/CANVAS_H\b/CANVAS_H()/g; s/SCREEN_W\b/SCREEN_W()/g; s/SCREEN_H\b/SCREEN_H()/g' src/TraceModal.cpp
```

After the replacement, verify:

```bash
grep -n "CANVAS_W\|CANVAS_H\|SCREEN_W\|SCREEN_H" src/TraceModal.cpp
```

Every occurrence should have `()` appended. Expected result (all 7 lines transformed):

```text
10:    if (mouse.y < (float)CANVAS_H() || mouse.y >= (float)SCREEN_H()) return -1;
11:    if (mouse.x >= (float)CANVAS_W()) return -1;
18:        int       lineY = CANVAS_H() + 8 + (shown - 1 - i) * 24;
19:        Rectangle r     = {0.f, (float)(lineY - 2), (float)CANVAS_W(), 22.f};
30:    DrawRectangle(0, 0, SCREEN_W(), SCREEN_H(), Color{0, 0, 0, 140});
33:    const float MX = (SCREEN_W() - MW) / 2.f;
34:    const float MY = (SCREEN_H() - MH) / 2.f;
```

- [ ] **Step 3: Build to verify**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | grep -E "GameUI|TraceModal" | head -10
```

Expected: zero errors from either file.

- [ ] **Step 4: Commit**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
git add src/GameUI.cpp src/TraceModal.cpp
git commit -m "refactor: CANVAS_W/H, SCREEN_W/H → function calls in GameUI and TraceModal"
```

---

## Task 4: Update `src/NetworkCanvas.cpp`

**Files:**

- Modify: `src/NetworkCanvas.cpp`

This is the largest file — 127 uses of layout constants. The change is purely mechanical: add `()` after `CANVAS_W`, `CANVAS_H`, `SCREEN_W`, `SCREEN_H`. `PANEL_W`, `LOG_H`, `MENU_ITEM_H`, `CONTEXT_MENU_W` need no change (they stay as constexpr constants).

- [ ] **Step 1: Run the mechanical rename**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
sed -i '' 's/CANVAS_W\b/CANVAS_W()/g; s/CANVAS_H\b/CANVAS_H()/g; s/SCREEN_W\b/SCREEN_W()/g; s/SCREEN_H\b/SCREEN_H()/g' src/NetworkCanvas.cpp
```

- [ ] **Step 2: Verify counts**

```bash
grep -c "CANVAS_W()" src/NetworkCanvas.cpp
grep -c "CANVAS_H()" src/NetworkCanvas.cpp
grep -c "SCREEN_W()" src/NetworkCanvas.cpp
grep -c "SCREEN_H()" src/NetworkCanvas.cpp
```

Expected approximate counts: `CANVAS_W()` ~100+, `CANVAS_H()` ~20+, `SCREEN_W()` ~3, `SCREEN_H()` ~3. Any bare `CANVAS_W` or `SCREEN_W` (without parens) remaining is a bug — there should be none:

```bash
grep -n "\bCANVAS_W\b\|\bCANVAS_H\b\|\bSCREEN_W\b\|\bSCREEN_H\b" src/NetworkCanvas.cpp
```

Expected: no output.

- [ ] **Step 3: Check for double-parens (regression guard)**

```bash
grep -n "CANVAS_W()()\|SCREEN_W()()" src/NetworkCanvas.cpp
```

Expected: no output. If any appear, the file had pre-existing function calls — fix those lines manually (e.g., `CANVAS_W()()` → `CANVAS_W()`).

- [ ] **Step 4: Build to verify**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | grep "NetworkCanvas" | head -15
```

Expected: zero errors from NetworkCanvas.cpp. At this point only `src/main.cpp` errors should remain.

- [ ] **Step 5: Quick visual sanity check**

Spot-check three representative lines to confirm correct transformation:

```bash
grep -n "CANVAS_W\|SCREEN_H" src/NetworkCanvas.cpp | head -5
```

You should see lines like:

```text
63:    Vector2 botRight = GetScreenToWorld2D({(float)CANVAS_W(), (float)CANVAS_H()}, cam);
298:    DrawRectangle(0, CANVAS_H(), SCREEN_W(), LOG_H, Color{10, 15, 28, 255});
1153:    float x = std::min(menu.screenPos.x, (float)(CANVAS_W() - CONTEXT_MENU_W - 4));
```

- [ ] **Step 6: Commit**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
git add src/NetworkCanvas.cpp
git commit -m "refactor: CANVAS_W/H, SCREEN_W/H → function calls in NetworkCanvas.cpp (127 sites)"
```

---

## Task 5: Update `src/main.cpp` — Enable Resizable Window + Rename Constants

**Files:**

- Modify: `src/main.cpp`

This task wires in the actual resize support and fixes the 10 remaining constant uses.

- [ ] **Step 1: Update `InitWindow` and add resize setup**

Open `src/main.cpp`. Find line 12:

```cpp
    InitWindow(SCREEN_W, SCREEN_H, "Packet Path");
```

Replace with:

```cpp
    InitWindow(1280, 720, "Packet Path");
    SetWindowResizable(true);
    SetWindowMinSize(MIN_W, MIN_H);
```

The literal `1280, 720` is correct for startup — we don't have a window to query yet. `SetWindowResizable` must be called after `InitWindow`.

- [ ] **Step 2: Fix the camera offset initialization**

Find line 21:

```cpp
    camera.offset   = {CANVAS_W / 2.0f, CANVAS_H / 2.0f};
```

Replace with:

```cpp
    camera.offset   = {CANVAS_W() / 2.0f, CANVAS_H() / 2.0f};
```

- [ ] **Step 3: Add `IsWindowResized` handler in the frame loop**

Find line 54 (the start of the frame loop):

```cpp
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        Vector2 screenMouse = GetMousePosition();
        Vector2 worldMouse  = GetScreenToWorld2D(screenMouse, camera);
        bool inCanvas = (screenMouse.x < (float)CANVAS_W &&
                         screenMouse.y < (float)CANVAS_H);
```

Replace the entire block with:

```cpp
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        if (IsWindowResized())
            camera.offset = {CANVAS_W() / 2.0f, CANVAS_H() / 2.0f};

        Vector2 screenMouse = GetMousePosition();
        Vector2 worldMouse  = GetScreenToWorld2D(screenMouse, camera);
        bool inCanvas = (screenMouse.x < (float)CANVAS_W() &&
                         screenMouse.y < (float)CANVAS_H());
```

- [ ] **Step 4: Fix trace modal click-outside detection (line 227)**

Find:

```cpp
                Rectangle modal = {(SCREEN_W - MW) / 2.f, (SCREEN_H - MH) / 2.f, MW, MH};
```

Replace with:

```cpp
                Rectangle modal = {(SCREEN_W() - MW) / 2.f, (SCREEN_H() - MH) / 2.f, MW, MH};
```

- [ ] **Step 5: Fix log console region hit test (lines 345–347)**

Find:

```cpp
            } else if (screenMouse.y >= (float)CANVAS_H &&
                       screenMouse.y <  (float)SCREEN_H  &&
                       screenMouse.x <  (float)CANVAS_W) {
```

Replace with:

```cpp
            } else if (screenMouse.y >= (float)CANVAS_H() &&
                       screenMouse.y <  (float)SCREEN_H()  &&
                       screenMouse.x <  (float)CANVAS_W()) {
```

- [ ] **Step 6: Fix HUD positions (lines 962, 989, 992)**

Find:

```cpp
                DrawText(hint, (CANVAS_W - tw) / 2, 12, 12,
```

Replace with:

```cpp
                DrawText(hint, (CANVAS_W() - tw) / 2, 12, 12,
```

Find:

```cpp
            DrawFPS(CANVAS_W - 80, 10);
```

Replace with:

```cpp
            DrawFPS(CANVAS_W() - 80, 10);
```

Find:

```cpp
                     10, CANVAS_H - 24, 10, Color{100, 116, 139, 255});
```

Replace with:

```cpp
                     10, CANVAS_H() - 24, 10, Color{100, 116, 139, 255});
```

- [ ] **Step 7: Verify no bare constants remain in main.cpp**

```bash
grep -n "\bCANVAS_W\b\|\bCANVAS_H\b\|\bSCREEN_W\b\|\bSCREEN_H\b" src/main.cpp
```

Expected: no output. If any remain, fix them manually.

- [ ] **Step 8: Full build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | tail -10
```

Expected: `Linking packet-path...` then `Build complete.` with zero errors or warnings.

- [ ] **Step 9: Smoke test**

Run the game:

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && ./packet-path &
```

Verify:

1. Window opens at 1280×720 — layout looks identical to before
2. Drag the window edge to make it larger — panel stays on the right, log console stays at the bottom, canvas fills the center
3. Shrink the window toward minimum (1024×600) — layout still correct, nothing clips into the panel
4. Right-click context menu appears near the click position and stays within canvas bounds
5. Send a packet — animation plays correctly after resize
6. T-key troubleshoot overlay renders correctly at different window sizes

Kill the background process when done:

```bash
pkill packet-path
```

- [ ] **Step 10: Commit**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
git add src/main.cpp
git commit -m "feat: enable resizable window with dynamic camera offset update on resize"
```

---

## Verification Checklist

Run after all 5 tasks are complete:

- [ ] `make` produces zero errors or warnings
- [ ] At 1280×720: layout is pixel-for-pixel identical to before this change
- [ ] Enlarge window: canvas grows, panel stays 280px wide, log strip stays 90px tall
- [ ] Shrink to ~1024×600: nothing clips, everything readable
- [ ] Context menu stays within canvas bounds at all window sizes
- [ ] Packet animation plays correctly after a mid-session resize
- [ ] Trace modal centers correctly on the screen after resize
- [ ] FPS counter and keyboard-hint text track the right edge / bottom of canvas
- [ ] `grep -rn "\bCANVAS_W\b\|\bCANVAS_H\b\|\bSCREEN_W\b\|\bSCREEN_H\b" src/` returns no output (all bare names gone)

---

## Self-Review

**Spec coverage:**

- `SetWindowResizable(true)` + `SetWindowMinSize(MIN_W, MIN_H)` — Task 5 Step 1 ✓
- `IsWindowResized()` camera update every frame — Task 5 Step 3 ✓
- `CANVAS_W`, `CANVAS_H`, `SCREEN_W`, `SCREEN_H` become functions — Task 1 ✓
- `PANEL_W`, `LOG_H`, `MENU_ITEM_H`, `CONTEXT_MENU_W` stay as constants — Layout.h ✓
- `MIN_W = 1024`, `MIN_H = 600` — Layout.h ✓
- All call sites in ConfigPanel.cpp — Task 2 ✓
- All call sites in GameUI.cpp and TraceModal.cpp — Task 3 ✓
- All 127 call sites in NetworkCanvas.cpp — Task 4 ✓
- All 10 call sites in main.cpp — Task 5 ✓
- Preserve 1280×720 behavior — `InitWindow(1280, 720, ...)` literal startup + inline functions return exact same values at that size ✓

**Placeholder scan:** None found.

**Type consistency:** All functions return `int` matching the old `static const int` types. All cast sites use `(float)CANVAS_W()` or `(float)CANVAS_H()` which is identical to the old `(float)CANVAS_W` pattern.

**Risk note:** `sed -i '' 's/CANVAS_W\b/CANVAS_W()/g'` on NetworkCanvas.cpp is safe because no existing function in that file is named `CANVAS_W` — it was always a constant. The `\b` word boundary prevents double-application on a re-run.
