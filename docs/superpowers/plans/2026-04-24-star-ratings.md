# Star Ratings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 1–3 star ratings to level completion, computed from the number of failed simulation attempts before the player wins.

**Architecture:** A single `failedAttempts` counter (reset on level load, incremented on each failed `SimulateForward` call) feeds `ComputeStars()` at win time, producing `starsEarned` (1–3). This value is passed to the GameUI draw functions, which render filled gold / outlined grey circles in both the win overlay and the HUD badge. No JSON changes — scoring is universal.

**Scoring rules:**

- **3 stars (PERFECT!)** — 0 failed simulations. Configured correctly first try.
- **2 stars (GREAT!)** — 1–2 failed simulations. Minor troubleshooting needed.
- **1 star (CLEARED!)** — 3+ failed simulations. Got there eventually.

**Tech Stack:** C++17, raylib 5.5, existing Level.h / GameUI.h / main.cpp patterns.

---

## File Map

| Action | File | Responsibility |
| -------- | ------ | --------------- |
| Modify | `src/Level.h` | Declare `ComputeStars(int failedAttempts)` |
| Modify | `src/Level.cpp` | Implement `ComputeStars()` |
| Modify | `src/GameUI.h` | Add `int starsEarned = 0` default param to `DrawWinOverlay` and `DrawLevelHUD` |
| Modify | `src/GameUI.cpp` | Win overlay: earned/unearned circles + score label + layout 240→260px; HUD badge: 224→240px + star dots |
| Modify | `src/main.cpp` | Add `failedAttempts` / `starsEarned` state; reset on load; increment on fail; compute on win; pass to draw functions |

---

## Task 1: ComputeStars() Pure Function

**Files:**

- Modify: `src/Level.h`
- Modify: `src/Level.cpp`

- [ ] **Step 1: Add declaration to Level.h**

Add after `CheckWinConditions`:

```cpp
// Returns 1-3 stars based on failed simulations before winning.
// 0 failures → 3, 1-2 → 2, 3+ → 1.
int ComputeStars(int failedAttempts);
```

Full updated tail of `src/Level.h`:

```cpp
int CheckWinConditions(const LevelDef& def,
                       const std::vector<DeviceNode>& nodes,
                       const std::vector<Cable>& cables);

int ComputeStars(int failedAttempts);
```

- [ ] **Step 2: Implement in Level.cpp**

Add at the bottom of `src/Level.cpp`, after `CheckWinConditions`:

```cpp
int ComputeStars(int failedAttempts) {
    if (failedAttempts == 0) return 3;
    if (failedAttempts <= 2) return 2;
    return 1;
}
```

- [ ] **Step 3: Build to verify clean**

```bash
make 2>&1
```

Expected: clean compile, `Nothing to be done for 'all'` or recompile of Level.cpp only with no errors.

- [ ] **Step 4: Commit**

```bash
git add src/Level.h src/Level.cpp
git commit -m "feat: add ComputeStars() — 0 fails=3★, 1-2=2★, 3+=1★"
```

---

## Task 2: Win Overlay Visual Upgrade

**Files:**

- Modify: `src/GameUI.h`
- Modify: `src/GameUI.cpp`

Changes: add `int starsEarned = 0` default param (existing call sites need no update), grow overlay height 240→260px, replace 3 gold circles with earned/unearned logic, add score label, adjust button rect offsets.

- [ ] **Step 1: Update GameUI.h**

Replace the `DrawWinOverlay` declaration with:

```cpp
void DrawWinOverlay(const LevelDef& def, bool hasNextLevel, int starsEarned = 0);
```

- [ ] **Step 2: Update WinOverlayRect, WinRetryBtnRect, WinNextBtnRect in GameUI.cpp**

```cpp
Rectangle WinOverlayRect() {
    return {(float)(CANVAS_W - 320) / 2.0f,
            (float)(CANVAS_H - 260) / 2.0f,
            320.0f, 260.0f};
}

Rectangle WinRetryBtnRect() {
    Rectangle r = WinOverlayRect();
    return {r.x + 20, r.y + 206, 120.0f, 36.0f};
}

Rectangle WinNextBtnRect() {
    Rectangle r = WinOverlayRect();
    return {r.x + 180, r.y + 206, 120.0f, 36.0f};
}
```

- [ ] **Step 3: Replace DrawWinOverlay body in GameUI.cpp**

Replace the entire `DrawWinOverlay` function with:

```cpp
void DrawWinOverlay(const LevelDef& def, bool hasNextLevel, int starsEarned) {
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

    // Stars: filled gold = earned, grey ring = not yet earned
    float sy  = r.y + 90.0f;
    float scx = r.x + r.width / 2.0f;
    float offsets[3] = {-30.0f, 0.0f, 30.0f};
    for (int i = 0; i < 3; ++i) {
        int cx = (int)(scx + offsets[i]);
        int cy = (int)sy;
        if (i < starsEarned)
            DrawCircle(cx, cy, 11.0f, Color{234, 179, 8, 255});
        else
            DrawCircleLines(cx, cy, 11.0f, Color{71, 85, 105, 255});
    }

    // Score label beneath stars
    const char* scoreLabel = (starsEarned == 3) ? "PERFECT!"
                           : (starsEarned == 2) ? "GREAT!" : "CLEARED!";
    Color slColor = (starsEarned == 3) ? Color{234, 179, 8, 255}
                  : (starsEarned == 2) ? Color{148, 163, 184, 255}
                  :                      Color{100, 116, 139, 255};
    int slw = MeasureText(scoreLabel, 11);
    DrawText(scoreLabel, (int)(r.x + (r.width - slw) / 2.0f), (int)r.y + 110, 11, slColor);

    // Separator
    DrawLineEx({r.x + 16, r.y + 126}, {r.x + r.width - 16, r.y + 126},
               0.5f, Color{51, 65, 85, 255});

    // Win conditions checklist (capped at 3 rows to stay above buttons)
    int cy2 = (int)r.y + 134;
    int shownConditions = 0;
    for (const auto& wc : def.winConditions) {
        if (shownConditions >= 3) break;
        char lineBuf[128];
        std::snprintf(lineBuf, sizeof(lineBuf), "\xe2\x9c\x93 %s", wc.description.c_str());
        DrawText(lineBuf, (int)(r.x + 20), cy2, 11, Color{34, 197, 94, 255});
        cy2 += 18;
        ++shownConditions;
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

- [ ] **Step 4: Build to verify clean**

```bash
make 2>&1
```

Expected: clean compile. Existing `DrawWinOverlay(activeLevelDef, currentLevel < 10)` call in main.cpp uses the default `starsEarned = 0` and still compiles. All 3 circles will render as grey rings until Task 4 wires the real value.

- [ ] **Step 5: Commit**

```bash
git add src/GameUI.h src/GameUI.cpp
git commit -m "feat: win overlay with earned/unearned star circles and score label"
```

---

## Task 3: HUD Badge Star Dots

**Files:**

- Modify: `src/GameUI.h`
- Modify: `src/GameUI.cpp`

Changes: add `int starsEarned = 0` default param to `DrawLevelHUD`, widen badge 224→240px, replace progress counter with 3 small circles when a star rating is available.

- [ ] **Step 1: Update DrawLevelHUD declaration in GameUI.h**

```cpp
void DrawLevelHUD(int levelId, const std::string& title,
                  int conditionsPassed, int conditionsTotal, int starsEarned = 0);
```

- [ ] **Step 2: Replace DrawLevelHUD body in GameUI.cpp**

```cpp
void DrawLevelHUD(int levelId, const std::string& title,
                  int conditionsPassed, int conditionsTotal, int starsEarned) {
    // Badge: 240px wide (was 224) to accommodate 3 star dots on the right
    DrawRectangle(8, 8, 240, 22, Color{10, 15, 28, 210});
    DrawRectangleLinesEx({8, 8, 240, 22}, 1.0f, Color{51, 65, 85, 255});

    char buf[80];
    std::snprintf(buf, sizeof(buf), "LVL %d  %s", levelId, title.c_str());
    DrawText(buf, 14, 13, 10, Color{148, 163, 184, 255});

    if (starsEarned > 0) {
        // Three star dots right-aligned: x = 220, 232, 244 (radius 4, spacing 12)
        for (int i = 0; i < 3; ++i) {
            int cx = 220 + i * 12;
            if (i < starsEarned)
                DrawCircle(cx, 19, 4.0f, Color{234, 179, 8, 255});
            else
                DrawCircleLines(cx, 19, 4.0f, Color{71, 85, 105, 255});
        }
    } else {
        // Condition progress counter (right-aligned inside badge)
        char prog[8];
        std::snprintf(prog, sizeof(prog), "%d/%d", conditionsPassed, conditionsTotal);
        Color progColor = (conditionsPassed == conditionsTotal && conditionsTotal > 0)
                        ? Color{34, 197, 94, 255}
                        : Color{234, 179, 8, 255};
        int pw = MeasureText(prog, 10);
        DrawText(prog, 8 + 240 - pw - 8, 13, 10, progColor);
    }
}
```

**Layout notes:**

- Badge: x=8, width=240 → right edge at x=248
- Star circle centers: x=220, 232, 244 (left to right = star 1, 2, 3)
- Right edge of star 3: 244+4=248 — flush with badge right edge ✓
- Title text starts at x=14, has ~206px before stars begin — fits all level titles ✓

- [ ] **Step 3: Build to verify clean**

```bash
make 2>&1
```

Expected: clean compile. Existing `DrawLevelHUD(currentLevel, activeLevelDef.title, lastConditionsPassed, ...)` call still compiles via default arg. Badge shows progress counter until Task 4 wires the real `starsEarned`.

- [ ] **Step 4: Commit**

```bash
git add src/GameUI.h src/GameUI.cpp
git commit -m "feat: HUD badge 240px with star dots (gold=earned, grey=unearned)"
```

---

## Task 4: main.cpp — Tracking, Computation, and Wiring

**Files:**

- Modify: `src/main.cpp`

Changes: add `failedAttempts` and `starsEarned` state variables; reset both on every level load; increment `failedAttempts` on each failed simulation; compute `starsEarned = ComputeStars(failedAttempts)` on win; pass `starsEarned` to both draw functions.

- [ ] **Step 1: Add state variables after `lastConditionsPassed`**

Locate `int lastConditionsPassed = 0;` (line ~44) and add the two new variables immediately after:

```cpp
int         lastConditionsPassed = 0;
int         failedAttempts       = 0;   // failed sims this level; drives star rating
int         starsEarned          = 0;   // computed on win via ComputeStars()
```

- [ ] **Step 2: Reset both variables at every level load**

There are three ApplyLevel call sites. Each already resets `lastConditionsPassed = 0;` and `gameMode = GAME_PLAYING;`. Add the two resets immediately after `lastConditionsPassed = 0;` in all three blocks:

**Block 1** — key-press level load (inside the `for (int k = 1; k <= 10; ++k)` loop):

```cpp
lastConditionsPassed = 0;
failedAttempts       = 0;
starsEarned          = 0;
gameMode             = GAME_PLAYING;
```

**Block 2** — Retry button click:

```cpp
lastConditionsPassed = 0;
failedAttempts       = 0;
starsEarned          = 0;
gameMode             = GAME_PLAYING;
```

**Block 3** — Next Level button click:

```cpp
lastConditionsPassed = 0;
failedAttempts       = 0;
starsEarned          = 0;
gameMode             = GAME_PLAYING;
```

- [ ] **Step 3: Increment failedAttempts on each failed simulation**

Locate the block that handles `fr.success` / `fr.failure` (search for `PlayPacketFail()`):

```cpp
if (fr.success) {
    PlayPacketArrive();
    failAnnotationTimer = 0.f;
} else {
    PlayPacketFail();
    failAnnotationTimer = 5.0f;
    lastFailedTrace     = fr;
    ++failedAttempts;          // ← add this line
}
```

- [ ] **Step 4: Compute starsEarned when win condition is met**

Locate the `gameMode = GAME_WIN;` assignment (inside the `CheckWinConditions` block):

```cpp
if (passed == (int)activeLevelDef.winConditions.size()) {
    starsEarned = ComputeStars(failedAttempts);   // ← add this line
    gameMode    = GAME_WIN;
}
```

- [ ] **Step 5: Pass starsEarned to DrawLevelHUD**

Locate the `DrawLevelHUD` call (line ~952):

```cpp
DrawLevelHUD(currentLevel, activeLevelDef.title,
             lastConditionsPassed,
             (int)activeLevelDef.winConditions.size(),
             starsEarned);
```

- [ ] **Step 6: Pass starsEarned to DrawWinOverlay**

Locate the `DrawWinOverlay` call (line ~957):

```cpp
DrawWinOverlay(activeLevelDef, currentLevel < 10, starsEarned);
```

- [ ] **Step 7: Build to verify clean**

```bash
make 2>&1
```

Expected: clean compile, no warnings.

- [ ] **Step 8: Smoke test**

Launch `./packet-path`, press `1` for Level 1. Configure R1's interface IP and PC-A's destination IP. Send a packet that succeeds immediately (0 fails). Verify:

- Win overlay shows 3 gold circles and "PERFECT!"
- HUD badge shows 3 gold dots

Press Retry. Send one wrong packet (fails). Then configure correctly and win. Verify:

- Win overlay shows 2 gold circles + 1 grey ring and "GREAT!"
- HUD badge shows 2 gold + 1 grey

Load Level 10 (press `0`). Don't configure anything — send 3 failing packets. Then configure correctly (trunk + subinterfaces) and win. Verify:

- Win overlay shows 1 gold circle + 2 grey rings and "CLEARED!"
- HUD badge shows 1 gold + 2 grey

- [ ] **Step 9: Regression — Levels 1–10 all load and win overlays appear**

Quick pass: load each level with `1`–`0`, trigger the win condition, confirm overlay and HUD look correct. No visual glitches on any level.

- [ ] **Step 10: Commit**

```bash
git add src/main.cpp
git commit -m "feat: star rating — track failures, compute on win, wire to HUD and overlay"
```

---

## Self-Review

### Spec coverage

| Requirement | Task |
| ------------- | ------ |
| 3 stars = 0 failures, 2 = 1-2 failures, 1 = 3+ | Task 1 `ComputeStars()` |
| Scoring computed on win (not from JSON) | Task 1 + Task 4 |
| Win overlay: 3 stars + breakdown label | Task 2 `DrawWinOverlay` |
| HUD badge: star dots (★★☆ style) | Task 3 `DrawLevelHUD` |
| All levels 1–10 work with default scoring | All tasks — universal formula, no JSON needed |
| Existing call sites unaffected until Task 4 | Default params in Tasks 2–3 |

### Placeholder scan

No TBDs, no "add validation", no "similar to Task N" references. All code blocks are complete.

### Type consistency

- `ComputeStars(int failedAttempts) -> int` declared in Level.h, called in main.cpp ✓
- `DrawWinOverlay(const LevelDef& def, bool hasNextLevel, int starsEarned = 0)` — signature matches across .h and .cpp ✓
- `DrawLevelHUD(int, const std::string&, int, int, int starsEarned = 0)` — same ✓
- `starsEarned` is `int` throughout; `failedAttempts` is `int` throughout ✓

### Build ordering

Tasks 1 → 2 → 3 each build clean independently (default params protect existing call sites). Task 4 wires everything and is the final integration. No task leaves the build broken.
