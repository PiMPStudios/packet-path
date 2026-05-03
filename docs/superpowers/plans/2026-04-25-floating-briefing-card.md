# Floating Draggable Briefing Card — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the blocking mission-briefing modal with a floating, draggable, collapsible card that stays accessible throughout a level without interrupting play.

**Architecture:** A `BriefingCardState` struct (position, collapsed, drag state) lives in `main.cpp` alongside the existing `briefingVisible` flag. Click handling changes from "all clicks consumed while briefing open" to "clicks on the card are consumed; clicks elsewhere pass through." The card is drawn on top of the canvas but is only shown during `GAME_PLAYING` — never in `GAME_SANDBOX`.

**Tech Stack:** C++17, raylib 5.5, JetBrains Mono via Font.h (GFont/FS/Sp/TW).

---

## File Map

| File | Change |
| --- | --- |
| `src/GameUI.h` | Remove 3 old declarations; add `BriefingCardState` struct + 7 new function declarations |
| `src/GameUI.cpp` | Remove old 4 implementations; add new rendering + rect helpers (~70 lines net change) |
| `src/main.cpp` | Add `BriefingCardState`; update click, drag, release, ESC, B-key, HUD hint, draw call |

---

## Task 1: GameUI.h + GameUI.cpp — New Briefing Card API

**Files:**

- Modify: `src/GameUI.h:53-58`
- Modify: `src/GameUI.cpp:367-448`

### Step 1 — Update `src/GameUI.h`

Replace lines 53–58 (the old briefing card block):

```cpp
// ── Mission briefing card ─────────────────────────────────────────────────
// Auto-shown when a level loads. Dismiss via [×] or [Got it].
void      DrawBriefingCard(const LevelDef& def);
Rectangle BriefingCardRect();
Rectangle BriefingCloseBtnRect();
Rectangle BriefingGotItBtnRect();
```

With:

```cpp
// ── Mission briefing card ─────────────────────────────────────────────────
// Floating, draggable, collapsible. Only shown during GAME_PLAYING.
struct BriefingCardState {
    Vector2 pos       = {0.f, 82.f};   // top-left corner; init via BriefingDefaultPos()
    bool    collapsed = false;          // true = only title bar visible
    bool    dragging  = false;          // true while LMB held on title bar
    Vector2 dragOff   = {};             // mouse offset from pos when drag began
};

Vector2   BriefingDefaultPos();                              // canvas-centered starting pos
Rectangle BriefingCardBounds(Vector2 pos, bool collapsed);  // full card rect
Rectangle BriefingTitleBarRect(Vector2 pos);                // drag zone (full-width × 30 px)
Rectangle BriefingCollapseBtnRect(Vector2 pos);             // [−]/[+] toggle button
Rectangle BriefingCloseBtnRect(Vector2 pos);                // [×] hide-entirely button
Rectangle BriefingGotItBtnRect(Vector2 pos);                // "Got it" → collapses card
void      DrawBriefingCard(const LevelDef& def, const BriefingCardState& state);
```

- [ ] **Step 2 — Verify the edit compiles in isolation**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
make 2>&1 | head -30
```

Expected: compile errors about missing/changed functions in GameUI.cpp — that is correct; we haven't updated it yet. Errors about `BriefingCardRect` or `DrawBriefingCard` signature mismatch confirm the header change landed.

- [ ] **Step 3 — Replace the briefing card section in `src/GameUI.cpp`**

Replace lines 367–448 (everything from `// ── Mission briefing card` through the closing `}` of `DrawBriefingCard`) with:

```cpp
// ── Mission briefing card ─────────────────────────────────────────────────
static const float BC_W  = 520.f;   // card width
static const float BC_H  = 210.f;   // card height (expanded)
static const float BC_TH = 30.f;    // title bar height

Vector2 BriefingDefaultPos() {
    return {std::max(8.f, ((float)CANVAS_W() - BC_W) / 2.f), 82.f};
}

Rectangle BriefingCardBounds(Vector2 pos, bool collapsed) {
    return {pos.x, pos.y, BC_W, collapsed ? BC_TH : BC_H};
}

Rectangle BriefingTitleBarRect(Vector2 pos) {
    return {pos.x, pos.y, BC_W, BC_TH};
}

Rectangle BriefingCollapseBtnRect(Vector2 pos) {
    return {pos.x + BC_W - 50.f, pos.y + 5.f, 20.f, 20.f};
}

Rectangle BriefingCloseBtnRect(Vector2 pos) {
    return {pos.x + BC_W - 26.f, pos.y + 5.f, 20.f, 20.f};
}

Rectangle BriefingGotItBtnRect(Vector2 pos) {
    return {pos.x + BC_W - 92.f, pos.y + BC_H - 34.f, 82.f, 26.f};
}

void DrawBriefingCard(const LevelDef& def, const BriefingCardState& state) {
    Vector2   pos      = state.pos;
    bool      col      = state.collapsed;
    Rectangle bounds   = BriefingCardBounds(pos, col);
    Rectangle titleBar = BriefingTitleBarRect(pos);
    Rectangle colBtn   = BriefingCollapseBtnRect(pos);
    Rectangle closeBtn = BriefingCloseBtnRect(pos);
    Vector2   mouse    = GetMousePosition();

    // Expanded card body
    if (!col) {
        DrawRectangleRounded(bounds, 0.07f, 6, Color{10, 17, 35, 245});
        DrawRectangleRoundedLinesEx(bounds, 0.07f, 6, 1.5f, Color{59, 130, 246, 180});
    }

    // Title bar
    Color titleBg = Color{23, 47, 110, 240};
    if (col) {
        DrawRectangleRounded(titleBar, 0.07f, 6, titleBg);
        DrawRectangleRoundedLinesEx(titleBar, 0.07f, 6, 1.5f, Color{59, 130, 246, 180});
    } else {
        DrawRectangleRounded(titleBar, 0.07f, 6, titleBg);
    }

    // Drag indicator (three bullets on the left)
    DrawTextEx(GFont(), "\xe2\x80\xa2 \xe2\x80\xa2 \xe2\x80\xa2",
               {pos.x + 8.f, pos.y + 9.f},
               FS(8), Sp(FS(8)), Color{71, 85, 105, 200});

    // Title text
    const char* hdr = "MISSION BRIEFING";
    float hw = TW(hdr, 10);
    DrawTextEx(GFont(), hdr,
               {pos.x + (BC_W - hw) * 0.5f, pos.y + 9.f},
               FS(10), Sp(FS(10)), Color{147, 197, 253, 255});

    // [−]/[+] collapse toggle
    bool colHov = CheckCollisionPointRec(mouse, colBtn);
    DrawRectangleRounded(colBtn, 0.35f, 4,
                         colHov ? Color{51, 65, 85, 255} : Color{30, 41, 59, 180});
    const char* colSym = col ? "+" : "-";
    float csw = TW(colSym, 11);
    DrawTextEx(GFont(), colSym,
               {colBtn.x + (colBtn.width - csw) * 0.5f, colBtn.y + 4.f},
               FS(11), Sp(FS(11)), WHITE);

    // [×] close
    bool closeHov = CheckCollisionPointRec(mouse, closeBtn);
    DrawRectangleRounded(closeBtn, 0.35f, 4,
                         closeHov ? Color{239, 68, 68, 220} : Color{51, 65, 85, 180});
    float xw = TW("x", 10);
    DrawTextEx(GFont(), "x",
               {closeBtn.x + (closeBtn.width - xw) * 0.5f, closeBtn.y + 5.f},
               FS(10), Sp(FS(10)), WHITE);

    if (col) return;   // title bar only when collapsed

    // ── Expanded body ──────────────────────────────────────────────────────
    int px   = (int)(pos.x + 14);
    int py   = (int)(pos.y + BC_TH + 8);
    int maxW = (int)(BC_W - 28);

    auto lines = WordWrap(def.briefing, maxW, 11);
    for (const auto& ln : lines) {
        if (py > (int)(pos.y + BC_H - 60)) break;
        DrawTextEx(GFont(), ln.c_str(),
                   {(float)px, (float)py},
                   FS(11), Sp(FS(11)), Color{203, 213, 225, 255});
        py += 16;
    }

    if (!def.winConditions.empty()) {
        py += 6;
        DrawTextEx(GFont(), "OBJECTIVE",
                   {(float)px, (float)py},
                   FS(9), Sp(FS(9)), Color{147, 197, 253, 255});
        py += 14;
        for (const auto& wc : def.winConditions) {
            if (py > (int)(pos.y + BC_H - 44)) break;
            std::string bullet = "\xE2\x96\xB8 " + wc.description;
            DrawTextEx(GFont(), bullet.c_str(),
                       {(float)(px + 4), (float)py},
                       FS(10), Sp(FS(10)), Color{167, 243, 208, 255});
            py += 14;
        }
    }

    // [Got it] collapses the card
    Rectangle gotit  = BriefingGotItBtnRect(pos);
    bool      gotHov = CheckCollisionPointRec(mouse, gotit);
    DrawRectangleRounded(gotit, 0.35f, 4,
                         gotHov ? Color{37, 99, 235, 255} : Color{30, 58, 138, 220});
    DrawRectangleRoundedLinesEx(gotit, 0.35f, 4, 1.f, Color{59, 130, 246, 160});
    const char* gtxt = "Got it";
    float gtw = TW(gtxt, 10);
    DrawTextEx(GFont(), gtxt,
               {gotit.x + (gotit.width - gtw) * 0.5f, gotit.y + 8.f},
               FS(10), Sp(FS(10)), WHITE);
}
```

- [ ] **Step 4 — Build and confirm only main.cpp call-site errors remain**

```bash
make 2>&1 | grep -E "error:|warning:" | head -20
```

Expected: errors in `main.cpp` about `DrawBriefingCard` wrong argument count and `BriefingCardRect` / `BriefingCloseBtnRect` / `BriefingGotItBtnRect` not declared. Zero errors from `GameUI.cpp`.

- [ ] **Step 5 — Commit**

```bash
git add src/GameUI.h src/GameUI.cpp
git commit -m "feat: floating briefing card API — BriefingCardState + pos-based rect helpers"
```

---

## Task 2: main.cpp — Wire Up State, Interactions, Drag, Key Binding

**Files:**

- Modify: `src/main.cpp` (multiple targeted sections)

### Step 1 — Add `BriefingCardState` variable

Find line 59 (the `briefingVisible` declaration):

```cpp
    bool          briefingVisible = false;
```

Add `briefingCard` on the line immediately after:

```cpp
    bool          briefingVisible = false;
    BriefingCardState briefingCard;
```

### Step 2 — Reset briefing card in `goSandbox` lambda

Find the `goSandbox` lambda body (around line 104). Add two lines at the end, just before the closing `};`:

```cpp
        briefingVisible = false;
        briefingCard    = BriefingCardState{};
```

So the final two lines of the lambda body look like:

```cpp
        briefingVisible = false;
        briefingCard    = BriefingCardState{};
    };
```

### Step 3 — Initialize card position on every level load

There are **three** places in main.cpp where `briefingVisible = true;` is set. Each needs two lines added immediately after:

```cpp
briefingCard.pos       = BriefingDefaultPos();
briefingCard.collapsed = false;
briefingCard.dragging  = false;
```

Find them by searching for `briefingVisible      = true;` (note the extra spaces — the codebase uses alignment spacing). The three occurrences are:

**A.** Restart button handler (~line 410):

```cpp
                    briefingVisible      = true;
                    helpVisible          = false;
```

Becomes:

```cpp
                    briefingVisible      = true;
                    briefingCard.pos       = BriefingDefaultPos();
                    briefingCard.collapsed = false;
                    briefingCard.dragging  = false;
                    helpVisible          = false;
```

**B.** Next Level button handler (~line 484):

```cpp
                        briefingVisible      = true;
                        helpVisible          = false;
```

Becomes:

```cpp
                        briefingVisible      = true;
                        briefingCard.pos       = BriefingDefaultPos();
                        briefingCard.collapsed = false;
                        briefingCard.dragging  = false;
                        helpVisible          = false;
```

**C.** Level-select card click handler (~line 522):

```cpp
                            briefingVisible      = true;
                            helpVisible          = false;
```

Becomes:

```cpp
                            briefingVisible      = true;
                            briefingCard.pos       = BriefingDefaultPos();
                            briefingCard.collapsed = false;
                            briefingCard.dragging  = false;
                            helpVisible          = false;
```

### Step 4 — Add B key handler for briefing toggle

Find the H key handler (around line 232):

```cpp
        if (fileOp == FILEOP_NONE && !menuVisible && IsKeyPressed(KEY_H) &&
            gameMode != GAME_LEVEL_SELECT && ps.activeField == -1 &&
            ps.activeRouteField == -1 && ps.aclActiveField == -1 && ps.natField == -1) {
            helpVisible = !helpVisible;
            if (helpVisible) briefingVisible = false;
        }
```

Add the B key handler immediately after the closing `}`:

```cpp
        if (fileOp == FILEOP_NONE && !menuVisible && IsKeyPressed(KEY_B) &&
            gameMode == GAME_PLAYING && ps.activeField == -1 &&
            ps.activeRouteField == -1 && ps.aclActiveField == -1 && ps.natField == -1) {
            if (!briefingVisible) {
                briefingVisible        = true;
                briefingCard.collapsed = false;
                briefingCard.pos       = BriefingDefaultPos();
            } else {
                briefingCard.collapsed = !briefingCard.collapsed;
            }
        }
```

### Step 5 — Replace the old `else if (briefingVisible)` click-consumer

Find and remove the old briefing block in the LMB-pressed handler (around lines 346–350):

```cpp
            } else if (briefingVisible) {
                if (CheckCollisionPointRec(screenMouse, BriefingCloseBtnRect()) ||
                    CheckCollisionPointRec(screenMouse, BriefingGotItBtnRect()))
                    briefingVisible = false;
                // all other clicks consumed while briefing open
            } else if (menuVisible) {
```

Replace with (the `else if (menuVisible)` line stays; we only remove the briefing block):

```cpp
            } else if (menuVisible) {
```

Then, inside the big `else` block at the bottom (around line 530, labeled `// HUD navigation buttons`), add briefing card hit-testing **as the very first thing** in that `else` block, before the sandbox/level HUD button checks:

Find this comment and the code right after it:

```cpp
            } else {
            // HUD navigation buttons — take priority over canvas interactions
            if (gameMode == GAME_SANDBOX &&
```

Insert the briefing card interaction block between the `} else {` line and the HUD navigation comment:

```cpp
            } else {
            // ── Briefing card interactions (non-consuming) ─────────────────
            bool cardConsumedClick = false;
            if (briefingVisible && gameMode == GAME_PLAYING) {
                Rectangle cardBounds = BriefingCardBounds(briefingCard.pos, briefingCard.collapsed);
                if (CheckCollisionPointRec(screenMouse, cardBounds)) {
                    cardConsumedClick = true;
                    Rectangle closeBtn = BriefingCloseBtnRect(briefingCard.pos);
                    Rectangle colBtn   = BriefingCollapseBtnRect(briefingCard.pos);
                    if (CheckCollisionPointRec(screenMouse, closeBtn)) {
                        briefingVisible = false;
                    } else if (CheckCollisionPointRec(screenMouse, colBtn)) {
                        briefingCard.collapsed = !briefingCard.collapsed;
                    } else if (!briefingCard.collapsed &&
                               CheckCollisionPointRec(screenMouse, BriefingGotItBtnRect(briefingCard.pos))) {
                        briefingCard.collapsed = true;
                    } else if (CheckCollisionPointRec(screenMouse, BriefingTitleBarRect(briefingCard.pos))) {
                        if (briefingCard.collapsed) {
                            briefingCard.collapsed = false;  // click collapsed bar → expand
                        } else {
                            briefingCard.dragging = true;
                            briefingCard.dragOff  = {screenMouse.x - briefingCard.pos.x,
                                                     screenMouse.y - briefingCard.pos.y};
                        }
                    }
                }
            }
            if (!cardConsumedClick) {
            // HUD navigation buttons — take priority over canvas interactions
            if (gameMode == GAME_SANDBOX &&
```

Then close the `if (!cardConsumedClick)` block just before the two closing braces at the bottom of the LMB handler. The current last lines of the `else` block end with:

```cpp
            }  // closes if (!handled)
            }  // closes else if (inCanvas)
            }  // closes else (gameMode != GAME_WIN)
```

Change to:

```cpp
            }  // closes if (!handled)
            }  // closes else if (inCanvas)
            }  // closes if (!cardConsumedClick)
            }  // closes else (gameMode != GAME_WIN)
```

### Step 6 — Add drag update in LMB-held section

Find the existing LMB-held block (around line 724):

```cpp
        // ── LMB held ──────────────────────────────────────────────────
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragging) {
```

Add the briefing card drag update **immediately before** that block:

```cpp
        // ── Briefing card drag ────────────────────────────────────────
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && briefingVisible && briefingCard.dragging) {
            // 520 = BC_W, 30/210 = BC_TH/BC_H from GameUI.cpp — must match
            const float CW = 520.f;
            const float CH = briefingCard.collapsed ? 30.f : 210.f;
            briefingCard.pos.x = std::clamp(screenMouse.x - briefingCard.dragOff.x,
                                            0.f, (float)CANVAS_W() - CW);
            briefingCard.pos.y = std::clamp(screenMouse.y - briefingCard.dragOff.y,
                                            0.f, (float)CANVAS_H() - CH);
        }

        // ── LMB held ──────────────────────────────────────────────────
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragging) {
```

### Step 7 — Clear drag flag on LMB release

Find the LMB-released block (around line 741):

```cpp
        // ── LMB released ──────────────────────────────────────────────
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (connecting && hoverNodeId != -1) {
```

Add one line immediately after `if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {`:

```cpp
            briefingCard.dragging = false;
```

### Step 8 — Update the draw call

Find line ~1561:

```cpp
            if (briefingVisible && gameMode == GAME_PLAYING)
                DrawBriefingCard(activeLevelDef);
```

Change to:

```cpp
            if (briefingVisible && gameMode == GAME_PLAYING)
                DrawBriefingCard(activeLevelDef, briefingCard);
```

### Step 9 — Update the HUD key hint

Find lines ~1554–1557:

```cpp
            if (gameMode != GAME_LEVEL_SELECT)
                DrawTextEx(GFont(), "H = Help  \xe2\x80\xa2  M = Menu",
                           {10.f, (float)(CANVAS_H() - 20)},
                           FS(9), Sp(FS(9)), Color{71, 85, 105, 255});
```

Replace with:

```cpp
            if (gameMode != GAME_LEVEL_SELECT) {
                const char* hint = (gameMode == GAME_PLAYING)
                    ? "B = Briefing  \xe2\x80\xa2  H = Help  \xe2\x80\xa2  M = Menu"
                    : "H = Help  \xe2\x80\xa2  M = Menu";
                DrawTextEx(GFont(), hint,
                           {10.f, (float)(CANVAS_H() - 20)},
                           FS(9), Sp(FS(9)), Color{71, 85, 105, 255});
            }
```

- [ ] **Step 10 — Build clean**

```bash
make 2>&1 | grep -E "error:|warning:"
```

Expected: zero errors, zero warnings. If there are warnings about unused `BriefingCardRect` — verify those old declarations are fully removed from `GameUI.h`.

- [ ] **Step 11 — Launch and test**

```bash
make && ./PacketPath
```

Test checklist:

1. From level select, click any level → briefing card appears, expanded, centered, title bar visible
2. Drag the title bar → card follows the mouse, stays within canvas bounds
3. Click [−] → card collapses to just the title bar; click [+] → expands again
4. Click "Got it" → card collapses
5. Click collapsed title bar → card expands
6. Click [×] → card disappears; press B → card re-appears expanded at center
7. Press B while expanded → collapses; press B again → expands
8. Click canvas device nodes while card is visible (not on card) → device gets selected, config panel updates normally
9. Press ESC → card hides (briefingVisible = false); press B → card re-appears
10. Enter sandbox mode → no briefing card visible at all
11. Open menu (M key) → menu appears; card is underneath, not on top
12. Press H for help → help overlay appears on top of card

- [ ] **Step 12 — Commit**

```bash
git add src/main.cpp
git commit -m "feat: floating draggable briefing card — drag, collapse, non-blocking, sandbox-excluded"
```

- [ ] **Step 13 — Push**

```bash
git push
```

---

## Self-Review

**Spec coverage:**

- [x] Floating card (not a blocking modal) — non-consuming click handler in Task 2 Step 5
- [x] Draggable — LMB drag via title bar, Task 2 Steps 5–7
- [x] Collapsible — [−]/[+] button and "Got it" collapse in Task 1 Step 3, Task 2 Steps 4–5
- [x] Not in sandbox mode — `gameMode == GAME_PLAYING` guard in draw call + `goSandbox` reset
- [x] Re-openable — B key in Task 2 Step 4
- [x] HUD hint updated — "B = Briefing" added in GAME_PLAYING mode, Task 2 Step 9

**Placeholder scan:** No TBDs. All code blocks are complete.

**Type consistency:**

- `BriefingCardState` declared in GameUI.h, used in main.cpp as `BriefingCardState briefingCard;` ✓
- `DrawBriefingCard(const LevelDef&, const BriefingCardState&)` matches both declaration and call site ✓
- `BriefingCardBounds`, `BriefingTitleBarRect`, `BriefingCollapseBtnRect`, `BriefingCloseBtnRect`, `BriefingGotItBtnRect` all take `Vector2 pos` — consistent throughout ✓
- `BC_W = 520.f` used in both GameUI.cpp (rect helpers) and main.cpp drag clamp (hard-coded 520.f with comment) ✓
