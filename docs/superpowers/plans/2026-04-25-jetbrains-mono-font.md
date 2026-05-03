# JetBrains Mono + UI Scale Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace raylib's default bitmap font with JetBrains Mono Regular and add a UI Scale preset (1x / 1.25x / 1.5x / 2x) in the pause menu.

**Architecture:** A new `Font.h` / `Font.cpp` pair owns the global font state and exposes `GFont()`, `FS(base)`, `Sp(fs)`, and `TW(text, base)`. All `DrawText` → `DrawTextEx` and `MeasureText` → `(int)TW(...)` conversions are mechanical find-and-replace across four files. The menu scale buttons live entirely in `GameUI.h`/`GameUI.cpp`; `main.cpp` wires init/teardown and the scale-click handler.

**Tech Stack:** C++17, raylib 5.5 (`LoadFontEx`, `DrawTextEx`, `MeasureTextEx`), JetBrains Mono Regular TTF (already placed at `assets/fonts/JetBrainsMono-Regular.ttf`).

---

## Parallelisation map

```text
[pre-done] fonts in assets/fonts/
Task 1 ──────────────────── Font.h + Font.cpp (font system)
Task 2 ──────────────────── GameUI scale buttons (GameUI.h + GameUI.cpp only)
         ↓ (after Task 1)
Task 3 ─ NetworkCanvas.cpp  (200 call sites — parallel)
Task 4 ─ GameUI.cpp         ( 56 call sites — parallel)
Task 5 ─ TraceModal.cpp     ( 17 call sites — parallel)
         ↓ (after Tasks 1-5)
Task 6 ─ main.cpp wire-up + compile + commit + push
```

Tasks 1 and 2 are **independent — dispatch in parallel**.
Tasks 3, 4, 5 **all depend on Task 1** and are **independent of each other — dispatch in parallel**.
Task 6 **depends on all prior tasks**.

---

## Conversion reference (read this before any conversion task)

Every `DrawText` call becomes `DrawTextEx`.
Every `MeasureText` call becomes `(int)TW(...)` (or `TW(...)` when the result is used as float).

```cpp
// ── DrawText ─────────────────────────────────────────────────────────────
// BEFORE:
DrawText("text", x, y, 10, color);
// AFTER:
DrawTextEx(GFont(), "text", {(float)(x), (float)(y)}, FS(10), Sp(FS(10)), color);

// ── MeasureText (result stored as int) ───────────────────────────────────
// BEFORE:
int tw = MeasureText("text", 10);
// AFTER:
int tw = (int)TW("text", 10);

// ── MeasureText (result used inline in expression) ────────────────────────
// BEFORE:
if (MeasureText(str.c_str(), 12) > maxW)
// AFTER:
if ((int)TW(str.c_str(), 12) > maxW)

// ── MeasureText on variable with cast ────────────────────────────────────
// BEFORE:
int lw = MeasureText(lbl, 8);
DrawText(lbl, (int)(pp.x - lw * 0.5f), y, 8, col);
// AFTER:
int lw = (int)TW(lbl, 8);
DrawTextEx(GFont(), lbl, {(float)(int)(pp.x - lw * 0.5f), (float)y}, FS(8), Sp(FS(8)), col);
```

**Required `#include` at top of every converted file:**

```cpp
#include "Font.h"
```

**Font sizes used in the codebase:** 8, 9, 10, 11, 12, 13, 14 (`NODE_FONT_SZ`), 16, 18, 20.
`NODE_FONT_SZ` is an `int` constant — `FS(NODE_FONT_SZ)` compiles fine (implicit int→float).

---

## Task 1: Font system — `Font.h` + `Font.cpp`

**Files:**

- Create: `src/Font.h`
- Create: `src/Font.cpp`
- (Makefile uses `$(wildcard src/*.cpp)` — no Makefile change needed)

- [ ] **Step 1: Write `src/Font.h`**

```cpp
#pragma once
#include "raylib.h"

// Call once after InitWindow().
void  InitGameFont();

// Call once before CloseWindow().
void  UnloadGameFont();

// Change UI scale without reloading the font.
// Valid values: 1.0f, 1.25f, 1.5f, 2.0f
void  SetUiScale(float scale);
float GetUiScale();

// Returns the loaded JetBrains Mono Regular font.
Font& GFont();

// Scaled font size: base * uiScale.
// Pass as fontSize to DrawTextEx / MeasureTextEx.
float FS(float base);

// Letter spacing for DrawTextEx (fixed at 1.0f for monospace).
float Sp(float fs);

// Convenience: measure text width in pixels at scaled size.
float TW(const char* text, float base);
```

- [ ] **Step 2: Write `src/Font.cpp`**

```cpp
#include "Font.h"
#include <cmath>

static Font  gFont   = {};
static float gScale  = 1.0f;
static bool  gLoaded = false;

void InitGameFont() {
    // Codepoint set: printable ASCII + unicode symbols used in the codebase
    int codepoints[120];
    int count = 0;
    for (int i = 32; i <= 126; i++) codepoints[count++] = i;
    // ✓ ✗ → — • … ★ ☆
    int extras[] = {0x2713, 0x2717, 0x2192, 0x2014,
                    0x2022, 0x2026, 0x2605, 0x2606};
    for (int e : extras) codepoints[count++] = e;

    // Load at 64px — all draw sizes (max ~40px at 2x) are downscales → crisp
    gFont   = LoadFontEx("assets/fonts/JetBrainsMono-Regular.ttf",
                         64, codepoints, count);
    SetTextureFilter(gFont.texture, TEXTURE_FILTER_BILINEAR);
    gLoaded = true;
}

void UnloadGameFont() {
    if (gLoaded) { UnloadFont(gFont); gLoaded = false; }
}

void  SetUiScale(float scale) { gScale = scale; }
float GetUiScale()             { return gScale; }
Font& GFont()                  { return gFont; }
float FS(float base)           { return base * gScale; }
float Sp(float)                { return 1.0f; }
float TW(const char* text, float base) {
    return MeasureTextEx(gFont, text, FS(base), Sp(FS(base))).x;
}
```

- [ ] **Step 3: Compile to verify Font.cpp builds in isolation**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
make 2>&1 | head -20
```

Expected: compiles successfully (Font.cpp picked up by wildcard). If there are errors in Font.cpp, fix them.

- [ ] **Step 4: Commit**

```bash
git add src/Font.h src/Font.cpp assets/fonts/
git commit -m "feat: add Font.h/cpp — JetBrains Mono loader with unicode codepoints and UI scale"
```

---

## Task 2: Menu scale buttons — `GameUI.h` + `GameUI.cpp`

**Files:**

- Modify: `src/GameUI.h` — add `uiScale` to `GameMenuState`; add `uiScaleBtns[4]` + `uiScaleLabelY` to `GameMenuLayout`
- Modify: `src/GameUI.cpp` — update `ComputeGameMenuLayout` and `DrawGameMenu`

> **Do NOT touch `main.cpp` in this task** — click handling is wired in Task 6.

- [ ] **Step 1: Add `uiScale` to `GameMenuState` in `src/GameUI.h`**

Locate the struct (currently ends with `float volume = 1.0f;`) and add one line:

```cpp
struct GameMenuState {
    std::vector<std::pair<int,int>> resolutions;
    int   resIdx     = 0;
    bool  fullscreen = false;
    bool  showFps    = true;
    bool  soundOn    = true;
    float volume     = 1.0f;
    float uiScale    = 1.0f;   // ← add this line
};
```

- [ ] **Step 2: Add scale layout fields to `GameMenuLayout` in `src/GameUI.h`**

Locate the struct. Add after the existing `showFps` rectangle:

```cpp
struct GameMenuLayout {
    Rectangle card;
    Rectangle resume;
    Rectangle levelSelect;
    Rectangle resBtns[7];
    int       numRes = 0;
    Rectangle fullscreen;
    Rectangle showFps;
    Rectangle uiScaleBtns[4];   // ← add
    float     uiScaleLabelY;    // ← add
    Rectangle mute;
    Rectangle volDown;
    Rectangle volUp;
    Rectangle save;
    Rectangle load;
    Rectangle restart;
    Rectangle quit;
    float     displayLabelY;
    float     audioLabelY;
    float     fileLabelY;
    float     quitDividerY;
};
```

- [ ] **Step 3: Update height calculation in `ComputeGameMenuLayout` (`src/GameUI.cpp`)**

Find the height-accumulation block. After the line that adds the resolution grid height (the `if (numRes > 0)` block) and **before** the `Fullscreen` line, insert:

```cpp
    h += 12.f + 7.f;    // UI Scale label + gap
    h += RBH + 8.f;     // 4 scale buttons row + gap
```

The surrounding context for reference:

```cpp
    if (numRes > 0)
        h += numResRows * (RBH + 4.f) - 4.f + 8.f;         // resolution grid
    h += 12.f + 7.f;    // UI Scale label + gap              ← INSERT
    h += RBH + 8.f;     // 4 scale buttons row + gap         ← INSERT
    h += BH   + 8.f;                                         // Fullscreen
    h += BH   + 14.f;                                        // Show FPS
```

- [ ] **Step 4: Add scale button rects in `ComputeGameMenuLayout`**

Find the block that sets `L.fullscreen` and `L.showFps`. **Before** those lines, insert the UI Scale section:

```cpp
    L.uiScaleLabelY = y;
    y += 19.f;   // label height + gap

    float sbw = (IW - 3.f * 4.f) / 4.f;   // 4 buttons with 4px gaps
    for (int i = 0; i < 4; ++i)
        L.uiScaleBtns[i] = {x0 + i * (sbw + 4.f), y, sbw, RBH};
    y += RBH + 8.f;

    L.fullscreen = {x0, y, IW, BH};  y += BH + 8.f;
    L.showFps    = {x0, y, IW, BH};  y += BH + 14.f;
```

Make sure to **remove or comment out** the original `L.fullscreen` and `L.showFps` lines that already exist below (they will be duplicated otherwise).

- [ ] **Step 5: Draw UI Scale section in `DrawGameMenu` (`src/GameUI.cpp`)**

Find the block that draws the resolution buttons (the `for (int i = 0; i < L.numRes; ++i)` loop). **After** that loop and **before** the fullscreen toggle draw, insert:

```cpp
    // UI Scale
    drawSecLabel(C.x + 12.f, L.uiScaleLabelY, "UI SCALE");
    const float scaleVals[4]   = {1.0f, 1.25f, 1.5f, 2.0f};
    const char* scaleLabels[4] = {"1x", "1.25x", "1.5x", "2x"};
    for (int i = 0; i < 4; ++i) {
        bool active = (std::fabs(s.uiScale - scaleVals[i]) < 0.01f);
        drawToggle(L.uiScaleBtns[i], scaleLabels[i], active,
                   Color{30,58,138,255}, Color{59,130,246,255});
    }
```

Add `#include <cmath>` at the top of `GameUI.cpp` if not already present (needed for `std::fabs`).

- [ ] **Step 6: Compile**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
make 2>&1
```

Expected: clean compile, zero warnings. Fix any issues before committing.

- [ ] **Step 7: Commit**

```bash
git add src/GameUI.h src/GameUI.cpp
git commit -m "feat: add UI Scale presets (1x/1.25x/1.5x/2x) to pause menu layout"
```

---

## Task 3: Convert `NetworkCanvas.cpp` (200 call sites)

**Files:**

- Modify: `src/NetworkCanvas.cpp`

> **Depends on Task 1 being committed.** Verify `src/Font.h` exists before starting.

- [ ] **Step 1: Add `Font.h` include**

At the top of `src/NetworkCanvas.cpp`, after the existing includes, add:

```cpp
#include "Font.h"
```

- [ ] **Step 2: Convert all `MeasureText` calls**

Work through the file top to bottom. Every occurrence of:

```cpp
MeasureText(expr, size)
```

becomes:

```cpp
(int)TW(expr, size)
```

When the result is stored in a `float` or used in float arithmetic, omit the `(int)` cast:

```cpp
float lw = TW(expr, size);
```

- [ ] **Step 3: Convert all `DrawText` calls**

Every occurrence of:

```cpp
DrawText(text, x, y, size, color);
```

becomes:

```cpp
DrawTextEx(GFont(), text, {(float)(x), (float)(y)}, FS(size), Sp(FS(size)), color);
```

Where `x` and `y` are already `int` expressions like `(int)(n.position.x - tw/2.0f)`, wrap them:

```cpp
DrawTextEx(GFont(), text,
           {(float)(int)(n.position.x - tw / 2.0f),
            (float)(int)(n.position.y - FS(NODE_FONT_SZ) / 2.0f)},
           FS(NODE_FONT_SZ), Sp(FS(NODE_FONT_SZ)), WHITE);
```

Special attention — `NODE_FONT_SZ` usages (3 call sites near top of file):

```cpp
// BEFORE:
int tw = MeasureText(n.label.c_str(), NODE_FONT_SZ);
DrawText(n.label.c_str(),
         (int)(n.position.x - tw / 2.0f),
         (int)(n.position.y - NODE_FONT_SZ / 2.0f),
         NODE_FONT_SZ, WHITE);
// AFTER:
int tw = (int)TW(n.label.c_str(), NODE_FONT_SZ);
DrawTextEx(GFont(), n.label.c_str(),
           {(float)(int)(n.position.x - tw / 2.0f),
            (float)(int)(n.position.y - FS(NODE_FONT_SZ) / 2.0f)},
           FS(NODE_FONT_SZ), Sp(FS(NODE_FONT_SZ)), WHITE);
```

The em dash buffer pattern (two locations ~line 1068, 1082):

```cpp
// BEFORE (writes raw UTF-8 em dash into pbuf then passes to DrawText):
else { pbuf[0]='\xe2'; pbuf[1]='\x80'; pbuf[2]='\x94'; pbuf[3]='\0'; }
DrawText(pbuf, x, y, 10, color);
// AFTER: leave pbuf unchanged; just convert the DrawText:
DrawTextEx(GFont(), pbuf, {(float)(x), (float)(y)}, FS(10), Sp(FS(10)), color);
```

- [ ] **Step 4: Compile**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
make 2>&1
```

Expected: clean compile, zero warnings. Fix any errors before committing.

- [ ] **Step 5: Commit**

```bash
git add src/NetworkCanvas.cpp
git commit -m "refactor: convert NetworkCanvas.cpp DrawText→DrawTextEx (200 sites)"
```

---

## Task 4: Convert `GameUI.cpp` (56 call sites)

**Files:**

- Modify: `src/GameUI.cpp`

> **Depends on Task 1 being committed.** Verify `src/Font.h` exists before starting.

- [ ] **Step 1: Add `Font.h` include**

At the top of `src/GameUI.cpp`, after the existing includes, add:

```cpp
#include "Font.h"
```

- [ ] **Step 2: Convert all `MeasureText` calls**

Every `MeasureText(expr, size)` → `(int)TW(expr, size)` (use plain `TW(...)` when the result is assigned to `float`).

- [ ] **Step 3: Convert all `DrawText` calls**

Every `DrawText(text, x, y, size, color)` →
`DrawTextEx(GFont(), text, {(float)(x), (float)(y)}, FS(size), Sp(FS(size)), color)`.

Examples from this file:

```cpp
// HUD badge text:
// BEFORE: DrawText(buf, 14, 13, 10, Color{148,163,184,255});
// AFTER:
DrawTextEx(GFont(), buf, {14.f, 13.f}, FS(10), Sp(FS(10)), Color{148,163,184,255});

// Win overlay header:
// BEFORE:
int tw = MeasureText(done, 20);
DrawText(done, (int)(r.x + (r.width - tw) / 2.0f), (int)r.y + 20, 20, WHITE);
// AFTER:
int tw = (int)TW(done, 20);
DrawTextEx(GFont(), done,
           {(float)(int)(r.x + (r.width - tw) / 2.0f), r.y + 20.f},
           FS(20), Sp(FS(20)), WHITE);

// Menu drawBtn lambda — font size 10:
// BEFORE: DrawText(label, tx, ty, 10, tc);
// AFTER:
DrawTextEx(GFont(), label, {(float)tx, (float)ty}, FS(10), Sp(FS(10)), tc);
```

The `drawBtn` and `drawToggle` lambdas inside `DrawGameMenu` also use `MeasureText`/`DrawText` — convert those too.

- [ ] **Step 4: Compile**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
make 2>&1
```

Expected: clean compile, zero warnings.

- [ ] **Step 5: Commit**

```bash
git add src/GameUI.cpp
git commit -m "refactor: convert GameUI.cpp DrawText→DrawTextEx (56 sites)"
```

---

## Task 5: Convert `TraceModal.cpp` (17 call sites)

**Files:**

- Modify: `src/TraceModal.cpp`

> **Depends on Task 1 being committed.** Verify `src/Font.h` exists before starting.

- [ ] **Step 1: Add `Font.h` include**

```cpp
#include "Font.h"
```

- [ ] **Step 2: Convert all `MeasureText` calls**

`MeasureText(expr, size)` → `(int)TW(expr, size)`.

- [ ] **Step 3: Convert all `DrawText` calls**

`DrawText(text, x, y, size, color)` →
`DrawTextEx(GFont(), text, {(float)(x), (float)(y)}, FS(size), Sp(FS(size)), color)`.

Notable: the success/fail icon uses unicode `"\xe2\x9c\x93"` (✓) and `"\xe2\x9c\x97"` (✗) — these are included in the codepoint list loaded by `InitGameFont()`, so they will render correctly. No change needed to the icon strings themselves.

- [ ] **Step 4: Compile**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
make 2>&1
```

- [ ] **Step 5: Commit**

```bash
git add src/TraceModal.cpp
git commit -m "refactor: convert TraceModal.cpp DrawText→DrawTextEx (17 sites)"
```

---

## Task 6: Wire `main.cpp` + final compile + commit + push

**Files:**

- Modify: `src/main.cpp`

> **Depends on ALL prior tasks being committed** (Font.h exists; GameUI scale buttons exist; all other files converted).

- [ ] **Step 1: Add `Font.h` include to `main.cpp`**

Near the top with other includes:

```cpp
#include "Font.h"
```

- [ ] **Step 2: Call `InitGameFont()` at startup**

Find the line `SetExitKey(0);` (right after `SetWindowState`). Add `InitGameFont()` immediately after:

```cpp
    InitWindow(1280, 720, "Packet Path");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetExitKey(0);
    InitGameFont();                    // ← add
```

- [ ] **Step 3: Call `UnloadGameFont()` before close**

Find the end of `main()`. The while loop ends and `CloseWindow()` is called. Add `UnloadGameFont()` before it:

```cpp
    UnloadGameFont();                  // ← add
    CloseWindow();
    return 0;
```

- [ ] **Step 4: Wire the UI Scale click handler in the menu section**

Find the menu click handler block (inside `menuVisible` LMB-press section). Locate the `else` branch that handles resolution buttons (a `for` loop over `ML.resBtns`). **After that loop**, add the scale button handler:

```cpp
                // Resolution buttons
                for (int i = 0; i < ML.numRes && i < 7; ++i) {
                    if (CheckCollisionPointRec(screenMouse, ML.resBtns[i])) {
                        gameSettings.resIdx = i;
                        if (!gameSettings.fullscreen) {
                            int rw = gameSettings.resolutions[i].first;
                            int rh = gameSettings.resolutions[i].second;
                            SetWindowSize(rw, rh);
                        }
                        break;
                    }
                }
                // UI Scale buttons                               ← add below
                const float scaleVals[4] = {1.0f, 1.25f, 1.5f, 2.0f};
                for (int i = 0; i < 4; ++i) {
                    if (CheckCollisionPointRec(screenMouse, ML.uiScaleBtns[i])) {
                        gameSettings.uiScale = scaleVals[i];
                        SetUiScale(scaleVals[i]);
                        break;
                    }
                }
```

- [ ] **Step 5: Replace `DrawFPS` with custom version**

Find:

```cpp
            if (gameSettings.showFps) DrawFPS(CANVAS_W() - 80, 10);
```

Replace with:

```cpp
            if (gameSettings.showFps) {
                char fpsBuf[16];
                std::snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %d", GetFPS());
                float fpsW = TW(fpsBuf, 10);
                DrawTextEx(GFont(), fpsBuf,
                           {(float)(CANVAS_W()) - fpsW - 8.f, 8.f},
                           FS(10), Sp(FS(10)), LIME);
            }
```

- [ ] **Step 6: Convert remaining `DrawText` / `MeasureText` calls in `main.cpp`**

There are 8 `DrawText` calls remaining in main.cpp. Apply the standard conversion to each:

```cpp
// Hint text (bottom-left of canvas):
// BEFORE:
DrawText("H = Help  \xe2\x80\xa2  M = Menu", 10, CANVAS_H() - 20, 9, Color{71,85,105,255});
// AFTER:
DrawTextEx(GFont(), "H = Help  \xe2\x80\xa2  M = Menu",
           {10.f, (float)(CANVAS_H() - 20)}, FS(9), Sp(FS(9)),
           Color{71,85,105,255});
```

Apply the same pattern to every remaining `DrawText` / `MeasureText` in the file.

- [ ] **Step 7: Compile**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
make 2>&1
```

Expected: clean compile, zero warnings. Fix any errors before proceeding.

- [ ] **Step 8: Smoke test**

Launch the game:

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
./packet-path
```

Verify:

- [ ] Level select screen renders text in JetBrains Mono (smooth, not pixelated)
- [ ] Pause menu (M key) shows UI SCALE section with 1x / 1.25x / 1.5x / 2x buttons
- [ ] Clicking 1.5x scales all text up; clicking 1x returns to normal
- [ ] Unicode symbols render: ✓ ✗ → — • … in trace modal and log
- [ ] FPS counter uses new font (top-right, green "FPS: XX")
- [ ] No text clipping or obvious layout breaks at 1x and 1.25x

- [ ] **Step 9: Commit**

```bash
git add src/main.cpp
git commit -m "feat: wire JetBrains Mono + UI scale into main — InitGameFont, SetUiScale handler, custom FPS"
```

- [ ] **Step 10: Push**

```bash
git push
```

---

## Self-review checklist

- [x] Font.h/cpp: InitGameFont, UnloadGameFont, SetUiScale, GFont, FS, Sp, TW all defined
- [x] All 281 DrawText/MeasureText call sites covered (200 NetworkCanvas + 56 GameUI + 17 TraceModal + 8 main)
- [x] GameMenuState.uiScale added
- [x] GameMenuLayout.uiScaleBtns[4] + uiScaleLabelY added
- [x] ComputeGameMenuLayout height updated for scale row
- [x] DrawGameMenu draws scale toggle buttons
- [x] main.cpp click handler sets gameSettings.uiScale + SetUiScale()
- [x] InitGameFont() called after InitWindow()
- [x] UnloadGameFont() called before CloseWindow()
- [x] DrawFPS replaced with custom DrawTextEx version
- [x] Unicode codepoints (✓ ✗ → — • … ★ ☆) included in font load
- [x] Font files committed to assets/fonts/
