# Sandbox / Free-Play Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a dedicated Sandbox mode with a teal SANDBOX badge and HUD navigation buttons, a full-screen level-select overlay showing all 16 levels accessible via key `M` or MENU button, and fix key `0` to return to sandbox instead of loading level 10.

**Architecture:** A new `GAME_LEVEL_SELECT` enum value drives a full-screen overlay with a 4×4 level-card grid and a sandbox card; the existing `GAME_SANDBOX` state gains a teal badge and MENU button; level play gains MENU + SANDBOX shortcut buttons; `main.cpp` routes all clicks and key presses to the correct mode transitions. All new UI functions live in `GameUI.cpp` / `GameUI.h`. No changes to simulation, levels, or protocol engines.

**Tech Stack:** C++17, raylib 5.x, nlohmann/json (level titles preloaded at startup for level-select screen)

---

## File Map

| File | Change |
|------|--------|
| `src/GameUI.h` | Add `GAME_LEVEL_SELECT` to `GameMode` enum; declare 7 new functions + rect helpers |
| `src/GameUI.cpp` | Implement `DrawSandboxHUD`, `DrawLevelSelectScreen`, 5 rect functions |
| `src/main.cpp` | Wire startup mode, `goSandbox` lambda, key shortcuts, LMB handlers, draw calls, hint text |

---

### Task 1: GameUI.h — Enum Extension + Declarations

**Files:**
- Modify: `src/GameUI.h`

- [ ] **Step 1: Add GAME_LEVEL_SELECT to the enum**

```cpp
// BEFORE (line 6):
enum GameMode { GAME_SANDBOX, GAME_PLAYING, GAME_WIN };

// AFTER:
enum GameMode { GAME_SANDBOX, GAME_PLAYING, GAME_WIN, GAME_LEVEL_SELECT };
```

- [ ] **Step 2: Append new declarations after the existing DrawFileDialog declaration**

Add at the end of `src/GameUI.h`:

```cpp
// ── Sandbox mode HUD ──────────────────────────────────────────────────────
// Teal "SANDBOX" badge {8,8,108,22} + MENU button at {120,8,52,22}.
void DrawSandboxHUD();
Rectangle SandboxMenuBtnRect();      // {120,8,52,22}

// ── Level-play HUD additions ──────────────────────────────────────────────
// Drawn to the right of the existing 240 px level badge.
Rectangle LevelHudMenuBtnRect();     // {252,8,52,22}
Rectangle LevelHudSandboxBtnRect();  // {308,8,72,22}

// ── Level-select overlay ──────────────────────────────────────────────────
// Full-screen overlay; levelTitles must be a 16-element array (indices 0–15).
void DrawLevelSelectScreen(const std::string* levelTitles);
Rectangle LevelSelectCardRect(int i);   // i=0..15 → level card for level i+1
Rectangle LevelSelectSandboxBtnRect();  // Full-width sandbox card below the grid
```

- [ ] **Step 3: Build — verify it compiles cleanly**

```bash
make 2>&1 | grep -E "error:|warning:"
```

Expected: zero errors, zero new warnings.

- [ ] **Step 4: Commit**

```bash
git add src/GameUI.h
git commit -m "feat: add GAME_LEVEL_SELECT enum + sandbox/level-select HUD declarations"
```

---

### Task 2: GameUI.cpp — Rect Helpers + DrawSandboxHUD

**Files:**
- Modify: `src/GameUI.cpp`

- [ ] **Step 1: Insert rect implementations after WinNextBtnRect() and before DrawLevelHUD()**

```cpp
Rectangle SandboxMenuBtnRect()     { return {120.f, 8.f,  52.f, 22.f}; }
Rectangle LevelHudMenuBtnRect()    { return {252.f, 8.f,  52.f, 22.f}; }
Rectangle LevelHudSandboxBtnRect() { return {308.f, 8.f,  72.f, 22.f}; }

Rectangle LevelSelectCardRect(int i) {
    // 4-column grid, 180×80 cards, 10 px gaps, centered in canvas
    const float cardW = 180.f, cardH = 80.f, gapX = 10.f, gapY = 10.f;
    const float gridW = 4.f * cardW + 3.f * gapX;   // 750 px
    float xs = std::max(8.f, ((float)CANVAS_W() - gridW) / 2.f);
    int col = i % 4, row = i / 4;
    return {xs + col * (cardW + gapX), 90.f + row * (cardH + gapY), cardW, cardH};
}

Rectangle LevelSelectSandboxBtnRect() {
    const float cardW = 180.f, cardH = 80.f, gapX = 10.f, gapY = 10.f;
    const float gridW = 4.f * cardW + 3.f * gapX;
    float xs = std::max(8.f, ((float)CANVAS_W() - gridW) / 2.f);
    return {xs, 90.f + 4.f * (cardH + gapY), gridW, 50.f};
}
```

Note: `std::max` is in `<algorithm>`. `GameUI.cpp` already includes `NetworkCanvas.h` which pulls it in transitively, but add `#include <algorithm>` at the top of `GameUI.cpp` if the build errors on `std::max`.

- [ ] **Step 2: Add DrawSandboxHUD() before DrawLevelHUD()**

```cpp
void DrawSandboxHUD() {
    // Teal "SANDBOX" badge
    DrawRectangle(8, 8, 108, 22, Color{15, 118, 110, 210});
    DrawRectangleLinesEx({8.f, 8.f, 108.f, 22.f}, 1.0f, Color{20, 184, 166, 255});
    DrawText("SANDBOX", 14, 13, 10, Color{204, 251, 241, 255});

    // MENU button — opens level select
    Rectangle mb = SandboxMenuBtnRect();
    DrawRectangle((int)mb.x, (int)mb.y, (int)mb.width, (int)mb.height,
                  Color{30, 41, 59, 210});
    DrawRectangleLinesEx(mb, 1.0f, Color{51, 65, 85, 255});
    int tw = MeasureText("MENU", 10);
    DrawText("MENU", (int)(mb.x + (mb.width - tw) / 2.f), (int)(mb.y + 6),
             10, Color{148, 163, 184, 255});
}
```

- [ ] **Step 3: Build — verify it compiles cleanly**

```bash
make 2>&1 | grep -E "error:|warning:"
```

Expected: zero errors.

- [ ] **Step 4: Commit**

```bash
git add src/GameUI.cpp
git commit -m "feat: add DrawSandboxHUD and rect helpers for sandbox/level-select HUD"
```

---

### Task 3: GameUI.cpp — DrawLevelSelectScreen

**Files:**
- Modify: `src/GameUI.cpp`

- [ ] **Step 1: Add DrawLevelSelectScreen() after DrawSandboxHUD()**

```cpp
void DrawLevelSelectScreen(const std::string* levelTitles) {
    // Dim the entire canvas + panel area
    DrawRectangle(0, 0, CANVAS_W(), SCREEN_H(), Color{0, 0, 0, 200});

    // Title
    const char* hdr = "SELECT A LEVEL";
    int hdw = MeasureText(hdr, 18);
    DrawText(hdr, (CANVAS_W() - hdw) / 2, 50, 18, WHITE);

    Vector2 mouse = GetMousePosition();

    // 4×4 grid of level cards  (i = 0..15 → level i+1)
    for (int i = 0; i < 16; ++i) {
        Rectangle r  = LevelSelectCardRect(i);
        bool hovered = CheckCollisionPointRec(mouse, r);

        Color bg  = hovered ? Color{30,  58, 138, 255} : Color{22, 33,  62, 255};
        Color brd = hovered ? Color{59, 130, 246, 255} : Color{51, 65,  85, 255};
        DrawRectangleRounded(r, 0.1f, 4, bg);
        DrawRectangleRoundedLinesEx(r, 0.1f, 4, 1.5f, brd);

        // "LVL N" number
        char lvlBuf[8];
        std::snprintf(lvlBuf, sizeof(lvlBuf), "LVL %d", i + 1);
        DrawText(lvlBuf, (int)(r.x + 10), (int)(r.y + 12), 11,
                 Color{148, 163, 184, 255});

        // Level title — truncate at 22 chars to fit 180 px card
        std::string title = levelTitles[i];
        if ((int)title.size() > 22) title = title.substr(0, 19) + "...";
        DrawText(title.c_str(), (int)(r.x + 10), (int)(r.y + 32), 10,
                 Color{203, 213, 225, 255});
    }

    // Full-width sandbox card below the grid
    Rectangle sr   = LevelSelectSandboxBtnRect();
    bool sandHover = CheckCollisionPointRec(mouse, sr);
    Color sbg  = sandHover ? Color{15, 118, 110, 255} : Color{13,  94,  88, 255};
    Color sbrd = sandHover ? Color{20, 184, 166, 255} : Color{13, 148, 136, 255};
    DrawRectangleRounded(sr, 0.12f, 4, sbg);
    DrawRectangleRoundedLinesEx(sr, 0.12f, 4, 1.5f, sbrd);

    const char* sandTxt = "SANDBOX \xe2\x80\x94 Free Build Mode";
    int stw = MeasureText(sandTxt, 13);
    DrawText(sandTxt,
             (int)(sr.x + (sr.width  - stw) / 2.f),
             (int)(sr.y + (sr.height - 13)  / 2.f),
             13, Color{204, 251, 241, 255});
}
```

- [ ] **Step 2: Build — verify it compiles cleanly**

```bash
make 2>&1 | grep -E "error:|warning:"
```

Expected: zero errors.

- [ ] **Step 3: Commit**

```bash
git add src/GameUI.cpp
git commit -m "feat: add DrawLevelSelectScreen — 4x4 level grid + sandbox card"
```

---

### Task 4: main.cpp — Full Wiring

**Files:**
- Modify: `src/main.cpp`

Changes in order: levelTitles + goSandbox → startup mode → key loop → canvas guard → ESC handler → LMB level-select branch → LMB HUD button handlers → draw section → hint text.

- [ ] **Step 1: Add levelTitles preload + goSandbox lambda before the while loop**

After the line `float fileOpTimer = 0.f;` and before `while (!WindowShouldClose())`, insert:

```cpp
// Preload level titles for the level-select screen
std::string levelTitles[16];
for (int i = 0; i < 16; ++i) {
    char lpath[64];
    std::snprintf(lpath, sizeof(lpath), "levels/level_%02d.json", i + 1);
    LevelDef tmpDef;
    levelTitles[i] = LoadLevel(lpath, tmpDef) ? tmpDef.title
                   : std::string("Level ") + std::to_string(i + 1);
}

// Clears all canvas state and enters GAME_SANDBOX
auto goSandbox = [&]() {
    nodes.clear();
    nodes.push_back(SpawnNode(PC, {0.0f, 0.0f}));
    cables.clear();
    selectedId           = -1;
    currentLevel         = 0;
    activeLevelDef       = LevelDef{};
    lastConditionsPassed = 0;
    failedAttempts       = 0;
    starsEarned          = 0;
    gameMode             = GAME_SANDBOX;
    ps                   = PanelState{};
    simState             = SimState{};
    logEntries.clear();
    dragging             = false;
    connecting           = false;
    hoverNodeId          = -1;
    hoverPort            = -1;
    contextMenu.visible  = false;
    troubleshootMode     = false;
    traceModalOpen       = false;
    failAnnotationTimer  = 0.f;
    lastFailedTrace      = {};
};
```

- [ ] **Step 2: Change startup mode to GAME_LEVEL_SELECT**

```cpp
// BEFORE (line ~44):
GameMode gameMode = GAME_SANDBOX;

// AFTER:
GameMode gameMode = GAME_LEVEL_SELECT;
```

- [ ] **Step 3: Fix key loop — k=1..9 only, key 0 → sandbox, key M → level select**

Locate `for (int k = 1; k <= 10; ++k)` inside the canvas spawn guard and replace the entire `if (ps.activePortAreaField == -1)` block:

```cpp
// BEFORE:
if (ps.activePortAreaField == -1) {
    for (int k = 1; k <= 10; ++k) {
        int key = (k <= 9) ? (KEY_ONE + (k - 1)) : KEY_ZERO;
        if (IsKeyPressed(key)) {
            char path[64];
            std::snprintf(path, sizeof(path), "levels/level_%02d.json", k);
            LevelDef def;
            if (LoadLevel(path, def)) {
                currentLevel         = k;
                activeLevelDef       = def;
                ApplyLevel(def, nodes, cables, selectedId);
                ps                   = PanelState{};
                simState             = SimState{};
                logEntries.clear();
                lastConditionsPassed = 0;
                failedAttempts       = 0;
                starsEarned          = 0;
                gameMode             = GAME_PLAYING;
                dragging             = false;
                connecting           = false;
                hoverNodeId          = -1;
                hoverPort            = -1;
                contextMenu.visible  = false;
                troubleshootMode     = false;
                traceModalOpen       = false;
                failAnnotationTimer  = 0.f;
                lastFailedTrace      = {};
            }
        }
    }
}

// AFTER:
if (ps.activePortAreaField == -1) {
    for (int k = 1; k <= 9; ++k) {
        if (IsKeyPressed(KEY_ONE + (k - 1))) {
            char path[64];
            std::snprintf(path, sizeof(path), "levels/level_%02d.json", k);
            LevelDef def;
            if (LoadLevel(path, def)) {
                currentLevel         = k;
                activeLevelDef       = def;
                ApplyLevel(def, nodes, cables, selectedId);
                ps                   = PanelState{};
                simState             = SimState{};
                logEntries.clear();
                lastConditionsPassed = 0;
                failedAttempts       = 0;
                starsEarned          = 0;
                gameMode             = GAME_PLAYING;
                dragging             = false;
                connecting           = false;
                hoverNodeId          = -1;
                hoverPort            = -1;
                contextMenu.visible  = false;
                troubleshootMode     = false;
                traceModalOpen       = false;
                failAnnotationTimer  = 0.f;
                lastFailedTrace      = {};
            }
        }
    }
    if (IsKeyPressed(KEY_ZERO)) goSandbox();
    if (IsKeyPressed(KEY_M))    gameMode = GAME_LEVEL_SELECT;
}
```

- [ ] **Step 4: Guard canvas spawn block against GAME_LEVEL_SELECT**

```cpp
// BEFORE:
if (fileOp == FILEOP_NONE && inCanvas && gameMode != GAME_WIN &&
    ps.activeField == -1 && ps.activeRouteField == -1 &&
    simState.mode == SIM_IDLE) {

// AFTER:
if (fileOp == FILEOP_NONE && inCanvas && gameMode != GAME_WIN &&
    gameMode != GAME_LEVEL_SELECT &&
    ps.activeField == -1 && ps.activeRouteField == -1 &&
    simState.mode == SIM_IDLE) {
```

- [ ] **Step 5: Add ESC handler for GAME_LEVEL_SELECT as the first branch**

```cpp
// BEFORE:
if (IsKeyPressed(KEY_ESCAPE)) {
    if (traceModalOpen) {

// AFTER:
if (IsKeyPressed(KEY_ESCAPE)) {
    if (gameMode == GAME_LEVEL_SELECT) {
        gameMode = (currentLevel > 0) ? GAME_PLAYING : GAME_SANDBOX;
    } else if (traceModalOpen) {
```

(The rest of the ESC chain — field dismissal, connecting cancel, etc. — is unchanged.)

- [ ] **Step 6: Add level-select LMB branch**

Inside `if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))`, add a new `else if` branch AFTER `else if (traceModalOpen)` and BEFORE the final `else`:

```cpp
} else if (gameMode == GAME_LEVEL_SELECT) {
    for (int i = 0; i < 16; ++i) {
        if (CheckCollisionPointRec(screenMouse, LevelSelectCardRect(i))) {
            int lvlNum = i + 1;
            char path[64];
            std::snprintf(path, sizeof(path), "levels/level_%02d.json", lvlNum);
            LevelDef def;
            if (LoadLevel(path, def)) {
                currentLevel         = lvlNum;
                activeLevelDef       = def;
                ApplyLevel(def, nodes, cables, selectedId);
                ps                   = PanelState{};
                simState             = SimState{};
                logEntries.clear();
                lastConditionsPassed = 0;
                failedAttempts       = 0;
                starsEarned          = 0;
                gameMode             = GAME_PLAYING;
                dragging             = false;
                connecting           = false;
                hoverNodeId          = -1;
                hoverPort            = -1;
                contextMenu.visible  = false;
                troubleshootMode     = false;
                traceModalOpen       = false;
                failAnnotationTimer  = 0.f;
                lastFailedTrace      = {};
            }
        }
    }
    if (CheckCollisionPointRec(screenMouse, LevelSelectSandboxBtnRect()))
        goSandbox();
} else {
    // ... existing canvas / panel logic — unchanged ...
```

- [ ] **Step 7: Add MENU / SANDBOX button click handlers inside the outer else block**

At the very start of the big `else` block, BEFORE the existing `if (contextMenu.visible)`, insert HUD navigation checks and convert `if (contextMenu.visible)` to `else if`:

```cpp
} else {
    // HUD navigation buttons — checked before canvas interactions
    if (gameMode == GAME_SANDBOX &&
        CheckCollisionPointRec(screenMouse, SandboxMenuBtnRect())) {
        gameMode = GAME_LEVEL_SELECT;
    } else if (gameMode == GAME_PLAYING &&
               CheckCollisionPointRec(screenMouse, LevelHudMenuBtnRect())) {
        gameMode = GAME_LEVEL_SELECT;
    } else if (gameMode == GAME_PLAYING &&
               CheckCollisionPointRec(screenMouse, LevelHudSandboxBtnRect())) {
        goSandbox();
    } else if (contextMenu.visible) {
        // ... existing contextMenu.visible block unchanged ...
```

(The `if (contextMenu.visible)` that was previously the first statement in the `else` block becomes `else if (contextMenu.visible)`.)

- [ ] **Step 8: Replace the level-HUD / win-overlay draw block**

Locate and replace the section starting with `// Level HUD badge (top-left) and win overlay`:

```cpp
// BEFORE:
// Level HUD badge (top-left) and win overlay
if (gameMode == GAME_PLAYING || gameMode == GAME_WIN) {
    DrawLevelHUD(currentLevel, activeLevelDef.title,
                 lastConditionsPassed,
                 (int)activeLevelDef.winConditions.size(),
                 starsEarned);
    if (troubleshootMode) {
        DrawRectangle(8, 34, 148, 18, Color{239, 68, 68, 200});
        DrawRectangleLinesEx({8, 34, 148, 18}, 1.0f, Color{239, 68, 68, 255});
        DrawText("TROUBLESHOOT [T]", 14, 38, 9, WHITE);
    }
}
if (gameMode == GAME_WIN) {
    DrawWinOverlay(activeLevelDef, currentLevel < 16, starsEarned);
}

// AFTER:
// Sandbox badge (only in sandbox mode)
if (gameMode == GAME_SANDBOX)
    DrawSandboxHUD();

// Level HUD badge + MENU / SANDBOX buttons (only while actively playing)
if (gameMode == GAME_PLAYING || gameMode == GAME_WIN) {
    DrawLevelHUD(currentLevel, activeLevelDef.title,
                 lastConditionsPassed,
                 (int)activeLevelDef.winConditions.size(),
                 starsEarned);
    if (gameMode == GAME_PLAYING) {
        // MENU button
        {
            Rectangle mb = LevelHudMenuBtnRect();
            DrawRectangle((int)mb.x,(int)mb.y,(int)mb.width,(int)mb.height,
                          Color{30,41,59,210});
            DrawRectangleLinesEx(mb, 1.0f, Color{51,65,85,255});
            int tw = MeasureText("MENU",10);
            DrawText("MENU",(int)(mb.x+(mb.width-tw)/2.f),(int)(mb.y+6),
                     10, Color{148,163,184,255});
        }
        // SANDBOX shortcut button
        {
            Rectangle sb = LevelHudSandboxBtnRect();
            DrawRectangle((int)sb.x,(int)sb.y,(int)sb.width,(int)sb.height,
                          Color{13,94,88,180});
            DrawRectangleLinesEx(sb, 1.0f, Color{13,148,136,255});
            int tw = MeasureText("SANDBOX",10);
            DrawText("SANDBOX",(int)(sb.x+(sb.width-tw)/2.f),(int)(sb.y+6),
                     10, Color{204,251,241,255});
        }
        if (troubleshootMode) {
            DrawRectangle(8, 34, 148, 18, Color{239, 68, 68, 200});
            DrawRectangleLinesEx({8, 34, 148, 18}, 1.0f, Color{239, 68, 68, 255});
            DrawText("TROUBLESHOOT [T]", 14, 38, 9, WHITE);
        }
    }
}
if (gameMode == GAME_WIN)
    DrawWinOverlay(activeLevelDef, currentLevel < 16, starsEarned);

// Level-select overlay (drawn last so it sits above everything)
if (gameMode == GAME_LEVEL_SELECT)
    DrawLevelSelectScreen(levelTitles);
```

- [ ] **Step 9: Update hint text**

```cpp
// BEFORE:
DrawText("P=PC  R=Router  S=Switch  Del=Delete  MMB=Pan  Scroll=Zoom  "
         "Drag-port=Cable  Esc=Cancel  1-9,0=Level",
         10, CANVAS_H() - 24, 10, Color{100, 116, 139, 255});

// AFTER:
DrawText("P=PC  R=Router  S=Switch  Del=Delete  MMB=Pan  Scroll=Zoom  "
         "Drag-port=Cable  Esc=Cancel  1-9=Level  0=Sandbox  M=Menu",
         10, CANVAS_H() - 24, 10, Color{100, 116, 139, 255});
```

- [ ] **Step 10: Build — verify it compiles cleanly**

```bash
make 2>&1 | grep -E "error:|warning:"
```

Expected: zero errors.

- [ ] **Step 11: Smoke test**

Launch the game and verify each flow manually:

| Action | Expected |
|--------|----------|
| Launch | Level-select overlay opens over dimmed canvas |
| Hover over any card | Card highlights blue |
| Click Level 1 card | Level 1 loads; level badge + MENU + SANDBOX buttons appear |
| Click MENU | Level-select screen reopens |
| Click Sandbox card | Teal SANDBOX badge + MENU button; single PC on empty canvas |
| Click MENU (in sandbox) | Level-select reopens |
| Click Level 16 | NAT level loads; verify level badge shows "Level 16 — NAT: Internet Access" |
| Click SANDBOX button (level HUD) | Clears to sandbox |
| Key `1` | Level 1 loads |
| Key `9` | Level 9 loads |
| Key `0` | Sandbox (teal badge, empty canvas) |
| Key `M` | Level-select screen |
| ESC (during level select, with active level) | Returns to GAME_PLAYING |
| ESC (during level select, no active level) | Returns to GAME_SANDBOX |
| Win a level → GAME_WIN | Win overlay shown; no MENU/SANDBOX buttons visible |
| Ctrl+S in sandbox | Save dialog opens and works |
| Packet sim in all levels | Trace modal opens; ACL/NAT badges shown correctly |
| All 11 config tabs | Config, Routes, ARP, OSPF, MPLS, BGP, VLAN, Sub, VXLAN, ACL, NAT all functional |

- [ ] **Step 12: Commit**

```bash
git add src/main.cpp
git commit -m "feat: wire level-select, sandbox HUD, key 0 → sandbox, key M → menu"
```

---

## Self-Review

**Spec coverage:**

| Requirement | Task / Step |
|---|---|
| Dedicated Sandbox mode — no win conditions, no star ratings | Task 4 — GAME_SANDBOX path has no win-check logic |
| Key `0` → sandbox | Task 4 Step 3 — `if (IsKeyPressed(KEY_ZERO)) goSandbox()` |
| "Sandbox" button in level HUD | Task 4 Steps 7–8 — `LevelHudSandboxBtnRect` drawn + handled |
| "SANDBOX" badge in HUD | Tasks 2, 4 Step 8 — `DrawSandboxHUD` with teal rectangle |
| MENU button in HUD (sandbox mode) | Tasks 2, 4 Steps 7–8 — `SandboxMenuBtnRect` |
| MENU button in HUD (level mode) | Task 4 Steps 7–8 — `LevelHudMenuBtnRect` |
| Level-select screen showing all 16 levels | Task 3 — 4×4 grid with all 16 cards |
| Level-select accessible via key M | Task 4 Step 3 |
| Sandbox card in level-select | Task 3 — teal card below grid; Task 4 Step 6 handles click |
| Startup → level-select | Task 4 Step 2 |
| Block canvas input during level-select | Task 4 Step 4 — `gameMode != GAME_LEVEL_SELECT` guard |
| ESC from level-select navigates back | Task 4 Step 5 |
| All existing features intact | All tasks — no changes to simulation, protocol engines, or config panel |

**Placeholder scan:** None — every step contains complete, runnable code.

**Type consistency:**
- `goSandbox` lambda captured by reference; used in Steps 3 (key 0), 6 (sandbox card), and 7 (SANDBOX button) — identical callsite, consistent.
- `LevelSelectCardRect(int i)` declared in Task 1, implemented in Task 2, drawn in Task 3, hit-tested in Task 4 Step 6 — parameter type and usage consistent throughout.
- `const std::string* levelTitles` in `DrawLevelSelectScreen` accepts `std::string levelTitles[16]` from `main.cpp` via array-to-pointer decay — valid C++.
- `GAME_LEVEL_SELECT` added to enum in Task 1; referenced in Tasks 4 Steps 2, 3, 4, 5, 6, 7, 8 — all consistent.
- `LevelHudMenuBtnRect()` and `LevelHudSandboxBtnRect()` declared in Task 1, implemented in Task 2, drawn in Task 4 Step 8, hit-tested in Task 4 Step 7 — consistent.
