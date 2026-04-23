# M3.3 — Success Arrival Flash Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a green expanding ring pulse at the destination node when a packet is delivered successfully — symmetric with the existing red failure pulse.

**Architecture:** `PacketAnim` gains a `successPulse` float (mirrors `failPulse`). `UpdatePacketAnim` sets it to `0.5f` on successful delivery and decays it every frame alongside `failPulse`. `DrawPacketAnim` draws a green expanding ring when `successPulse > 0`, using identical radius/alpha math to the existing red ring. `main.cpp`'s done-check is extended to wait for both pulses to fully drain before returning to idle.

**Tech Stack:** C++17, raylib 5.5, GNU Make (`make` builds `./packet-path`). No external test framework — verification is build-clean + manual gameplay check.

---

## File Map

| File | Change |
|------|--------|
| `src/Packet.h` | Add `float successPulse = 0.f` to `PacketAnim` |
| `src/Packet.cpp` | Set `successPulse = 0.5f` on success; decay both pulses in done branch |
| `src/main.cpp` line 169 | Extend brace-init: add sixth `0.f` for `successPulse` |
| `src/main.cpp` line 403 | Extend done-check: `&& simState.anim.successPulse <= 0.f` |
| `src/NetworkCanvas.cpp` | Add green ring block in `DrawPacketAnim` before existing red ring block |

---

### Task 1: Add `successPulse` to `PacketAnim` and wire update logic

**Files:**
- Modify: `src/Packet.h`
- Modify: `src/Packet.cpp`
- Modify: `src/main.cpp` (one line)

#### Context

`src/Packet.h` currently defines `PacketAnim` as:

```cpp
struct PacketAnim {
    ForwardResult result;
    int   hop       = 0;
    float t         = 0.f;
    bool  done      = false;
    float failPulse = 0.f;
};
```

`src/Packet.cpp` `UpdatePacketAnim` decays only `failPulse` in the done branch and sets only `failPulse` on failure completion:

```cpp
void UpdatePacketAnim(PacketAnim& anim, float dt,
                      const std::vector<DeviceNode>& nodes,
                      const std::vector<Cable>& cables)
{
    (void)nodes; (void)cables;
    if (anim.done) {
        anim.failPulse = std::max(0.f, anim.failPulse - dt);
        return;
    }

    const auto& path = anim.result.path;
    if ((int)path.size() <= 1) {
        anim.done = true;
        if (!anim.result.success) anim.failPulse = 0.5f;  // pulse even on first-hop failure
        return;
    }

    anim.t += dt / HOP_DURATION;
    if (anim.t >= 1.f) {
        anim.t = 0.f;
        anim.hop++;
        if (anim.hop >= (int)path.size() - 1) {
            anim.done = true;
            if (!anim.result.success)
                anim.failPulse = 0.5f;
        }
    }
}
```

`src/main.cpp` line 169 initializes `PacketAnim` with five positional values:

```cpp
simState.anim = PacketAnim{fr, 0, 0.f, false, 0.f};
```

- [ ] **Step 1: Add `successPulse` field to `PacketAnim` in `src/Packet.h`**

Replace the `PacketAnim` struct:

```cpp
struct PacketAnim {
    ForwardResult result;
    int   hop          = 0;
    float t            = 0.f;
    bool  done         = false;
    float failPulse    = 0.f;
    float successPulse = 0.f;
};
```

- [ ] **Step 2: Rewrite `UpdatePacketAnim` in `src/Packet.cpp`**

Replace the entire function body so both pulses decay and success triggers `successPulse`:

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

    anim.t += dt / HOP_DURATION;
    if (anim.t >= 1.f) {
        anim.t = 0.f;
        anim.hop++;
        if (anim.hop >= (int)path.size() - 1) {
            anim.done = true;
            if (anim.result.success) anim.successPulse = 0.5f;
            else                     anim.failPulse    = 0.5f;
        }
    }
}
```

- [ ] **Step 3: Add sixth positional value to `PacketAnim` brace-init in `src/main.cpp`**

At line 169, change:

```cpp
simState.anim = PacketAnim{fr, 0, 0.f, false, 0.f};
```

to:

```cpp
simState.anim = PacketAnim{fr, 0, 0.f, false, 0.f, 0.f};
```

(Sixth value `0.f` initialises `successPulse`.)

- [ ] **Step 4: Build — zero errors, zero warnings**

```bash
make 2>&1
```

Expected: clean compile, zero errors, zero warnings with `-Wall -Wextra -std=c++17`.

If you see `-Wmissing-field-initializers` on the brace-init line, the sixth value `0.f` is missing — re-check Step 3.

- [ ] **Step 5: Commit**

```bash
git add src/Packet.h src/Packet.cpp src/main.cpp
git commit -m "feat(m3.3): add successPulse to PacketAnim — set on delivery, decay alongside failPulse"
```

---

### Task 2: Draw green success ring + extend idle transition guard

**Files:**
- Modify: `src/NetworkCanvas.cpp` (function `DrawPacketAnim`)
- Modify: `src/main.cpp` (line 403 — done-check)

#### Context

`src/NetworkCanvas.cpp` `DrawPacketAnim` currently reads (lines 142–187):

```cpp
void DrawPacketAnim(const PacketAnim& anim,
                    const std::vector<DeviceNode>& nodes,
                    const std::vector<Cable>& cables)
{
    const auto& path = anim.result.path;
    if (path.empty()) return;

    // Failure pulse — red ring expanding on the last node
    if (anim.failPulse > 0.f) {
        const DeviceNode* failNode = FindNode(nodes, path.back());
        if (failNode) {
            float frac = anim.failPulse / 0.5f;   // 1..0 as pulse fades
            float r    = 30.f + 20.f * (1.f - frac);
            DrawCircleV(failNode->position, r,
                        Color{239, 68, 68, (unsigned char)(frac * 80.f)});
            DrawCircleLinesV(failNode->position, r, Color{239, 68, 68, (unsigned char)(frac * 180.f)});
        }
        return;
    }

    if (anim.done) return;
    if (anim.hop >= (int)path.size() - 1) return;

    int fromId = path[anim.hop];
    int toId   = path[anim.hop + 1];

    const DeviceNode* fromNode = FindNode(nodes, fromId);
    const DeviceNode* toNode   = FindNode(nodes, toId);
    const Cable*      cable    = FindCable(cables, fromId, toId);
    if (!fromNode || !toNode || !cable) return;

    // Resolve port indices (cable can be stored in either direction)
    int fromPort = (cable->fromId == fromId) ? cable->fromPort : cable->toPort;
    int toPort   = (cable->fromId == toId)   ? cable->fromPort : cable->toPort;

    Vector2 p0 = GetPortPosition(*fromNode, fromPort);
    Vector2 p3 = GetPortPosition(*toNode,   toPort);
    Vector2 c1 = BezierCtrl(p0, fromPort);
    Vector2 c2 = BezierCtrl(p3, toPort);

    Vector2 pos = EvaluateCubicBezier(p0, c1, c2, p3, anim.t);

    // Green glow (outer) + core dot — always green during travel
    DrawCircleV(pos, 14.f, Color{34, 197, 94, 55});
    DrawCircleV(pos, 7.f,  Color{34, 197, 94, 255});
}
```

`src/main.cpp` lines 401–407:

```cpp
if (simState.mode == SIM_ANIMATING) {
    UpdatePacketAnim(simState.anim, GetFrameTime(), nodes, cables);
    if (simState.anim.done && simState.anim.failPulse <= 0.f) {
        simState.mode  = SIM_IDLE;
        simState.srcId = -1;
    }
}
```

- [ ] **Step 1: Replace `DrawPacketAnim` in `src/NetworkCanvas.cpp`**

Replace the entire function (lines 142–187) with this version that adds the green ring block before the red ring block:

```cpp
void DrawPacketAnim(const PacketAnim& anim,
                    const std::vector<DeviceNode>& nodes,
                    const std::vector<Cable>& cables)
{
    const auto& path = anim.result.path;
    if (path.empty()) return;

    // Success pulse — green ring expanding on the destination node
    if (anim.successPulse > 0.f) {
        const DeviceNode* destNode = FindNode(nodes, path.back());
        if (destNode) {
            float frac = anim.successPulse / 0.5f;   // 1..0 as pulse fades
            float r    = 30.f + 20.f * (1.f - frac);
            DrawCircleV(destNode->position, r,
                        Color{34, 197, 94, (unsigned char)(frac * 80.f)});
            DrawCircleLinesV(destNode->position, r, Color{34, 197, 94, (unsigned char)(frac * 180.f)});
        }
        return;
    }

    // Failure pulse — red ring expanding on the last node
    if (anim.failPulse > 0.f) {
        const DeviceNode* failNode = FindNode(nodes, path.back());
        if (failNode) {
            float frac = anim.failPulse / 0.5f;   // 1..0 as pulse fades
            float r    = 30.f + 20.f * (1.f - frac);
            DrawCircleV(failNode->position, r,
                        Color{239, 68, 68, (unsigned char)(frac * 80.f)});
            DrawCircleLinesV(failNode->position, r, Color{239, 68, 68, (unsigned char)(frac * 180.f)});
        }
        return;
    }

    if (anim.done) return;
    if (anim.hop >= (int)path.size() - 1) return;

    int fromId = path[anim.hop];
    int toId   = path[anim.hop + 1];

    const DeviceNode* fromNode = FindNode(nodes, fromId);
    const DeviceNode* toNode   = FindNode(nodes, toId);
    const Cable*      cable    = FindCable(cables, fromId, toId);
    if (!fromNode || !toNode || !cable) return;

    // Resolve port indices (cable can be stored in either direction)
    int fromPort = (cable->fromId == fromId) ? cable->fromPort : cable->toPort;
    int toPort   = (cable->fromId == toId)   ? cable->fromPort : cable->toPort;

    Vector2 p0 = GetPortPosition(*fromNode, fromPort);
    Vector2 p3 = GetPortPosition(*toNode,   toPort);
    Vector2 c1 = BezierCtrl(p0, fromPort);
    Vector2 c2 = BezierCtrl(p3, toPort);

    Vector2 pos = EvaluateCubicBezier(p0, c1, c2, p3, anim.t);

    // Green glow (outer) + core dot — always green during travel
    DrawCircleV(pos, 14.f, Color{34, 197, 94, 55});
    DrawCircleV(pos, 7.f,  Color{34, 197, 94, 255});
}
```

- [ ] **Step 2: Extend done-check in `src/main.cpp`**

At line 403, replace:

```cpp
if (simState.anim.done && simState.anim.failPulse <= 0.f) {
```

with:

```cpp
if (simState.anim.done && simState.anim.failPulse    <= 0.f
                       && simState.anim.successPulse <= 0.f) {
```

This keeps `SIM_ANIMATING` active while the green ring is still visible, so `DrawPacketAnim` keeps being called and the ring fades smoothly before idle returns.

- [ ] **Step 3: Build — zero errors, zero warnings**

```bash
make 2>&1
```

Expected: clean compile, zero errors, zero warnings.

- [ ] **Step 4: Commit**

```bash
git add src/NetworkCanvas.cpp src/main.cpp
git commit -m "feat(m3.3): draw green success ring on packet arrival — symmetric with red failure pulse"
```

---

### Task 3: Verification

**Files:** Read-only — no code changes.

- [ ] **Step 1: Build the binary**

```bash
make 2>&1
```

Expected: `make: Nothing to be done for 'all'` (already built) or clean compile. Zero errors, zero warnings.

- [ ] **Step 2: Test success path — green ring fires**

Launch `./packet-path`. Build a two-hop topology:

1. Place PC1 (left), Router1 (center), PC2 (right).
2. Connect PC1 port 0 → Router1 port 0; Router1 port 1 → PC2 port 0.
3. Configure PC1: port 0 = `10.0.0.1/24`. Configure Router1: port 0 = `10.0.0.254/24`, port 1 = `10.0.1.254/24`. Configure PC2: port 0 = `10.0.1.1/24`.
4. Add static route on Router1: dest `10.0.1.0/24`, next-hop `10.0.1.1`.
5. Right-click PC1 → "Send Packet To…" → click PC2 (enter `10.0.1.1`).

Expected:
- Green dot travels PC1 → Router1 → PC2.
- On arrival at PC2, a **green expanding ring** pulses outward from PC2's center, fades over ~0.5 seconds.
- After the ring fades, the mode returns to idle (right-click context menu works again).

- [ ] **Step 3: Test failure path — red ring unchanged**

With the same topology, right-click PC1 → send to `10.0.2.1` (no route).

Expected:
- Red dot travels as far as possible, stops at Router1.
- **Red expanding ring** pulses from Router1 as before.
- No green ring appears.

- [ ] **Step 4: Test single-hop success**

Place two PCs, connect directly, configure both on `192.168.1.0/24`. Send between them.

Expected: After a single 0.4s hop, green ring fires at destination. No crash.

- [ ] **Step 5: Verify ARP and log entries unaffected**

After a successful sim, check the bottom log console.

Expected:
- ARP entries (blue `?` / teal `!` / gray `~`) appear before the routing entry.
- Success routing entry shows green `✓ PC1 → Router1 → PC2 — delivered`.
- ARP tab on the right panel populates with the resolved IP→MAC entries.
