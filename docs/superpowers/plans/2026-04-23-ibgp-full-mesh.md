# iBGP Full Mesh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add iBGP (intra-AS BGP) full-mesh sessions to Packet Path — routers with the same AS number and a direct cable automatically form iBGP sessions, border routers relay eBGP-learned routes inward with next-hop-self, and a new Level 7 teaches the concept with a 3-router (2 internal + 1 external) topology.

**Architecture:** Option C — Full Mesh now, Route Reflection hook later. iBGP sessions form on direct cables between same-AS routers (parallel to eBGP which requires different ASes). `BgpNeighbor` gets an `ibgp` bool flag (Phase 2 RR hook: add `isRouteReflector`). `UpdateBgp()` Phase 1 forms both eBGP and iBGP sessions from the cable scan. Phase 2a advertises own networks to all peers (iBGP NHS = cable-facing IP). Phase 2b relay applies iBGP split-horizon (no iBGP→iBGP relay) and preserves AS-path for iBGP peers. No changes needed to `GetRoutingTable`, `SimulationEngine`, or `TraceModal` — `ROUTE_BGP` already covers iBGP routes.

**Tech Stack:** C++17, raylib 5.x, nlohmann/json, make (wildcard build — any `src/*.cpp` auto-included)

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `src/Device.h` | Modify | Add `bool ibgp = false` to `BgpNeighbor` struct |
| `src/BgpEngine.cpp` | Modify | Phase 1: form iBGP sessions (same AS, direct cable); Phase 2b: iBGP split-horizon + AS-path preservation |
| `src/NetworkCanvas.cpp` | Modify | `DrawBgpTab` NEIGHBORS section: add iBGP/eBGP type badge column |
| `levels/level_07.json` | Create | 5-device iBGP teaching scenario |
| `src/main.cpp` | Modify | Level count 6→7 in three places |

---

### Task 1: Add `ibgp` flag to `BgpNeighbor`

**Files:**
- Modify: `src/Device.h:21-26`

This is the RR extension point: Phase 2 adds `isRouteReflector` beside this flag. No other changes needed in this task.

- [ ] **Step 1: Add `ibgp` field to `BgpNeighbor` in `src/Device.h`**

Current struct (lines 21–26):
```cpp
struct BgpNeighbor {
    std::string neighborIp;        // peer's port IP on shared cable (no mask)
    int         neighborNodeId = -1;
    uint32_t    neighborAsn    = 0;
    bool        established    = false;
};
```

Replace with:
```cpp
struct BgpNeighbor {
    std::string neighborIp;        // peer's port IP on shared cable (no mask)
    int         neighborNodeId = -1;
    uint32_t    neighborAsn    = 0;
    bool        established    = false;
    bool        ibgp           = false;  // same-AS peer; Phase 2: add isRouteReflector
};
```

- [ ] **Step 2: Build to confirm the change compiles cleanly**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | tail -5
```

Expected: `Linking PacketPath` and no errors. (All existing code uses aggregate init or default-constructed `BgpNeighbor`; adding a bool with a default doesn't break any caller.)

- [ ] **Step 3: Commit**

```bash
git add src/Device.h
git commit -m "feat(ibgp): add ibgp flag to BgpNeighbor (RR extension point)"
```

---

### Task 2: iBGP session formation in `BgpEngine.cpp`

**Files:**
- Modify: `src/BgpEngine.cpp:30-60`

Phase 1 currently skips same-AS pairs with `continue`. Remove that guard and form iBGP sessions. The rest of Phase 2a and dedup require no changes: the cable-facing IP is the correct NHS for directly-cabled iBGP peers, and Phase 2a already iterates all `bgpNeighbors`.

- [ ] **Step 1: Replace Phase 1 in `src/BgpEngine.cpp`**

Current Phase 1 (lines 30–60):
```cpp
    // ── Phase 1: Form eBGP sessions ──────────────────────────────────────
    // One session per cable where both endpoints: ROUTER, bgpEnabled, localAsn != 0,
    // and different AS numbers.
    for (const auto& cable : cables) {
        DeviceNode* a = FindNodeMut(nodes, cable.fromId);
        DeviceNode* b = FindNodeMut(nodes, cable.toId);
        if (!a || !b) continue;
        // Raw pointers are stable: no nodes are added/removed inside this loop
        if (a->type != ROUTER || b->type != ROUTER) continue;
        if (!a->bgpEnabled || !b->bgpEnabled) continue;
        if (a->localAsn == 0 || b->localAsn == 0) continue;
        if (a->localAsn == b->localAsn) continue;  // iBGP not supported

        std::string aIp = FaceIp(*a, cable);
        std::string bIp = FaceIp(*b, cable);
        if (aIp.empty() || bIp.empty()) continue;

        BgpNeighbor nbA;
        nbA.neighborIp     = StripMask(bIp);
        nbA.neighborNodeId = b->id;
        nbA.neighborAsn    = b->localAsn;
        nbA.established    = true;
        a->bgpNeighbors.push_back(nbA);

        BgpNeighbor nbB;
        nbB.neighborIp     = StripMask(aIp);
        nbB.neighborNodeId = a->id;
        nbB.neighborAsn    = a->localAsn;
        nbB.established    = true;
        b->bgpNeighbors.push_back(nbB);
    }
```

Replace with:
```cpp
    // ── Phase 1: Form BGP sessions (eBGP = different AS, iBGP = same AS) ─
    // One session per cable where both endpoints: ROUTER, bgpEnabled, localAsn != 0.
    // Same AS → iBGP (full mesh, direct cable); different AS → eBGP.
    for (const auto& cable : cables) {
        DeviceNode* a = FindNodeMut(nodes, cable.fromId);
        DeviceNode* b = FindNodeMut(nodes, cable.toId);
        if (!a || !b) continue;
        if (a->type != ROUTER || b->type != ROUTER) continue;
        if (!a->bgpEnabled || !b->bgpEnabled) continue;
        if (a->localAsn == 0 || b->localAsn == 0) continue;

        std::string aIp = FaceIp(*a, cable);
        std::string bIp = FaceIp(*b, cable);
        if (aIp.empty() || bIp.empty()) continue;

        bool sameAs = (a->localAsn == b->localAsn);

        BgpNeighbor nbA;
        nbA.neighborIp     = StripMask(bIp);
        nbA.neighborNodeId = b->id;
        nbA.neighborAsn    = b->localAsn;
        nbA.established    = true;
        nbA.ibgp           = sameAs;
        a->bgpNeighbors.push_back(nbA);

        BgpNeighbor nbB;
        nbB.neighborIp     = StripMask(aIp);
        nbB.neighborNodeId = a->id;
        nbB.neighborAsn    = a->localAsn;
        nbB.established    = true;
        nbB.ibgp           = sameAs;
        b->bgpNeighbors.push_back(nbB);
    }
```

- [ ] **Step 2: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | tail -5
```

Expected: clean build, no errors.

- [ ] **Step 3: Smoke-test Level 6 still works**

Run `./PacketPath`, load Level 6, set RTR-1 ASN=100 and RTR-2 ASN=200 (different AS). Both should show ESTAB as eBGP peers. iBGP flag makes no difference here.

- [ ] **Step 4: Commit**

```bash
git add src/BgpEngine.cpp
git commit -m "feat(ibgp): form iBGP sessions on same-AS direct cables"
```

---

### Task 3: iBGP route relay with split-horizon in `BgpEngine.cpp`

**Files:**
- Modify: `src/BgpEngine.cpp:108-146` (Phase 2b)

Phase 2b currently relays all routes to all peers with eBGP AS-path prepend. With iBGP:
- **iBGP split-horizon**: never relay a route learned from an iBGP peer to another iBGP peer (full-mesh rule — everyone hears directly from the border router).
- **iBGP relay**: no AS-path modification; next-hop = cable-facing IP (already the correct NHS since sessions are on direct cables).
- **eBGP relay**: unchanged — AS-path prepend, loop prevention.

- [ ] **Step 1: Replace Phase 2b in `src/BgpEngine.cpp`**

Current Phase 2b (lines 108–146):
```cpp
    // ── Phase 2b: Relay routes learned in 2a to other peers ──────────────
    // Enables 3-AS linear chains (one relay hop: AS100→AS200→AS300).
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

                // AS path loop prevention: skip if peer's ASN is already in path
                bool loop = false;
                for (auto asn : learned.asPath)
                    if (asn == nb.neighborAsn) { loop = true; break; }
                if (loop) continue;

                BgpRoute relay;
                relay.prefix         = learned.prefix;
                relay.nextHop        = myFaceIp;
                relay.asPath         = {n.localAsn};
                for (auto asn : learned.asPath) relay.asPath.push_back(asn);
                relay.neighborNodeId = n.id;
                pending.push_back({nb.neighborNodeId, relay});
            }
        }
    }
    for (auto& [tid, r] : pending)
        if (auto* nd = FindNodeMut(nodes, tid)) nd->bgpRoutes.push_back(r);
```

Replace with:
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

- [ ] **Step 2: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | tail -5
```

Expected: clean, no errors.

- [ ] **Step 3: Verify Level 6 eBGP still works**

Run `./PacketPath`, load Level 6, set RTR-1=100 / RTR-2=200. Both should reach ESTAB and BGP RIBs should be populated as before. Send PC-A→PC-B — should succeed.

- [ ] **Step 4: Commit**

```bash
git add src/BgpEngine.cpp
git commit -m "feat(ibgp): iBGP relay with split-horizon and AS-path preservation"
```

---

### Task 4: Show iBGP/eBGP session type in BGP tab

**Files:**
- Modify: `src/NetworkCanvas.cpp:660-678` (NEIGHBORS loop inside `DrawBgpTab`)

Add a session-type badge ("iBGP" in purple / "eBGP" in orange) between the IP and the ESTAB/DOWN state. Drop the separate ASN column — the type badge communicates the relationship. Adjust column positions to fit three items: IP, type, state.

- [ ] **Step 1: Replace the NEIGHBORS loop in `DrawBgpTab` in `src/NetworkCanvas.cpp`**

Current NEIGHBORS loop (lines 660–678):
```cpp
    // ── Neighbors ─────────────────────────────────────────────────────
    DrawText("NEIGHBORS", CANVAS_W + 12, y, 10, Color{71,85,105,255});
    y += 14;
    if (n->bgpNeighbors.empty()) {
        DrawText("(none)", CANVAS_W + 16, y, 10, Color{71,85,105,255});
        y += 14;
    } else {
        for (const auto& nb : n->bgpNeighbors) {
            if (y > CANVAS_H - 80) break;  // leave room for BGP RIB section below
            DrawText(nb.neighborIp.c_str(),  CANVAS_W + 12,  y, 10, WHITE);
            std::string asnTag = "AS" + std::to_string(nb.neighborAsn);
            if (asnTag.size() > 9) asnTag = asnTag.substr(0, 8) + "\xe2\x80\xa6";
            DrawText(asnTag.c_str(), CANVAS_W + 105, y, 10, Color{253,186,116,255});
            const char* state = nb.established ? "ESTAB" : "DOWN";
            Color stCol = nb.established ? Color{34,197,94,255} : Color{239,68,68,255};
            DrawText(state, CANVAS_W + 155, y, 10, stCol);
            y += 14;
        }
    }
```

Replace with:
```cpp
    // ── Neighbors ─────────────────────────────────────────────────────
    DrawText("NEIGHBORS", CANVAS_W + 12, y, 10, Color{71,85,105,255});
    y += 14;
    if (n->bgpNeighbors.empty()) {
        DrawText("(none)", CANVAS_W + 16, y, 10, Color{71,85,105,255});
        y += 14;
    } else {
        for (const auto& nb : n->bgpNeighbors) {
            if (y > CANVAS_H - 80) break;
            std::string ip = nb.neighborIp.size() > 12
                             ? nb.neighborIp.substr(0, 11) + "\xe2\x80\xa6"
                             : nb.neighborIp;
            DrawText(ip.c_str(), CANVAS_W + 12, y, 10, WHITE);
            const char* typeLabel = nb.ibgp ? "iBGP" : "eBGP";
            Color typeCol = nb.ibgp ? Color{167,139,250,255}   // purple
                                    : Color{253,186,116,255};  // orange
            DrawText(typeLabel, CANVAS_W + 102, y, 10, typeCol);
            const char* state = nb.established ? "ESTAB" : "DOWN";
            Color stCol = nb.established ? Color{34,197,94,255} : Color{239,68,68,255};
            DrawText(state, CANVAS_W + 142, y, 10, stCol);
            y += 14;
        }
    }
```

- [ ] **Step 2: Build**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | tail -5
```

Expected: clean.

- [ ] **Step 3: Verify UI in Level 6**

Run `./PacketPath`, load Level 6, configure ASNs. RTR-1's BGP tab NEIGHBORS should now show `10.1.0.2  eBGP  ESTAB` (orange "eBGP" badge, green "ESTAB"). Confirm previous ASN column is gone and columns don't overlap.

- [ ] **Step 4: Commit**

```bash
git add src/NetworkCanvas.cpp
git commit -m "feat(ibgp): show iBGP/eBGP session type badge in BGP tab neighbors"
```

---

### Task 5: Level 7 — iBGP teaching scenario

**Files:**
- Create: `levels/level_07.json`

**Topology:**
```
PC-A ─── RTR-2 ─────── RTR-1 ─────── RTR-3 ─── PC-B
         AS100          AS100          AS200
         internal       border         external
         [iBGP ←──────────→ eBGP]
         [OSPF ←──────────]
```

**IP addressing:**
- PC-A: 10.0.0.2/24, default via 10.0.0.1
- RTR-2 port3: 10.0.0.1/24 (to PC-A) | port1: 10.1.0.1/30 (to RTR-1)
- RTR-1 port3: 10.1.0.2/30 (from RTR-2) | port1: 10.2.0.1/30 (to RTR-3)
- RTR-3 port3: 10.2.0.2/30 (from RTR-1) | port1: 172.16.0.1/24 (to PC-B)
- PC-B: 172.16.0.2/24, default via 172.16.0.1

**Pre-configuration:**
- OSPF area 0 on the RTR-2 ↔ RTR-1 link (RTR-2 ospfArea1=0, RTR-1 ospfArea3=0) — enables OSPF route resolution for iBGP next-hops
- BGP enabled on all three routers; RTR-3 pre-configured with ASN=200
- bgpNetworks=["10.0.0.0/24"] on RTR-2, bgpNetworks=["172.16.0.0/24"] on RTR-3

**Student task:** Set RTR-1 ASN=100 and RTR-2 ASN=100. iBGP session forms on the RTR-1↔RTR-2 cable; eBGP session forms on the RTR-1↔RTR-3 cable. RTR-2's BGP RIB shows 172.16.0.0/24 via RTR-1's IP (iBGP next-hop-self). Send PC-A→PC-B.

**Route flow:**
- Phase 2a: RTR-3→RTR-1 (eBGP): 172.16.0.0/24, next-hop=10.2.0.2, path=[200]
- Phase 2a: RTR-2→RTR-1 (iBGP): 10.0.0.0/24, next-hop=10.1.0.1, path=[100]
- Phase 2b: RTR-1→RTR-2 (iBGP relay): 172.16.0.0/24, next-hop=10.1.0.2, path=[200] ✓
- Phase 2b: RTR-1→RTR-3 (eBGP relay): 10.0.0.0/24, next-hop=10.2.0.1, path=[100,100] ✓

- [ ] **Step 1: Create `levels/level_07.json`**

```json
{
  "id": 7,
  "title": "iBGP Full Mesh",
  "briefing": "AS100 has two routers: RTR-2 (internal) and RTR-1 (border). RTR-3 is the external AS200 peer. OSPF is already running between RTR-2 and RTR-1. Set RTR-1 and RTR-2 to the same ASN (100) in their BGP tabs. Once both are set, RTR-2 should show an iBGP session to RTR-1 (purple badge) and its BGP RIB should list 172.16.0.0/24 — the external prefix learned via RTR-1. Then send a packet from PC-A to PC-B.",
  "devices": [
    {
      "id": 1, "label": "PC-A", "type": "PC",
      "x": -560, "y": 0,
      "portIp1": "10.0.0.2/24",
      "staticRoutes": [{"dest": "0.0.0.0/0", "nextHop": "10.0.0.1"}]
    },
    {
      "id": 2, "label": "RTR-2", "type": "ROUTER",
      "x": -280, "y": 0,
      "portIp3": "10.0.0.1/24",
      "portIp1": "10.1.0.1/30",
      "ospfEnabled": true,
      "ospfArea1": 0,
      "bgpEnabled": true,
      "localAsn": 0,
      "bgpNetworks": ["10.0.0.0/24"]
    },
    {
      "id": 3, "label": "RTR-1", "type": "ROUTER",
      "x": 0, "y": 0,
      "portIp3": "10.1.0.2/30",
      "portIp1": "10.2.0.1/30",
      "ospfEnabled": true,
      "ospfArea3": 0,
      "bgpEnabled": true,
      "localAsn": 0
    },
    {
      "id": 4, "label": "RTR-3", "type": "ROUTER",
      "x": 280, "y": 0,
      "portIp3": "10.2.0.2/30",
      "portIp1": "172.16.0.1/24",
      "bgpEnabled": true,
      "localAsn": 200,
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
      "description": "PC-A reaches PC-B via iBGP + eBGP"
    }
  ]
}
```

- [ ] **Step 2: Load level 7 in game and verify topology renders**

Run `./PacketPath`, press `7`. Confirm five devices appear in a linear layout. RTR-3's BGP tab should already show ASN=200. RTR-1 and RTR-2 should show ASN=0 with "Set ASN to form sessions" hint.

- [ ] **Step 3: Verify the full iBGP teaching flow**

1. In RTR-2's BGP tab: set ASN=100, press Enter.
2. In RTR-1's BGP tab: set ASN=100, press Enter.
3. Check RTR-2's BGP tab NEIGHBORS: should show `10.1.0.2  iBGP  ESTAB` (purple badge).
4. Check RTR-2's BGP tab BGP RIB: should list `172.16.0.0/24 | 10.1.0.2 | 200`.
5. Check RTR-1's BGP tab NEIGHBORS: should show `10.1.0.1  iBGP  ESTAB` and `10.2.0.2  eBGP  ESTAB`.
6. Send packet PC-A→PC-B. Should succeed with route hops through RTR-2→RTR-1→RTR-3.

- [ ] **Step 4: Commit**

```bash
git add levels/level_07.json
git commit -m "feat(ibgp): add Level 7 iBGP teaching scenario"
```

---

### Task 6: Update level count 6→7 in `main.cpp`

**Files:**
- Modify: `src/main.cpp:185`, `src/main.cpp:804`, `src/main.cpp:810`

Three independent one-line changes in `main.cpp`. All guard the "next level" button and HUD display.

- [ ] **Step 1: Update `currentLevel < 6` at line 185**

Current (line 185):
```cpp
                           currentLevel < 6) {
```

Change to:
```cpp
                           currentLevel < 7) {
```

- [ ] **Step 2: Update `DrawWinOverlay` at line 804**

Current (line 804):
```cpp
                DrawWinOverlay(activeLevelDef, currentLevel < 6);
```

Change to:
```cpp
                DrawWinOverlay(activeLevelDef, currentLevel < 7);
```

- [ ] **Step 3: Update key hint string at line 810**

Current (line 810):
```cpp
                     "Drag-port=Cable  Esc=Cancel  1-6=Level  0=Sandbox",
```

Change to:
```cpp
                     "Drag-port=Cable  Esc=Cancel  1-7=Level  0=Sandbox",
```

- [ ] **Step 4: Build and verify level progression**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | tail -5
```

Run `./PacketPath`. Complete Level 6 win condition → "Next Level" button should appear and advance to Level 7. At Level 7 win, "Next Level" button should not appear (max level reached). Key hint at bottom shows "1-7=Level".

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat(ibgp): extend level count to 7"
```
