# BGP Route Reflection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add BGP Route Reflection so a single Route Reflector router inside an AS can distribute iBGP-learned routes to all clients, eliminating the full-mesh requirement.

**Architecture:** One `isRouteReflector` bool on `DeviceNode` activates RR behavior for that router. In `BgpEngine` Phase 2b, the existing iBGP split-horizon check is replaced with a branch: RRs skip split-horizon and instead use `CLUSTER_LIST` loop prevention before reflecting routes to all iBGP clients. `ORIGINATOR_ID` and `CLUSTER_LIST` are stamped onto `BgpRoute` when an RR first reflects a route. The BGP tab gains a Route Reflector toggle button (visible when BGP is enabled and ASN > 0). Level 8 teaches this with a star topology: RTR-A (RR, AS100) ↔ RTR-B (client, AS100) via iBGP, RTR-A ↔ RTR-D (AS200) via eBGP, win condition PC-A → PC-B.

**Tech Stack:** C++17, raylib 5.5, nlohmann/json (header-only). No test harness — verification is build-clean + manual run.

---

## File Map

| File | Change |
| ------ | -------- |
| `src/Device.h` | Add `isRouteReflector` to `DeviceNode`; add `originatorId`+`clusterList` to `BgpRoute` |
| `src/Level.cpp` | Parse `isRouteReflector` from JSON |
| `src/BgpEngine.cpp` | Phase 2b: RR relay logic (CLUSTER_LIST loop prevention, ORIGINATOR_ID stamping) |
| `src/ConfigPanel.h` | Declare `PnlBgpRrRect()` |
| `src/ConfigPanel.cpp` | Implement `PnlBgpRrRect()` |
| `src/NetworkCanvas.cpp` | `DrawBgpTab()`: add RR toggle button; relabel NEIGHBORS→CLIENTS when RR |
| `src/main.cpp` | Wire RR click handler; extend level count 7→8 (4 occurrences) |
| `levels/level_08.json` | New RR teaching level |

---

## Context for Every Task

This codebase is a C++ network simulator game built with raylib. Key patterns:

- `DeviceNode` (in `src/Device.h`) holds all per-device state including BGP fields.
- `BgpEngine.cpp` runs every frame: clears state, builds sessions (Phase 1), advertises origins (Phase 2a), relays/reflects routes (Phase 2b), deduplicates.
- `NetworkCanvas.cpp` contains `DrawBgpTab()` which renders the BGP config panel for a selected router.
- `ConfigPanel.cpp` holds layout rect helper functions used by both the draw code and the click handler in `main.cpp`.
- `main.cpp` contains all mouse/keyboard input handling including the BGP click handler (search for `TAB_BGP`).
- Levels are JSON files in `levels/`. `Level.cpp` parses them into `LevelDef` structs via nlohmann/json.
- Build command: `make` (runs in project root). Verify: `make 2>&1 | tail -5` — should show only the compile line, no warnings or errors.

The iBGP full-mesh feature (shipped in the prior session) added `bool ibgp = false;` to `BgpNeighbor` and the split-horizon rule in Phase 2b. Route Reflection extends Phase 2b by replacing the split-horizon check with a branch keyed on `n.isRouteReflector`.

---

### Task 1: Data model — `isRouteReflector` on DeviceNode + RR attributes on BgpRoute

**Files:**

- Modify: `src/Device.h`

**What and why:** Add `bool isRouteReflector = false;` to `DeviceNode` — the single flag that activates RR behavior. Add `uint32_t originatorId = 0;` and `std::vector<uint32_t> clusterList;` to `BgpRoute` — these carry RFC 4456 ORIGINATOR_ID and CLUSTER_LIST through the relay chain and are used for loop detection.

- [ ] **Step 1: Locate the structs**

Open `src/Device.h`. The two structs to modify are:

- `BgpRoute` (around line 29) — add two new fields
- `DeviceNode` (around line 122) — add `isRouteReflector` near the other BGP fields (around line 145)

- [ ] **Step 2: Add fields to `BgpRoute`**

Current `BgpRoute` (lines 29–34):

```cpp
struct BgpRoute {
    std::string           prefix;           // CIDR e.g. "10.0.0.0/24"
    std::string           nextHop;          // peer's facing IP (no mask)
    std::vector<uint32_t> asPath;           // ASNs, closest first
    int                   neighborNodeId = -1;  // node that sent this route
};
```

Replace with:

```cpp
struct BgpRoute {
    std::string           prefix;
    std::string           nextHop;
    std::vector<uint32_t> asPath;
    int                   neighborNodeId = -1;
    uint32_t              originatorId   = 0;        // RFC 4456: first originating client node ID
    std::vector<uint32_t> clusterList;               // RFC 4456: RR cluster IDs traversed
};
```

- [ ] **Step 3: Add `isRouteReflector` to `DeviceNode`**

Locate the BGP state block in `DeviceNode` (around line 145):

```cpp
    // BGP state (routers only)
    bool                     bgpEnabled  = false;
    uint32_t                 localAsn    = 0;
    std::vector<std::string> bgpNetworks;
    std::vector<BgpNeighbor> bgpNeighbors;
    std::vector<BgpRoute>    bgpRoutes;
```

Replace with:

```cpp
    // BGP state (routers only)
    bool                     bgpEnabled       = false;
    bool                     isRouteReflector = false;   // RR-centric toggle; all iBGP peers are clients
    uint32_t                 localAsn         = 0;
    std::vector<std::string> bgpNetworks;
    std::vector<BgpNeighbor> bgpNeighbors;
    std::vector<BgpRoute>    bgpRoutes;
```

- [ ] **Step 4: Build clean**

```bash
make 2>&1 | tail -5
```

Expected: single compile line, no errors. The new fields have defaults, so nothing else needs updating yet.

- [ ] **Step 5: Commit**

```bash
git add src/Device.h
git commit -m "feat(rr): add isRouteReflector to DeviceNode and RR attrs to BgpRoute"
```

---

### Task 2: Level.cpp — parse `isRouteReflector` from JSON

**Files:**

- Modify: `src/Level.cpp`

**What and why:** Levels pre-configure `isRouteReflector = true` on the RR router in JSON. Without this, the Level 8 briefing can't pre-wire the RR toggle — students would have to discover it themselves. This task adds a one-liner to `LoadLevel()`.

- [ ] **Step 1: Locate the BGP field block in `LoadLevel()`**

In `src/Level.cpp`, find the BGP loading block (around line 40):

```cpp
        n.bgpEnabled = d.value("bgpEnabled", false);
        n.localAsn   = (uint32_t)d.value("localAsn",  0);
        if (d.contains("bgpNetworks") && d["bgpNetworks"].is_array())
            for (const auto& net : d["bgpNetworks"])
                n.bgpNetworks.push_back(net.get<std::string>());
```

- [ ] **Step 2: Add `isRouteReflector` parsing**

Replace the above block with:

```cpp
        n.bgpEnabled       = d.value("bgpEnabled",       false);
        n.isRouteReflector = d.value("isRouteReflector", false);
        n.localAsn         = (uint32_t)d.value("localAsn",  0);
        if (d.contains("bgpNetworks") && d["bgpNetworks"].is_array())
            for (const auto& net : d["bgpNetworks"])
                n.bgpNetworks.push_back(net.get<std::string>());
```

- [ ] **Step 3: Build clean**

```bash
make 2>&1 | tail -5
```

Expected: single compile line, no errors or warnings.

- [ ] **Step 4: Commit**

```bash
git add src/Level.cpp
git commit -m "feat(rr): parse isRouteReflector from level JSON"
```

---

### Task 3: BgpEngine — Route Reflector relay logic

**Files:**

- Modify: `src/BgpEngine.cpp`

**What and why:** This is the core engine change. In Phase 2b, the current split-horizon check (`if (nb.ibgp) { ... block iBGP→iBGP relay }`) is replaced with a two-branch block:

- If `n.isRouteReflector`: apply CLUSTER_LIST loop prevention instead of split-horizon — allow relay to iBGP clients.
- If NOT RR: keep classic split-horizon (unchanged behavior for non-RR routers).

After the loop-check gate, when an RR relays to an iBGP client, it stamps `originatorId` (first client to originate the route) and appends its own node ID to `clusterList`.

The "don't reflect back to sender" guard (`learned.neighborNodeId == nb.neighborNodeId`) remains unconditionally — no router, RR or not, reflects a route back to who sent it.

- [ ] **Step 1: Read the full Phase 2b block**

Open `src/BgpEngine.cpp` and read lines 110–165 (the `// ── Phase 2b` block). It looks like this:

```cpp
    // ── Phase 2b: Relay routes to peers ──────────────────────────────────
    // eBGP: prepend local AS, AS-path loop prevention (unchanged).
    // iBGP: preserve AS-path (no prepend), split-horizon (no iBGP→iBGP relay).
    pending.clear();
    for (const auto& n : nodes) {
        if (!n.bgpEnabled || n.localAsn == 0 || n.bgpRoutes.empty()) continue;

        for (const auto& nb : n.bgpNeighbors) {
            if (!nb.established) continue;

            std::string myFaceIp;
            for (const auto& cable : cables) {
                if (!((cable.fromId == n.id && cable.toId   == nb.neighborNodeId) ||
                      (cable.toId   == n.id && cable.fromId == nb.neighborNodeId))) continue;
                myFaceIp = StripMask(FaceIp(n, cable));
                break;
            }
            if (myFaceIp.empty()) continue;

            for (const auto& learned : n.bgpRoutes) {
                if (learned.neighborNodeId == nb.neighborNodeId) continue;  // don't reflect

                // iBGP split-horizon: never relay a route received from an iBGP peer
                // to another iBGP peer (full-mesh rule; every router hears from border directly).
                if (nb.ibgp) {
                    bool learnedViaIbgp = false;
                    for (const auto& src : n.bgpNeighbors)
                        if (src.neighborNodeId == learned.neighborNodeId && src.ibgp)
                            { learnedViaIbgp = true; break; }
                    if (learnedViaIbgp) continue;
                }

                // eBGP: AS-path loop prevention
                if (!nb.ibgp) {
                    bool loop = false;
                    for (auto asn : learned.asPath)
                        if (asn == nb.neighborAsn) { loop = true; break; }
                    if (loop) continue;
                }

                BgpRoute relay;
                relay.prefix         = learned.prefix;
                relay.nextHop        = myFaceIp;
                relay.neighborNodeId = n.id;
                if (nb.ibgp) {
                    relay.asPath = learned.asPath;       // iBGP: unchanged
                } else {
                    relay.asPath = {n.localAsn};         // eBGP: prepend local AS
                    for (auto asn : learned.asPath) relay.asPath.push_back(asn);
                }
                pending.push_back({nb.neighborNodeId, relay});
            }
        }
    }
    for (auto& [tid, r] : pending)
        if (auto* nd = FindNodeMut(nodes, tid)) nd->bgpRoutes.push_back(r);
```

- [ ] **Step 2: Replace Phase 2b with RR-aware version**

Replace the entire Phase 2b block (from `// ── Phase 2b` to the final `for (auto& [tid, r]...` push) with:

```cpp
    // ── Phase 2b: Relay routes to peers ──────────────────────────────────
    // eBGP: prepend local AS, AS-path loop prevention.
    // iBGP non-RR: split-horizon (no iBGP→iBGP relay).
    // iBGP RR: reflect to all clients; CLUSTER_LIST loop prevention;
    //          stamp ORIGINATOR_ID + append cluster ID on first reflection.
    pending.clear();
    for (const auto& n : nodes) {
        if (!n.bgpEnabled || n.localAsn == 0 || n.bgpRoutes.empty()) continue;

        for (const auto& nb : n.bgpNeighbors) {
            if (!nb.established) continue;

            std::string myFaceIp;
            for (const auto& cable : cables) {
                if (!((cable.fromId == n.id && cable.toId   == nb.neighborNodeId) ||
                      (cable.toId   == n.id && cable.fromId == nb.neighborNodeId))) continue;
                myFaceIp = StripMask(FaceIp(n, cable));
                break;
            }
            if (myFaceIp.empty()) continue;

            for (const auto& learned : n.bgpRoutes) {
                if (learned.neighborNodeId == nb.neighborNodeId) continue;  // don't reflect back

                if (nb.ibgp) {
                    if (n.isRouteReflector) {
                        // RR: CLUSTER_LIST loop prevention (RFC 4456 §8)
                        bool loop = false;
                        for (auto cid : learned.clusterList)
                            if ((uint32_t)n.id == cid) { loop = true; break; }
                        if (loop) continue;
                    } else {
                        // Non-RR: classic iBGP split-horizon
                        bool learnedViaIbgp = false;
                        for (const auto& src : n.bgpNeighbors)
                            if (src.neighborNodeId == learned.neighborNodeId && src.ibgp)
                                { learnedViaIbgp = true; break; }
                        if (learnedViaIbgp) continue;
                    }
                }

                // eBGP: AS-path loop prevention
                if (!nb.ibgp) {
                    bool loop = false;
                    for (auto asn : learned.asPath)
                        if (asn == nb.neighborAsn) { loop = true; break; }
                    if (loop) continue;
                }

                BgpRoute relay;
                relay.prefix         = learned.prefix;
                relay.nextHop        = myFaceIp;
                relay.neighborNodeId = n.id;
                if (nb.ibgp) {
                    relay.asPath = learned.asPath;       // iBGP: unchanged
                } else {
                    relay.asPath = {n.localAsn};         // eBGP: prepend local AS
                    for (auto asn : learned.asPath) relay.asPath.push_back(asn);
                }
                // Stamp ORIGINATOR_ID and append cluster ID when RR reflects to client
                if (n.isRouteReflector && nb.ibgp) {
                    relay.originatorId = (learned.originatorId != 0)
                                         ? learned.originatorId
                                         : (uint32_t)learned.neighborNodeId;
                    relay.clusterList  = learned.clusterList;
                    relay.clusterList.push_back((uint32_t)n.id);
                } else {
                    relay.originatorId = learned.originatorId;
                    relay.clusterList  = learned.clusterList;
                }
                pending.push_back({nb.neighborNodeId, relay});
            }
        }
    }
    for (auto& [tid, r] : pending)
        if (auto* nd = FindNodeMut(nodes, tid)) nd->bgpRoutes.push_back(r);
```

- [ ] **Step 3: Build clean**

```bash
make 2>&1 | tail -5
```

Expected: single compile line, no errors or warnings. The new fields (`originatorId`, `clusterList`, `isRouteReflector`) were added in Task 1 so this compiles immediately.

- [ ] **Step 4: Manual engine smoke-test**

Run `./packet-path`. Open Level 7 (press `7`). The level has RTR-1 (AS100), RTR-2 (AS100), RTR-3 (AS200). Neither RTR-1 nor RTR-2 has `isRouteReflector = true`, so behavior should be identical to before: iBGP split-horizon active, RTR-2 learns 172.16.0.0/24 via RTR-1. Set ASNs (100/100/200) and verify the Level 7 BGP RIB is unchanged.

- [ ] **Step 5: Commit**

```bash
git add src/BgpEngine.cpp
git commit -m "feat(rr): Route Reflector relay — CLUSTER_LIST loop prevention, ORIGINATOR_ID stamping"
```

---

### Task 4: BGP tab UI — Route Reflector toggle + client label

**Files:**

- Modify: `src/ConfigPanel.h`
- Modify: `src/ConfigPanel.cpp`
- Modify: `src/NetworkCanvas.cpp`

**What and why:** Students need a toggle in the BGP tab to mark a router as a Route Reflector. The toggle appears only when BGP is enabled AND ASN > 0 (the "Set ASN..." warning takes its y-slot when ASN == 0). When a router IS an RR, the NEIGHBORS section header changes to CLIENTS to reinforce the concept.

Layout after this task (BGP tab y-coordinates):

- `y=120`: BGP Enable/Disable button (26px, unchanged)
- `y=152`: ASN row (unchanged)
- `y=182`: Route Reflector toggle (22px) — shown when `localAsn > 0`; OR "Set ASN..." warning — shown when `localAsn == 0`
- `y=210`: NEIGHBORS (or CLIENTS) section header

- [ ] **Step 1: Declare `PnlBgpRrRect()` in ConfigPanel.h**

Open `src/ConfigPanel.h`. Find the BGP rect declarations (around line 33):

```cpp
Rectangle PnlBgpTabRect();
Rectangle PnlBgpToggleRect();
Rectangle PnlBgpAsnRect();
```

Replace with:

```cpp
Rectangle PnlBgpTabRect();
Rectangle PnlBgpToggleRect();
Rectangle PnlBgpAsnRect();
Rectangle PnlBgpRrRect();
```

- [ ] **Step 2: Implement `PnlBgpRrRect()` in ConfigPanel.cpp**

Open `src/ConfigPanel.cpp`. Find the `PnlBgpAsnRect()` implementation (around line 46):

```cpp
Rectangle PnlBgpAsnRect() {
    return {(float)(CANVAS_W + 56), 152.0f, (float)(PANEL_W - 68), 22.0f};
}
```

Add `PnlBgpRrRect()` directly after it:

```cpp
Rectangle PnlBgpAsnRect() {
    return {(float)(CANVAS_W + 56), 152.0f, (float)(PANEL_W - 68), 22.0f};
}
Rectangle PnlBgpRrRect() {
    return {(float)(CANVAS_W + 12), 182.0f, (float)(PANEL_W - 24), 22.0f};
}
```

- [ ] **Step 3: Update `DrawBgpTab()` in NetworkCanvas.cpp**

Open `src/NetworkCanvas.cpp`. Find the section after the ASN field draw (around line 653):

```cpp
    if (n->localAsn == 0)
        DrawText("Set ASN to form sessions", CANVAS_W + 12, y + 28, 10,
                 Color{234,179,8,255});

    y = 192;

    // ── Neighbors ─────────────────────────────────────────────────────
    DrawText("NEIGHBORS", CANVAS_W + 12, y, 10, Color{71,85,105,255});
```

Replace this section with:

```cpp
    if (n->localAsn == 0) {
        DrawText("Set ASN to form sessions", CANVAS_W + 12, y + 30, 10,
                 Color{234,179,8,255});
    } else {
        // Route Reflector toggle (only when ASN is set)
        Rectangle rrRect = PnlBgpRrRect();
        Color rrCol = n->isRouteReflector ? Color{167,139,250,255} : Color{51,65,85,255};
        DrawRectangleRec(rrRect, rrCol);
        DrawRectangleLinesEx(rrRect, 1.0f, Color{71,85,105,255});
        const char* rrLabel = n->isRouteReflector ? "Route Reflector: ON"
                                                  : "Route Reflector: OFF";
        int rrTw = MeasureText(rrLabel, 11);
        DrawText(rrLabel, (int)(rrRect.x + (rrRect.width - rrTw) / 2),
                 (int)(rrRect.y + 5), 11,
                 n->isRouteReflector ? Color{15,23,42,255} : Color{148,163,184,255});
    }

    y = 210;

    // ── Neighbors ─────────────────────────────────────────────────────
    const char* neighborHeader = n->isRouteReflector ? "CLIENTS" : "NEIGHBORS";
    DrawText(neighborHeader, CANVAS_W + 12, y, 10, Color{71,85,105,255});
```

Also add the `#include "ConfigPanel.h"` reference check — `PnlBgpRrRect()` is declared there. `NetworkCanvas.cpp` already includes `ConfigPanel.h` via the existing `PnlBgpToggleRect` / `PnlBgpAsnRect` calls, so no new include is needed.

- [ ] **Step 4: Build clean**

```bash
make 2>&1 | tail -5
```

Expected: single compile line, no errors.

- [ ] **Step 5: Manual visual check**

Run `./packet-path`. Open Level 7 (press `7`). Select RTR-1. Click BGP tab.

- With ASN=0: "Set ASN to form sessions" shows at the usual spot. No RR toggle yet. ✓
- Set ASN=100 (click ASN field, type 100, Enter). "Route Reflector: OFF" button appears in purple/dark. ✓
- NEIGHBORS label shows (not CLIENTS since RTR-1 is not an RR). ✓

- [ ] **Step 6: Commit**

```bash
git add src/ConfigPanel.h src/ConfigPanel.cpp src/NetworkCanvas.cpp
git commit -m "feat(rr): add Route Reflector toggle button and CLIENTS label to BGP tab"
```

---

### Task 5: Input handler — wire RR toggle click + extend level count to 8

**Files:**

- Modify: `src/main.cpp`

**What and why:** The RR toggle button drawn in Task 4 has no click handler yet — clicks pass through. This task wires the click, toggles `isRouteReflector`, and extends the level count from 7 to 8 (four occurrences: the loop bound, two `currentLevel < 7` guards, and the HUD text).

- [ ] **Step 1: Find the BGP tab click handler**

In `src/main.cpp`, search for `TAB_BGP`. The click handler block (around line 601) looks like:

```cpp
                if (ps.activeTab == TAB_BGP) {
                    // ...
                        if (CheckCollisionPointRec(screenMouse, PnlBgpToggleRect())) {
                            selNode->bgpEnabled = !selNode->bgpEnabled;
                            // ...
                        }
                        if (selNode->bgpEnabled &&
                            CheckCollisionPointRec(screenMouse, PnlBgpAsnRect())) {
                            // ...
                        }
```

- [ ] **Step 2: Add RR rect click after the ASN rect check**

Find the line:

```cpp
                        if (selNode->bgpEnabled &&
                            CheckCollisionPointRec(screenMouse, PnlBgpAsnRect())) {
```

After the closing brace `}` of that `if` block, add:

```cpp
                        if (selNode->bgpEnabled && selNode->localAsn > 0 &&
                            CheckCollisionPointRec(screenMouse, PnlBgpRrRect())) {
                            selNode->isRouteReflector = !selNode->isRouteReflector;
                        }
```

- [ ] **Step 3: Extend level count — loop bound**

Find line 66 (the keyboard shortcut loop):

```cpp
                for (int k = 1; k <= 7; ++k) {
```

Change the comment on line 64 and the loop bound on line 66:

```cpp
            // Level shortcuts: 1–8 load JSON levels, 0 returns to sandbox
            if (ps.activePortAreaField == -1) {
                for (int k = 1; k <= 8; ++k) {
```

- [ ] **Step 4: Extend level count — "Next Level" win overlay guard**

Find (around line 185):

```cpp
                           currentLevel < 7) {
```

Change to:

```cpp
                           currentLevel < 8) {
```

- [ ] **Step 5: Extend level count — win overlay draw**

Find (around line 804):

```cpp
                DrawWinOverlay(activeLevelDef, currentLevel < 7);
```

Change to:

```cpp
                DrawWinOverlay(activeLevelDef, currentLevel < 8);
```

- [ ] **Step 6: Extend level count — HUD text**

Find (around line 810):

```cpp
                     "Drag-port=Cable  Esc=Cancel  1-7=Level  0=Sandbox",
```

Change to:

```cpp
                     "Drag-port=Cable  Esc=Cancel  1-8=Level  0=Sandbox",
```

- [ ] **Step 7: Build clean**

```bash
make 2>&1 | tail -5
```

Expected: single compile line, no errors or warnings.

- [ ] **Step 8: Manual click test**

Run `./packet-path`. Open Level 7, select RTR-1, open BGP tab. Set ASN=100. Click "Route Reflector: OFF" button. It should toggle to "Route Reflector: ON" (purple, light text). NEIGHBORS → CLIENTS label. Click again → back to OFF. ✓

- [ ] **Step 9: Commit**

```bash
git add src/main.cpp
git commit -m "feat(rr): wire Route Reflector toggle click; extend level count to 8"
```

---

### Task 6: Level 8 — Route Reflection teaching scenario

**Files:**

- Create: `levels/level_08.json`

**What and why:** Level 8 teaches that a Route Reflector inside an AS enables clients to learn external routes without a direct eBGP session to the border router. Topology: PC-A → RTR-B (AS100, client) ↔ iBGP ↔ RTR-A (AS100, RR, pre-set `isRouteReflector=true`) ↔ eBGP ↔ RTR-D (AS200) → PC-B. The student sets the three ASNs; the RR toggle is pre-wired on RTR-A. RTR-B's RIB shows 172.16.0.0/24 reflected from RTR-A. Win condition: PC-A → PC-B.

IP plan:

| Device | Port | IP | Role |
| -------- | ------ | ---- | ------ |
| PC-A | port1 | 10.0.0.2/24 | source |
| RTR-B | port3 | 10.0.0.1/24 | LAN facing PC-A |
| RTR-B | port1 | 10.1.0.2/30 | link to RTR-A |
| RTR-A | port3 | 10.1.0.1/30 | link to RTR-B |
| RTR-A | port1 | 10.2.0.1/30 | link to RTR-D |
| RTR-D | port3 | 10.2.0.2/30 | link to RTR-A |
| RTR-D | port1 | 172.16.0.1/24 | LAN facing PC-B |
| PC-B | port3 | 172.16.0.2/24 | destination |

RTR-B advertises `10.0.0.0/24`. RTR-D advertises `172.16.0.0/24`. RTR-A has no bgpNetworks (auto-advertises connected). `isRouteReflector=true` is pre-set on RTR-A so the student doesn't need to toggle it — the level teaches them to recognize and use the RR by observing the reflected route in RTR-B's RIB.

- [ ] **Step 1: Create `levels/level_08.json`**

```json
{
  "id": 8,
  "title": "BGP Route Reflection",
  "briefing": "A Route Reflector (RR) lets clients inside an AS learn external routes without a full mesh. RTR-A is pre-configured as the RR. Set RTR-B's ASN to 100 (BGP tab → ASN → type 100 → Enter). Set RTR-A's ASN to 100 — an iBGP session forms. Set RTR-D's ASN to 200 — an eBGP session forms. Open RTR-B's BGP tab: the RIB should show 172.16.0.0/24 reflected from RTR-A. Then send a packet from PC-A to PC-B.",
  "devices": [
    {
      "id": 1, "label": "PC-A", "type": "PC",
      "x": -500, "y": 0,
      "portIp1": "10.0.0.2/24",
      "staticRoutes": [{"dest": "0.0.0.0/0", "nextHop": "10.0.0.1"}]
    },
    {
      "id": 2, "label": "RTR-B", "type": "ROUTER",
      "x": -220, "y": 0,
      "portIp3": "10.0.0.1/24",
      "portIp1": "10.1.0.2/30",
      "bgpEnabled": true,
      "localAsn": 0,
      "bgpNetworks": ["10.0.0.0/24"]
    },
    {
      "id": 3, "label": "RTR-A", "type": "ROUTER",
      "x": 60, "y": 0,
      "portIp3": "10.1.0.1/30",
      "portIp1": "10.2.0.1/30",
      "bgpEnabled": true,
      "isRouteReflector": true,
      "localAsn": 0
    },
    {
      "id": 4, "label": "RTR-D", "type": "ROUTER",
      "x": 340, "y": 0,
      "portIp3": "10.2.0.2/30",
      "portIp1": "172.16.0.1/24",
      "bgpEnabled": true,
      "localAsn": 0,
      "bgpNetworks": ["172.16.0.0/24"]
    },
    {
      "id": 5, "label": "PC-B", "type": "PC",
      "x": 560, "y": 0,
      "portIp3": "172.16.0.2/24",
      "staticRoutes": [{"dest": "0.0.0.0/0", "nextHop": "172.16.0.1"}]
    }
  ],
  "cables": [
    {"from": 1, "fromPort": 1, "to": 2, "toPort": 3},
    {"from": 2, "fromPort": 1, "to": 3, "toPort": 3},
    {"from": 3, "fromPort": 1, "to": 4, "toPort": 3},
    {"from": 4, "fromPort": 1, "to": 5, "toPort": 3}
  ],
  "winConditions": [
    {
      "src": "PC-A",
      "dst": "PC-B",
      "description": "PC-A reaches PC-B via iBGP route reflection"
    }
  ]
}
```

- [ ] **Step 2: Build clean (no code change, just verify JSON is valid)**

```bash
make 2>&1 | tail -5
python3 -c "import json; json.load(open('levels/level_08.json')); print('JSON valid')"
```

Expected: compile line + `JSON valid`.

- [ ] **Step 3: Full end-to-end test**

Run `./packet-path`. Press `8` to load Level 8.

1. Select RTR-B → BGP tab. ASN=0, RR toggle hidden. ✓
2. Set RTR-B ASN=100. RR toggle appears: Route Reflector: OFF. ✓
3. Select RTR-A → BGP tab. "Route Reflector: ON" button shows in purple (pre-configured). Header shows "CLIENTS". ✓  
4. Set RTR-A ASN=100. RTR-A CLIENTS: RTR-B (iBGP, ESTAB). ✓
5. Set RTR-D ASN=200. RTR-A CLIENTS list: RTR-B (iBGP, ESTAB). RTR-A NEIGHBORS also shows RTR-D (eBGP, ESTAB). ✓
6. Select RTR-B → BGP tab → BGP RIB. Should show `172.16.0.0/24` with next-hop `10.1.0.1` (RTR-A's port toward RTR-B). ✓
7. Send packet PC-A → PC-B. Win condition triggers. ✓

- [ ] **Step 4: Commit**

```bash
git add levels/level_08.json
git commit -m "feat(rr): add Level 8 BGP Route Reflection teaching scenario"
```

---

## Self-Review

### Spec coverage

- [x] `isRouteReflector` flag on DeviceNode — Task 1
- [x] Automatic client behavior (all same-AS iBGP peers are clients) — Task 3 (no per-neighbor flag needed; all iBGP neighbors of an RR are clients by definition in Option A design)
- [x] Route reflection logic in BgpEngine Phase 2b — Task 3
- [x] CLUSTER_LIST loop prevention — Task 3
- [x] ORIGINATOR_ID stamping — Task 3
- [x] BGP tab Route Reflector toggle — Tasks 4+5
- [x] CLIENTS label when RR — Task 4
- [x] Level loading parses `isRouteReflector` — Task 2
- [x] Level 8 teaching scenario — Task 6
- [x] Level count extended to 8 (4 occurrences in main.cpp) — Task 5

### Placeholder scan

No TBDs, TODOs, or vague steps found.

### Type consistency

- `n.isRouteReflector` — field name matches across Device.h (definition), BgpEngine.cpp (read), NetworkCanvas.cpp (read), main.cpp (write).
- `relay.originatorId` / `relay.clusterList` — field names match BgpRoute definition in Device.h.
- `PnlBgpRrRect()` — declared in ConfigPanel.h, defined in ConfigPanel.cpp, called in NetworkCanvas.cpp and main.cpp.
- All y-coordinates in DrawBgpTab: ASN stays at 152, RR at 182, NEIGHBORS/CLIENTS section at 210.

### Ambiguity resolved

- "Automatic client behavior" in Option A means: no per-neighbor client flag. Any same-AS iBGP neighbor of an RR is implicitly a client. This is implemented in BgpEngine by the `n.isRouteReflector` branch — the RR's iBGP neighbors are all treated as clients without any additional flag.
- ORIGINATOR_ID is set to `learned.neighborNodeId` (the client's node ID) on first reflection. If already set (route was reflected by another RR), it's preserved.
- Level 8 pre-sets `isRouteReflector=true` on RTR-A so students observe the effect without having to discover the toggle — the briefing draws their attention to RTR-B's RIB instead.
