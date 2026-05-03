# Phase 6: MVP Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add packet trace modal (click log entry → see per-hop decisions), on-canvas break annotation (red glow + badge at failure point), and procedural placeholder SFX (send/arrive/fail tones).

**Architecture:** Four tasks build on each other — Task 1 enriches the data model with `HopDecision` structs; Task 2 uses that data to draw a floating trace modal triggered by log console clicks; Task 3 draws a red path annotation when the last simulation failed; Task 4 adds procedural raylib audio. All tasks touch `main.cpp` for wiring; Tasks 2–4 each introduce a new source module. M6.2 (path animation) was already implemented in Phase 5.

**Tech Stack:** C++17, raylib 5.5, GNU Make, macOS. No new dependencies.

---

## File Map

| Action | File | Responsibility |
| -------- | ------ | ---------------- |
| Modify | `src/Device.h` | Add `HopDecision` struct; add `hops` field to `ForwardResult`; add `traceResult` field to `LogEntry` |
| Modify | `src/SimulationEngine.cpp` | Populate `result.hops` at each forwarding decision (connected + non-connected routes) |
| Create | `src/TraceModal.h` | Declare `LogConsoleHitTest`, `DrawTraceModal` |
| Create | `src/TraceModal.cpp` | Log console hit-test geometry; 480×360 centered trace modal draw |
| Modify | `src/NetworkCanvas.h` | Declare `DrawBrokenPath` |
| Modify | `src/NetworkCanvas.cpp` | Implement `DrawBrokenPath` — red bezier glow on path cables + red badge at break node |
| Create | `src/SoundEngine.h` | Declare `InitSounds`, `UnloadSounds`, `PlayPacketSend`, `PlayPacketArrive`, `PlayPacketFail` |
| Create | `src/SoundEngine.cpp` | Procedural wave synthesis via `new short[]` + `LoadSoundFromWave`; sweep and chord helpers |
| Modify | `src/main.cpp` | Wire all four features: modal state + ESC/LMB handlers, annotation timer, audio init/shutdown, draw calls |

---

## Task 1: Data model — HopDecision, ForwardResult.hops, LogEntry.traceResult

**Files:**

- Modify: `src/Device.h`
- Modify: `src/SimulationEngine.cpp`
- Modify: `src/main.cpp`

---

- [ ] **Step 1: Add `HopDecision` struct to `src/Device.h`**

Open `src/Device.h`. Find the block (lines 56–68):

```cpp
struct ArpEvent {
    int         nodeId   = 0;
    std::string ip;
    std::string mac;
    bool        cacheHit = false;
};

struct ForwardResult {
    bool                  success = false;
    std::vector<int>      path;
    std::string           reason;
    std::vector<ArpEvent> arpEvents;
};
```

Replace it with:

```cpp
struct ArpEvent {
    int         nodeId   = 0;
    std::string ip;
    std::string mac;
    bool        cacheHit = false;
};

struct HopDecision {
    int         nodeId;
    std::string nodeLabel;
    std::string routeType;   // "C"=connected, "S"=static, "O"=OSPF, "O IA"=inter-area
    std::string destPrefix;  // matched route prefix, e.g. "10.0.1.0/24"
    std::string nextHopIp;   // next-hop IP, or "delivered" for connected routes
    int         outPort;     // egress port index, -1 if delivered
};

struct ForwardResult {
    bool                     success = false;
    std::vector<int>         path;
    std::string              reason;
    std::vector<ArpEvent>    arpEvents;
    std::vector<HopDecision> hops;
};
```

---

- [ ] **Step 2: Add `traceResult` to `LogEntry` in `src/Device.h`**

In the same file, find the `LogEntry` struct (lines 70–76):

```cpp
struct LogEntry {
    bool        success   = false;
    std::string pathStr;
    std::string reason;
    float       timestamp = 0.f;
    LogType     type      = LOG_FORWARD;
};
```

Replace it with:

```cpp
struct LogEntry {
    bool          success     = false;
    std::string   pathStr;
    std::string   reason;
    float         timestamp   = 0.f;
    LogType       type        = LOG_FORWARD;
    ForwardResult traceResult;   // populated for LOG_FORWARD entries only
};
```

---

- [ ] **Step 3: Populate `result.hops` for connected routes in `src/SimulationEngine.cpp`**

Open `src/SimulationEngine.cpp`. Find the `ROUTE_CONNECTED` branch (around line 36):

```cpp
            if (route.src == ROUTE_CONNECTED) {
                result.success = true;
                result.reason  = "delivered";
                return result;
            }
```

Replace it with:

```cpp
            if (route.src == ROUTE_CONNECTED) {
                HopDecision hd;
                hd.nodeId     = currentId;
                hd.nodeLabel  = cur->label;
                hd.routeType  = "C";
                hd.destPrefix = route.dest;
                hd.nextHopIp  = "delivered";
                hd.outPort    = -1;
                result.hops.push_back(hd);
                result.success = true;
                result.reason  = "delivered";
                return result;
            }
```

---

- [ ] **Step 4: Populate `result.hops` for forwarded routes in `src/SimulationEngine.cpp`**

In the same file, find the block just before `visited.insert(neighborId)` (around lines 90–94):

```cpp
            visited.insert(neighborId);
            result.path.push_back(neighborId);
            currentId = neighborId;
            matched   = true;
            break;
```

Replace it with:

```cpp
            {
                HopDecision hd;
                hd.nodeId     = currentId;
                hd.nodeLabel  = cur->label;
                if      (route.src == ROUTE_STATIC)  hd.routeType = "S";
                else if (route.src == ROUTE_OSPF)    hd.routeType = "O";
                else if (route.src == ROUTE_OSPF_IA) hd.routeType = "O IA";
                else                                  hd.routeType = "?";
                hd.destPrefix = route.dest;
                hd.nextHopIp  = route.nextHop;
                hd.outPort    = route.outPort;
                result.hops.push_back(hd);
            }
            visited.insert(neighborId);
            result.path.push_back(neighborId);
            currentId = neighborId;
            matched   = true;
            break;
```

---

- [ ] **Step 5: Store `traceResult` in the `LOG_FORWARD` log entry in `src/main.cpp`**

Open `src/main.cpp`. Find the block in the `SIM_SELECTING_DST` handler that assigns `le` fields (around lines 248–255). The block currently reads:

```cpp
                        simState.anim = PacketAnim{.result = fr};
                        le.success    = fr.success;
                        le.pathStr    = BuildPathStr(fr.path, nodes);
                        le.reason     = fr.reason;
                        le.type       = LOG_FORWARD;
                        le.timestamp  = GetTime();
                        simState.mode = SIM_ANIMATING;
```

Replace it with:

```cpp
                        simState.anim  = PacketAnim{.result = fr};
                        le.success     = fr.success;
                        le.pathStr     = BuildPathStr(fr.path, nodes);
                        le.reason      = fr.reason;
                        le.type        = LOG_FORWARD;
                        le.traceResult = fr;
                        le.timestamp   = GetTime();
                        simState.mode  = SIM_ANIMATING;
```

---

- [ ] **Step 6: Build and verify**

```bash
make clean && make
```

Expected: clean compile, zero errors. The binary `packet-path` is produced. (No runtime change yet — hops are populated but nothing displays them.)

---

- [ ] **Step 7: Commit**

```bash
git add src/Device.h src/SimulationEngine.cpp src/main.cpp
git commit -m "feat(M6): data model — HopDecision, ForwardResult.hops, LogEntry.traceResult"
```

---

## Task 2: M6.1 — Packet Trace Modal

**Files:**

- Create: `src/TraceModal.h`
- Create: `src/TraceModal.cpp`
- Modify: `src/main.cpp`

---

- [ ] **Step 1: Create `src/TraceModal.h`**

```cpp
#pragma once
#include "raylib.h"
#include "Device.h"
#include <vector>

// Returns index into logEntries of the LOG_FORWARD entry under screenMouse,
// or -1 if no LOG_FORWARD entry was clicked.
int  LogConsoleHitTest(Vector2 screenMouse, const std::vector<LogEntry>& entries);

// Draws the centered 480x360 trace modal over a dimmed screen.
// Call only when the modal is open (outside BeginMode2D).
void DrawTraceModal(const ForwardResult& trace);
```

---

- [ ] **Step 2: Create `src/TraceModal.cpp`**

```cpp
#include "TraceModal.h"
#include "NetworkCanvas.h"   // CANVAS_H, CANVAS_W, SCREEN_W, SCREEN_H
#include <algorithm>
#include <cstdio>

// Geometry mirrors DrawLogConsole:
//   lineY = CANVAS_H + 8 + (shown - 1 - i) * 24   (newest at top, 24px stride)
int LogConsoleHitTest(Vector2 mouse, const std::vector<LogEntry>& entries) {
    if (entries.empty()) return -1;
    if (mouse.y < (float)CANVAS_H || mouse.y >= (float)SCREEN_H) return -1;
    if (mouse.x >= (float)CANVAS_W) return -1;   // ignore panel-side clicks

    int maxLines = 3;
    int startIdx = std::max(0, (int)entries.size() - maxLines);
    int shown    = std::min(maxLines, (int)entries.size());

    for (int i = 0; i < shown; ++i) {
        int       lineY = CANVAS_H + 8 + (shown - 1 - i) * 24;
        Rectangle r     = {0.f, (float)(lineY - 2), (float)CANVAS_W, 22.f};
        if (CheckCollisionPointRec(mouse, r)) {
            int idx = startIdx + i;
            return (entries[idx].type == LOG_FORWARD) ? idx : -1;
        }
    }
    return -1;
}

void DrawTraceModal(const ForwardResult& trace) {
    // Dim entire screen behind modal
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, 140});

    const float MW = 480.f, MH = 360.f;
    const float MX = (SCREEN_W - MW) / 2.f;
    const float MY = (SCREEN_H - MH) / 2.f;
    Rectangle modal = {MX, MY, MW, MH};

    DrawRectangleRounded(modal, 0.08f, 8, Color{22, 33, 62, 255});
    DrawRectangleRoundedLinesEx(modal, 0.08f, 8, 1.5f, Color{59, 130, 246, 255});

    // Header
    DrawText("Packet Trace", (int)(MX + 16), (int)(MY + 14), 13,
             Color{226, 232, 240, 255});
    const char* icon  = trace.success ? "\xe2\x9c\x93" : "\xe2\x9c\x97";
    Color       icCol = trace.success ? Color{34, 197, 94, 255}
                                      : Color{239, 68, 68, 255};
    DrawText(icon, (int)(MX + MW - 28), (int)(MY + 14), 13, icCol);
    DrawLineEx({MX, MY + 36.f}, {MX + MW, MY + 36.f}, 1.f,
               Color{51, 65, 85, 255});

    if (trace.hops.empty()) {
        DrawText("No hop detail available", (int)(MX + 16), (int)(MY + 52), 11,
                 Color{100, 116, 139, 255});
    } else {
        float rowY = MY + 44.f;
        for (int i = 0; i < (int)trace.hops.size() && rowY < MY + MH - 36.f; ++i) {
            const HopDecision& h = trace.hops[i];

            // Hop index circle
            DrawCircle((int)(MX + 22.f), (int)(rowY + 10.f), 10.f,
                       Color{30, 64, 175, 255});
            char num[4];
            std::snprintf(num, sizeof(num), "%d", i + 1);
            int nw = MeasureText(num, 10);
            DrawText(num, (int)(MX + 22.f) - nw / 2, (int)(rowY + 5.f), 10, WHITE);

            // Node label
            DrawText(h.nodeLabel.c_str(), (int)(MX + 40.f), (int)rowY, 12,
                     Color{226, 232, 240, 255});

            // Route type badge (color-coded)
            Color rtCol;
            if      (h.routeType == "C")    rtCol = Color{34, 197, 94, 255};   // green
            else if (h.routeType == "S")    rtCol = Color{234, 179, 8, 255};   // yellow
            else if (h.routeType == "O")    rtCol = Color{59, 130, 246, 255};  // blue
            else                            rtCol = Color{168, 85, 247, 255};   // purple (O IA)
            DrawText(h.routeType.c_str(), (int)(MX + 40.f), (int)(rowY + 16.f), 10, rtCol);

            // Matched prefix → next hop
            char detail[128];
            std::snprintf(detail, sizeof(detail), "%s \xe2\x86\x92 %s",
                          h.destPrefix.c_str(), h.nextHopIp.c_str());
            DrawText(detail, (int)(MX + 72.f), (int)(rowY + 16.f), 10,
                     Color{100, 116, 139, 255});

            rowY += 44.f;
            if (i + 1 < (int)trace.hops.size())
                DrawLineEx({MX + 8.f, rowY - 4.f}, {MX + MW - 8.f, rowY - 4.f},
                           0.5f, Color{30, 41, 59, 255});
        }
    }

    DrawText("ESC or click outside to close",
             (int)(MX + 16), (int)(MY + MH - 24), 10, Color{71, 85, 105, 255});
}
```

---

- [ ] **Step 3: Add `TraceModal.h` include and state variables to `src/main.cpp`**

At the top of `src/main.cpp`, after the existing four includes, add:

```cpp
#include "TraceModal.h"
```

Inside `main()`, after `int lastConditionsPassed = 0;`, add:

```cpp
bool traceModalOpen = false;
int  selectedLogIdx = -1;
```

---

- [ ] **Step 4: Guard ESC handler with `traceModalOpen` check in `src/main.cpp`**

Find the **entire** `if (IsKeyPressed(KEY_ESCAPE))` block (around lines 85–102):

```cpp
        if (IsKeyPressed(KEY_ESCAPE)) {
            contextMenu.visible = false;
            if (ps.activeField != -1) {
                ps.activeField = -1;
            } else if (ps.activeRouteField != -1) {
                ps.activeRouteField = -1;
            } else if (ps.activePortAreaField != -1) {
                ps.activePortAreaField = -1;
                ps.portAreaBuf.clear();
            } else if (connecting) {
                connecting  = false;
                hoverNodeId = -1;
                hoverPort   = -1;
            } else if (simState.mode == SIM_SELECTING_DST) {
                simState.mode  = SIM_IDLE;
                simState.srcId = -1;
            }
        }
```

Replace it entirely with:

```cpp
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (traceModalOpen) {
                traceModalOpen = false;
            } else {
                contextMenu.visible = false;
                if (ps.activeField != -1) {
                    ps.activeField = -1;
                } else if (ps.activeRouteField != -1) {
                    ps.activeRouteField = -1;
                } else if (ps.activePortAreaField != -1) {
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                } else if (connecting) {
                    connecting  = false;
                    hoverNodeId = -1;
                    hoverPort   = -1;
                } else if (simState.mode == SIM_SELECTING_DST) {
                    simState.mode  = SIM_IDLE;
                    simState.srcId = -1;
                }
            }
        }
```

---

- [ ] **Step 5: Add `traceModalOpen` intercept in the LMB handler in `src/main.cpp`**

Find the LMB pressed block (around line 139):

```cpp
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (gameMode == GAME_WIN) {
                // Win overlay clicks...
                ...
                // any other click on the WIN screen is silently consumed
            } else {
            if (contextMenu.visible) {
```

Change `} else {` to:

```cpp
            } else if (traceModalOpen) {
                const float MW = 480.f, MH = 360.f;
                Rectangle modal = {(SCREEN_W - MW) / 2.f, (SCREEN_H - MH) / 2.f, MW, MH};
                if (!CheckCollisionPointRec(screenMouse, modal))
                    traceModalOpen = false;
                // all clicks consumed while modal is open
            } else {
```

---

- [ ] **Step 6: Add log console hit test inside the `else` block in `src/main.cpp`**

Inside the `else` block added in Step 5, find the chain (around lines 178–299):

```cpp
            if (contextMenu.visible) {
                ...
            } else if (simState.mode == SIM_SELECTING_DST && inCanvas) {
                ...
            } else if (inCanvas) {
            int pNode = -1, pPort = -1;
```

Insert a new branch before `} else if (inCanvas) {`:

```cpp
            } else if (screenMouse.y >= (float)CANVAS_H &&
                       screenMouse.y <  (float)SCREEN_H  &&
                       screenMouse.x <  (float)CANVAS_W) {
                // Log console click — open trace modal for LOG_FORWARD entries
                int hitIdx = LogConsoleHitTest(screenMouse, logEntries);
                if (hitIdx >= 0) {
                    selectedLogIdx = hitIdx;
                    traceModalOpen = true;
                }
            } else if (inCanvas) {
            int pNode = -1, pPort = -1;
```

---

- [ ] **Step 7: Guard the panel click handler against `traceModalOpen` in `src/main.cpp`**

Find the panel click handler (around line 386):

```cpp
        if (gameMode != GAME_WIN && !inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
```

Replace it with:

```cpp
        if (gameMode != GAME_WIN && !traceModalOpen && !inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
```

---

- [ ] **Step 8: Add `DrawTraceModal` draw call in `src/main.cpp`**

Find the draw section, after `DrawLogConsole(logEntries);` (around line 638):

```cpp
            DrawLogConsole(logEntries);

            // Level HUD badge (top-left) and win overlay
```

Insert between them:

```cpp
            DrawLogConsole(logEntries);
            if (traceModalOpen && selectedLogIdx >= 0 &&
                selectedLogIdx < (int)logEntries.size())
                DrawTraceModal(logEntries[selectedLogIdx].traceResult);

            // Level HUD badge (top-left) and win overlay
```

---

- [ ] **Step 9: Build and verify**

```bash
make clean && make
```

Expected: clean compile, zero errors.

Run `./packet-path`. Load level 1 (press 1). Click a PC to start a simulation, click a destination. A LOG_FORWARD entry appears in the log console. Click it — a centered blue-bordered modal appears showing numbered hops. Press ESC or click outside — modal closes.

---

- [ ] **Step 10: Commit**

```bash
git add src/TraceModal.h src/TraceModal.cpp src/main.cpp
git commit -m "feat(M6.1): packet trace modal — click log entry to see per-hop decisions"
```

---

## Task 3: M6.3 — On-Canvas Break Annotation

**Files:**

- Modify: `src/NetworkCanvas.h`
- Modify: `src/NetworkCanvas.cpp`
- Modify: `src/main.cpp`

---

- [ ] **Step 1: Declare `DrawBrokenPath` in `src/NetworkCanvas.h`**

Open `src/NetworkCanvas.h`. Find the last draw function declaration:

```cpp
void DrawLogConsole(const std::vector<LogEntry>& entries);
```

Add after it:

```cpp
void DrawBrokenPath(const std::vector<DeviceNode>& nodes,
                    const std::vector<Cable>& cables,
                    const ForwardResult& result);
```

---

- [ ] **Step 2: Implement `DrawBrokenPath` in `src/NetworkCanvas.cpp`**

Open `src/NetworkCanvas.cpp`. After the closing `}` of `DrawLogConsole`, add the following new function. It must be called inside `BeginMode2D` because it draws in world coordinates.

```cpp
void DrawBrokenPath(const std::vector<DeviceNode>& nodes,
                    const std::vector<Cable>& cables,
                    const ForwardResult& result)
{
    const auto& path = result.path;
    if (path.size() < 2) return;

    // Red glow on every cable segment the packet walked
    for (int i = 0; i < (int)path.size() - 1; ++i) {
        const DeviceNode* from  = FindNode(nodes, path[i]);
        const DeviceNode* to    = FindNode(nodes, path[i + 1]);
        const Cable*      cable = FindCable(cables, path[i], path[i + 1]);
        if (!from || !to || !cable) continue;

        int fromPort = (cable->fromId == path[i])     ? cable->fromPort : cable->toPort;
        int toPort   = (cable->fromId == path[i + 1]) ? cable->fromPort : cable->toPort;

        Vector2 p0 = GetPortPosition(*from, fromPort);
        Vector2 p3 = GetPortPosition(*to,   toPort);
        DrawSplineSegmentBezierCubic(p0, BezierCtrl(p0, fromPort),
                                     BezierCtrl(p3, toPort), p3,
                                     5.0f, Color{239, 68, 68, 140});
    }

    // Red badge above the last node in path (break point)
    const DeviceNode* breakNode = FindNode(nodes, path.back());
    if (!breakNode) return;

    float bx = breakNode->position.x;
    float by  = breakNode->position.y - NODE_H / 2.f - 18.f;
    DrawCircle((int)bx, (int)by, 12.f, Color{239, 68, 68, 255});
    const char* xmark = "\xe2\x9c\x97";
    int xw = MeasureText(xmark, 11);
    DrawText(xmark, (int)bx - xw / 2, (int)(by - 6.f), 11, WHITE);
}
```

---

- [ ] **Step 3: Add annotation state to `src/main.cpp`**

Inside `main()`, after `int selectedLogIdx = -1;` (added in Task 2), add:

```cpp
float         failAnnotationTimer = 0.f;
ForwardResult lastFailedTrace;
```

---

- [ ] **Step 4: Set/clear annotation timer in the simulation result handler in `src/main.cpp`**

Find the block modified in Task 1 Step 5 (around line 248–258):

```cpp
                        simState.anim  = PacketAnim{.result = fr};
                        le.success     = fr.success;
                        le.pathStr     = BuildPathStr(fr.path, nodes);
                        le.reason      = fr.reason;
                        le.type        = LOG_FORWARD;
                        le.traceResult = fr;
                        le.timestamp   = GetTime();
                        simState.mode  = SIM_ANIMATING;
```

Replace it with:

```cpp
                        simState.anim  = PacketAnim{.result = fr};
                        le.success     = fr.success;
                        le.pathStr     = BuildPathStr(fr.path, nodes);
                        le.reason      = fr.reason;
                        le.type        = LOG_FORWARD;
                        le.traceResult = fr;
                        le.timestamp   = GetTime();
                        simState.mode  = SIM_ANIMATING;
                        if (fr.success) {
                            failAnnotationTimer = 0.f;
                        } else {
                            failAnnotationTimer = 5.0f;
                            lastFailedTrace     = fr;
                        }
```

---

- [ ] **Step 5: Decrement annotation timer each frame in `src/main.cpp`**

Find the OSPF engine tick section (around line 558):

```cpp
        // ── OSPF engine tick ─────────────────────────────────────────────
        {
```

Add the timer decrement immediately before that comment:

```cpp
        if (failAnnotationTimer > 0.f)
            failAnnotationTimer -= dt;

        // ── OSPF engine tick ─────────────────────────────────────────────
        {
```

---

- [ ] **Step 6: Add `DrawBrokenPath` call inside `BeginMode2D` in `src/main.cpp`**

Find the BeginMode2D draw section (around line 589):

```cpp
            BeginMode2D(camera);
                DrawDotGrid(camera);
                DrawAllCables(cables, nodes);
                DrawPacketAnim(simState.anim, nodes, cables);
```

Replace it with:

```cpp
            BeginMode2D(camera);
                DrawDotGrid(camera);
                DrawAllCables(cables, nodes);
                if (failAnnotationTimer > 0.f)
                    DrawBrokenPath(nodes, cables, lastFailedTrace);
                DrawPacketAnim(simState.anim, nodes, cables);
```

---

- [ ] **Step 7: Build and verify**

```bash
make clean && make
```

Expected: clean compile, zero errors.

Run `./packet-path`. Load level 1 (press 1). Delete a cable between two devices. Simulate a packet that would cross that cable. The failed path lights up red on-canvas — cables glow red, the break node gets a red badge. The annotation fades after 5 seconds. Simulate a successful packet — annotation immediately disappears.

---

- [ ] **Step 8: Commit**

```bash
git add src/NetworkCanvas.h src/NetworkCanvas.cpp src/main.cpp
git commit -m "feat(M6.3): on-canvas break annotation — red path glow + badge at failure point"
```

---

## Task 4: M6.4 — Procedural SFX

**Files:**

- Create: `src/SoundEngine.h`
- Create: `src/SoundEngine.cpp`
- Modify: `src/main.cpp`

---

- [ ] **Step 1: Create `src/SoundEngine.h`**

```cpp
#pragma once

void InitSounds();
void UnloadSounds();
void PlayPacketSend();
void PlayPacketArrive();
void PlayPacketFail();
```

---

- [ ] **Step 2: Create `src/SoundEngine.cpp`**

```cpp
#include "SoundEngine.h"
#include "raylib.h"
#include <cmath>

static Sound sndSend   = {};
static Sound sndArrive = {};
static Sound sndFail   = {};

static const float PI = 3.14159265f;

// Builds a mono 16-bit PCM sound from a frequency sweep.
// freqStart→freqEnd over `duration` seconds, linear amplitude fade-out.
// square=true gives a square wave, false gives sine.
static Sound MakeSweep(float freqStart, float freqEnd,
                        float duration, float volume, bool square)
{
    const int SR = 44100;
    int n        = (int)(SR * duration);
    short* data  = new short[n];

    float phase = 0.f;
    for (int i = 0; i < n; ++i) {
        float t    = (float)i / (float)n;
        float freq = freqStart + (freqEnd - freqStart) * t;
        float fade = 1.0f - t;
        phase     += 2.f * PI * freq / (float)SR;
        float s    = square ? (std::sin(phase) >= 0.f ? 1.f : -1.f)
                            : std::sin(phase);
        data[i]    = (short)(s * fade * volume * 32767.f);
    }

    Wave w       = {};
    w.frameCount = (unsigned int)n;
    w.sampleRate = (unsigned int)SR;
    w.sampleSize = 16;
    w.channels   = 1;
    w.data       = data;
    Sound snd    = LoadSoundFromWave(w);   // copies data to audio device
    delete[] data;
    return snd;
}

// Builds a two-frequency sine chord with linear fade-out.
static Sound MakeChord(float freq1, float freq2, float duration, float volume) {
    const int SR = 44100;
    int n        = (int)(SR * duration);
    short* data  = new short[n];

    float ph1 = 0.f, ph2 = 0.f;
    for (int i = 0; i < n; ++i) {
        float t    = (float)i / (float)n;
        float fade = 1.0f - t;
        ph1 += 2.f * PI * freq1 / (float)SR;
        ph2 += 2.f * PI * freq2 / (float)SR;
        float s = (std::sin(ph1) + std::sin(ph2)) * 0.5f;
        data[i] = (short)(s * fade * volume * 32767.f);
    }

    Wave w       = {};
    w.frameCount = (unsigned int)n;
    w.sampleRate = (unsigned int)SR;
    w.sampleSize = 16;
    w.channels   = 1;
    w.data       = data;
    Sound snd    = LoadSoundFromWave(w);
    delete[] data;
    return snd;
}

void InitSounds() {
    sndSend   = MakeSweep(880.f, 1100.f, 0.15f, 0.4f, false);  // sine sweep up
    sndArrive = MakeChord(660.f, 880.f,  0.25f, 0.4f);          // two-tone chord
    sndFail   = MakeSweep(220.f, 110.f,  0.30f, 0.3f, true);    // square sweep down
}

void UnloadSounds() {
    UnloadSound(sndSend);
    UnloadSound(sndArrive);
    UnloadSound(sndFail);
}

void PlayPacketSend()   { PlaySound(sndSend);   }
void PlayPacketArrive() { PlaySound(sndArrive); }
void PlayPacketFail()   { PlaySound(sndFail);   }
```

---

- [ ] **Step 3: Add `SoundEngine.h` include to `src/main.cpp`**

At the top of `src/main.cpp`, after `#include "TraceModal.h"`, add:

```cpp
#include "SoundEngine.h"
```

---

- [ ] **Step 4: Initialize audio in `src/main.cpp`**

Find the start of `main()`:

```cpp
    InitWindow(SCREEN_W, SCREEN_H, "Packet Path");
    SetTargetFPS(60);
```

Replace with:

```cpp
    InitWindow(SCREEN_W, SCREEN_H, "Packet Path");
    SetTargetFPS(60);
    InitAudioDevice();
    InitSounds();
```

---

- [ ] **Step 5: Add sound triggers to the simulation handler in `src/main.cpp`**

**5a — empty destIp branch:** Find the `if (destIp.empty())` block (around line 200):

```cpp
                    if (destIp.empty()) {
                        le.success   = false;
                        le.type      = LOG_FORWARD;
                        const DeviceNode* src = FindNode(nodes, simState.srcId);
                        le.pathStr   = (src ? src->label : "?") + " \xe2\x86\x92 " +
                                       (dst ? dst->label : "?");
                        le.reason    = "destination has no configured IP";
                        le.timestamp = GetTime();
                    } else {
```

Replace it with:

```cpp
                    if (destIp.empty()) {
                        PlayPacketSend();
                        PlayPacketFail();
                        le.success   = false;
                        le.type      = LOG_FORWARD;
                        const DeviceNode* src = FindNode(nodes, simState.srcId);
                        le.pathStr   = (src ? src->label : "?") + " \xe2\x86\x92 " +
                                       (dst ? dst->label : "?");
                        le.reason    = "destination has no configured IP";
                        le.timestamp = GetTime();
                    } else {
```

**5b — non-empty destIp branch:** Find the line:

```cpp
                    } else {
                        ForwardResult fr = SimulateForward(simState.srcId, destIp,
                                                           nodes, cables);
```

Replace with:

```cpp
                    } else {
                        PlayPacketSend();
                        ForwardResult fr = SimulateForward(simState.srcId, destIp,
                                                           nodes, cables);
```

Then find the if/else block added in Task 3 Step 4:

```cpp
                        if (fr.success) {
                            failAnnotationTimer = 0.f;
                        } else {
                            failAnnotationTimer = 5.0f;
                            lastFailedTrace     = fr;
                        }
```

Replace it with:

```cpp
                        if (fr.success) {
                            PlayPacketArrive();
                            failAnnotationTimer = 0.f;
                        } else {
                            PlayPacketFail();
                            failAnnotationTimer = 5.0f;
                            lastFailedTrace     = fr;
                        }
```

---

- [ ] **Step 6: Shut down audio in `src/main.cpp`**

Find the shutdown at the end of `main()`:

```cpp
    CloseWindow();
    return 0;
}
```

Replace with:

```cpp
    UnloadSounds();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
```

---

- [ ] **Step 7: Build and verify**

```bash
make clean && make
```

Expected: clean compile, zero errors.

Run `./packet-path`. Load a level. Simulate a successful ping — hear a short rising tone (send) followed by a two-tone chord (arrive). Simulate a failing path — hear the send tone followed by a descending square-wave buzz (fail). Volume should be moderate, not harsh.

---

- [ ] **Step 8: Commit**

```bash
git add src/SoundEngine.h src/SoundEngine.cpp src/main.cpp
git commit -m "feat(M6.4): procedural SFX — sweep/chord tones for packet send, arrive, fail"
```

---

## Final Verification Checklist

After all four tasks:

- [ ] `make clean && make` produces zero warnings or errors
- [ ] Click a `LOG_FORWARD` entry → trace modal opens, shows numbered hops with route types (C/S/O/O IA) and prefix→next-hop detail
- [ ] Click outside modal or press ESC → modal closes
- [ ] Non-LOG_FORWARD entries (ARP, OSPF) do not open modal when clicked
- [ ] Failed simulation → red cable glow + red badge appear on canvas at break node
- [ ] Successful simulation → red annotation immediately clears
- [ ] Red annotation auto-fades after 5 seconds with no further input
- [ ] Send tone plays on every packet simulation attempt
- [ ] Arrive chord plays on success, descending buzz plays on failure
- [ ] Win overlay still works (modal does not intercept retry/next buttons)
- [ ] Panel fields, tab clicks, OSPF toggle still work when modal is closed
