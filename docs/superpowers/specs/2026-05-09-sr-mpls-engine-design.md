# SR-MPLS Engine Design

**Date:** 2026-05-09
**Ticket:** PKT-2
**RFCs:** RFC 8402, RFC 8660

## Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Learning goals | Both: shortest-path SR + SR policies | SR without policies is just LDP with different label allocation; policies are the educational revolution |
| Segment list input | IP addresses with live SID/label preview | Familiar input + teaches IP→SID→label mapping in real time |
| ConfigPanel layout | New `TAB_SR` tab (13 tabs total) | SR is a full protocol, not an extension; same treatment as TE |
| Canvas visualization | Dashed overlays + ingress label stack badge | Dashed = SR, solid = TE visual grammar; badge shows source routing concept directly |

## New Files

- `src/SrEngine.h` — `UpdateSr` and `ResolveSrSegments` declarations
- `src/SrEngine.cpp` — SR engine: SID map, adj SID allocation, srFib, SR policy computation

## Data Structures

### Constants (in `Device.h`)

```cpp
constexpr uint32_t SRGB_BASE = 1000;
constexpr uint32_t SRGB_SIZE = 1000;
constexpr uint32_t SRGB_END  = SRGB_BASE + SRGB_SIZE;  // exclusive upper bound
```

### `SrLfibEntry` (new, in `Device.h` alongside `TeLfibEntry`)

```cpp
struct SrLfibEntry {
    uint32_t inLabel  = 0;
    uint32_t outLabel = 0;   // MPLS_IMPLICIT_NULL = PHP at penultimate hop
    int      outPort  = -1;
    int      policyId = 0;   // 0 = node SID shortest-path; >0 = SR policy
};
```

### `SrPolicy` (new, in `Device.h` alongside `TeTunnel`)

```cpp
struct SrPolicy {
    int         id          = 0;        // 1–255, unique per router
    std::string destIp;                 // tail-end destination IP (no mask)

    std::vector<std::string> segmentIps;   // hop IPs as typed (UI storage)
    std::vector<int>         segmentHops;  // resolved node IDs (engine use)
    std::vector<uint32_t>    labelStack;   // computed label stack, innermost first / outermost at back
                                           // e.g. segment list [R2→R4] → labelStack = {1004, 1002}
                                           // back() = first segment's label = outermost (processed first)
    bool             segmentsResolved = false;  // cleared on topology change; set by ResolveSrSegments

    // computed each tick by UpdateSr
    bool             isActive  = false;
    std::vector<int> activePath;   // forced node-ID sequence head→tail (not OSPF shortest path)
    std::string      statusMsg;    // "Active", "Segment unreachable", "No Node SID for X"
};
```

### `DeviceNode` additions (in `Device.h`)

```cpp
bool     srEnabled = false;
uint32_t nodeSid   = 0;      // 1–(SRGB_SIZE-1); 0 = not configured
std::unordered_map<uint32_t, SrLfibEntry> srFib;    // key = inLabel
std::vector<SrPolicy>                     srPolicies;
std::unordered_map<int, uint32_t>         adjSids;  // key = port index; value = adj SID label
```

### `PacketAnim` addition (in `Packet.h`)

```cpp
std::vector<uint32_t> srLabelStack;  // SR label stack stored innermost-first, outermost at back
                                     // back() = current top label (next to be processed)
                                     // e.g. segment list [R2→R4] → srLabelStack = {1004, 1002}
int                   srSegmentIdx = 0;   // which segment of the active SR policy we are on
int                   srPolicyId   = 0;   // ID of the active SR policy (0 = no SR policy)
```

### `HopDecision` additions (in `Device.h`)

```cpp
int policyId     = 0;   // non-zero = this hop is inside a named SR policy
int segmentIndex = 0;   // which segment of the policy this hop belongs to (0-based)
```

## `SrEngine.cpp` — `UpdateSr(nodes, cables)`

Called once per simulation tick, after `UpdateRsvp`.

### Phase 1 — Build Global SID Map

Scan all SR-enabled nodes with `nodeSid > 0`. Build two lookup tables local to `UpdateSr`:

```
nodeSidToLabel: nodeId  → SRGB_BASE + nodeSid
labelToNodeId:  label   → nodeId
```

If two nodes share the same `nodeSid`, log a conflict warning and skip the duplicate.

### Phase 2 — Assign Adj SIDs

For each SR-enabled router, for each port with a connected cable:

```
node.adjSids[port] = 5000 + node.id * 8 + port
```

Deterministic and stable — never changes between ticks, never conflicts across nodes (assuming `node.id` is unique and ≤ 4095).

### Phase 3 — Build `srFib` (Shortest-Path SR)

Clear `node.srFib` before rebuilding. For every label in `labelToNodeId`, for every SR-enabled router:

1. Find OSPF next-hop port toward `labelToNodeId[label]` by scanning `node.ospfRoutes` for the destination's router-ID.
2. Determine hop role:
   - **Egress** (I am the destination): no `srFib` entry needed — label was PHP'd by penultimate.
   - **Penultimate** (my OSPF next-hop IS the destination): `srFib[label] = { label, MPLS_IMPLICIT_NULL, outPort, 0 }`.
   - **Transit**: `srFib[label] = { label, label, outPort, 0 }` — same in/out label, forward via OSPF. SR shortest-path uses PHP, so only the penultimate hop modifies the label.
3. If no OSPF route to the destination: skip (node is isolated from that SID).

### Phase 4 — Compute SR Policies

For each SR-enabled router, for each `srPolicy`:

1. If `!policy.segmentsResolved` (dirty flag): call `ResolveSrSegments(policy, nodes)` — resolves `segmentIps` → `segmentHops` + `labelStack` (innermost-first: `labelStack.back()` = first segment's label).
2. Verify reachability: for each consecutive pair in `segmentHops`, check that an OSPF route exists. If any segment is unreachable: `isActive = false`, `statusMsg = "Segment N unreachable"`.
3. If all segments reachable:
   - Build `activePath` as the concatenated OSPF path through each segment in order.
   - Set `isActive = true`, `statusMsg = "Active"`.

### `ResolveSrSegments(policy, nodes)`

Called only when the user edits `segmentIps` or when `segmentsResolved == false`. Not called every tick.

For each IP in `segmentIps`:
1. Find the node whose `routerId` or any `portIp` matches the IP.
2. If found and `srEnabled` and `nodeSid > 0`: append `nodeId` to `segmentHops`; append `SRGB_BASE + nodeSid` to `labelStack`.
3. If not found or SID not configured: append a placeholder and set a warning in `statusMsg`.
4. On success: set `segmentsResolved = true`.

## `SimulationEngine.cpp` Changes

Priority order: **SR → RSVP-TE → LDP**. Add SR check before the existing RSVP-TE check.

### Head-End (unlabeled packet, `currentLabel == 0`)

```cpp
if (cur->srEnabled) {
    for (auto& p : cur->srPolicies) {
        if (p.isActive && IpMatchesPolicy(dstIp, p.destIp)) {
            // push full label stack (stored innermost-first; back() = outermost = first to process)
            anim.srLabelStack = p.labelStack;
            anim.currentLabel = anim.srLabelStack.back();
            anim.srSegmentIdx = 0;
            anim.srPolicyId   = p.id;
            hop.labelOp      = LABEL_PUSH;
            hop.outLabel     = anim.currentLabel;
            hop.policyId     = p.id;
            hop.segmentIndex = 0;
            goto done_mpls;
        }
    }
}
// fall through to RSVP-TE check, then LDP
```

### Transit (labeled packet, `currentLabel != 0`)

```cpp
if (cur->srEnabled && currentLabel != 0) {
    auto it = cur->srFib.find(currentLabel);
    if (it != cur->srFib.end()) {
        const SrLfibEntry& se = it->second;
        hop.policyId     = anim.srPolicyId;   // carried from head-end push; se.policyId is always 0
        hop.segmentIndex = anim.srSegmentIdx;
        if (se.outLabel == MPLS_IMPLICIT_NULL) {
            hop.labelOp    = LABEL_POP;
            hop.outLabel   = 0;
            // pop the consumed label and expose the next
            if (!anim.srLabelStack.empty()) anim.srLabelStack.pop_back();
            anim.currentLabel = anim.srLabelStack.empty() ? 0 : anim.srLabelStack.back();
            anim.srSegmentIdx++;
        } else {
            hop.labelOp    = LABEL_SWAP;
            hop.inLabel    = currentLabel;
            hop.outLabel   = se.outLabel;   // same value for SR shortest-path transit
        }
        hop.outPort = se.outPort;
        goto done_mpls;
    }
}
// fall through to teLfib, then lfib
```

The `done_mpls:` label already exists from RSVP-TE — no new label needed.

## Canvas Rendering (`NetworkCanvas.cpp`)

### `DrawSrPolicyOverlays(nodes, cables, camera)`

Called after `DrawTeTunnelOverlays`. Color palette (distinct from TE amber/cyan/magenta palette):

```
electric-blue (#3b82f6), violet (#8b5cf6), emerald (#10b981),
orange (#f97316), pink (#ec4899), yellow (#eab308)
```

For each SR-enabled node → each active `srPolicy`:

1. For each consecutive node pair in `activePath`, find their shared cable.
2. Draw a **dashed** bezier overlay using the same control points as the base cable:
   - `stroke-width = 5`, `dash = 8px on, 5px off`, offset ±3px per policy index
   - Alpha-blended at ~0.8, same glow approach as TE overlays
3. At the head-end router position, draw the **label stack badge**:
   ```
   Policy-N push
   [ 1002 | 1004 ]
   ```
   Positioned above the router node, font size 10, background `#0f172a`, border in policy color.
4. Mid-cable badge: `"Policy-N"` only (no BW — SR has no bandwidth constraint).

## `ConfigPanel` Changes (`ConfigPanel.h/.cpp`)

### New `TAB_SR` entry

Appended after `TAB_TE` in `PanelTab` enum. `PnlTabCount` for ROUTER goes from 12 to 13.

### New `PanelState` fields

```cpp
// SR tab
bool        srNodeSidEditing = false;
std::string srNodeSidBuf;              // digit buffer for Node SID
int         srExpandedIdx    = -1;     // policy index currently expanded (-1 = none)
int         srActiveField    = -1;     // 0 = dest, 1 = segs, -1 = none
std::string srDestBuf;
std::string srSegsBuf;
```

### TAB_SR layout (rendered by `DrawSrTab` in `NetworkCanvas.cpp`)

```
[sr-mpls]  [ON / OFF]

Node SID:  [__]   → label: 1NNN     SRGB: 1000–1999 (global)

Adj SIDs (auto):
  Gi0/0  adj: 5NNN
  Gi0/1  adj: 5NNN
  ...

SR Policies:
  ▶ Policy-1  → 10.0.5.1     [ACTIVE]
  ▼ Policy-2  → 10.0.9.1     [DOWN]
       Dest:  [_________]
       Segs:  [_________________]
              → SID:2·label:1002  SID:5·label:1005  ← live, updates on each keystroke
       [✕ Del]
[+ Add Policy]
```

`ResolveSrSegments` is called on each keystroke in the Segs field to update the live preview. `segmentsResolved = false` is set on each keystroke so `UpdateSr` re-resolves on the next tick.

## Build Integration

Add to `Makefile`:

```makefile
SRCS += src/SrEngine.cpp
```

`main.cpp` calls `UpdateSr(nodes, cables)` in the simulation update loop, after `UpdateRsvp`.

## Out of Scope

- Per-router SRGB configuration (fixed global default 1000–1999 is sufficient for the simulator)
- Flex-Algo (algorithm-based path computation)
- TI-LFA (topology-independent loop-free alternate) fast reroute
- SR-TE with bandwidth constraints (that's RSVP-TE's job)
- SRv6 (separate PKT-3 spec)
- IS-IS SR extensions (IS-IS not yet implemented)
