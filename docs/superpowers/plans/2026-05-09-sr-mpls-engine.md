# SR-MPLS Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement SR-MPLS (RFC 8402, RFC 8660) in Packet Path — SID allocation, label stack forwarding, SR policy UI, and dashed canvas overlay visualization.

**Architecture:** A new `SrEngine` runs after `UpdateRsvp` each tick. It allocates Node SIDs and Adj SIDs, builds per-router `srFib` tables for shortest-path forwarding, and resolves user-defined SR policies into label stacks. SimulationEngine checks SR before RSVP-TE and LDP. The UI adds a 13th router tab (`TAB_SR`) for Node SID input, Adj SID display, and SR policy management.

**Tech Stack:** C++17, raylib 5.5. Build: `make` (uses `$(wildcard src/*.cpp)` — no Makefile changes needed for new `SrEngine.cpp`).

---

## File Map

| File | Status | Change |
|---|---|---|
| `src/Device.h` | Modify | SRGB constants, SrLfibEntry, SrPolicy, DeviceNode SR fields, HopDecision SR fields |
| `src/Packet.h` | Modify | PacketAnim SR label stack fields |
| `src/SrEngine.h` | **Create** | UpdateSr + ResolveSrSegments declarations |
| `src/SrEngine.cpp` | **Create** | Full SR engine (Phase 1–4) + ResolveSrSegments |
| `src/SimulationEngine.cpp` | Modify | SR head-end + transit forwarding before RSVP-TE checks |
| `src/ConfigPanel.h` | Modify | TAB_SR enum, PanelState SR fields, SR rect declarations |
| `src/ConfigPanel.cpp` | Modify | PnlTabW/Count updates, kRtTabs SR entry, SR rect implementations |
| `src/NetworkCanvas.h` | Modify | DrawSrTab + DrawSrPolicyOverlays declarations |
| `src/NetworkCanvas.cpp` | Modify | DrawSrTab implementation, DrawSrPolicyOverlays, DrawPanel dispatch |
| `src/main.cpp` | Modify | #include SrEngine.h, UpdateSr call, TAB_SR mouse+keyboard handlers, selection reset |
| `docs/superpowers/specs/2026-05-09-sr-mpls-engine-design.md` | Modify | Fix SRGB_BASE 1000→17000 |

---

## Critical Implementation Note

**SRGB_BASE = 17000** — The spec originally said 1000, but `LdpEngine.cpp` allocates labels as `n.id * 100 + prefix_idx`. For node IDs ≥ 10, LDP labels start at 1000+, directly colliding with SRGB 1000–1999. Use 17000 (above RSVP-TE's 16000 range).

**Egress self-pop** — Phase 3 must add an srFib entry on the egress node itself (`outLabel=MPLS_IMPLICIT_NULL, outPort=-1`). The SimulationEngine transit code must only override `hop.outPort` when `se.outPort >= 0`, letting IP routing handle egress outPort. This ensures correct label stack behavior when the head-end is directly connected to the first SR segment.

---

## Task 1: Device.h — SR data structures

**Files:**

- Modify: `src/Device.h`

- [ ] **Step 1: Add SRGB constants after `MPLS_IMPLICIT_NULL` (line 25)**

In `src/Device.h`, after line 25 (`static const uint32_t MPLS_IMPLICIT_NULL = 3;`), add:

```cpp
constexpr uint32_t SRGB_BASE = 17000;   // above LDP (n.id*100) and RSVP-TE (16000+)
constexpr uint32_t SRGB_SIZE = 1000;
constexpr uint32_t SRGB_END  = SRGB_BASE + SRGB_SIZE;   // exclusive upper bound
```

- [ ] **Step 2: Add `SrLfibEntry` struct after `TeLfibEntry` (line 39)**

```cpp
struct SrLfibEntry {
    uint32_t inLabel  = 0;
    uint32_t outLabel = 0;   // MPLS_IMPLICIT_NULL = PHP/self-pop; same as inLabel = transit
    int      outPort  = -1;  // -1 = egress self-pop (let IP routing determine outPort)
    int      policyId = 0;   // always 0 in srFib; carried on PacketAnim for display
};
```

- [ ] **Step 3: Add `SrPolicy` struct after `TeTunnel` (line 56)**

```cpp
struct SrPolicy {
    int         id          = 0;        // 1–255, unique per router
    std::string destIp;                 // tail-end destination IP (no mask)

    std::vector<std::string> segmentIps;    // hop IPs as typed (UI storage)
    std::vector<int>         segmentHops;   // resolved node IDs (engine use)
    std::vector<uint32_t>    labelStack;    // innermost first, outermost at back()
                                            // e.g. segs [R2→R4]: {17004, 17002}
                                            // back() = 17002 (R2's label, processed first)
    bool             segmentsResolved = false;   // cleared on edit; set by ResolveSrSegments

    bool             isActive  = false;
    std::vector<int> activePath;    // full OSPF path head→tail through all waypoints
    std::string      statusMsg;     // "Active" / "Segment N unreachable" / "No SID for X"
};
```

- [ ] **Step 4: Add SR fields to `DeviceNode` after the RSVP-TE section (after line 235)**

In `src/Device.h`, after the line `std::vector<TeTunnel> pendingTunnels;  // MBB hold buffer`, add:

```cpp
    // SR-MPLS (routers only)
    bool     srEnabled = false;
    uint32_t nodeSid   = 0;      // 1–(SRGB_SIZE-1); 0 = not configured
    std::unordered_map<uint32_t, SrLfibEntry> srFib;      // key = inLabel
    std::vector<SrPolicy>                     srPolicies;
    std::unordered_map<int, uint32_t>         adjSids;    // key = port index; value = adj SID label
```

- [ ] **Step 5: Add SR fields to `HopDecision` after `int tunnelId = 0;` (line 158)**

```cpp
    int policyId     = 0;   // non-zero = this hop is inside a named SR policy
    int segmentIndex = 0;   // which segment of the policy this hop belongs to (0-based)
```

- [ ] **Step 6: Build to confirm no errors**

```bash
make 2>&1 | head -20
```

Expected: compiles cleanly (new fields are unused so far — no errors).

- [ ] **Step 7: Commit**

```bash
git add src/Device.h
git commit -m "feat(sr): add SR-MPLS data structures to Device.h"
```

---

## Task 2: Packet.h — PacketAnim SR fields

**Files:**

- Modify: `src/Packet.h`

- [ ] **Step 1: Add SR fields to `PacketAnim` after `overrideColor` (line 23)**

In `src/Packet.h`, after `Color overrideColor = {0, 0, 0, 0};`, add:

```cpp
    // SR-MPLS label stack (independent of RSVP-TE / LDP currentLabel)
    std::vector<uint32_t> srLabelStack;   // innermost first, outermost at back()
    int                   srSegmentIdx = 0;  // current segment in the active SR policy
    int                   srPolicyId   = 0;  // ID of the active SR policy (0 = none)
```

- [ ] **Step 2: Build to confirm**

```bash
make 2>&1 | head -20
```

Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add src/Packet.h
git commit -m "feat(sr): add SR label stack fields to PacketAnim"
```

---

## Task 3: SrEngine.h + SrEngine.cpp — core SR engine

**Files:**

- Create: `src/SrEngine.h`
- Create: `src/SrEngine.cpp`

- [ ] **Step 1: Create `src/SrEngine.h`**

```cpp
#pragma once
#include "Device.h"
#include "Cable.h"
#include <vector>

// Called once per simulation tick, after UpdateRsvp.
// Rebuilds all srFib tables, adj SIDs, and SR policy states.
void UpdateSr(std::vector<DeviceNode>& nodes, const std::vector<Cable>& cables);

// Resolves SrPolicy::segmentIps → segmentHops + labelStack.
// Call when the user edits segmentIps or when segmentsResolved == false.
void ResolveSrSegments(SrPolicy& policy, const std::vector<DeviceNode>& nodes);
```

- [ ] **Step 2: Create `src/SrEngine.cpp` with helpers + ResolveSrSegments**

```cpp
#include "SrEngine.h"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

// ── Internal helpers ──────────────────────────────────────────────────────

// Returns src's OSPF outPort toward dest, or -1 if no route found.
static int NextHopPort(const DeviceNode& src, const DeviceNode& dest) {
    for (const auto& r : src.ospfRoutes) {
        if (!dest.routerId.empty() && IpInSubnet(dest.routerId, r.dest))
            return r.outPort;
        for (int p = 0; p < PORTS_PER_NODE; ++p) {
            if (dest.portIp[p].empty()) continue;
            auto sl = dest.portIp[p].find('/');
            std::string plain = (sl != std::string::npos)
                ? dest.portIp[p].substr(0, sl) : dest.portIp[p];
            if (IpInSubnet(plain, r.dest)) return r.outPort;
        }
    }
    return -1;
}

// Returns the node ID at the other end of src's cable on `port`, or -1.
static int NextHopNodeId(const DeviceNode& src, int port,
                         const std::vector<Cable>& cables) {
    for (const auto& c : cables) {
        if (c.broken) continue;
        if (c.fromId == src.id && c.fromPort == port) return c.toId;
        if (c.toId   == src.id && c.toPort   == port) return c.fromId;
    }
    return -1;
}

// Traces the OSPF shortest path from srcId to destId (for activePath building).
// Returns ordered node IDs [srcId, ..., destId], or empty if unreachable.
static std::vector<int> OspfPath(int srcId, int destId,
                                 const std::vector<DeviceNode>& nodes,
                                 const std::vector<Cable>& cables) {
    if (srcId == destId) return {srcId};
    std::vector<int> path = {srcId};
    std::unordered_set<int> visited = {srcId};
    int cur = srcId;
    for (int i = 0; i < 16; ++i) {
        const DeviceNode* curNode  = FindNode(nodes, cur);
        const DeviceNode* destNode = FindNode(nodes, destId);
        if (!curNode || !destNode) return {};
        int port = NextHopPort(*curNode, *destNode);
        if (port < 0) return {};
        int next = NextHopNodeId(*curNode, port, cables);
        if (next < 0 || visited.count(next)) return {};
        path.push_back(next);
        visited.insert(next);
        cur = next;
        if (cur == destId) return path;
    }
    return {};
}

// ── ResolveSrSegments ─────────────────────────────────────────────────────

void ResolveSrSegments(SrPolicy& policy, const std::vector<DeviceNode>& nodes) {
    policy.segmentHops.clear();
    policy.labelStack.clear();
    policy.statusMsg.clear();
    bool allOk = true;

    for (const auto& ip : policy.segmentIps) {
        const DeviceNode* match = nullptr;
        for (const auto& n : nodes) {
            if (n.routerId == ip) { match = &n; break; }
            for (int p = 0; p < PORTS_PER_NODE; ++p) {
                if (n.portIp[p].empty()) continue;
                auto sl = n.portIp[p].find('/');
                std::string plain = (sl != std::string::npos)
                    ? n.portIp[p].substr(0, sl) : n.portIp[p];
                if (plain == ip) { match = &n; break; }
            }
            if (match) break;
        }
        if (!match || !match->srEnabled || match->nodeSid == 0) {
            if (!policy.statusMsg.empty()) policy.statusMsg += "; ";
            policy.statusMsg += "No SID for " + ip;
            policy.segmentHops.push_back(-1);
            policy.labelStack.push_back(0);
            allOk = false;
        } else {
            policy.segmentHops.push_back(match->id);
            policy.labelStack.push_back(SRGB_BASE + match->nodeSid);
        }
    }

    // Reverse so back() = first segment's label (outermost, processed first at head-end).
    // e.g. segs [R2, R4] → temp [17002, 17004] → reversed {17004, 17002}, back()=17002
    std::reverse(policy.labelStack.begin(), policy.labelStack.end());

    if (allOk && !policy.segmentIps.empty())
        policy.segmentsResolved = true;
    else if (policy.segmentIps.empty())
        policy.segmentsResolved = true;
}

// ── UpdateSr ──────────────────────────────────────────────────────────────

void UpdateSr(std::vector<DeviceNode>& nodes, const std::vector<Cable>& cables) {

    // ── Phase 1: Build global SID map ──────────────────────────────────────
    std::unordered_map<int,      uint32_t> nodeSidToLabel;  // nodeId  → SR label
    std::unordered_map<uint32_t, int>      labelToNodeId;   // SR label → nodeId

    for (const auto& n : nodes) {
        if (!n.srEnabled || n.nodeSid == 0) continue;
        uint32_t label = SRGB_BASE + n.nodeSid;
        if (labelToNodeId.count(label)) continue;   // skip duplicate SID (conflict)
        nodeSidToLabel[n.id] = label;
        labelToNodeId[label]  = n.id;
    }

    // ── Phase 2: Assign Adj SIDs ───────────────────────────────────────────
    for (auto& n : nodes) {
        if (!n.srEnabled) continue;
        n.adjSids.clear();
        for (int port = 0; port < PORTS_PER_NODE; ++port) {
            if (n.portIp[port].empty()) continue;
            for (const auto& c : cables) {
                if (!c.broken &&
                    ((c.fromId == n.id && c.fromPort == port) ||
                     (c.toId   == n.id && c.toPort   == port))) {
                    n.adjSids[port] = 5000u + (uint32_t)n.id * 8u + (uint32_t)port;
                    break;
                }
            }
        }
    }

    // ── Phase 3: Build srFib (shortest-path SR) ────────────────────────────
    for (auto& n : nodes) {
        if (!n.srEnabled) continue;
        n.srFib.clear();

        for (const auto& [label, destNodeId] : labelToNodeId) {
            const DeviceNode* destNode = FindNode(nodes, destNodeId);
            if (!destNode) continue;

            if (n.id == destNodeId) {
                // Egress self-pop: pop label; IP routing determines outPort.
                // outPort=-1 means the SimulationEngine transit code does NOT
                // override hop.outPort, so the IP route's outPort is preserved.
                n.srFib[label] = {label, MPLS_IMPLICIT_NULL, -1, 0};
                continue;
            }

            int port = NextHopPort(n, *destNode);
            if (port < 0) continue;   // no OSPF route

            int nhNodeId = NextHopNodeId(n, port, cables);
            if (nhNodeId == destNodeId) {
                // Penultimate hop: PHP — pop label, forward to destination
                n.srFib[label] = {label, MPLS_IMPLICIT_NULL, port, 0};
            } else {
                // Transit: forward with same label unchanged
                n.srFib[label] = {label, label, port, 0};
            }
        }
    }

    // ── Phase 4: Compute SR policies ──────────────────────────────────────
    for (auto& n : nodes) {
        if (!n.srEnabled) continue;
        for (auto& p : n.srPolicies) {
            if (!p.segmentsResolved)
                ResolveSrSegments(p, nodes);

            if (p.segmentHops.empty() || p.segmentIps.empty()) {
                p.isActive = false;
                if (p.statusMsg.empty()) p.statusMsg = "No segments";
                continue;
            }

            // Verify all segment nodes have positive IDs (resolved OK)
            bool allOk = true;
            for (int si = 0; si < (int)p.segmentHops.size(); ++si) {
                if (p.segmentHops[si] < 0) {
                    allOk = false;
                    break;
                }
                const DeviceNode* segNode = FindNode(nodes, p.segmentHops[si]);
                if (!segNode) { allOk = false; break; }
                int port = NextHopPort(n, *segNode);
                if (port < 0) {
                    allOk = false;
                    p.statusMsg = "Segment " + std::to_string(si + 1) + " unreachable";
                    break;
                }
            }

            if (!allOk) {
                p.isActive = false;
                continue;
            }

            // Build activePath: full OSPF path through each waypoint in order
            p.activePath.clear();
            int prev = n.id;
            p.activePath.push_back(prev);
            for (int segId : p.segmentHops) {
                auto seg = OspfPath(prev, segId, nodes, cables);
                if (seg.size() < 2) { allOk = false; break; }
                for (size_t k = 1; k < seg.size(); ++k)
                    p.activePath.push_back(seg[k]);
                prev = segId;
            }

            if (!allOk) {
                p.isActive = false;
                continue;
            }

            p.isActive = true;
            p.statusMsg = "Active";
        }
    }
}
```

- [ ] **Step 3: Build**

```bash
make 2>&1 | head -30
```

Expected: clean build. `SrEngine.cpp` is picked up automatically by `$(wildcard src/*.cpp)`.

- [ ] **Step 4: Commit**

```bash
git add src/SrEngine.h src/SrEngine.cpp
git commit -m "feat(sr): add SrEngine with 4-phase SR computation and ResolveSrSegments"
```

---

## Task 4: SimulationEngine.cpp — SR forwarding

**Files:**

- Modify: `src/SimulationEngine.cpp`

- [ ] **Step 1: Add SR head-end check before the TE head-end check**

In `src/SimulationEngine.cpp`, the TE head-end check begins at line 380:
```cpp
// ── TE tunnel head-end: impose tunnel label stack ──────────
if (cur->rsvpEnabled && currentLabel == 0) {
```

Add the following **immediately before** that block (before the `// ── TE tunnel head-end` comment):

```cpp
                // ── SR head-end: push full label stack ──────────────────
                if (cur->srEnabled && currentLabel == 0) {
                    auto slash = destIp.find('/');
                    std::string plainDest = (slash != std::string::npos)
                                           ? destIp.substr(0, slash) : destIp;
                    for (auto& p : cur->srPolicies) {
                        if (!p.isActive || p.destIp.empty()) continue;
                        if (p.destIp == plainDest && !p.labelStack.empty()) {
                            anim.srLabelStack = p.labelStack;
                            anim.currentLabel = anim.srLabelStack.back();
                            anim.srSegmentIdx = 0;
                            anim.srPolicyId   = p.id;
                            hd.labelOp   = LABEL_PUSH;
                            hd.inLabel   = 0;
                            hd.outLabel  = anim.currentLabel;
                            hd.policyId  = p.id;
                            hd.segmentIndex = 0;
                            currentLabel = anim.currentLabel;
                            goto done_mpls;
                        }
                    }
                }
```

- [ ] **Step 2: Add SR transit check before the TE transit check**

At line 396 in `src/SimulationEngine.cpp`:
```cpp
                // MPLS: TE teLfib takes priority over LDP lfib
                if (cur->rsvpEnabled && currentLabel != 0) {
```

Add the following **immediately before** that block (before the `// MPLS: TE teLfib` comment):

```cpp
                // ── SR transit: srFib lookup (PHP/transit/self-pop) ──────
                if (cur->srEnabled && currentLabel != 0) {
                    auto it = cur->srFib.find(currentLabel);
                    if (it != cur->srFib.end()) {
                        const SrLfibEntry& se = it->second;
                        hd.policyId     = anim.srPolicyId;
                        hd.segmentIndex = anim.srSegmentIdx;
                        if (se.outLabel == MPLS_IMPLICIT_NULL) {
                            hd.labelOp  = LABEL_POP;
                            hd.inLabel  = currentLabel;
                            hd.outLabel = 0;
                            if (!anim.srLabelStack.empty()) anim.srLabelStack.pop_back();
                            anim.currentLabel = anim.srLabelStack.empty()
                                                ? 0 : anim.srLabelStack.back();
                            anim.srSegmentIdx++;
                            // Only override outPort when srFib has a specific port.
                            // outPort=-1 (egress self-pop) lets IP routing determine outPort.
                            if (se.outPort >= 0) hd.outPort = se.outPort;
                        } else {
                            hd.labelOp  = LABEL_SWAP;
                            hd.inLabel  = currentLabel;
                            hd.outLabel = se.outLabel;
                            hd.outPort  = se.outPort;
                        }
                        currentLabel = anim.currentLabel;
                        goto done_mpls;
                    }
                }
```

Note: `anim` is the `PacketAnim&` variable in `SimulationEngine.cpp`. Confirm the variable name by checking nearby TE head-end code where it's used (same context block).

- [ ] **Step 3: Build**

```bash
make 2>&1 | head -30
```

Expected: clean build. (The `anim` fields `srLabelStack`, `srSegmentIdx`, `srPolicyId` were added in Task 2. The `hd.policyId` and `hd.segmentIndex` fields were added in Task 1.)

If `anim` is not the variable name in scope, search for how the TE head-end accesses `tun.headLabel`:
```bash
grep -n "anim\." src/SimulationEngine.cpp | head -10
```

- [ ] **Step 4: Commit**

```bash
git add src/SimulationEngine.cpp
git commit -m "feat(sr): add SR forwarding to SimulationEngine (head-end push + transit PHP/swap)"
```

---

## Task 5: ConfigPanel.h — TAB_SR enum + PanelState + rect declarations

**Files:**

- Modify: `src/ConfigPanel.h`

- [ ] **Step 1: Add `TAB_SR` to the `PanelTab` enum**

In `src/ConfigPanel.h`, change line 7:

```cpp
enum PanelTab { TAB_CONFIG, TAB_ROUTES, TAB_ARP, TAB_OSPF, TAB_MPLS, TAB_BGP,
                TAB_VLAN, TAB_SUB, TAB_VXLAN, TAB_ACL, TAB_NAT, TAB_TE };
```

to:

```cpp
enum PanelTab { TAB_CONFIG, TAB_ROUTES, TAB_ARP, TAB_OSPF, TAB_MPLS, TAB_BGP,
                TAB_VLAN, TAB_SUB, TAB_VXLAN, TAB_ACL, TAB_NAT, TAB_TE, TAB_SR };
```

- [ ] **Step 2: Add SR fields to `PanelState` after the TE tab fields**

In `src/ConfigPanel.h`, after the TE tab fields block (after `std::string tePbwBuf;`), add:

```cpp
    // SR tab
    bool        srNodeSidEditing = false;
    std::string srNodeSidBuf;            // digit buffer for Node SID (1-999)
    int         srExpandedIdx    = -1;   // policy index currently expanded (-1=none)
    int         srActiveField    = -1;   // 0=dest, 1=segs, -1=none
    std::string srDestBuf;
    std::string srSegsBuf;
```

- [ ] **Step 3: Add SR rect declarations after the TE rect declarations**

In `src/ConfigPanel.h`, after `Rectangle PnlTeDelBtnRect();`, add:

```cpp
// SR tab
float     SrListBaseY();
float     SrRowH();
float     SrFormH();
Rectangle PnlSrTabRect();
Rectangle PnlSrToggleRect();
Rectangle PnlSrNodeSidRect();
Rectangle PnlSrPolicyRowRect(int idx);
Rectangle PnlSrAddBtnRect(int policyCount);
```

- [ ] **Step 4: Build**

```bash
make 2>&1 | head -20
```

Expected: clean build (declarations added, implementations pending in Task 6).

- [ ] **Step 5: Commit**

```bash
git add src/ConfigPanel.h
git commit -m "feat(sr): add TAB_SR enum, PanelState SR fields, and SR rect declarations"
```

---

## Task 6: ConfigPanel.cpp — tab count + SR rect implementations

**Files:**

- Modify: `src/ConfigPanel.cpp`

- [ ] **Step 1: Update `PnlTabW()` for 12 router tabs**

In `src/ConfigPanel.cpp`, change line 18:

```cpp
float PnlTabW() { return (PANEL_W - 24.0f - 10.0f * 4.0f) / 11.0f; }
```

to:

```cpp
float PnlTabW() { return (PANEL_W - 24.0f - 11.0f * 4.0f) / 12.0f; }
```

(12 tabs with 11 gaps between them.)

- [ ] **Step 2: Add SR entry to `kRtTabs[]`**

In `src/ConfigPanel.cpp`, change the `kRtTabs[]` array (lines 196–208). The current last entry is:
```cpp
    {TAB_TE,     "TE",   Color{251,191,36,255}, Color{251,191,36,255}},
```

Add after it (before the closing `};`):

```cpp
    {TAB_SR,     "SR",   Color{59,130,246,255},  Color{59,130,246,255}},
```

Also update the comment on line 195 from `"Router: 10 tabs"` to `"Router: 12 tabs"`.

- [ ] **Step 3: Update `PnlTabCount` for ROUTER**

Change `return 11;` to `return 12;` in the `if (t == PC) ... return 11;` branch for ROUTER.

In `src/ConfigPanel.cpp` (lines 210–213):
```cpp
int PnlTabCount(DeviceType t) {
    if (t == PC)     return 3;
    if (t == SWITCH) return 3;
    return 11;
}
```

Change to:
```cpp
int PnlTabCount(DeviceType t) {
    if (t == PC)     return 3;
    if (t == SWITCH) return 3;
    return 12;
}
```

- [ ] **Step 4: Add SR layout helpers and rect functions**

At the end of `src/ConfigPanel.cpp` (after `PnlTeDelBtnRect()`), add:

```cpp
// ── SR tab helpers and rects ──────────────────────────────────────────────
float SrListBaseY() { return 316.0f; }
float SrRowH()      { return 28.0f;  }
float SrFormH()     { return 132.0f; }

Rectangle PnlSrTabRect() {
    return {(float)(CANVAS_W() + 12) + 11.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlSrToggleRect() {
    return {(float)(CANVAS_W() + 12), 120.0f, (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlSrNodeSidRect() {
    return {(float)(CANVAS_W() + 80), 152.0f, 44.0f, 22.0f};
}
Rectangle PnlSrPolicyRowRect(int idx) {
    return {(float)(CANVAS_W() + 12), SrListBaseY() + (float)idx * SrRowH(),
            (float)(PANEL_W - 24), SrRowH() - 2.0f};
}
Rectangle PnlSrAddBtnRect(int policyCount) {
    float y = SrListBaseY() + (float)policyCount * SrRowH() + 4.0f;
    return {(float)(CANVAS_W() + 12), y, (float)(PANEL_W - 24), 26.0f};
}
```

- [ ] **Step 5: Build**

```bash
make 2>&1 | head -20
```

Expected: clean build. The tab row now shows 12 tabs for routers.

- [ ] **Step 6: Commit**

```bash
git add src/ConfigPanel.cpp
git commit -m "feat(sr): add TAB_SR to router tab list and SR panel rect helpers"
```

---

## Task 7: NetworkCanvas.h + DrawSrTab

**Files:**

- Modify: `src/NetworkCanvas.h`
- Modify: `src/NetworkCanvas.cpp`

- [ ] **Step 1: Declare `DrawSrTab` and `DrawSrPolicyOverlays` in `NetworkCanvas.h`**

After `void DrawTeTab(const DeviceNode* n, const PanelState& ps);`, add:

```cpp
void DrawSrTab(const DeviceNode* n, const PanelState& ps);
void DrawSrPolicyOverlays(const std::vector<DeviceNode>& nodes,
                          const std::vector<Cable>&      cables);
```

- [ ] **Step 2: Implement `DrawSrTab` in `NetworkCanvas.cpp`**

Add after `DrawTeTab`'s closing `}` (after line 1469):

```cpp
void DrawSrTab(const DeviceNode* n, const PanelState& ps)
{
    float px = (float)(CANVAS_W() + 12);
    float pw = (float)(PANEL_W - 24);
    Color DIM  = {100, 116, 139, 255};
    Color WHT  = WHITE;
    Color ON   = {34,  197, 94,  255};
    Color OFF  = {100, 116, 139, 255};
    Color BLUE = {59,  130, 246, 255};

    if (!n) { DrawTextEx(GFont(), "No device selected", {px, 130.0f}, FS(11), Sp(FS(11)), DIM); return; }
    if (n->type != ROUTER) { DrawTextEx(GFont(), "SR: routers only", {px, 130.0f}, FS(11), Sp(FS(11)), DIM); return; }

    // Toggle row
    const char* lbl  = n->srEnabled ? "sr-mpls  ON" : "sr-mpls  OFF";
    Color       tcol = n->srEnabled ? ON : OFF;
    DrawRectangleRoundedLines(PnlSrToggleRect(), 0.4f, 4, tcol);
    float tw = TW(lbl, 12);
    DrawTextEx(GFont(), lbl, {px + (pw - tw) * 0.5f, 126.0f}, FS(12), Sp(FS(12)), tcol);

    if (!n->srEnabled) return;

    // Node SID row
    DrawTextEx(GFont(), "Node SID:", {px, 156.0f}, FS(10), Sp(FS(10)), DIM);
    Rectangle sidRect = PnlSrNodeSidRect();
    bool sidActive = ps.srNodeSidEditing;
    DrawRectangleRec(sidRect, Color{30, 41, 59, 255});
    DrawRectangleLinesEx(sidRect, 1.0f, sidActive ? BLUE : Color{51,65,85,255});
    const std::string& sidTxt = sidActive ? ps.srNodeSidBuf
                                           : (n->nodeSid > 0 ? std::to_string(n->nodeSid) : "—");
    DrawTextEx(GFont(), sidTxt.c_str(), {sidRect.x + 4.0f, sidRect.y + 4.0f},
               FS(11), Sp(FS(11)), WHT);
    if (n->nodeSid > 0) {
        char lblBuf[32];
        std::snprintf(lblBuf, sizeof(lblBuf), "-> label: %u", SRGB_BASE + n->nodeSid);
        DrawTextEx(GFont(), lblBuf, {sidRect.x + sidRect.width + 6.0f, sidRect.y + 4.0f},
                   FS(10), Sp(FS(10)), BLUE);
    }
    DrawTextEx(GFont(), "SRGB: 17000-17999 (global)", {px, 178.0f}, FS(9), Sp(FS(9)), DIM);

    // Adj SIDs (auto) section
    DrawTextEx(GFont(), "Adj SIDs (auto)", {px, 198.0f}, FS(9), Sp(FS(9)), DIM);
    int adjRowY = 212;
    for (int p = 0; p < PORTS_PER_NODE; ++p) {
        if (n->portIp[p].empty()) continue;
        auto it = n->adjSids.find(p);
        if (it == n->adjSids.end()) continue;
        std::string portName = GetPortName(n->type, p);
        char adjBuf[48];
        std::snprintf(adjBuf, sizeof(adjBuf), "%s  adj:%u", portName.c_str(), it->second);
        DrawTextEx(GFont(), adjBuf, {px + 4.0f, (float)adjRowY}, FS(10), Sp(FS(10)), DIM);
        adjRowY += 18;
    }

    // Section divider + SR Policies header
    DrawLineEx({px, SrListBaseY() - 12.0f}, {px + pw, SrListBaseY() - 12.0f}, 1.0f,
               Color{51, 65, 85, 255});
    DrawTextEx(GFont(), "SR Policies", {px, SrListBaseY() - 10.0f}, FS(9), Sp(FS(9)), DIM);

    // Policy list
    float listY = SrListBaseY();
    for (int i = 0; i < (int)n->srPolicies.size(); ++i) {
        const auto& p    = n->srPolicies[i];
        bool expanded    = (ps.srExpandedIdx == i);
        float rowY       = listY;
        listY           += SrRowH();

        Color statusColor = p.isActive ? ON : Color{239,68,68,255};
        DrawRectangleRounded({px, rowY, pw, SrRowH() - 2.0f}, 0.3f, 4, Color{21, 30, 47, 255});
        DrawRectangle((int)px, (int)rowY, 3, (int)(SrRowH() - 2.0f), BLUE);

        char rowLabel[64];
        std::snprintf(rowLabel, sizeof(rowLabel), "%s Policy-%d  ->%s",
                      expanded ? "v" : ">", p.id,
                      p.destIp.empty() ? "?" : p.destIp.c_str());
        DrawTextEx(GFont(), rowLabel, {px + 8.0f, rowY + 7.0f}, FS(10), Sp(FS(10)), WHT);
        const char* statusTxt = p.isActive ? "ACTIVE" : "DOWN";
        float sw = TW(statusTxt, 10);
        DrawTextEx(GFont(), statusTxt, {px + pw - sw - 4.0f, rowY + 7.0f},
                   FS(10), Sp(FS(10)), statusColor);

        if (!expanded) continue;
        listY += SrFormH();

        float fy = rowY + SrRowH();
        auto field = [&](const char* flbl, float y, const std::string& val, bool act) {
            DrawTextEx(GFont(), flbl, {px + 4.0f, y}, FS(10), Sp(FS(10)), DIM);
            Rectangle r = {px + 44.0f, y - 2.0f, pw - 48.0f, 20.0f};
            DrawRectangleRec(r, Color{30, 41, 59, 255});
            DrawRectangleLinesEx(r, 1.0f, act ? BLUE : Color{51,65,85,255});
            DrawTextEx(GFont(), val.c_str(), {r.x + 4.0f, r.y + 3.0f}, FS(10), Sp(FS(10)), WHT);
        };

        field("Dest:", fy, (ps.srActiveField == 0) ? ps.srDestBuf : p.destIp, ps.srActiveField == 0);
        fy += 24.0f;
        field("Segs:", fy, (ps.srActiveField == 1) ? ps.srSegsBuf : [&]{
            std::string s;
            for (const auto& ip : p.segmentIps) s += ip + " ";
            return s;
        }(), ps.srActiveField == 1);
        fy += 20.0f;

        // Live label preview
        if (!p.segmentHops.empty()) {
            std::string preview;
            for (int si = 0; si < (int)p.segmentHops.size(); ++si) {
                if (si < (int)p.segmentIps.size()) {
                    if (p.segmentHops[si] >= 0 && si < (int)p.labelStack.size()) {
                        // labelStack is reversed: label for seg[si] is at index (size-1-si)
                        size_t li = p.labelStack.size() - 1 - (size_t)si;
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "SID:%u·lbl:%u  ",
                                      (unsigned)p.segmentHops[si],
                                      (unsigned)p.labelStack[li]);
                        preview += buf;
                    } else {
                        preview += "?  ";
                    }
                }
            }
            DrawTextEx(GFont(), preview.c_str(), {px + 8.0f, fy}, FS(9), Sp(FS(9)), DIM);
        }
        fy += 18.0f;

        float delW = pw * 0.4f;
        DrawRectangleRounded({px + pw - delW, fy, delW, 22.0f}, 0.4f, 4, Color{127,29,29,255});
        DrawTextEx(GFont(), "Del", {px + pw - delW + 4.0f, fy + 4.0f}, FS(10), Sp(FS(10)), WHT);
    }

    // Add Policy button
    int visCount = (int)n->srPolicies.size() + (ps.srExpandedIdx >= 0 ? 1 : 0);
    Rectangle addBtn = PnlSrAddBtnRect(visCount);
    DrawRectangleRounded(addBtn, 0.4f, 4, Color{21,128,61,200});
    float atw = TW("+ Add Policy", 11);
    DrawTextEx(GFont(), "+ Add Policy",
               {addBtn.x + (addBtn.width - atw) * 0.5f, addBtn.y + 6.0f},
               FS(11), Sp(FS(11)), WHT);
}
```

Note on the live label preview: `labelStack` is stored innermost-first (outermost at `back()`). The segment at index `si` in `segmentIps` corresponds to `labelStack[size-1-si]` (the outermost label is for segment 0). Verify this matches the order in `ResolveSrSegments`.

- [ ] **Step 3: Build**

```bash
make 2>&1 | head -30
```

Expected: clean build. If a lambda-in-argument issue arises for the `field(...)` Segs call, extract the segment IPs string to a local variable first:

```cpp
std::string segsStr;
for (const auto& ip : p.segmentIps) segsStr += ip + " ";
field("Segs:", fy, (ps.srActiveField == 1) ? ps.srSegsBuf : segsStr, ps.srActiveField == 1);
```

- [ ] **Step 4: Commit**

```bash
git add src/NetworkCanvas.h src/NetworkCanvas.cpp
git commit -m "feat(sr): implement DrawSrTab UI panel"
```

---

## Task 8: NetworkCanvas.cpp — DrawSrPolicyOverlays + DrawPanel dispatch

**Files:**

- Modify: `src/NetworkCanvas.cpp`

- [ ] **Step 1: Implement `DrawSrPolicyOverlays` after `DrawTeTunnelOverlays`**

Add after `DrawTeTunnelOverlays`'s closing `}` (after line 202), before `HitTestPort`:

```cpp
// Draws dashed bezier overlays for active SR policies.
// "Dashed = SR, solid = TE" visual grammar.
void DrawSrPolicyOverlays(const std::vector<DeviceNode>& nodes,
                          const std::vector<Cable>&      cables)
{
    static const Color kSrPalette[] = {
        { 59, 130, 246, 200},   // electric-blue
        {139,  92, 246, 200},   // violet
        { 16, 185, 129, 200},   // emerald
        {249, 115,  22, 200},   // orange
        {236,  72, 153, 200},   // pink
        {234, 179,   8, 200},   // yellow
    };
    static const int kPaletteSize = 6;

    int policyColorIdx = 0;
    for (const auto& n : nodes) {
        if (!n.srEnabled) continue;
        for (const auto& p : n.srPolicies) {
            if (!p.isActive || p.activePath.size() < 2) { ++policyColorIdx; continue; }
            Color col = kSrPalette[policyColorIdx % kPaletteSize];
            ++policyColorIdx;

            // Draw dashed overlay for each consecutive node pair in activePath
            for (size_t i = 0; i + 1 < p.activePath.size(); ++i) {
                int aId = p.activePath[i];
                int bId = p.activePath[i + 1];

                const DeviceNode* nodeA = FindNode(nodes, aId);
                const DeviceNode* nodeB = FindNode(nodes, bId);
                if (!nodeA || !nodeB) continue;

                const Cable* cab = FindCable(cables, aId, bId);
                if (!cab) continue;

                int aPort = (cab->fromId == aId) ? cab->fromPort : cab->toPort;
                int bPort = (cab->fromId == bId) ? cab->fromPort : cab->toPort;

                Vector2 p0 = GetPortPosition(*nodeA, aPort);
                Vector2 p3 = GetPortPosition(*nodeB, bPort);
                Vector2 c1 = BezierCtrl(p0, aPort);
                Vector2 c2 = BezierCtrl(p3, bPort);

                // Perpendicular offset to separate stacked policies
                float   off  = (float)(((policyColorIdx - 1) % 3) - 1) * 3.0f;
                Vector2 dir  = {p3.x - p0.x, p3.y - p0.y};
                float   len  = sqrtf(dir.x * dir.x + dir.y * dir.y);
                Vector2 perp = (len > 0.001f) ? Vector2{-dir.y / len, dir.x / len}
                                              : Vector2{0.f, 0.f};
                Vector2 o    = {perp.x * off, perp.y * off};

                Vector2 op0 = {p0.x + o.x, p0.y + o.y};
                Vector2 op3 = {p3.x + o.x, p3.y + o.y};
                Vector2 oc1 = {c1.x + o.x, c1.y + o.y};
                Vector2 oc2 = {c2.x + o.x, c2.y + o.y};

                // Dashed line: draw every other segment of 24 equal bezier subdivisions
                const int SEGS = 24;
                for (int si = 0; si < SEGS; si += 2) {
                    float t0 = (float)si       / SEGS;
                    float t1 = (float)(si + 1) / SEGS;
                    Vector2 a = EvaluateCubicBezier(op0, oc1, oc2, op3, t0);
                    Vector2 b = EvaluateCubicBezier(op0, oc1, oc2, op3, t1);
                    DrawLineEx(a, b, 5.0f, col);
                }
            }

            // Label stack badge at head-end router
            const DeviceNode* headNode = FindNode(nodes, p.activePath[0]);
            if (headNode && !p.labelStack.empty()) {
                Vector2 pos = headNode->position;
                char line1[32], line2[64];
                std::snprintf(line1, sizeof(line1), "Policy-%d push", p.id);

                // labelStack: innermost first, outermost at back.
                // Display from outermost (back) to innermost (front).
                std::string stackStr = "[ ";
                for (int li = (int)p.labelStack.size() - 1; li >= 0; --li) {
                    if (li < (int)p.labelStack.size() - 1) stackStr += " | ";
                    stackStr += std::to_string(p.labelStack[li]);
                }
                stackStr += " ]";
                std::snprintf(line2, sizeof(line2), "%s", stackStr.c_str());

                float bw1 = TW(line1, 9) + 10.0f;
                float bw2 = TW(line2, 9) + 10.0f;
                float bw  = std::max(bw1, bw2);
                float bx  = pos.x - bw * 0.5f;
                float by  = pos.y - NODE_H * 0.5f - 38.0f;

                DrawRectangleRounded({bx, by, bw, 28.0f}, 0.4f, 4, Color{15, 23, 42, 220});
                DrawRectangleRoundedLinesEx({bx, by, bw, 28.0f}, 0.4f, 4, 1.0f, col);
                DrawTextEx(GFont(), line1, {bx + 5.0f, by + 3.0f},  FS(9), Sp(FS(9)), col);
                DrawTextEx(GFont(), line2, {bx + 5.0f, by + 15.0f}, FS(9), Sp(FS(9)), WHITE);
            }
        }
    }
}
```

- [ ] **Step 2: Add TAB_SR dispatch in `DrawPanel`**

In `src/NetworkCanvas.cpp`, find the tab dispatch block (around line 1545):
```cpp
    else if (ps.activeTab == TAB_TE)   DrawTeTab(n, ps);
```

Add after it:
```cpp
    else if (ps.activeTab == TAB_SR)   DrawSrTab(n, ps);
```

- [ ] **Step 3: Build**

```bash
make 2>&1 | head -30
```

Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add src/NetworkCanvas.cpp
git commit -m "feat(sr): add DrawSrPolicyOverlays (dashed bezier) and DrawPanel TAB_SR dispatch"
```

---

## Task 9: main.cpp — integration

**Files:**

- Modify: `src/main.cpp`

- [ ] **Step 1: Add `#include "SrEngine.h"` after `#include "RsvpEngine.h"`**

In `src/main.cpp` (line 4):
```cpp
#include "RsvpEngine.h"
```

Add after it:
```cpp
#include "SrEngine.h"
```

- [ ] **Step 2: Call `UpdateSr` after `UpdateRsvp` in the simulation tick**

In `src/main.cpp`, find line 1647:
```cpp
            UpdateRsvp(nodes, cables);
```

Add after it:
```cpp
            UpdateSr(nodes, cables);
```

- [ ] **Step 3: Call `DrawSrPolicyOverlays` after `DrawTeTunnelOverlays` in the render loop**

In `src/main.cpp`, find line 1741:
```cpp
                DrawTeTunnelOverlays(nodes, cables);
```

Add after it:
```cpp
                DrawSrPolicyOverlays(nodes, cables);
```

- [ ] **Step 4: Add TAB_SR mouse handler**

In `src/main.cpp`, find the end of the TAB_TE mouse block (around line 1329):
```cpp
                        te_input_done:;
                    }
                }
```

Add the following TAB_SR mouse handler immediately after the closing `}` for TAB_TE (before the next `}`):

```cpp
                if (ps.activeTab == TAB_SR && selectedId != -1) {
                    DeviceNode* selNode = nullptr;
                    for (auto& nd : nodes) if (nd.id == selectedId) { selNode = &nd; break; }
                    if (selNode && selNode->type == ROUTER) {
                        // Toggle sr-mpls
                        if (CheckCollisionPointRec(screenMouse, PnlSrToggleRect())) {
                            selNode->srEnabled = !selNode->srEnabled;
                            if (!selNode->srEnabled) {
                                selNode->srFib.clear();
                                selNode->srPolicies.clear();
                                selNode->adjSids.clear();
                                selNode->nodeSid = 0;
                            }
                        }
                        if (!selNode->srEnabled) goto sr_input_done;

                        // Node SID field
                        if (CheckCollisionPointRec(screenMouse, PnlSrNodeSidRect())) {
                            ps.srNodeSidEditing = !ps.srNodeSidEditing;
                            if (ps.srNodeSidEditing)
                                ps.srNodeSidBuf = selNode->nodeSid > 0
                                                  ? std::to_string(selNode->nodeSid) : "";
                        }

                        // Policy row click → expand/collapse
                        for (int i = 0; i < (int)selNode->srPolicies.size(); ++i) {
                            if (CheckCollisionPointRec(screenMouse, PnlSrPolicyRowRect(i))) {
                                ps.srExpandedIdx = (ps.srExpandedIdx == i) ? -1 : i;
                                ps.srActiveField = -1;
                                if (ps.srExpandedIdx == i) {
                                    ps.srDestBuf = selNode->srPolicies[i].destIp;
                                    std::string segs;
                                    for (const auto& seg : selNode->srPolicies[i].segmentIps)
                                        segs += seg + " ";
                                    ps.srSegsBuf = segs;
                                }
                            }
                        }

                        // Expanded form field clicks and Del button
                        if (ps.srExpandedIdx >= 0 &&
                            ps.srExpandedIdx < (int)selNode->srPolicies.size()) {
                            auto& p  = selNode->srPolicies[ps.srExpandedIdx];
                            float fy = SrListBaseY()
                                       + (float)ps.srExpandedIdx * SrRowH() + SrRowH();

                            Rectangle destR = {(float)(CANVAS_W() + 44 + 4), fy - 2.0f,
                                              (float)(PANEL_W - 52), 20.0f};
                            if (CheckCollisionPointRec(screenMouse, destR))
                                ps.srActiveField = 0;
                            fy += 24.0f;

                            Rectangle segsR = {(float)(CANVAS_W() + 44 + 4), fy - 2.0f,
                                              (float)(PANEL_W - 52), 20.0f};
                            if (CheckCollisionPointRec(screenMouse, segsR))
                                ps.srActiveField = 1;
                            fy += 20.0f + 18.0f;  // segs field + preview row

                            float delW = (float)(PANEL_W - 24) * 0.4f;
                            Rectangle delR = {(float)(CANVAS_W() + 12 + PANEL_W - 24 - delW),
                                              fy, delW, 22.0f};
                            if (CheckCollisionPointRec(screenMouse, delR)) {
                                selNode->srPolicies.erase(
                                    selNode->srPolicies.begin() + ps.srExpandedIdx);
                                ps.srExpandedIdx = -1;
                                ps.srActiveField = -1;
                            }
                        }

                        // Add Policy button
                        {
                            int visCount = (int)selNode->srPolicies.size()
                                           + (ps.srExpandedIdx >= 0 ? 1 : 0);
                            if (CheckCollisionPointRec(screenMouse, PnlSrAddBtnRect(visCount))) {
                                SrPolicy np;
                                np.id = selNode->srPolicies.empty()
                                        ? 1 : selNode->srPolicies.back().id + 1;
                                selNode->srPolicies.push_back(np);
                            }
                        }

                        sr_input_done:;
                    }
                }
```

- [ ] **Step 5: Add TAB_SR keyboard handler**

In `src/main.cpp`, find the end of the TAB_TE keyboard block (around line 1611):
```cpp
        }
```

Add the TAB_SR keyboard handler after the TAB_TE keyboard block (after line 1611, before the selection-change reset):

```cpp
        if (ps.activeTab == TAB_SR && selectedId != -1) {
            DeviceNode* sn = nullptr;
            for (auto& nd : nodes) if (nd.id == selectedId) { sn = &nd; break; }
            if (sn && sn->srEnabled) {
                // Node SID digit editing
                if (ps.srNodeSidEditing) {
                    // Accept only digits 0-9
                    int ch;
                    while ((ch = GetCharPressed()) > 0)
                        if ((int)ps.srNodeSidBuf.size() < 3 && ch >= '0' && ch <= '9')
                            ps.srNodeSidBuf += (char)ch;
                    if (IsKeyPressed(KEY_BACKSPACE) && !ps.srNodeSidBuf.empty())
                        ps.srNodeSidBuf.pop_back();
                    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) {
                        int sid = std::atoi(ps.srNodeSidBuf.c_str());
                        if (sid >= 1 && sid < (int)SRGB_SIZE) {
                            sn->nodeSid = (uint32_t)sid;
                            // Invalidate all policies on all nodes (topology SID change)
                            for (auto& nd : nodes)
                                for (auto& p : nd.srPolicies)
                                    p.segmentsResolved = false;
                        }
                        ps.srNodeSidEditing = false;
                    }
                }

                // Expanded policy form keyboard input
                if (ps.srExpandedIdx >= 0 &&
                    ps.srExpandedIdx < (int)sn->srPolicies.size()) {
                    auto& p = sn->srPolicies[ps.srExpandedIdx];

                    if (ps.srActiveField == 0) {
                        // Dest field
                        UpdateTextField(ps.srDestBuf, 15);
                        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) {
                            p.destIp = ps.srDestBuf;
                            ps.srActiveField = -1;
                        }
                    } else if (ps.srActiveField == 1) {
                        // Segs field — live preview on every keystroke
                        UpdateTextField(ps.srSegsBuf, 120);

                        // Parse and resolve immediately for live label preview
                        p.segmentIps.clear();
                        std::istringstream ss(ps.srSegsBuf);
                        std::string tok;
                        while (ss >> tok) p.segmentIps.push_back(tok);
                        p.segmentsResolved = false;
                        ResolveSrSegments(p, nodes);

                        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) {
                            // Already committed on each keystroke; just close field
                            ps.srActiveField = -1;
                        }
                    }
                }
            }
        }
```

- [ ] **Step 6: Add SR field resets in the selection-change block**

In `src/main.cpp`, find the selection-change reset block at line 1614:
```cpp
        if (selectedId != prevSelectedId) {
            ps.activeField         = -1;
```

Find the last line of this block (around line 1637):
```cpp
            prevSelectedId         = selectedId;
```

Add the following lines **before** `prevSelectedId = selectedId;`:

```cpp
            ps.srNodeSidEditing    = false;
            ps.srNodeSidBuf.clear();
            ps.srExpandedIdx       = -1;
            ps.srActiveField       = -1;
            ps.srDestBuf.clear();
            ps.srSegsBuf.clear();
```

Also add the TE fields that are currently missing from the reset (check if `teExpandedIdx` and `teActiveField` are reset — if not, add them at the same time):

```cpp
            ps.teExpandedIdx       = -1;
            ps.teActiveField       = -1;
            ps.tePbwActivePort     = -1;
            ps.tePbwBuf.clear();
            ps.teDestBuf.clear();
            ps.teBwBuf.clear();
            ps.teHopsBuf.clear();
```

(Only add TE resets if they're not already present — grep for `teExpandedIdx` in the reset block to confirm.)

- [ ] **Step 7: Build**

```bash
make 2>&1 | head -40
```

Expected: clean build. Common issues:
- `SRGB_SIZE` not in scope in `main.cpp`: add `#include "SrEngine.h"` if not already done — SrEngine.h includes Device.h which has SRGB_SIZE.
- `sr_input_done:` label — must be within a function scope; verify the `goto sr_input_done:` and label are in the same block.
- `SrListBaseY()`, `SrRowH()`, `SrFormH()` — declared in ConfigPanel.h and implemented in ConfigPanel.cpp; included via NetworkCanvas.h.

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp
git commit -m "feat(sr): wire UpdateSr, TAB_SR mouse/keyboard handlers, and SR overlay into main loop"
```

---

## Task 10: Spec update + build verification

**Files:**

- Modify: `docs/superpowers/specs/2026-05-09-sr-mpls-engine-design.md`

- [ ] **Step 1: Fix SRGB_BASE in the spec**

In `docs/superpowers/specs/2026-05-09-sr-mpls-engine-design.md`, find the Constants section:

```cpp
constexpr uint32_t SRGB_BASE = 1000;
constexpr uint32_t SRGB_SIZE = 1000;
constexpr uint32_t SRGB_END  = SRGB_BASE + SRGB_SIZE;  // exclusive upper bound
```

Change to:

```cpp
constexpr uint32_t SRGB_BASE = 17000;  // above LDP (n.id*100) and RSVP-TE (16000+)
constexpr uint32_t SRGB_SIZE = 1000;
constexpr uint32_t SRGB_END  = SRGB_BASE + SRGB_SIZE;  // exclusive upper bound (17999)
```

Also update any label example values in the spec that use 1000-range labels (e.g., `1002`, `1004`, `1005`) to use 17000-range values (`17002`, `17004`, `17005`). Search:

```bash
grep -n "100[0-9]" docs/superpowers/specs/2026-05-09-sr-mpls-engine-design.md
```

- [ ] **Step 2: Full clean build**

```bash
make clean && make 2>&1 | tail -5
```

Expected output ends with something like:
```
g++ ... -o packet-path
```

No errors, no warnings (or only known pre-existing warnings).

- [ ] **Step 3: Smoke test — enable SR, configure SIDs, create policy**

Launch the binary: `./packet-path`

Verify these behaviors manually:

1. Spawn 4 routers (R1–R4) in a chain. Wire them: R1-R2, R2-R3, R3-R4.
2. Enable OSPF on all routers (OSP tab) and set router IDs. Wait for adjacencies.
3. Open R2's SR tab → toggle ON → set Node SID = 2. Label preview shows "→ label: 17002".
4. Open R3's SR tab → toggle ON → set Node SID = 3.
5. Open R4's SR tab → toggle ON → set Node SID = 4.
6. Open R1's SR tab → toggle ON → Add Policy → expand Policy-1.
   - Dest: R4's port IP (e.g., "10.0.4.1")
   - Segs: R3's IP then R4's IP (e.g., "10.0.3.1 10.0.4.1")
   - Live preview shows: "SID:3·lbl:17003  SID:4·lbl:17004"
   - Policy status shows "ACTIVE"
7. Canvas shows dashed electric-blue overlay R1→R2→R3→R4 with label stack badge at R1.
8. Send a packet from a PC attached to R1 toward a PC attached to R4.
   - Packet trace should show LABEL_PUSH at R1, LABEL_SWAP or PHP at intermediate hops, LABEL_POP at R3/R4.

- [ ] **Step 4: Commit spec update**

```bash
git add docs/superpowers/specs/2026-05-09-sr-mpls-engine-design.md
git commit -m "docs(sr): fix SRGB_BASE 1000→17000 in spec (LDP label collision)"
```

---

## Known Limitations (v1)

- **Head-end = penultimate edge case**: If the head-end is directly connected to the first segment node with no transit hop between them, the self-pop srFib entry will fire at R2 but IP routing handles outPort correctly. Packets should still flow correctly due to the `outPort=-1` design (IP routing determines egress port). Test confirms with simple topologies.
- **activePath accuracy**: Phase 4 builds activePath by tracing OSPF next-hops per segment. If OSPF isn't converged, some paths may show as empty and policies will show DOWN.
- **Per-segment inter-node reachability**: Currently checks reachability from the HEAD node to each segment, not between consecutive segments. For segment-to-segment reachability in multi-segment policies, OSPF must cover the full topology.
- **No PHP for direct head-end**: When the head-end IS directly connected to a segment node (e.g., R1→R2 with no transit hops), PHP at the head-end is not performed. The self-pop entry on the egress node handles the label correctly.
