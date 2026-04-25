# Slow-Motion Replay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add pause/step/speed-control to the packet animation system so players can freeze a packet in flight, step it hop-by-hop, and watch it at 0.25×–2× speed.

**Architecture:** Add `paused` and `speedMult` directly to `PacketAnim`; `UpdatePacketAnim` skips motion when paused and scales dt by `speedMult`. A new `StepForwardAnim` advances one hop instantly. `DrawReplayHUD` draws a PAUSED badge + four speed buttons. `DrawTraceModal` gains an `activeHop` parameter that highlights the current hop row.

**Tech Stack:** C++17, raylib 5.x, no new dependencies.

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `src/Packet.h` | Modify | Add `paused`/`speedMult` fields; declare `StepForwardAnim`, `GetPacketWorldPos` |
| `src/Packet.cpp` | Modify | Implement `StepForwardAnim`, `GetPacketWorldPos`; patch `UpdatePacketAnim` |
| `src/TraceModal.h` | Modify | Change `DrawTraceModal` signature to accept `activeHop = -1` |
| `src/TraceModal.cpp` | Modify | Hoist per-hop booleans; draw highlight rect on active hop row |
| `src/GameUI.h` | Modify | Declare `DrawReplayHUD`, `ReplaySpeedBtnRect` |
| `src/GameUI.cpp` | Modify | Implement `DrawReplayHUD`, `ReplaySpeedBtnRect` |
| `src/main.cpp` | Modify | Space/Right key, speed button clicks, packet dot click, HUD + modal calls |

---

## Task 1: `Packet.h` / `Packet.cpp` — core replay state

**Files:**
- Modify: `src/Packet.h`
- Modify: `src/Packet.cpp`

- [ ] **Step 1: Add `paused` / `speedMult` to `PacketAnim` in `src/Packet.h`**

Replace the existing `PacketAnim` struct (currently ends at `uint32_t currentVni = 0;`) with:

```cpp
struct PacketAnim {
    ForwardResult result;
    int      hop          = 0;
    float    t            = 0.f;
    bool     done         = false;
    float    failPulse    = 0.f;
    float    successPulse = 0.f;
    uint32_t currentLabel = 0;
    int      currentVlan  = 0;
    uint32_t currentVni   = 0;
    bool     paused       = false;
    float    speedMult    = 1.f;
};
```

- [ ] **Step 2: Declare `StepForwardAnim` and `GetPacketWorldPos` in `src/Packet.h`**

After the `UpdatePacketAnim` declaration add:

```cpp
void    StepForwardAnim(PacketAnim& anim);
Vector2 GetPacketWorldPos(const PacketAnim& anim,
                          const std::vector<DeviceNode>& nodes,
                          const std::vector<Cable>& cables);
```

- [ ] **Step 3: Patch `UpdatePacketAnim` in `src/Packet.cpp` to respect `paused` and `speedMult`**

The function currently reads `anim.t += dt / HOP_DURATION;` (line 65). After the early-return block for `path.size() <= 1`, add a pause guard and change the advance line.

Replace this block:

```cpp
    anim.t += dt / HOP_DURATION;
```

With:

```cpp
    if (anim.paused) return;
    anim.t += dt * anim.speedMult / HOP_DURATION;
```

The full function after the edit (for reference — only the two lines above change):

```cpp
void UpdatePacketAnim(PacketAnim& anim, float dt,
                      const std::vector<DeviceNode>& nodes,
                      const std::vector<Cable>& cables)
{
    (void)nodes; (void)cables;
    if (anim.done) {
        anim.failPulse    = std::max(0.f, anim.failPulse    - dt);
        anim.successPulse = std::max(0.f, anim.successPulse - dt);
        return;
    }

    const auto& path = anim.result.path;
    if ((int)path.size() <= 1) {
        anim.done = true;
        if (anim.result.success) anim.successPulse = 0.5f;
        else                     anim.failPulse    = 0.5f;
        return;
    }

    if (anim.paused) return;
    anim.t += dt * anim.speedMult / HOP_DURATION;
    if (anim.t >= 1.f) {
        anim.t = 0.f;
        anim.hop++;
        if (anim.hop < (int)anim.result.hops.size()) {
            uint32_t raw = anim.result.hops[anim.hop].outLabel;
            anim.currentLabel = (raw == MPLS_IMPLICIT_NULL) ? 0 : raw;
            anim.currentVlan  = anim.result.hops[anim.hop].vlanTag;
            anim.currentVni   = anim.result.hops[anim.hop].vxlanVni;
        } else {
            anim.currentLabel = 0;
            anim.currentVlan  = 0;
            anim.currentVni   = 0;
        }
        if (anim.hop >= (int)path.size() - 1) {
            anim.done = true;
            anim.currentLabel = 0;
            anim.currentVlan  = 0;
            anim.currentVni   = 0;
            if (anim.result.success) anim.successPulse = 0.5f;
            else                     anim.failPulse    = 0.5f;
        }
    }
}
```

- [ ] **Step 4: Implement `StepForwardAnim` in `src/Packet.cpp`**

Append after `UpdatePacketAnim`:

```cpp
void StepForwardAnim(PacketAnim& anim) {
    if (anim.done) return;
    const auto& path = anim.result.path;
    if ((int)path.size() <= 1) return;
    anim.t = 0.f;
    anim.hop++;
    if (anim.hop < (int)anim.result.hops.size()) {
        uint32_t raw      = anim.result.hops[anim.hop].outLabel;
        anim.currentLabel = (raw == MPLS_IMPLICIT_NULL) ? 0 : raw;
        anim.currentVlan  = anim.result.hops[anim.hop].vlanTag;
        anim.currentVni   = anim.result.hops[anim.hop].vxlanVni;
    } else {
        anim.currentLabel = 0;
        anim.currentVlan  = 0;
        anim.currentVni   = 0;
    }
    if (anim.hop >= (int)path.size() - 1) {
        anim.done         = true;
        anim.currentLabel = 0;
        anim.currentVlan  = 0;
        anim.currentVni   = 0;
        if (anim.result.success) anim.successPulse = 0.5f;
        else                     anim.failPulse    = 0.5f;
    }
}
```

- [ ] **Step 5: Implement `GetPacketWorldPos` in `src/Packet.cpp`**

Append after `StepForwardAnim`:

```cpp
Vector2 GetPacketWorldPos(const PacketAnim& anim,
                          const std::vector<DeviceNode>& nodes,
                          const std::vector<Cable>& cables)
{
    const auto& path = anim.result.path;
    if (path.empty() || anim.done || anim.hop >= (int)path.size() - 1)
        return {-99999.f, -99999.f};

    int fromId = path[anim.hop];
    int toId   = path[anim.hop + 1];
    const DeviceNode* fromNode = FindNode(nodes, fromId);
    const DeviceNode* toNode   = FindNode(nodes, toId);
    const Cable*      cable    = FindCable(cables, fromId, toId);
    if (!fromNode || !toNode || !cable) return {-99999.f, -99999.f};

    int fromPort = (cable->fromId == fromId) ? cable->fromPort : cable->toPort;
    int toPort   = (cable->fromId == toId)   ? cable->fromPort : cable->toPort;

    Vector2 p0 = GetPortPosition(*fromNode, fromPort);
    Vector2 p3 = GetPortPosition(*toNode,   toPort);
    Vector2 c1 = BezierCtrl(p0, fromPort);
    Vector2 c2 = BezierCtrl(p3, toPort);
    return EvaluateCubicBezier(p0, c1, c2, p3, anim.t);
}
```

`GetPortPosition` and `BezierCtrl` are declared in `NetworkCanvas.h`, which `Packet.h` does not include. Add the include in `src/Packet.cpp` at the top:

```cpp
#include "Packet.h"
#include "NetworkCanvas.h"   // GetPortPosition, BezierCtrl
#include <algorithm>
```

- [ ] **Step 6: Build and verify**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | head -30
```

Expected: no errors. If `GetPortPosition` or `BezierCtrl` are not found, check that `NetworkCanvas.h` declares them (it does — they're in the header).

- [ ] **Step 7: Commit**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
git add src/Packet.h src/Packet.cpp
git commit -m "feat: add paused/speedMult to PacketAnim; implement StepForwardAnim, GetPacketWorldPos"
```

---

## Task 2: `TraceModal.h` / `TraceModal.cpp` — active-hop highlight

**Files:**
- Modify: `src/TraceModal.h`
- Modify: `src/TraceModal.cpp`

- [ ] **Step 1: Update `DrawTraceModal` signature in `src/TraceModal.h`**

Change:

```cpp
void DrawTraceModal(const ForwardResult& trace);
```

To:

```cpp
void DrawTraceModal(const ForwardResult& trace, int activeHop = -1);
```

- [ ] **Step 2: Hoist per-hop booleans and draw highlight in `src/TraceModal.cpp`**

The current loop body computes `hasLabel`, `hasAcl`, `hasNat`, and `rowStride` at the bottom of each iteration. Hoist them to the top so the highlight rect can be drawn at the correct height before any text.

Replace the entire `for` loop body (lines 55–145 in the current file):

```cpp
    float rowY = MY + 44.f;
    for (int i = 0; i < (int)trace.hops.size() && rowY < MY + MH - 36.f; ++i) {
        const HopDecision& h = trace.hops[i];

        // Pre-compute so highlight rect uses the correct row height
        bool hasLabel = (h.labelOp != LABEL_NONE);
        bool hasAcl   = !h.aclResult.empty();
        bool hasNat   = !h.natResult.empty();
        int  extras   = (hasLabel ? 1 : 0) + (hasAcl ? 1 : 0) + (hasNat ? 1 : 0);
        float rowStride = 44.f + extras * 16.f;

        // Active-hop highlight — subtle blue background behind the entire row
        if (i == activeHop)
            DrawRectangleRounded({MX + 4.f, rowY - 2.f, MW - 8.f, rowStride - 4.f},
                                 0.06f, 4, Color{30, 58, 138, 80});

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

        // Route type badge
        Color rtCol;
        if      (h.routeType == "C")    rtCol = Color{34, 197, 94, 255};
        else if (h.routeType == "S")    rtCol = Color{234, 179, 8, 255};
        else if (h.routeType == "O")    rtCol = Color{59, 130, 246, 255};
        else if (h.routeType == "B")    rtCol = Color{20, 184, 166, 255};
        else                            rtCol = Color{168, 85, 247, 255};
        DrawText(h.routeType.c_str(), (int)(MX + 40.f), (int)(rowY + 16.f), 10, rtCol);

        // Matched prefix → next hop
        char detail[256];
        std::snprintf(detail, sizeof(detail), "%s \xe2\x86\x92 %s",
                      h.destPrefix.c_str(), h.nextHopIp.c_str());
        DrawText(detail, (int)(MX + 72.f), (int)(rowY + 16.f), 10,
                 Color{100, 116, 139, 255});

        // MPLS label op annotation
        if (hasLabel) {
            const char* opStr = "";
            Color        opCol = WHITE;
            char         lblBuf[32] = "";

            if (h.labelOp == LABEL_PUSH) {
                opStr = "PUSH";
                opCol = Color{249, 115, 22, 255};
                std::snprintf(lblBuf, sizeof(lblBuf), "%u", h.outLabel);
            } else if (h.labelOp == LABEL_SWAP) {
                opStr = "SWAP";
                opCol = Color{234, 179, 8, 255};
                std::snprintf(lblBuf, sizeof(lblBuf), "%u\xe2\x86\x92%u",
                              h.inLabel, h.outLabel);
            } else if (h.labelOp == LABEL_POP) {
                opStr = "POP";
                opCol = Color{168, 85, 247, 255};
                std::snprintf(lblBuf, sizeof(lblBuf), "%u", h.inLabel);
            }

            float bw = (float)(MeasureText(opStr, 9) + 10);
            DrawRectangleRounded({MX + 40.f, rowY + 30.f, bw, 13.f},
                                  0.4f, 4, opCol);
            int tw5 = MeasureText(opStr, 9);
            DrawText(opStr, (int)(MX + 40.f + (bw - tw5) / 2.f),
                     (int)(rowY + 32.f), 9, WHITE);
            DrawText(lblBuf, (int)(MX + 40.f + bw + 6.f),
                     (int)(rowY + 31.f), 10, Color{253, 186, 116, 255});
        }

        // ACL annotation badge
        if (hasAcl) {
            float annotY = rowY + 30.f + (hasLabel ? 16.f : 0.f);
            bool permit  = (h.aclResult.rfind("PERMIT", 0) == 0);
            Color ac     = permit ? Color{34,197,94,255} : Color{239,68,68,255};
            float bw = (float)(MeasureText(h.aclResult.c_str(), 9) + 10);
            DrawRectangleRounded({MX+40.f, annotY, bw, 13.f}, 0.4f, 4, ac);
            DrawText(h.aclResult.c_str(), (int)(MX+45.f), (int)(annotY+2.f), 9, WHITE);
        }
        // NAT annotation badge
        if (hasNat) {
            float annotY = rowY + 30.f + (hasLabel ? 16.f : 0.f) + (hasAcl ? 16.f : 0.f);
            char natBuf[64];
            std::snprintf(natBuf, sizeof(natBuf), "NAT %s", h.natResult.c_str());
            float bw = (float)(MeasureText(natBuf, 9) + 10);
            DrawRectangleRounded({MX+40.f, annotY, bw, 13.f}, 0.4f, 4, Color{234,179,8,255});
            DrawText(natBuf, (int)(MX+45.f), (int)(annotY+2.f), 9, WHITE);
        }

        rowY += rowStride;
        if (i + 1 < (int)trace.hops.size())
            DrawLineEx({MX + 8.f, rowY - 4.f}, {MX + MW - 8.f, rowY - 4.f},
                       0.5f, Color{30, 41, 59, 255});
    }
```

Key changes vs. the original:
1. `bool hasLabel`, `bool hasAcl`, `bool hasNat`, `int extras`, `float rowStride` moved to loop top.
2. Active-hop `DrawRectangleRounded` highlight added after computing `rowStride`.
3. The old `int extras` and `float rowStride = 44.f + extras * 16.f;` lines at the bottom of the loop body are removed.

- [ ] **Step 3: Build and verify**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | head -30
```

Expected: clean build. The `DrawTraceModal` call in `main.cpp` still passes one argument — the default `activeHop = -1` is used, so it compiles unchanged.

- [ ] **Step 4: Commit**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
git add src/TraceModal.h src/TraceModal.cpp
git commit -m "feat: add activeHop highlight to DrawTraceModal"
```

---

## Task 3: `GameUI.h` / `GameUI.cpp` — Replay HUD

**Files:**
- Modify: `src/GameUI.h`
- Modify: `src/GameUI.cpp`

- [ ] **Step 1: Declare `DrawReplayHUD` and `ReplaySpeedBtnRect` in `src/GameUI.h`**

Append after the `LevelSelectSandboxBtnRect` declaration:

```cpp
// ── Replay controls HUD ───────────────────────────────────────────────────
// Shown at y=58 when SIM_ANIMATING. PAUSED badge + four speed buttons.
void DrawReplayHUD(bool paused, float speedMult);
Rectangle ReplaySpeedBtnRect(int idx);  // idx=0..3 → 0.25x/0.5x/1x/2x
```

- [ ] **Step 2: Implement `ReplaySpeedBtnRect` in `src/GameUI.cpp`**

Append after `LevelHudSandboxBtnRect`:

```cpp
Rectangle ReplaySpeedBtnRect(int idx) {
    return {88.f + idx * 42.f, 58.f, 38.f, 18.f};
}
```

Button positions (screen coords, all h=18, y=58):
- idx=0 → {88, 58, 38, 18} → "0.25x"
- idx=1 → {130, 58, 38, 18} → "0.5x"
- idx=2 → {172, 58, 38, 18} → "1x"
- idx=3 → {214, 58, 38, 18} → "2x"

- [ ] **Step 3: Implement `DrawReplayHUD` in `src/GameUI.cpp`**

Append after `ReplaySpeedBtnRect`:

```cpp
void DrawReplayHUD(bool paused, float speedMult) {
    // PAUSED badge — amber, shown only when paused
    if (paused) {
        DrawRectangle(8, 58, 76, 18, Color{217, 119, 6, 210});
        DrawRectangleLinesEx({8.f, 58.f, 76.f, 18.f}, 1.f, Color{251, 191, 36, 255});
        DrawText("PAUSED", 14, 62, 10, Color{254, 243, 199, 255});
    }

    // Four speed buttons
    static const float  speeds[4] = {0.25f, 0.5f, 1.f, 2.f};
    static const char*  labels[4] = {"0.25x", "0.5x", "1x", "2x"};
    for (int i = 0; i < 4; ++i) {
        Rectangle r  = ReplaySpeedBtnRect(i);
        bool      active = (speedMult == speeds[i]);
        Color bg   = active ? Color{30, 58, 138, 255} : Color{30, 41, 59, 210};
        Color brd  = active ? Color{59, 130, 246, 255} : Color{51, 65, 85, 255};
        Color txtC = active ? WHITE : Color{148, 163, 184, 255};
        DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)r.height, bg);
        DrawRectangleLinesEx(r, 1.f, brd);
        int tw = MeasureText(labels[i], 9);
        DrawText(labels[i],
                 (int)(r.x + (r.width - tw) / 2.f),
                 (int)(r.y + 4),
                 9, txtC);
    }
}
```

- [ ] **Step 4: Build and verify**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | head -30
```

Expected: clean build. `DrawReplayHUD` and `ReplaySpeedBtnRect` are not yet called from `main.cpp`, so no behavior change yet.

- [ ] **Step 5: Commit**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
git add src/GameUI.h src/GameUI.cpp
git commit -m "feat: add DrawReplayHUD and ReplaySpeedBtnRect to GameUI"
```

---

## Task 4: `main.cpp` — wire everything together

**Files:**
- Modify: `src/main.cpp`

Four changes, applied top-to-bottom in the file:

### Change A — Space / Right key handling (after DELETE block, ~line 282)

After the closing `}` of the DELETE block at line ~282, add:

```cpp
        // Replay controls — Space toggles pause; Right steps one hop when paused
        if (simState.mode == SIM_ANIMATING &&
            ps.activeField == -1 && ps.activeRouteField == -1) {
            if (IsKeyPressed(KEY_SPACE))
                simState.anim.paused = !simState.anim.paused;
            if (simState.anim.paused && IsKeyPressed(KEY_RIGHT))
                StepForwardAnim(simState.anim);
        }
```

### Change B — Speed button clicks in LMB handler (~line 404)

Inside the `else {` block that starts at line ~394 (the not-WIN / not-modal / not-levelselect branch), after the `LevelHudSandboxBtnRect` check and before the `contextMenu.visible` check, add a new `else if`:

```cpp
        } else if (simState.mode == SIM_ANIMATING &&
                   CheckCollisionPointRec(screenMouse, {88.f, 58.f, 164.f, 18.f})) {
            // Speed button area — find which button was clicked
            static const float speeds[4] = {0.25f, 0.5f, 1.f, 2.f};
            for (int i = 0; i < 4; ++i) {
                if (CheckCollisionPointRec(screenMouse, ReplaySpeedBtnRect(i))) {
                    simState.anim.speedMult = speeds[i];
                    break;
                }
            }
```

The combined rect `{88, 58, 164, 18}` covers all four speed buttons (x=88 to x=252). Clicks in gaps between buttons enter this branch but match no button, which is a no-op.

The full context around the insertion (showing what's before and after):

```cpp
            } else if (gameMode == GAME_PLAYING &&
                       CheckCollisionPointRec(screenMouse, LevelHudSandboxBtnRect())) {
                goSandbox();
            } else if (simState.mode == SIM_ANIMATING &&
                       CheckCollisionPointRec(screenMouse, {88.f, 58.f, 164.f, 18.f})) {
                static const float speeds[4] = {0.25f, 0.5f, 1.f, 2.f};
                for (int i = 0; i < 4; ++i) {
                    if (CheckCollisionPointRec(screenMouse, ReplaySpeedBtnRect(i))) {
                        simState.anim.speedMult = speeds[i];
                        break;
                    }
                }
            } else if (contextMenu.visible) {
```

### Change C — Packet dot click in `else if (inCanvas)` block (~line 529)

Find the `else if (inCanvas)` branch (currently line ~529). Add a packet-dot hit-test at the very top of that block, before the existing port hit test:

```cpp
            } else if (inCanvas) {
            // Packet dot click — open trace modal; skip normal canvas interaction
            bool handled = false;
            if (simState.mode == SIM_ANIMATING && !simState.anim.done) {
                Vector2 wpos = GetPacketWorldPos(simState.anim, nodes, cables);
                Vector2 spos = GetWorldToScreen2D(wpos, camera);
                float dx = screenMouse.x - spos.x;
                float dy = screenMouse.y - spos.y;
                if (dx * dx + dy * dy <= 144.f) {   // 12 px radius
                    activeTrace    = simState.anim.result;
                    traceModalOpen = true;
                    handled        = true;
                }
            }
            if (!handled) {
            int pNode = -1, pPort = -1;
            if (HitTestPort(nodes, worldMouse, -1, pNode, pPort)) {
                connecting      = true;
                connectFromId   = pNode;
                connectFromPort = pPort;
                dragging        = false;
            } else {
                int hitIdx = -1;
                for (int i = (int)nodes.size() - 1; i >= 0; --i) {
                    if (CheckCollisionPointRec(worldMouse, GetNodeRect(nodes[i]))) {
                        hitIdx = i;
                        break;
                    }
                }
                for (auto& n : nodes) n.selected = false;
                if (hitIdx != -1) {
                    nodes[hitIdx].selected = true;
                    selectedId = nodes[hitIdx].id;
                    dragging   = true;
                    dragOffset = {worldMouse.x - nodes[hitIdx].position.x,
                                  worldMouse.y - nodes[hitIdx].position.y};
                } else {
                    selectedId = -1;
                    dragging   = false;
                }
            }
            }  // closes if (!handled)
            }  // closes else if (inCanvas)
```

### Change D — Draw calls (~line 1438–1478)

**D1** — Update `DrawTraceModal` call at ~line 1438 to pass active hop:

Change:
```cpp
            if (traceModalOpen)
                DrawTraceModal(activeTrace);
```

To:
```cpp
            if (traceModalOpen)
                DrawTraceModal(activeTrace,
                               (simState.mode == SIM_ANIMATING) ? simState.anim.hop : -1);
```

**D2** — Draw `DrawReplayHUD` after the existing sandbox/level HUD blocks:

After the troubleshootMode badge block (the `DrawRectangle(8, 34, 148, 18, ...)` section inside `if (gameMode == GAME_PLAYING)`) and before the win overlay / level-select overlay draws, add:

```cpp
            // Replay controls HUD — speed buttons + PAUSED badge
            if (simState.mode == SIM_ANIMATING)
                DrawReplayHUD(simState.anim.paused, simState.anim.speedMult);
```

Concretely, this goes after the closing `}` of the `if (gameMode == GAME_PLAYING || gameMode == GAME_WIN)` block (~line 1478) and before `if (gameMode == GAME_WIN)`.

- [ ] **Step 1: Apply Change A (Space/Right key)**

Open `src/main.cpp`. Find the DELETE handler block ending near line 282. Add the replay controls block immediately after its closing `}`.

- [ ] **Step 2: Apply Change B (speed button LMB)**

Find the `} else if (gameMode == GAME_PLAYING && CheckCollisionPointRec(screenMouse, LevelHudSandboxBtnRect())) {` line. Add the `else if (simState.mode == SIM_ANIMATING && ...)` block after `goSandbox();` and before `} else if (contextMenu.visible) {`.

- [ ] **Step 3: Apply Change C (packet dot click)**

Find `} else if (inCanvas) {` in the LMB handler. Wrap the existing `int pNode = -1, pPort = -1;` block in `if (!handled) { ... }` and prepend the packet hit-test above it.

- [ ] **Step 4: Apply Change D1 (trace modal call)**

Find `DrawTraceModal(activeTrace);` and change it to pass the active hop.

- [ ] **Step 5: Apply Change D2 (draw replay HUD)**

Add `if (simState.mode == SIM_ANIMATING) DrawReplayHUD(...)` after the level/sandbox HUD block.

- [ ] **Step 6: Build and verify**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | head -30
```

Expected: clean build.

- [ ] **Step 7: Smoke test**

Launch the game:
```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && ./PacketPath
```

Manual verification checklist:
- [ ] Launch game → open sandbox, add two PCs + a cable, configure IPs, send a packet
- [ ] While packet is animating, press **Space** → packet freezes; amber PAUSED badge appears at y=58
- [ ] Press **Space** again → packet resumes; PAUSED badge disappears
- [ ] While paused, press **Right arrow** → packet jumps one hop
- [ ] Click **0.25x** button → button highlights blue; packet moves at ¼ speed
- [ ] Click **2x** button → button highlights blue; packet moves at 2× speed; click **1x** to restore
- [ ] While packet is in flight, **click the green dot** → trace modal opens with the current hop highlighted in blue
- [ ] Open trace modal from log console while packet is paused → current hop highlighted
- [ ] Load level 1 (key 1), run the win condition packet → pause/step/speed work identically
- [ ] Press ESC while paused → cancels trace modal if open, otherwise ESC chain as before

- [ ] **Step 8: Commit**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path
git add src/main.cpp
git commit -m "feat: wire slow-motion replay controls — Space pause, Right step, speed buttons, packet click"
```

---

## Self-Review

### Spec coverage check

| Requirement | Task |
|---|---|
| Space toggles pause/resume during SIM_ANIMATING | Task 4, Change A |
| Right arrow steps one hop when paused | Task 4, Change A |
| Click on packet dot opens trace modal | Task 4, Change C |
| PAUSED badge (amber) at y=58 | Task 3 DrawReplayHUD |
| Current-hop highlight in trace modal | Task 2 DrawTraceModal |
| Speed buttons: 0.25×/0.5×/1×/2× | Task 3 ReplaySpeedBtnRect + DrawReplayHUD; Task 4 Change B |
| Speed indicator — active button highlighted blue | Task 3 DrawReplayHUD |
| `paused` / `speedMult` state on PacketAnim | Task 1 Packet.h |
| `UpdatePacketAnim` respects pause + speed | Task 1 Packet.cpp |
| `StepForwardAnim` advances exactly one hop | Task 1 Packet.cpp |

All requirements have corresponding tasks. ✓

### Placeholder scan

No TBDs, TODOs, or incomplete code blocks. All code is complete and compilable. ✓

### Type consistency

- `StepForwardAnim(PacketAnim&)` — declared Task 1 Step 2, implemented Task 1 Step 4, called Task 4 Change A. ✓
- `GetPacketWorldPos(const PacketAnim&, const vector<DeviceNode>&, const vector<Cable>&) → Vector2` — declared Task 1 Step 2, implemented Task 1 Step 5, called Task 4 Change C. ✓
- `DrawReplayHUD(bool, float)` — declared Task 3 Step 1, implemented Task 3 Step 3, called Task 4 Change D2. ✓
- `ReplaySpeedBtnRect(int) → Rectangle` — declared Task 3 Step 1, implemented Task 3 Step 2, called Task 3 Step 3 + Task 4 Change B. ✓
- `DrawTraceModal(const ForwardResult&, int activeHop = -1)` — updated Task 2, called Task 4 Change D1. ✓

### Edge cases confirmed

- `GetPacketWorldPos` returns `{-99999, -99999}` when anim is done or path is empty — packet dot hit-test in Change C checks `!simState.anim.done`, so the distance check is only attempted on a live packet.
- `StepForwardAnim` handles 1-node paths (returns early) and multi-hop paths (sets done + triggers pulse on final hop).
- `DrawTraceModal` with `activeHop = -1` (default) draws no highlight — backward-compatible with log-console clicks.
- Speed button `static const float speeds[4]` uses exact float literals matching `speedMult` initial value `1.f`, so the `1x` button is highlighted by default.
