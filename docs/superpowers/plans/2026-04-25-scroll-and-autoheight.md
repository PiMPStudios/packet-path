# Scroll & Auto-Height Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the mission briefing card auto-size to fit all content, and add mouse-wheel scrolling to the log console.

**Architecture:** Two independent features that both touch `main.cpp` at non-overlapping lines — run sequentially to avoid file conflicts. Task 1 replaces the fixed `BC_H = 210` constant with a computed height stored in `BriefingCardState.cardHeight`. Task 2 adds a `logScrollOffset` int to `main.cpp`, threads it through `DrawLogConsole` and `LogConsoleHitTest`, and handles mouse-wheel in the log area.

**Tech Stack:** C++17, raylib 5.5, custom font system (`GFont`/`FS`/`Sp`/`TW` from `Font.h`).

---

## File Map

| File | Task | Change |
|---|---|---|
| `src/GameUI.h` | 1 | Add `cardHeight` to `BriefingCardState`; add `BriefingComputeHeight`; update `BriefingCardBounds` + `BriefingGotItBtnRect` signatures |
| `src/GameUI.cpp` | 1 | Add `BriefingComputeHeight`; remove `BC_H` constant; update rect helpers + `DrawBriefingCard` |
| `src/NetworkCanvas.h` | 2 | Add `scrollOffset` param to `DrawLogConsole` |
| `src/NetworkCanvas.cpp` | 2 | Update `DrawLogConsole` to use offset; add scroll indicator |
| `src/TraceModal.h` | 2 | Add `scrollOffset` param to `LogConsoleHitTest` |
| `src/TraceModal.cpp` | 2 | Update `LogConsoleHitTest` geometry to respect offset |
| `src/main.cpp` | 1 then 2 | Task 1: add `cardHeight` to `BriefingCardState` inits + update call sites; Task 2: add `logScrollOffset`/`prevLogSize`, wheel handler, updated calls |

---

## Task 1: Auto-Height Briefing Card

**Files:**
- Modify: `src/GameUI.h` (~lines 53–68, the briefing card block)
- Modify: `src/GameUI.cpp` (~lines 367–492, the briefing card section)
- Modify: `src/main.cpp` (4 level-load init sites + 3 rect-helper call sites)

### Context

`BriefingCardState` is declared in `src/GameUI.h`. `BC_W = 520`, `BC_H = 210`, `BC_TH = 30` are `static const float` in `src/GameUI.cpp`. The `WordWrap` helper is a `static` function in `src/GameUI.cpp` (line 11) taking `(text, maxW, fontSize)`. `CANVAS_H()` is from `src/Layout.h` (included via `GameUI.h`→`Level.h` chain; also directly included in `GameUI.cpp`). Font helpers: `FS(base)`, `Sp(fs)`, `TW(text, base)` from `Font.h` (already included in `GameUI.cpp`).

---

- [ ] **Step 1 — Update `src/GameUI.h`: add `cardHeight` to struct; update signatures**

Find the briefing card section in `src/GameUI.h` (lines 53–68). The current content is:

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
Rectangle BriefingGotItBtnRect(Vector2 pos);                // "Got it" → collapses card (only valid when !collapsed)
void      DrawBriefingCard(const LevelDef& def, const BriefingCardState& state);
```

Replace it with:

```cpp
// ── Mission briefing card ─────────────────────────────────────────────────
// Floating, draggable, collapsible. Only shown during GAME_PLAYING.
struct BriefingCardState {
    Vector2 pos        = {0.f, 82.f};  // top-left corner; init via BriefingDefaultPos()
    bool    collapsed  = false;         // true = only title bar visible
    bool    dragging   = false;         // true while LMB held on title bar
    Vector2 dragOff    = {};            // mouse offset from pos when drag began
    float   cardHeight = 210.f;        // computed by BriefingComputeHeight; fallback default
};

float     BriefingComputeHeight(const LevelDef& def);                        // dry-run content height (max 500px)
Vector2   BriefingDefaultPos();                                               // canvas-centered starting pos
Rectangle BriefingCardBounds(Vector2 pos, bool collapsed, float cardHeight); // full card rect
Rectangle BriefingTitleBarRect(Vector2 pos);                                 // drag zone (full-width × 30 px)
Rectangle BriefingCollapseBtnRect(Vector2 pos);                              // [−]/[+] toggle button
Rectangle BriefingCloseBtnRect(Vector2 pos);                                 // [×] hide-entirely button
Rectangle BriefingGotItBtnRect(Vector2 pos, float cardHeight);               // "Got it" → collapses card (only valid when !collapsed)
void      DrawBriefingCard(const LevelDef& def, const BriefingCardState& state);
```

- [ ] **Step 2 — Update `src/GameUI.cpp`: add `BriefingComputeHeight`, remove `BC_H`, update helpers**

Find the briefing card section in `src/GameUI.cpp` starting at the `// ── Mission briefing card` comment (~line 367). The section currently starts:

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
```

And `BriefingGotItBtnRect` currently reads:
```cpp
Rectangle BriefingGotItBtnRect(Vector2 pos) {
    return {pos.x + BC_W - 92.f, pos.y + BC_H - 34.f, 82.f, 26.f};
}
```

**A.** Remove `BC_H = 210.f` and add `BriefingComputeHeight`. Replace the opening constants + helpers block (from `static const float BC_W` through `BriefingGotItBtnRect`) with:

```cpp
// ── Mission briefing card ─────────────────────────────────────────────────
static const float BC_W  = 520.f;   // card width (fixed)
static const float BC_TH = 30.f;    // title bar height

float BriefingComputeHeight(const LevelDef& def) {
    int   maxW  = (int)(BC_W - 28);
    auto  lines = WordWrap(def.briefing, maxW, 11);
    float h     = BC_TH + 8.f;                         // title bar + top padding
    h += 16.f * (float)lines.size();                   // briefing text rows
    if (!def.winConditions.empty()) {
        h += 6.f + 14.f;                               // spacer + OBJECTIVE label
        h += 14.f * (float)def.winConditions.size();   // condition rows
    }
    h += 8.f + 26.f + 8.f;                            // spacer + Got-it button + bottom padding
    return std::min(h, 500.f);
}

Vector2 BriefingDefaultPos() {
    return {std::max(8.f, ((float)CANVAS_W() - BC_W) / 2.f), 82.f};
}

Rectangle BriefingCardBounds(Vector2 pos, bool collapsed, float cardHeight) {
    return {pos.x, pos.y, BC_W, collapsed ? BC_TH : cardHeight};
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

Rectangle BriefingGotItBtnRect(Vector2 pos, float cardHeight) {
    return {pos.x + BC_W - 92.f, pos.y + cardHeight - 34.f, 82.f, 26.f};
}
```

**B.** Update `DrawBriefingCard` to use `state.cardHeight` instead of `BC_H` and remove clip guards.

Find the `DrawBriefingCard` function body. Make these changes inside it:

1. In the `if (!col)` block, update `BriefingCardBounds` call:
```cpp
    if (!col) {
        Rectangle bounds = BriefingCardBounds(pos, col, state.cardHeight);
        DrawRectangleRounded(bounds, 0.07f, 6, Color{10, 17, 35, 245});
        DrawRectangleRoundedLinesEx(bounds, 0.07f, 6, 1.5f, Color{59, 130, 246, 180});
    }
```

2. In the expanded body, remove the clip guard on briefing text lines. The current loop is:
```cpp
    auto lines = WordWrap(def.briefing, maxW, 11);
    for (const auto& ln : lines) {
        if (py > (int)(pos.y + BC_H - 60)) break;
        DrawTextEx(GFont(), ln.c_str(), ...);
        py += 16;
    }
```
Change to (remove the `break` guard):
```cpp
    auto lines = WordWrap(def.briefing, maxW, 11);
    for (const auto& ln : lines) {
        DrawTextEx(GFont(), ln.c_str(),
                   {(float)px, (float)py},
                   FS(11), Sp(FS(11)), Color{203, 213, 225, 255});
        py += 16;
    }
```

3. Remove the clip guard on win conditions. The current loop is:
```cpp
        for (const auto& wc : def.winConditions) {
            if (py > (int)(pos.y + BC_H - 44)) break;
            std::string bullet = "\xE2\x96\xB8 " + wc.description;
```
Change to (remove the `break` guard):
```cpp
        for (const auto& wc : def.winConditions) {
            std::string bullet = "\xE2\x96\xB8 " + wc.description;
```

4. Update the `BriefingGotItBtnRect` call near the bottom of the function:
```cpp
    Rectangle gotit  = BriefingGotItBtnRect(pos, state.cardHeight);
```

- [ ] **Step 3 — Build: confirm only main.cpp call-site errors**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
make 2>&1 | grep -E "error:|warning:" | head -20
```

Expected: errors in `main.cpp` about wrong argument counts for `BriefingCardBounds` and `BriefingGotItBtnRect`. Zero errors from `GameUI.cpp`.

- [ ] **Step 4 — Update `src/main.cpp`: add `cardHeight` init at all 4 level-load sites**

Search for every occurrence of `briefingCard.dragging  = false;` — there are exactly 4 (at the end of each level-load block). After each one, add:
```cpp
                    briefingCard.cardHeight = BriefingComputeHeight(activeLevelDef);
```

So each level-load block ends like:
```cpp
                    briefingCard.pos       = BriefingDefaultPos();
                    briefingCard.collapsed = false;
                    briefingCard.dragging  = false;
                    briefingCard.cardHeight = BriefingComputeHeight(activeLevelDef);
                    helpVisible          = false;
```

- [ ] **Step 5 — Update `src/main.cpp`: fix the 2 `BriefingCardBounds` call sites**

Search for `BriefingCardBounds(briefingCard.pos, briefingCard.collapsed)` — there are 2 occurrences. Add `briefingCard.cardHeight` as the third argument to both:

```cpp
BriefingCardBounds(briefingCard.pos, briefingCard.collapsed, briefingCard.cardHeight)
```

- [ ] **Step 6 — Update `src/main.cpp`: fix the 1 `BriefingGotItBtnRect` call site**

Search for `BriefingGotItBtnRect(briefingCard.pos)` — there is 1 occurrence. Add `briefingCard.cardHeight`:

```cpp
BriefingGotItBtnRect(briefingCard.pos, briefingCard.cardHeight)
```

- [ ] **Step 7 — Build clean**

```bash
make 2>&1 | grep -E "error:|warning:"
```

Expected: zero errors, zero warnings.

- [ ] **Step 8 — Commit**

```bash
git add src/GameUI.h src/GameUI.cpp src/main.cpp
git commit -m "feat: auto-height briefing card — expands to fit content, max 500px"
```

---

## Task 2: Log Console Mouse-Wheel Scroll

**Files:**
- Modify: `src/NetworkCanvas.h` (line 74: `DrawLogConsole` declaration)
- Modify: `src/NetworkCanvas.cpp` (~lines 311–379: `DrawLogConsole` implementation)
- Modify: `src/TraceModal.h` (line 8: `LogConsoleHitTest` declaration)
- Modify: `src/TraceModal.cpp` (~lines 9–27: `LogConsoleHitTest` implementation)
- Modify: `src/main.cpp` (new variables, wheel handler, updated calls)

### Context

`DrawLogConsole` is in `src/NetworkCanvas.cpp:311`. It shows the last 3 entries, newest at top (y = `CANVAS_H + 8`, stride 24px per row). `LogConsoleHitTest` in `src/TraceModal.cpp:9` mirrors that geometry exactly. The log area occupies `y ∈ [CANVAS_H(), SCREEN_H())` and `x < CANVAS_W()`. `LOG_H = 90` from `src/Layout.h`. `GetMouseWheelMove()` returns positive for wheel-up.

---

- [ ] **Step 1 — Update `src/NetworkCanvas.h`: add `scrollOffset` param**

Find line 74:
```cpp
void DrawLogConsole(const std::vector<LogEntry>& entries);
```

Change to:
```cpp
void DrawLogConsole(const std::vector<LogEntry>& entries, int scrollOffset = 0);
```

- [ ] **Step 2 — Update `src/NetworkCanvas.cpp`: update `DrawLogConsole`**

Find the `DrawLogConsole` function (~line 311). Replace the entire function with:

```cpp
void DrawLogConsole(const std::vector<LogEntry>& entries, int scrollOffset) {
    DrawRectangle(0, CANVAS_H(), SCREEN_W(), LOG_H, Color{10, 15, 28, 255});
    DrawLineEx({0.f, (float)CANVAS_H()}, {(float)SCREEN_W(), (float)CANVAS_H()},
               1.f, Color{51, 65, 85, 255});
    DrawTextEx(GFont(), "LOG", {(float)12, (float)(CANVAS_H() + 8)}, FS(9), Sp(FS(9)), Color{71, 85, 105, 255});

    // Scroll-back indicator: shown when not at the newest entries
    if (scrollOffset > 0) {
        char ibuf[32];
        std::snprintf(ibuf, sizeof(ibuf), "^ %d newer", scrollOffset);
        float iw = TW(ibuf, 9);
        DrawTextEx(GFont(), ibuf,
                   {(float)CANVAS_W() - iw - 12.f, (float)(CANVAS_H() + 6)},
                   FS(9), Sp(FS(9)), Color{148, 163, 184, 200});
    }

    if (entries.empty()) {
        DrawTextEx(GFont(), "No simulations run yet", {(float)36, (float)(CANVAS_H() + 36)}, FS(10), Sp(FS(10)),
                   Color{51, 65, 85, 255});
        return;
    }

    int maxLines = 3;
    int startIdx = std::max(0, (int)entries.size() - maxLines - scrollOffset);
    int shown    = std::min(maxLines, (int)entries.size() - scrollOffset);
    if (shown <= 0) return;

    for (int i = 0; i < shown; ++i) {
        const auto& e = entries[startIdx + i];
        int lineY = CANVAS_H() + 8 + (shown - 1 - i) * 24;  // newest at top

        int   secs = (int)e.timestamp;
        int   mins = (secs / 60) % 60; secs %= 60;
        char  tsbuf[16];
        std::snprintf(tsbuf, sizeof(tsbuf), "[%02d:%02d]", mins, secs);
        DrawTextEx(GFont(), tsbuf, {(float)36, (float)lineY}, FS(10), Sp(FS(10)), Color{71, 85, 105, 255});

        const char* icon;
        Color       icColor;
        switch (e.type) {
            case LOG_ARP_REQ:
                icon    = "?";
                icColor = Color{100, 160, 240, 255};
                break;
            case LOG_ARP_REPLY:
                icon    = "!";
                icColor = Color{80, 200, 180, 255};
                break;
            case LOG_ARP_HIT:
                icon    = "~";
                icColor = Color{140, 140, 140, 255};
                break;
            case LOG_OSPF:
                icon    = "O";
                icColor = Color{59, 130, 246, 255};
                break;
            case LOG_LINK_DOWN:
                icon    = "!";
                icColor = Color{239, 68, 68, 255};
                break;
            case LOG_DEVICE_CRASH:
                icon    = "!";
                icColor = Color{239, 68, 68, 255};
                break;
            case LOG_RESTORED:
                icon    = "+";
                icColor = Color{34, 197, 94, 255};
                break;
            default:
                icon    = e.success ? "\xe2\x9c\x93" : "\xe2\x9c\x97";
                icColor = e.success ? Color{34, 197, 94, 255}
                                    : Color{239, 68, 68, 255};
                break;
        }
        DrawTextEx(GFont(), icon, {(float)90, (float)lineY}, FS(10), Sp(FS(10)), icColor);

        std::string msg = e.pathStr;
        if (!e.reason.empty()) msg += "  \xe2\x80\x94  " + e.reason;
        DrawTextEx(GFont(), msg.c_str(), {(float)108, (float)lineY}, FS(10), Sp(FS(10)), icColor);
    }
}
```

- [ ] **Step 3 — Update `src/TraceModal.h`: add `scrollOffset` param**

Find in `src/TraceModal.h`:
```cpp
int  LogConsoleHitTest(Vector2 screenMouse, const std::vector<LogEntry>& entries);
```

Change to:
```cpp
int  LogConsoleHitTest(Vector2 screenMouse, const std::vector<LogEntry>& entries, int scrollOffset = 0);
```

- [ ] **Step 4 — Update `src/TraceModal.cpp`: update `LogConsoleHitTest`**

Find `LogConsoleHitTest` (~line 9). Replace the entire function with:

```cpp
int LogConsoleHitTest(Vector2 mouse, const std::vector<LogEntry>& entries, int scrollOffset) {
    if (entries.empty()) return -1;
    if (mouse.y < (float)CANVAS_H() || mouse.y >= (float)SCREEN_H()) return -1;
    if (mouse.x >= (float)CANVAS_W()) return -1;

    int maxLines = 3;
    int startIdx = std::max(0, (int)entries.size() - maxLines - scrollOffset);
    int shown    = std::min(maxLines, (int)entries.size() - scrollOffset);
    if (shown <= 0) return -1;

    for (int i = 0; i < shown; ++i) {
        int       lineY = CANVAS_H() + 8 + (shown - 1 - i) * 24;
        Rectangle r     = {0.f, (float)(lineY - 2), (float)CANVAS_W(), 22.f};
        if (CheckCollisionPointRec(mouse, r)) {
            int idx = startIdx + i;
            return (entries[idx].type == LOG_FORWARD) ? idx : -1;
        }
    }
    return -1;
}
```

- [ ] **Step 5 — Update `src/main.cpp`: add scroll state variables**

Find the variable declarations section near the top of `main()` (~line 59 area). After `bool shouldQuit = false;` add:

```cpp
    int logScrollOffset = 0;
    int prevLogSize     = 0;
```

- [ ] **Step 6 — Update `src/main.cpp`: auto-reset scroll on new log entries**

Find the OSPF engine tick section. It begins with `// ── OSPF engine tick` and the `pushLog` lambda is defined inside it. Look for the end of the OSPF tick block (~line 1418). Immediately after the closing `}` of that block, add:

```cpp
        // Auto-reset log scroll when new entries arrive so latest is always visible
        if ((int)logEntries.size() != prevLogSize) {
            logScrollOffset = 0;
            prevLogSize     = (int)logEntries.size();
        }
```

- [ ] **Step 7 — Update `src/main.cpp`: add mouse-wheel handler for log area**

Find the camera zoom / mouse-wheel section. It currently reads:
```cpp
        float wheel = inCanvas ? std::clamp(GetMouseWheelMove(), -3.0f, 3.0f) : 0.0f;
        if (wheel != 0.0f) {
            // camera zoom logic...
        }
```

Immediately after the closing `}` of that camera-zoom block, add the log scroll handler:

```cpp
        // Log console scroll — wheel up = older entries, wheel down = newer
        bool inLog = (screenMouse.y >= (float)CANVAS_H() &&
                      screenMouse.x  < (float)CANVAS_W() &&
                      !inCanvas);
        if (inLog) {
            float logWheel = GetMouseWheelMove();
            if (logWheel != 0.f) {
                int maxScroll = std::max(0, (int)logEntries.size() - 3);
                logScrollOffset = std::clamp(logScrollOffset + (int)std::round(logWheel),
                                             0, maxScroll);
            }
        }
```

- [ ] **Step 8 — Update `src/main.cpp`: pass `logScrollOffset` to `DrawLogConsole` and `LogConsoleHitTest`**

Find:
```cpp
            DrawLogConsole(logEntries);
```
Change to:
```cpp
            DrawLogConsole(logEntries, logScrollOffset);
```

Find:
```cpp
                int hitIdx = LogConsoleHitTest(screenMouse, logEntries);
```
Change to:
```cpp
                int hitIdx = LogConsoleHitTest(screenMouse, logEntries, logScrollOffset);
```

- [ ] **Step 9 — Build clean**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
make 2>&1 | grep -E "error:|warning:"
```

Expected: zero errors, zero warnings.

- [ ] **Step 10 — Commit**

```bash
git add src/NetworkCanvas.h src/NetworkCanvas.cpp src/TraceModal.h src/TraceModal.cpp src/main.cpp
git commit -m "feat: log console mouse-wheel scroll with auto-reset on new entries"
```

---

## Self-Review

**Spec coverage:**
- [x] Briefing card auto-heights to fit content — `BriefingComputeHeight` + `cardHeight` field (Task 1)
- [x] Max height cap (500px) prevents card overflowing canvas — `std::min(h, 500.f)` in `BriefingComputeHeight`
- [x] Content clip guards removed — both `break` guards removed in `DrawBriefingCard` (Task 1, Step 2B)
- [x] `BriefingCardBounds` drag clamp uses dynamic height — `briefingCard.cardHeight` passed through (Task 1, Steps 5–6)
- [x] Log scroll wheel-up = older, wheel-down = newer — `logScrollOffset += round(wheel)` (Task 2, Step 7)
- [x] Auto-reset to newest on new entry — `prevLogSize` tracking resets offset (Task 2, Step 6)
- [x] Scroll indicator shows "^ N newer" when offset > 0 (Task 2, Step 2)
- [x] `LogConsoleHitTest` uses same offset so clicking log entries still opens trace modal correctly (Task 2, Step 4)
- [x] Sandbox mode: briefing card still hidden, scroll still works in sandbox log

**Placeholder scan:** No TBDs or incomplete steps. All code blocks are complete.

**Type consistency:**
- `BriefingComputeHeight` returns `float`; `BriefingCardState.cardHeight` is `float`; all callers pass `float` ✓
- `BriefingCardBounds(pos, collapsed, cardHeight)` — signature matches all call sites ✓
- `BriefingGotItBtnRect(pos, cardHeight)` — signature matches all call sites ✓
- `DrawLogConsole(entries, scrollOffset)` — `int scrollOffset` matches declaration and all callers ✓
- `LogConsoleHitTest(mouse, entries, scrollOffset)` — same ✓
- `logScrollOffset` is `int`; `std::clamp` bounds are `int`; `std::round(logWheel)` cast to `int` ✓
