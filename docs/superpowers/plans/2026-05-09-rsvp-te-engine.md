# RSVP-TE Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement an RSVP-TE engine on top of the existing MPLS/LDP stack — instant-state tunnel computation with CSPF/explicit path selection, per-port bandwidth config, colored canvas overlays, and a "Simulate Setup" PATH/RESV replay animation.

**Architecture:** `RsvpEngine.cpp` runs `UpdateRsvp()` each frame (same pattern as `UpdateLdp`), storing tunnel state and TE LFIB entries directly on each `DeviceNode`. `SimulationEngine` consults `teLfib` before `lfib` at head-end and transit nodes. ConfigPanel gains a `TAB_TE` tab with inline-expand tunnel list; a separate `RsvpReplayState` in `main.cpp` drives the Simulate Setup animation.

**Tech Stack:** C++17, raylib, existing OSPF Dijkstra graph as CSPF topology source.

**Spec:** `docs/superpowers/specs/2026-05-09-rsvp-te-engine-design.md`

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `src/Device.h` | Modify | Add `TeLfibEntry`, `TeTunnel` structs; `DeviceNode` TE fields; `HopDecision.tunnelId` |
| `src/Packet.h` | Modify | Add `PacketAnim.overrideColor` for RSVP packet coloring |
| `src/RsvpEngine.h` | Create | `UpdateRsvp` + `ResolveExplicitHops` declarations; `RSVP_PATH_HOP_DELAY` constant |
| `src/RsvpEngine.cpp` | Create | BW map, CSPF Dijkstra, explicit path check, label allocation, teLfib, MBB |
| `src/SimulationEngine.cpp` | Modify | TE forwarding: check `teLfib` before `lfib`; set `HopDecision.tunnelId` |
| `src/ConfigPanel.h` | Modify | Add `TAB_TE`; TE `PanelState` fields; new rect declarations |
| `src/ConfigPanel.cpp` | Modify | Tab array, tab count (10→11), rect implementations |
| `src/NetworkCanvas.cpp` | Modify | `DrawTeTab`; tunnel overlay drawing; update `DrawPanel` dispatch; update `DrawPacketAnim` |
| `src/main.cpp` | Modify | `#include "RsvpEngine.h"`; call `UpdateRsvp`; `TAB_TE` input; `RsvpReplayState` + replay loop |

Makefile: no changes — uses `$(wildcard src/*.cpp)`.

---

## Task 1: Data Structures in `Device.h`

**Files:**
- Modify: `src/Device.h`

- [ ] **Step 1: Add `TeLfibEntry` struct after `LdpBinding`**

In `src/Device.h`, after the `LdpBinding` struct (line ~29), insert:

```cpp
struct TeLfibEntry {
    uint32_t inLabel  = 0;
    uint32_t outLabel = 0;   // MPLS_IMPLICIT_NULL = PHP at penultimate hop
    int      outPort  = -1;
    int      tunnelId = 0;
};
```

- [ ] **Step 2: Add `TeTunnel` struct after `TeLfibEntry`**

```cpp
struct TeTunnel {
    int         id          = 0;       // 1–255, unique per router
    std::string destIp;                // tail-end router IP (no mask)

    uint32_t    bandwidth   = 0;       // required Mbps

    bool        useExplicit = false;
    std::vector<std::string> explicitHopIps;  // human-readable (UI storage)
    std::vector<int>         explicitHops;    // resolved node IDs (engine use)

    // computed each tick by UpdateRsvp — do not set from UI
    bool             isUp      = false;
    std::vector<int> activePath;        // node IDs head→tail
    uint32_t         headLabel = 0;     // 0 = not yet allocated
    std::string      statusMsg;         // "Up" / "No CSPF path" / "BW insufficient"
};
```

- [ ] **Step 3: Add TE fields to `DeviceNode`**

In `src/Device.h`, inside `struct DeviceNode`, after the NAT fields (end of struct), add:

```cpp
    // RSVP-TE (routers only)
    bool        rsvpEnabled  = false;
    uint32_t    portBandwidth[PORTS_PER_NODE] = {1000, 1000, 1000, 1000};  // Mbps
    uint32_t    nextTeLabel  = 16000;   // monotonic allocator — never resets between ticks
    std::vector<TeTunnel>   teTunnels;
    std::unordered_map<uint32_t, TeLfibEntry> teLfib;  // key = inLabel
    std::vector<TeTunnel>   pendingTunnels;  // MBB hold buffer
```

- [ ] **Step 4: Add `tunnelId` to `HopDecision`**

In `src/Device.h`, inside `struct HopDecision`, after `natResult`:

```cpp
    int tunnelId = 0;   // non-zero = this hop is inside a named TE tunnel
```

- [ ] **Step 5: Compile**

```bash
make 2>&1 | head -20
```

Expected: zero errors, zero warnings about new fields.

- [ ] **Step 6: Commit**

```bash
git add src/Device.h
git commit -m "feat(rsvp-te): add TeLfibEntry, TeTunnel structs and DeviceNode TE fields"
```

---

## Task 2: `RsvpEngine` Skeleton

**Files:**
- Create: `src/RsvpEngine.h`
- Create: `src/RsvpEngine.cpp`

- [ ] **Step 1: Create `src/RsvpEngine.h`**

```cpp
#pragma once
#include "Device.h"
#include "Cable.h"
#include <vector>
#include <string>

// Delay between each PATH (or RESV) hop in the Simulate Setup replay.
// Tune during playtesting — 0.25s feels natural at 1× speed.
inline constexpr float RSVP_PATH_HOP_DELAY = 0.25f;

// Called once per simulation tick, after UpdateOspf and UpdateLdp.
// Recomputes all TE tunnel states and teLfib tables for every rsvp-enabled router.
// Returns human-readable log events (empty most ticks).
std::vector<std::string> UpdateRsvp(std::vector<DeviceNode>& nodes,
                                     const std::vector<Cable>& cables);

// Resolves TeTunnel::explicitHopIps → TeTunnel::explicitHops (node IDs).
// Call only when the user edits the hop string or toggles Explicit mode — not every tick.
void ResolveExplicitHops(TeTunnel& t, const std::vector<DeviceNode>& nodes);
```

- [ ] **Step 2: Create `src/RsvpEngine.cpp` stub**

```cpp
#include "RsvpEngine.h"
#include <unordered_map>
#include <queue>
#include <climits>
#include <algorithm>
#include <cstdio>

std::vector<std::string> UpdateRsvp(std::vector<DeviceNode>& nodes,
                                     const std::vector<Cable>& cables)
{
    std::vector<std::string> log;
    (void)nodes; (void)cables;
    return log;
}

void ResolveExplicitHops(TeTunnel& t, const std::vector<DeviceNode>& nodes)
{
    (void)t; (void)nodes;
}
```

- [ ] **Step 3: Compile**

```bash
make 2>&1 | head -20
```

Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add src/RsvpEngine.h src/RsvpEngine.cpp
git commit -m "feat(rsvp-te): add RsvpEngine skeleton (stub)"
```

---

## Task 3: Available Bandwidth Map

**Files:**
- Modify: `src/RsvpEngine.cpp`

- [ ] **Step 1: Add cable-key helper and BW map builder**

Replace the body of `UpdateRsvp` with:

```cpp
std::vector<std::string> UpdateRsvp(std::vector<DeviceNode>& nodes,
                                     const std::vector<Cable>& cables)
{
    std::vector<std::string> log;

    // ── Phase 1: build available-BW map keyed by sorted node-pair ────────
    // key = (min_id << 32) | max_id  →  avail Mbps
    auto cableKey = [](int a, int b) -> uint64_t {
        return ((uint64_t)std::min(a,b) << 32) | (uint32_t)std::max(a,b);
    };

    // max capacity per cable = min(portA_bw, portB_bw)
    std::unordered_map<uint64_t, uint32_t> maxBwMap;
    for (const auto& c : cables) {
        const DeviceNode* a = FindNode(nodes, c.fromId);
        const DeviceNode* b = FindNode(nodes, c.toId);
        if (!a || !b) continue;
        uint32_t bwA = a->portBandwidth[c.fromPort];
        uint32_t bwB = b->portBandwidth[c.toPort];
        maxBwMap[cableKey(c.fromId, c.toId)] = std::min(bwA, bwB);
    }

    // sum active tunnel reservations from last tick
    std::unordered_map<uint64_t, uint32_t> reservedMap;
    for (const auto& n : nodes) {
        for (const auto& t : n.teTunnels) {
            if (!t.isUp || t.activePath.size() < 2) continue;
            for (size_t i = 0; i + 1 < t.activePath.size(); ++i) {
                reservedMap[cableKey(t.activePath[i], t.activePath[i+1])] += t.bandwidth;
            }
        }
    }

    // available = max - reserved, clamped to 0
    std::unordered_map<uint64_t, uint32_t> availBwMap;
    for (auto& [key, maxBw] : maxBwMap) {
        uint32_t res = reservedMap.count(key) ? reservedMap[key] : 0u;
        availBwMap[key] = (res < maxBw) ? (maxBw - res) : 0u;
    }

    // Phase 2 placeholder — will be filled in Tasks 4–7
    (void)log;
    return log;
}
```

- [ ] **Step 2: Compile**

```bash
make 2>&1 | head -20
```

Expected: clean.

- [ ] **Step 3: Commit**

```bash
git add src/RsvpEngine.cpp
git commit -m "feat(rsvp-te): implement Phase 1 — available BW map"
```

---

## Task 4: CSPF Dijkstra

**Files:**
- Modify: `src/RsvpEngine.cpp`

- [ ] **Step 1: Add `CspfDijkstra` static helper before `UpdateRsvp`**

Insert before the `UpdateRsvp` function:

```cpp
// Returns ordered node IDs [head, ..., tail], or empty if no BW-constrained path exists.
// Walks the head-end router's OSPF LSDBs and prunes links without reservable bandwidth.
static std::vector<int> CspfDijkstra(
    int headId,
    const std::string& destIp,
    uint32_t requiredBw,
    const std::vector<DeviceNode>& nodes,
    const std::vector<Cable>& cables,
    const std::unordered_map<uint64_t, uint32_t>& availBwMap,
    std::function<uint64_t(int,int)> cableKey)
{
    // Resolve dest IP → node ID
    int tailId = -1;
    for (const auto& n : nodes) {
        for (int p = 0; p < PORTS_PER_NODE; ++p) {
            auto sl = n.portIp[p].find('/');
            std::string plain = (sl != std::string::npos)
                                ? n.portIp[p].substr(0, sl) : n.portIp[p];
            if (plain == destIp) { tailId = n.id; break; }
        }
        if (tailId != -1) break;
    }
    if (tailId == -1 || tailId == headId) return {};

    // Dijkstra: dist + prev maps
    std::unordered_map<int, int> dist, prev;
    for (const auto& n : nodes) dist[n.id] = INT_MAX;
    dist[headId] = 0;

    // min-heap: {cost, nodeId}
    std::priority_queue<std::pair<int,int>,
                        std::vector<std::pair<int,int>>,
                        std::greater<>> pq;
    pq.push({0, headId});

    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > dist[u]) continue;
        for (const auto& c : cables) {
            int v = (c.fromId == u) ? c.toId : (c.toId == u) ? c.fromId : -1;
            if (v < 0) continue;
            const DeviceNode* nbr = FindNode(nodes, v);
            if (!nbr || nbr->crashed) continue;
            // Prune links without enough BW
            auto it = availBwMap.find(cableKey(u, v));
            uint32_t avail = (it != availBwMap.end()) ? it->second : 1000u;
            if (avail < requiredBw) continue;
            int nd = dist[u] + 1;
            if (nd < dist[v]) {
                dist[v] = nd;
                prev[v] = u;
                pq.push({nd, v});
            }
        }
    }

    if (dist[tailId] == INT_MAX) return {};

    std::vector<int> path;
    for (int cur = tailId; cur != headId; ) {
        path.push_back(cur);
        auto it = prev.find(cur);
        if (it == prev.end()) return {};  // disconnected
        cur = it->second;
    }
    path.push_back(headId);
    std::reverse(path.begin(), path.end());
    return path;
}
```

- [ ] **Step 2: Compile**

```bash
make 2>&1 | head -20
```

Expected: clean. Note: `std::function` requires `<functional>` — add `#include <functional>` at the top of `RsvpEngine.cpp`.

- [ ] **Step 3: Commit**

```bash
git add src/RsvpEngine.cpp
git commit -m "feat(rsvp-te): add CspfDijkstra helper with BW constraint pruning"
```

---

## Task 5: Tunnel Computation, Label Allocation, and `teLfib`

**Files:**
- Modify: `src/RsvpEngine.cpp`

- [ ] **Step 1: Add `BuildTeLfib` static helper before `UpdateRsvp`**

```cpp
// Populates node.teLfib entries for a single tunnel whose activePath is set.
// Labels: head uses t.headLabel; each transit hop uses headLabel + hopIdx;
// penultimate egress uses MPLS_IMPLICIT_NULL (PHP).
// Call after t.activePath and t.headLabel are finalized.
static void BuildTeLfib(TeTunnel& t, std::vector<DeviceNode>& nodes,
                         const std::vector<Cable>& cables)
{
    const auto& path = t.activePath;
    if (path.size() < 2) return;

    for (size_t i = 0; i + 1 < path.size(); ++i) {
        DeviceNode* cur = FindNodeMut(nodes, path[i]);
        if (!cur) continue;

        uint32_t inLbl  = t.headLabel + (uint32_t)i;
        uint32_t outLbl = (i + 2 == path.size())
                          ? MPLS_IMPLICIT_NULL           // penultimate → PHP
                          : t.headLabel + (uint32_t)(i + 1);

        // Find outPort toward path[i+1]
        int outPort = -1;
        for (const auto& c : cables) {
            if ((c.fromId == path[i] && c.toId == path[i+1]) ||
                (c.toId   == path[i] && c.fromId == path[i+1])) {
                outPort = (c.fromId == path[i]) ? c.fromPort : c.toPort;
                break;
            }
        }

        TeLfibEntry entry;
        entry.inLabel  = inLbl;
        entry.outLabel = outLbl;
        entry.outPort  = outPort;
        entry.tunnelId = t.id;
        cur->teLfib[inLbl] = entry;
    }
}
```

- [ ] **Step 2: Replace the Phase 2 placeholder in `UpdateRsvp` with full tunnel computation**

Replace `// Phase 2 placeholder — will be filled in Tasks 4–7\n    (void)log;\n    return log;` with:

```cpp
    // ── Phase 2: compute tunnel states ───────────────────────────────────
    for (auto& n : nodes) {
        if (!n.rsvpEnabled || n.type != ROUTER) continue;
        n.teLfib.clear();

        for (auto& t : n.teTunnels) {
            std::vector<int> newPath;

            if (!t.useExplicit) {
                // CSPF
                newPath = CspfDijkstra(n.id, t.destIp, t.bandwidth,
                                       nodes, cables, availBwMap, cableKey);
            } else {
                // Explicit path: prepend head, check each hop has enough BW
                if (!t.explicitHops.empty()) {
                    newPath.push_back(n.id);
                    for (int hop : t.explicitHops) newPath.push_back(hop);
                    bool ok = true;
                    for (size_t i = 0; i + 1 < newPath.size(); ++i) {
                        auto it = availBwMap.find(cableKey(newPath[i], newPath[i+1]));
                        uint32_t avail = (it != availBwMap.end()) ? it->second : 1000u;
                        if (avail < t.bandwidth) { ok = false; break; }
                    }
                    if (!ok) newPath.clear();
                }
            }

            bool pathChanged = (newPath != t.activePath);

            if (!newPath.empty()) {
                // Allocate a new label only when path changes or tunnel is new
                if (t.headLabel == 0 || pathChanged) {
                    t.headLabel = n.nextTeLabel;
                    n.nextTeLabel += 10;  // reserve 10 labels per tunnel (max 9 hops)
                }
                t.activePath = newPath;
                t.isUp       = true;
                t.statusMsg  = "Up";
                n.pendingTunnels.erase(
                    std::remove_if(n.pendingTunnels.begin(), n.pendingTunnels.end(),
                                   [&](const TeTunnel& p){ return p.id == t.id; }),
                    n.pendingTunnels.end());
                BuildTeLfib(t, nodes, cables);

                if (pathChanged && t.headLabel != 0) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf),
                                  "RSVP-TE: %s Tunnel-%d %s (label %u)",
                                  n.label.c_str(), t.id,
                                  pathChanged ? "rerouted" : "up", t.headLabel);
                    log.push_back(buf);
                }
            } else {
                // No path — MBB hold for one tick via pendingTunnels
                bool alreadyPending = false;
                for (const auto& p : n.pendingTunnels)
                    if (p.id == t.id) { alreadyPending = true; break; }

                if (t.isUp && !alreadyPending) {
                    // First tick without path: hold current state one more tick
                    n.pendingTunnels.push_back(t);
                    BuildTeLfib(t, nodes, cables);  // keep forwarding for one tick
                } else {
                    // Second tick: go Down
                    t.isUp      = false;
                    t.activePath.clear();
                    t.statusMsg = t.explicitHops.empty()
                                  ? "No CSPF path" : "BW insufficient on explicit path";
                    n.pendingTunnels.erase(
                        std::remove_if(n.pendingTunnels.begin(), n.pendingTunnels.end(),
                                       [&](const TeTunnel& p){ return p.id == t.id; }),
                        n.pendingTunnels.end());
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "RSVP-TE: %s Tunnel-%d down: %s",
                                  n.label.c_str(), t.id, t.statusMsg.c_str());
                    log.push_back(buf);
                }
            }
        }
    }

    return log;
```

- [ ] **Step 3: Implement `ResolveExplicitHops`**

Replace the stub body:

```cpp
void ResolveExplicitHops(TeTunnel& t, const std::vector<DeviceNode>& nodes)
{
    t.explicitHops.clear();
    for (const auto& rawIp : t.explicitHopIps) {
        // Trim whitespace and strip any mask
        std::string ip = rawIp;
        auto slash = ip.find('/');
        if (slash != std::string::npos) ip = ip.substr(0, slash);
        // Trim leading/trailing spaces
        size_t s = ip.find_first_not_of(' ');
        if (s == std::string::npos) continue;
        ip = ip.substr(s, ip.find_last_not_of(' ') - s + 1);
        if (ip.empty()) continue;

        for (const auto& n : nodes) {
            bool found = false;
            for (int p = 0; p < PORTS_PER_NODE; ++p) {
                auto sl = n.portIp[p].find('/');
                std::string np = (sl != std::string::npos)
                                 ? n.portIp[p].substr(0, sl) : n.portIp[p];
                if (np == ip) { t.explicitHops.push_back(n.id); found = true; break; }
            }
            if (found) break;
        }
    }
}
```

- [ ] **Step 4: Compile**

```bash
make 2>&1 | head -20
```

Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add src/RsvpEngine.cpp
git commit -m "feat(rsvp-te): Phase 2 tunnel computation, label allocation, teLfib, MBB"
```

---

## Task 6: Hook `UpdateRsvp` into the Simulation Loop

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add include**

At the top of `src/main.cpp`, after `#include "LdpEngine.h"` (or wherever the engine includes are), add:

```cpp
#include "RsvpEngine.h"
```

- [ ] **Step 2: Call `UpdateRsvp` after `UpdateLdp`**

Find the block in `main.cpp` (~line 1484):

```cpp
            auto ospfEvents = UpdateOspf(dt, nodes, cables);
            UpdateLdp(nodes, cables);   // recompute LFIB after each OSPF tick
            UpdateBgp(nodes, cables);
```

Change to:

```cpp
            auto ospfEvents = UpdateOspf(dt, nodes, cables);
            UpdateLdp(nodes, cables);
            UpdateRsvp(nodes, cables);
            UpdateBgp(nodes, cables);
```

- [ ] **Step 3: Compile and smoke test**

```bash
make && ./packet-path
```

Expected: game opens, no crash, no visible change yet (no tunnels configured).

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat(rsvp-te): call UpdateRsvp in simulation loop"
```

---

## Task 7: `SimulationEngine` — TE Forwarding

**Files:**
- Modify: `src/SimulationEngine.cpp`

- [ ] **Step 1: Add TE head-end check before LDP LFIB lookup**

Find the MPLS label operation block in `SimulationEngine.cpp` (~line 380):

```cpp
                // MPLS label operation (preserve existing MPLS logic verbatim)
                if (cur->ldpEnabled) {
                    auto it = cur->lfib.find(NetworkAddress(route.dest));
```

Insert the TE head-end check immediately BEFORE the `if (cur->ldpEnabled)` block:

```cpp
                // ── TE tunnel head-end: impose tunnel label stack ──────────
                if (cur->rsvpEnabled && currentLabel == 0) {
                    // Check if dest matches an active TE tunnel
                    for (const auto& tun : cur->teTunnels) {
                        if (!tun.isUp || tun.destIp.empty()) continue;
                        // Match dest IP against tunnel destIp
                        auto slash = destIp.find('/');
                        std::string plainDest = (slash != std::string::npos)
                                                ? destIp.substr(0, slash) : destIp;
                        if (tun.destIp == plainDest) {
                            hd.labelOp   = LABEL_PUSH;
                            hd.inLabel   = 0;
                            hd.outLabel  = tun.headLabel;
                            hd.tunnelId  = tun.id;
                            currentLabel = tun.headLabel;
                            break;
                        }
                    }
                }
```

- [ ] **Step 2: Add TE transit/egress check (teLfib takes priority over lfib)**

Find the existing MPLS block that starts with `if (cur->ldpEnabled)`. Replace the whole block with:

```cpp
                // MPLS: TE teLfib takes priority over LDP lfib
                if (cur->rsvpEnabled && currentLabel != 0) {
                    auto it = cur->teLfib.find(currentLabel);
                    if (it != cur->teLfib.end()) {
                        const TeLfibEntry& te = it->second;
                        hd.tunnelId = te.tunnelId;
                        if (te.outLabel == MPLS_IMPLICIT_NULL) {
                            hd.labelOp   = LABEL_POP;
                            hd.inLabel   = currentLabel;
                            hd.outLabel  = 0;
                            currentLabel = 0;
                        } else {
                            hd.labelOp   = LABEL_SWAP;
                            hd.inLabel   = currentLabel;
                            hd.outLabel  = te.outLabel;
                            currentLabel = te.outLabel;
                        }
                        goto done_mpls;
                    }
                }
                if (cur->ldpEnabled) {
                    auto it = cur->lfib.find(NetworkAddress(route.dest));
                    if (it != cur->lfib.end()) {
                        uint32_t nextOut = it->second.outLabel;
                        if (currentLabel == 0) {
                            hd.labelOp    = LABEL_PUSH;
                            hd.inLabel    = 0;
                            hd.outLabel   = nextOut;
                            currentLabel  = nextOut;
                        } else if (nextOut == MPLS_IMPLICIT_NULL) {
                            hd.labelOp    = LABEL_POP;
                            hd.inLabel    = currentLabel;
                            hd.outLabel   = 0;
                            currentLabel  = 0;
                        } else {
                            hd.labelOp    = LABEL_SWAP;
                            hd.inLabel    = currentLabel;
                            hd.outLabel   = nextOut;
                            currentLabel  = nextOut;
                        }
                    } else if (currentLabel != 0) {
                        currentLabel = 0;
                    }
                } else if (currentLabel != 0) {
                    currentLabel = 0;
                }
                done_mpls:;
```

- [ ] **Step 3: Compile**

```bash
make 2>&1 | head -20
```

Expected: clean. If `goto` triggers a warning, add `(void)0;` after the label.

- [ ] **Step 4: Manual verify**

Run `./packet-path`. Build topology: R1–R2–R3. Configure OSPF on all. Enable RSVP-TE on R1, add Tunnel-1 dest=R3-IP, BW=100. (Tab not visible yet — skip; this will be verified after Task 10.) Verify no crash.

- [ ] **Step 5: Commit**

```bash
git add src/SimulationEngine.cpp
git commit -m "feat(rsvp-te): TE forwarding in SimulationEngine — teLfib before lfib"
```

---

## Task 8: `PacketAnim` — `overrideColor` Field

**Files:**
- Modify: `src/Packet.h`
- Modify: `src/NetworkCanvas.cpp`

This enables the Simulate Setup replay to draw PATH packets in blue and RESV packets in green.

- [ ] **Step 1: Add `overrideColor` to `PacketAnim`**

In `src/Packet.h`, inside `struct PacketAnim`, after `speedMult`:

```cpp
    Color    overrideColor = {0, 0, 0, 0};  // {0,0,0,0} = use default green
```

- [ ] **Step 2: Update `DrawPacketAnim` to respect `overrideColor`**

In `src/NetworkCanvas.cpp`, find the two lines that draw the packet dot (~line 306):

```cpp
    DrawCircleV(pos, 14.f, Color{34, 197, 94, 55});
    DrawCircleV(pos, 7.f,  Color{34, 197, 94, 255});
```

Replace with:

```cpp
    Color core  = (anim.overrideColor.a != 0) ? anim.overrideColor
                                               : Color{34, 197, 94, 255};
    Color glow  = {core.r, core.g, core.b, 55};
    DrawCircleV(pos, 14.f, glow);
    DrawCircleV(pos, 7.f,  core);
```

- [ ] **Step 3: Compile**

```bash
make 2>&1 | head -20
```

Expected: clean.

- [ ] **Step 4: Commit**

```bash
git add src/Packet.h src/NetworkCanvas.cpp
git commit -m "feat(rsvp-te): add PacketAnim.overrideColor for PATH/RESV packet coloring"
```

---

## Task 9: ConfigPanel — `TAB_TE` Tab + Toggle + Port Bandwidth

**Files:**
- Modify: `src/ConfigPanel.h`
- Modify: `src/ConfigPanel.cpp`
- Modify: `src/NetworkCanvas.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Add `TAB_TE` to `PanelTab` enum in `ConfigPanel.h`**

Find:
```cpp
enum PanelTab { TAB_CONFIG, TAB_ROUTES, TAB_ARP, TAB_OSPF, TAB_MPLS, TAB_BGP,
                TAB_VLAN, TAB_SUB, TAB_VXLAN, TAB_ACL, TAB_NAT };
```

Replace with:
```cpp
enum PanelTab { TAB_CONFIG, TAB_ROUTES, TAB_ARP, TAB_OSPF, TAB_MPLS, TAB_BGP,
                TAB_VLAN, TAB_SUB, TAB_VXLAN, TAB_ACL, TAB_NAT, TAB_TE };
```

- [ ] **Step 2: Add TE `PanelState` fields to `ConfigPanel.h`**

Inside `struct PanelState`, after the NAT fields, add:

```cpp
    // TE tab
    int         teExpandedIdx   = -1;   // index in teTunnels currently expanded (-1=none)
    int         teActiveField   = -1;   // 0=dest, 1=bw, 2=hops
    std::string teDestBuf;
    std::string teBwBuf;
    std::string teHopsBuf;
    int         tePbwActivePort = -1;   // 0-3: which port BW field is active
    std::string tePbwBuf;               // edit buffer for port BW
```

- [ ] **Step 3: Add rect declarations to `ConfigPanel.h`**

After the NAT rect declarations, add:

```cpp
// TE tab
Rectangle PnlTeTabRect();
Rectangle PnlTeToggleRect();
Rectangle PnlTePbwRect(int port);         // per-port bandwidth input row
Rectangle PnlTeTunnelRowRect(int idx);    // collapsed tunnel row (fixed offset from list base)
Rectangle PnlTeAddBtnRect(int tunnelCount);
Rectangle PnlTeSimBtnRect();              // "Simulate Setup" — y position passed at call site
Rectangle PnlTeDelBtnRect();              // "Del" button — y position passed at call site
```

- [ ] **Step 4: Add rect implementations to `ConfigPanel.cpp`**

At the end of the file, add:

```cpp
// ── TE tab rects ──────────────────────────────────────────────────────────
Rectangle PnlTeTabRect() {
    return {(float)(CANVAS_W() + 12) + 10.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlTeToggleRect() {
    return {(float)(CANVAS_W() + 12), 120.0f, (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlTePbwRect(int port) {
    // Bandwidth field: right-aligned input box per port row
    // Port rows start at y=152, step 26px
    float y = 152.0f + (float)port * 26.0f;
    return {(float)(CANVAS_W() + PANEL_W - 80), y, 60.0f, 22.0f};
}
static float TeListBaseY() { return 260.0f; }  // Y where tunnel list starts
static float TeRowH()       { return 28.0f;  }
static float TeFormH()      { return 128.0f; }  // extra height when expanded
Rectangle PnlTeTunnelRowRect(int idx) {
    return {(float)(CANVAS_W() + 12), TeListBaseY() + (float)idx * TeRowH(),
            (float)(PANEL_W - 24), TeRowH() - 2.0f};
}
Rectangle PnlTeAddBtnRect(int tunnelCount) {
    float y = TeListBaseY() + (float)tunnelCount * TeRowH() + 4.0f;
    return {(float)(CANVAS_W() + 12), y, (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlTeSimBtnRect() {
    // Caller computes y; we just return a width+height template
    return {0.0f, 0.0f, (float)(PANEL_W - 24) * 0.6f, 22.0f};
}
Rectangle PnlTeDelBtnRect() {
    return {0.0f, 0.0f, (float)(PANEL_W - 24) * 0.35f, 22.0f};
}
```

- [ ] **Step 5: Add `TAB_TE` to `kRtTabs` and update router tab count in `ConfigPanel.cpp`**

Find `kRtTabs`:
```cpp
static const TabInfo kRtTabs[] = {
    ...
    {TAB_NAT,    "NAT",  Color{59,130,246,255}, Color{234,179,8,255}},
};
```

Add `TAB_TE` as the last entry:
```cpp
    {TAB_TE,     "TE",   Color{251,191,36,255}, Color{251,191,36,255}},
```

Then update `PnlTabCount`:
```cpp
int PnlTabCount(DeviceType t) {
    if (t == PC)     return 3;
    if (t == SWITCH) return 3;
    return 11;   // was 10
}
```

- [ ] **Step 6: Add `DrawTeTab` to `NetworkCanvas.cpp`**

Before `DrawPanel`, add:

```cpp
static void DrawTeTab(const DeviceNode* n, const PanelState& ps)
{
    float px = (float)(CANVAS_W() + 12);
    float pw = (float)(PANEL_W - 24);
    Color DIM  = {100, 116, 139, 255};
    Color WHT  = WHITE;
    Color ON   = {34,  197, 94,  255};
    Color OFF  = {100, 116, 139, 255};

    // Toggle
    const char* label = n->rsvpEnabled ? "rsvp-te  ON" : "rsvp-te  OFF";
    Color       tcol  = n->rsvpEnabled ? ON : OFF;
    DrawRectangleRoundedLines(PnlTeToggleRect(), 0.4f, 4, tcol);
    float tw = TW(label, 12);
    DrawTextEx(GFont(), label,
               {px + (pw - tw) * 0.5f, 126.0f}, FS(12), Sp(FS(12)), tcol);

    if (!n->rsvpEnabled) return;

    // Per-port bandwidth rows (ports with configured IPs only)
    DrawTextEx(GFont(), "Interface Bandwidth", {px, 150.0f}, FS(10), Sp(FS(10)), DIM);
    for (int p = 0; p < PORTS_PER_NODE; ++p) {
        if (n->portIp[p].empty()) continue;
        float y = 152.0f + (float)p * 26.0f;
        char portName[16];
        std::snprintf(portName, sizeof(portName), "%s", GetPortName(n->type, p).c_str());
        DrawTextEx(GFont(), portName, {px, y + 4.0f}, FS(11), Sp(FS(11)), WHT);

        Rectangle bwRect = PnlTePbwRect(p);
        bool active = (ps.tePbwActivePort == p);
        DrawRectangleRec(bwRect, Color{30, 41, 59, 255});
        DrawRectangleLinesEx(bwRect, 1.0f, active ? Color{59,130,246,255} : Color{51,65,85,255});
        const std::string& txt = active ? ps.tePbwBuf
                                        : std::to_string(n->portBandwidth[p]);
        DrawTextEx(GFont(), txt.c_str(), {bwRect.x + 4.0f, bwRect.y + 4.0f},
                   FS(11), Sp(FS(11)), WHT);
        DrawTextEx(GFont(), "Mbps", {bwRect.x + bwRect.width + 4.0f, bwRect.y + 4.0f},
                   FS(10), Sp(FS(10)), DIM);
    }

    // Tunnel list header
    DrawTextEx(GFont(), "TE Tunnels",
               {px, TeListBaseY() - 18.0f}, FS(10), Sp(FS(10)), DIM);

    float listY = TeListBaseY();
    for (int i = 0; i < (int)n->teTunnels.size(); ++i) {
        const auto& t  = n->teTunnels[i];
        bool expanded  = (ps.teExpandedIdx == i);
        float rowY     = listY;
        listY         += TeRowH();

        // Tunnel status color
        Color statusColor = t.isUp ? ON : Color{239,68,68,255};
        Color rowBg       = Color{21, 30, 47, 255};
        DrawRectangleRounded({px, rowY, pw, TeRowH() - 2.0f}, 0.3f, 4, rowBg);
        // amber left-border accent
        DrawRectangle((int)px, (int)rowY, 3, (int)(TeRowH() - 2.0f),
                      Color{251,191,36,200});

        char rowLabel[64];
        std::snprintf(rowLabel, sizeof(rowLabel),
                      "%s Tunnel-%d  →%s  %uMbps  %s",
                      expanded ? "▼" : "▶",
                      t.id,
                      t.destIp.empty() ? "?" : t.destIp.c_str(),
                      t.bandwidth,
                      t.useExplicit ? "Explicit" : "CSPF");
        DrawTextEx(GFont(), rowLabel, {px + 8.0f, rowY + 7.0f},
                   FS(10), Sp(FS(10)), WHT);
        // Status badge
        const char* statusTxt = t.isUp ? "UP" : "DOWN";
        float sw = TW(statusTxt, 10);
        DrawTextEx(GFont(), statusTxt, {px + pw - sw - 4.0f, rowY + 7.0f},
                   FS(10), Sp(FS(10)), statusColor);

        if (!expanded) continue;
        listY += TeFormH();

        // Expanded form
        float fy = rowY + TeRowH();
        auto field = [&](const char* lbl, float y, const std::string& val, bool act) {
            DrawTextEx(GFont(), lbl, {px + 4.0f, y}, FS(10), Sp(FS(10)), DIM);
            Rectangle r = {px + 52.0f, y - 2.0f, pw - 56.0f, 20.0f};
            DrawRectangleRec(r, Color{30, 41, 59, 255});
            DrawRectangleLinesEx(r, 1.0f, act ? Color{59,130,246,255} : Color{51,65,85,255});
            DrawTextEx(GFont(), val.c_str(), {r.x + 4.0f, r.y + 3.0f},
                       FS(10), Sp(FS(10)), WHT);
        };
        bool destAct = (ps.teActiveField == 0);
        bool bwAct   = (ps.teActiveField == 1);
        bool hopAct  = (ps.teActiveField == 2);

        field("Dest:", fy,      destAct ? ps.teDestBuf : t.destIp,      destAct); fy += 24.0f;
        field("BW:",   fy,      bwAct   ? ps.teBwBuf   : std::to_string(t.bandwidth), bwAct); fy += 24.0f;

        // Mode dropdown (static text toggle)
        DrawTextEx(GFont(), "Mode:", {px + 4.0f, fy}, FS(10), Sp(FS(10)), DIM);
        const char* modeTxt = t.useExplicit ? "[Explicit]" : "[CSPF    ]";
        DrawTextEx(GFont(), modeTxt, {px + 52.0f, fy}, FS(10), Sp(FS(10)),
                   Color{251,191,36,255}); fy += 24.0f;

        if (t.useExplicit) {
            std::string hopsStr;
            for (const auto& h : t.explicitHopIps) hopsStr += h + " ";
            field("Hops:", fy, hopAct ? ps.teHopsBuf : hopsStr, hopAct);
            fy += 24.0f;
        }

        // Action buttons
        float btnW = (pw - 8.0f) * 0.55f;
        float delW = (pw - 8.0f) * 0.40f;
        bool canSim = t.isUp;
        Color simCol = canSim ? Color{59,130,246,255} : Color{51,65,85,255};
        DrawRectangleRounded({px, fy, btnW, 22.0f}, 0.4f, 4, simCol);
        DrawTextEx(GFont(), "Simulate Setup", {px + 4.0f, fy + 4.0f},
                   FS(10), Sp(FS(10)), WHT);
        DrawRectangleRounded({px + pw - delW, fy, delW, 22.0f}, 0.4f, 4,
                             Color{127,29,29,255});
        DrawTextEx(GFont(), "Del", {px + pw - delW + 4.0f, fy + 4.0f},
                   FS(10), Sp(FS(10)), WHT);
    }

    // Add Tunnel button
    Rectangle addBtn = PnlTeAddBtnRect((int)n->teTunnels.size()
                                       + (ps.teExpandedIdx >= 0 ? 1 : 0));
    DrawRectangleRounded(addBtn, 0.4f, 4, Color{21,128,61,200});
    float atw = TW("+ Add Tunnel", 11);
    DrawTextEx(GFont(), "+ Add Tunnel",
               {addBtn.x + (addBtn.width - atw) * 0.5f, addBtn.y + 6.0f},
               FS(11), Sp(FS(11)), WHT);
}
```

- [ ] **Step 7: Add `DrawTeTab` dispatch in `DrawPanel`**

In `NetworkCanvas.cpp`, inside `DrawPanel`, after `else if (ps.activeTab == TAB_NAT) DrawNatTab(n, ps);`, add:

```cpp
    else if (ps.activeTab == TAB_TE)   DrawTeTab(n, ps);
```

- [ ] **Step 8: Add `TAB_TE` input handling in `main.cpp`**

In `main.cpp`, inside the `IsMouseButtonPressed(MOUSE_BUTTON_LEFT)` block, after the `TAB_NAT` input section, add:

```cpp
                // ── TE tab input ───────────────────────────────────────────
                if (ps.activeTab == TAB_TE && selNode && selNode->type == ROUTER) {
                    // Toggle rsvp-te enable
                    if (CheckCollisionPointRec(screenMouse, PnlTeToggleRect())) {
                        selNode->rsvpEnabled = !selNode->rsvpEnabled;
                        if (!selNode->rsvpEnabled) {
                            selNode->teLfib.clear();
                            selNode->teTunnels.clear();
                            selNode->pendingTunnels.clear();
                        }
                    }
                    if (!selNode->rsvpEnabled) goto te_input_done;

                    // Per-port BW field activation
                    for (int p = 0; p < PORTS_PER_NODE; ++p) {
                        if (selNode->portIp[p].empty()) continue;
                        if (CheckCollisionPointRec(screenMouse, PnlTePbwRect(p))) {
                            ps.tePbwActivePort = (ps.tePbwActivePort == p) ? -1 : p;
                            ps.tePbwBuf = std::to_string(selNode->portBandwidth[p]);
                        }
                    }

                    // Tunnel row click → expand/collapse
                    for (int i = 0; i < (int)selNode->teTunnels.size(); ++i) {
                        if (CheckCollisionPointRec(screenMouse, PnlTeTunnelRowRect(i))) {
                            ps.teExpandedIdx = (ps.teExpandedIdx == i) ? -1 : i;
                            ps.teActiveField = -1;
                            if (ps.teExpandedIdx == i) {
                                ps.teDestBuf = selNode->teTunnels[i].destIp;
                                ps.teBwBuf   = std::to_string(selNode->teTunnels[i].bandwidth);
                                std::string hops;
                                for (const auto& h : selNode->teTunnels[i].explicitHopIps)
                                    hops += h + " ";
                                ps.teHopsBuf = hops;
                            }
                        }
                    }

                    // Expanded form field clicks (only when a tunnel is expanded)
                    if (ps.teExpandedIdx >= 0 && ps.teExpandedIdx < (int)selNode->teTunnels.size()) {
                        auto& t   = selNode->teTunnels[ps.teExpandedIdx];
                        float fy  = TeListBaseY() + (float)ps.teExpandedIdx * TeRowH() + TeRowH();

                        // Dest field
                        Rectangle destR = {(float)(CANVAS_W()+56), fy - 2.0f, (float)(PANEL_W-60), 20.0f};
                        if (CheckCollisionPointRec(screenMouse, destR)) ps.teActiveField = 0;
                        fy += 24.0f;
                        // BW field
                        Rectangle bwR = {(float)(CANVAS_W()+56), fy - 2.0f, (float)(PANEL_W-60), 20.0f};
                        if (CheckCollisionPointRec(screenMouse, bwR))   ps.teActiveField = 1;
                        fy += 24.0f;
                        // Mode toggle
                        Rectangle modeR = {(float)(CANVAS_W()+56), fy, 80.0f, 20.0f};
                        if (CheckCollisionPointRec(screenMouse, modeR)) {
                            t.useExplicit = !t.useExplicit;
                            t.headLabel   = 0;  // force label reallocation
                        }
                        fy += 24.0f;
                        if (t.useExplicit) {
                            Rectangle hopR = {(float)(CANVAS_W()+56), fy-2.0f, (float)(PANEL_W-60), 20.0f};
                            if (CheckCollisionPointRec(screenMouse, hopR)) ps.teActiveField = 2;
                            fy += 24.0f;
                        }

                        // Simulate Setup button
                        float btnW = (float)(PANEL_W - 8) * 0.55f;
                        float delW = (float)(PANEL_W - 8) * 0.40f;
                        Rectangle simR = {(float)(CANVAS_W()+12), fy, btnW, 22.0f};
                        Rectangle delR = {(float)(CANVAS_W() + 12 + PANEL_W - 24 - delW), fy, delW, 22.0f};

                        if (t.isUp && CheckCollisionPointRec(screenMouse, simR)) {
                            // Handled in Task 14: rsvpReplay trigger
                            // rsvpReplay.startReplay(selNode->id, t.id, nodes);
                        }
                        if (CheckCollisionPointRec(screenMouse, delR)) {
                            selNode->teTunnels.erase(selNode->teTunnels.begin() + ps.teExpandedIdx);
                            ps.teExpandedIdx = -1;
                            ps.teActiveField = -1;
                        }
                    }

                    // Add Tunnel button
                    int visCount = (int)selNode->teTunnels.size() + (ps.teExpandedIdx >= 0 ? 1 : 0);
                    if (CheckCollisionPointRec(screenMouse, PnlTeAddBtnRect(visCount))) {
                        TeTunnel nt;
                        nt.id = selNode->teTunnels.empty()
                                ? 1 : selNode->teTunnels.back().id + 1;
                        selNode->teTunnels.push_back(nt);
                    }

                    te_input_done:;
                }
```

- [ ] **Step 9: Add keyboard input for active TE fields**

In `main.cpp`, in the keyboard/text-field update section (near `UpdateRoutesTab`), add:

```cpp
            // TE tab text field updates
            if (ps.activeTab == TAB_TE && selectedId != -1) {
                DeviceNode* sn = FindNodeMut(nodes, selectedId);
                if (sn) {
                    // Port BW editing
                    if (ps.tePbwActivePort >= 0) {
                        UpdateTextField(ps.tePbwBuf, 7);
                        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) {
                            int p = ps.tePbwActivePort;
                            uint32_t bw = (uint32_t)std::max(1, std::atoi(ps.tePbwBuf.c_str()));
                            sn->portBandwidth[p] = bw;
                            ps.tePbwActivePort = -1;
                        }
                    }
                    // Per-tunnel form fields
                    if (ps.teExpandedIdx >= 0 && ps.teExpandedIdx < (int)sn->teTunnels.size()) {
                        auto& t = sn->teTunnels[ps.teExpandedIdx];
                        if (ps.teActiveField == 0) {
                            UpdateTextField(ps.teDestBuf, 15);
                            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) {
                                t.destIp = ps.teDestBuf;
                                t.headLabel = 0;  // path changed — force new label
                                ps.teActiveField = -1;
                            }
                        } else if (ps.teActiveField == 1) {
                            UpdateTextField(ps.teBwBuf, 7);
                            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) {
                                t.bandwidth = (uint32_t)std::max(0, std::atoi(ps.teBwBuf.c_str()));
                                t.headLabel = 0;
                                ps.teActiveField = -1;
                            }
                        } else if (ps.teActiveField == 2) {
                            UpdateTextField(ps.teHopsBuf, 120);
                            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) {
                                // Parse space-separated IPs into explicitHopIps
                                t.explicitHopIps.clear();
                                std::istringstream ss(ps.teHopsBuf);
                                std::string tok;
                                while (ss >> tok) t.explicitHopIps.push_back(tok);
                                ResolveExplicitHops(t, nodes);
                                t.headLabel = 0;
                                ps.teActiveField = -1;
                            }
                        }
                    }
                }
            }
```

Add `#include <sstream>` near top of `main.cpp` if not already present.

- [ ] **Step 10: Compile and manual verify**

```bash
make && ./packet-path
```

Expected: Click any router → `TE` tab visible at far right. Click it → shows rsvp-te toggle. Enable toggle → shows port BW rows and "TE Tunnels" list. Click "+ Add Tunnel" → new tunnel row appears. Click row to expand → form fields visible.

- [ ] **Step 11: Commit**

```bash
git add src/ConfigPanel.h src/ConfigPanel.cpp src/NetworkCanvas.cpp src/main.cpp
git commit -m "feat(rsvp-te): TAB_TE — toggle, port BW, tunnel list, inline form, input"
```

---

## Task 10: Canvas Tunnel Overlay

**Files:**
- Modify: `src/NetworkCanvas.cpp`

- [ ] **Step 1: Add tunnel overlay draw call after cables are drawn**

In `NetworkCanvas.cpp`, find where cables are drawn in world space (inside `BeginMode2D`). After the cable drawing loop, add:

```cpp
    // ── TE tunnel overlays ─────────────────────────────────────────────────
    static const Color kTePalette[] = {
        {251,191, 36,200},   // amber
        { 34,211,238,200},   // cyan
        {232,121,249,200},   // magenta
        {163,230, 53,200},   // lime
        {251,113,133,200},   // rose
        { 56,189,248,200},   // sky
    };
    static const int kPaletteSize = 6;

    int tunnelColorIdx = 0;
    for (const auto& n : nodes) {
        if (!n.rsvpEnabled) continue;
        for (const auto& t : n.teTunnels) {
            if (!t.isUp || t.activePath.size() < 2) { ++tunnelColorIdx; continue; }
            Color col = kTePalette[tunnelColorIdx % kPaletteSize];
            ++tunnelColorIdx;

            for (size_t i = 0; i + 1 < t.activePath.size(); ++i) {
                int aId = t.activePath[i], bId = t.activePath[i+1];
                const DeviceNode* a = FindNode(nodes, aId);
                const DeviceNode* b = FindNode(nodes, bId);
                const Cable*      c = FindCable(cables, aId, bId);
                if (!a || !b || !c) continue;

                int aPort = (c->fromId == aId) ? c->fromPort : c->toPort;
                int bPort = (c->fromId == bId) ? c->fromPort : c->toPort;

                Vector2 p0 = GetPortPosition(*a, aPort);
                Vector2 p3 = GetPortPosition(*b, bPort);
                Vector2 c1 = BezierCtrl(p0, aPort);
                Vector2 c2 = BezierCtrl(p3, bPort);

                // Perpendicular offset (±3px per tunnel color index to separate stacked tunnels)
                float off  = (float)(((tunnelColorIdx - 1) % 3) - 1) * 3.0f;
                auto perp  = [](Vector2 v) -> Vector2 {
                    float len = sqrtf(v.x*v.x + v.y*v.y);
                    if (len < 0.001f) return {0,0};
                    return {-v.y/len, v.x/len};
                };
                Vector2 dir = {p3.x - p0.x, p3.y - p0.y};
                Vector2 n2  = perp(dir);
                Vector2 o   = {n2.x * off, n2.y * off};

                // Offset the bezier control points
                Vector2 op0 = {p0.x+o.x, p0.y+o.y};
                Vector2 op3 = {p3.x+o.x, p3.y+o.y};
                Vector2 oc1 = {c1.x+o.x, c1.y+o.y};
                Vector2 oc2 = {c2.x+o.x, c2.y+o.y};

                DrawLineBezierCubic(op0, op3, oc1, oc2, 3.0f, col);

                // Midpoint label badge
                Vector2 mid = EvaluateCubicBezier(op0, oc1, oc2, op3, 0.5f);
                char badge[32];
                std::snprintf(badge, sizeof(badge), "T%d·%uM", t.id, t.bandwidth);
                int bw = (int)TW(badge, 9) + 8;
                DrawRectangleRounded({mid.x - bw*0.5f, mid.y - 9.0f, (float)bw, 14.0f},
                                     0.5f, 4, Color{15,23,42,210});
                DrawTextEx(GFont(), badge, {mid.x - bw*0.5f + 4.0f, mid.y - 7.0f},
                           FS(9), Sp(FS(9)), col);
            }
        }
    }
```

- [ ] **Step 2: Compile and manual verify**

```bash
make && ./packet-path
```

Expected: Build a 3-router topology (R1–R2–R3). Enable OSPF + RSVP-TE on R1. Add Tunnel-1 with dest = R3's IP, BW = 100. Amber colored bezier overlay appears on the R1→R2 and R2→R3 cables. Label badge "T1·100M" visible near cable midpoints.

- [ ] **Step 3: Commit**

```bash
git add src/NetworkCanvas.cpp
git commit -m "feat(rsvp-te): colored bezier tunnel overlays on canvas"
```

---

## Task 11: Simulate Setup Replay

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add `RsvpReplayState` struct near the top of `main.cpp` (after includes, before `main()`)**

```cpp
// ── RSVP-TE Simulate Setup replay state ──────────────────────────────────
struct RsvpReplayState {
    enum Phase { PATH_PHASE, HOLD_PHASE, RESV_PHASE, DONE };
    bool        active    = false;
    Phase       phase     = PATH_PHASE;
    int         hop       = 0;       // current hop index being animated
    float       holdTimer = 0.f;
    std::vector<int> path;           // activePath head→tail for this tunnel
    PacketAnim  pkt;
    bool        pktActive = false;
    uint32_t    headLabel = 0;       // for RESV badge display
};
```

- [ ] **Step 2: Declare `rsvpReplay` variable alongside `simState`**

Near `SimState simState;` (~line 47), add:

```cpp
    RsvpReplayState rsvpReplay;
```

- [ ] **Step 3: Wire the Simulate Setup button click to start a replay**

In the `TAB_TE` input handling added in Task 9, replace the comment `// Handled in Task 14: rsvpReplay trigger` with:

```cpp
                        if (t.isUp && CheckCollisionPointRec(screenMouse, simR)) {
                            rsvpReplay          = RsvpReplayState{};
                            rsvpReplay.active   = true;
                            rsvpReplay.phase    = RsvpReplayState::PATH_PHASE;
                            rsvpReplay.path     = t.activePath;
                            rsvpReplay.headLabel= t.headLabel;
                            rsvpReplay.hop      = 0;
                            rsvpReplay.pktActive= false;
                        }
```

- [ ] **Step 4: Add replay update logic in the simulation update section**

Near where `UpdatePacketAnim(simState.anim, dt, nodes, cables)` is called (~line 1510), add AFTER it:

```cpp
            // ── RSVP replay tick ──────────────────────────────────────────
            if (rsvpReplay.active) {
                if (rsvpReplay.pktActive) {
                    UpdatePacketAnim(rsvpReplay.pkt, dt, nodes, cables);
                    if (rsvpReplay.pkt.done) rsvpReplay.pktActive = false;
                }

                if (!rsvpReplay.pktActive) {
                    const auto& rpath = rsvpReplay.path;
                    int  n = (int)rpath.size();

                    if (rsvpReplay.phase == RsvpReplayState::PATH_PHASE) {
                        if (rsvpReplay.hop < n - 1) {
                            // Spawn PATH packet for this hop
                            ForwardResult fr;
                            fr.path    = {rpath[rsvpReplay.hop], rpath[rsvpReplay.hop+1]};
                            fr.success = true;
                            rsvpReplay.pkt             = PacketAnim{};
                            rsvpReplay.pkt.result      = fr;
                            rsvpReplay.pkt.overrideColor = Color{59, 130, 246, 255};  // blue
                            rsvpReplay.pktActive       = true;
                            ++rsvpReplay.hop;
                        } else {
                            rsvpReplay.phase     = RsvpReplayState::HOLD_PHASE;
                            rsvpReplay.holdTimer = 0.8f;
                        }
                    } else if (rsvpReplay.phase == RsvpReplayState::HOLD_PHASE) {
                        rsvpReplay.holdTimer -= dt;
                        if (rsvpReplay.holdTimer <= 0.f) {
                            rsvpReplay.phase = RsvpReplayState::RESV_PHASE;
                            rsvpReplay.hop   = 0;
                        }
                    } else if (rsvpReplay.phase == RsvpReplayState::RESV_PHASE) {
                        if (rsvpReplay.hop < n - 1) {
                            // Spawn RESV packet tail→head
                            int from = rpath[n - 1 - rsvpReplay.hop];
                            int to   = rpath[n - 2 - rsvpReplay.hop];
                            ForwardResult fr;
                            fr.path    = {from, to};
                            fr.success = true;
                            rsvpReplay.pkt               = PacketAnim{};
                            rsvpReplay.pkt.result        = fr;
                            rsvpReplay.pkt.currentLabel  = rsvpReplay.headLabel
                                                           + (uint32_t)(n - 2 - rsvpReplay.hop);
                            // RESV uses default green (no overrideColor)
                            rsvpReplay.pktActive = true;
                            ++rsvpReplay.hop;
                        } else {
                            rsvpReplay.active = false;
                        }
                    }
                }
            }
```

- [ ] **Step 5: Draw the active replay packet inside `BeginMode2D`**

After `DrawPacketAnim(simState.anim, nodes, cables);` (~line 1530), add:

```cpp
            if (rsvpReplay.active && rsvpReplay.pktActive)
                DrawPacketAnim(rsvpReplay.pkt, nodes, cables);
```

- [ ] **Step 6: Compile and manual verify**

```bash
make && ./packet-path
```

Expected: Build a 3-router topology, enable OSPF + RSVP-TE on R1, add Tunnel-1 (Up). Open R1's TE tab, expand Tunnel-1, click "Simulate Setup". Blue PATH packet animates R1→R2, then R2→R3. 0.8s pause. Green RESV packet with label badge animates R3→R2, then R2→R1.

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp
git commit -m "feat(rsvp-te): Simulate Setup PATH/RESV replay animation"
```

---

## Task 12: End-to-End Verification

**No code changes — manual scenario tests only.**

- [ ] **Test 1: CSPF basic path**

1. Place R1, R2, R3. Cable R1–R2 and R2–R3. Assign IPs on all ports.
2. Enable OSPF (area 0) on all three routers.
3. On R1: enable RSVP-TE. Set all port BW = 1000. Add Tunnel-1: dest = R3-port-IP, BW = 100, mode = CSPF.
4. Expected: Tunnel-1 shows `UP`. Amber overlay on R1–R2 and R2–R3 cables. Badge "T1·100M" visible.

- [ ] **Test 2: BW constraint forces alternate path**

1. Add R4. Cable R1–R4 and R4–R3.
2. Set R1 Gi0/1 BW = 50 (toward R4). Set R4 ports = 1000, R4–R3 = 1000.
3. Add Tunnel-2: dest = R3, BW = 200, CSPF.
4. Expected: Tunnel-2 takes R1→R4→R3 because R1–R2–R3 path has 1000 Mbps but after Tunnel-1 reserves 100, R1–R2 has 900 available. Tunnel-2 (200 Mbps) fits there too — verify it takes the shorter R1→R2→R3 path.
5. Set Tunnel-1 BW = 900 (fills R1–R2). Expected: Tunnel-2 reroutes to R1→R4→R3 (CSPF finds only path with ≥200 Mbps available).

- [ ] **Test 3: Explicit path override**

1. Same 4-router topology.
2. Add Tunnel-3: dest = R3, BW = 50, mode = Explicit, hops = R4-IP.
3. Expected: Tunnel-3 shows UP, overlay on R1–R4 and R4–R3 cables. Ignores the R1–R2–R3 path entirely.

- [ ] **Test 4: Simulate Setup replay**

1. With Tunnel-1 Up (R1→R2→R3 path), click Simulate Setup.
2. Expected sequence: Blue packet R1→R2 (PATH), 0.25s gap, blue R2→R3 (PATH), 0.8s hold, green R3→R2 (RESV, shows label badge), green R2→R1 (RESV, shows head label).

- [ ] **Test 5: TE forwarding — Send Packet through tunnel**

1. Assign a PC to R3's subnet. Right-click R1, Send Packet To… → PC.
2. Expected: Packet trace shows MPLS PUSH at R1 (tunnel label), SWAP/POP at transit hops. `HopDecision.tunnelId` non-zero in trace modal (if trace modal displays tunnel info — or verify via log console).

- [ ] **Test 6: Make-before-break on link failure**

1. Topology: R1–R2–R3 with R1–R4–R3 as alternate. Tunnel-1 Up via R1→R2→R3.
2. Right-click R1–R2 cable → Cut Link.
3. Expected: Tunnel-1 stays Up for one tick (MBB hold), then reroutes to R1→R4→R3. Canvas overlay moves to new cable segments. No flicker.

- [ ] **Final commit**

```bash
git add -A
git status   # should be clean or only untracked files
git commit -m "feat(rsvp-te): complete implementation — all manual tests passing" --allow-empty
```

---

## Self-Review Notes

After writing this plan, checked against the spec:

- ✅ Hybrid instant-state + replay: Tasks 5 (instant) + 11 (replay)
- ✅ CSPF + explicit override: Tasks 4, 5
- ✅ Per-port BW in ConfigPanel: Tasks 9
- ✅ Colored overlay on cables: Task 10
- ✅ Inline expand tunnel list: Tasks 9
- ✅ `TeLfibEntry` / `TeTunnel` in `Device.h`: Task 1
- ✅ `ResolveExplicitHops` called on edit only: Task 9 (keyboard handler)
- ✅ `RSVP_PATH_HOP_DELAY = 0.25f`: Task 2 (RsvpEngine.h)
- ✅ `portBandwidth` (not `portBw`): Task 1
- ✅ Monotonic `nextTeLabel`: Task 5 (increment by 10 per tunnel)
- ✅ `pendingTunnels` MBB hold: Task 5
- ✅ `HopDecision.tunnelId`: Tasks 1, 7
- ✅ Makefile: no changes needed (`$(wildcard src/*.cpp)`)
- ✅ `UpdateRsvp` called after `UpdateLdp`: Task 6
