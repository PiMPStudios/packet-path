# RSVP-TE Engine Design

**Date:** 2026-05-09
**Ticket:** PKT-1
**RFCs:** RFC 3209, RFC 4090

## Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Simulation model | Hybrid — instant state + Simulate Setup replay | Fast feedback + on-demand signaling story |
| Path selection | CSPF default, explicit path override | Teaches both; mirrors IOS-XR/Junos behavior |
| Bandwidth config | Per-port in ConfigPanel | Authentic (interface BW), consistent with existing UI |
| Canvas visualization | Colored overlay on active cable segments | Shows real hop-by-hop path; reuses bezier draw code |
| ConfigPanel layout | Inline expand/collapse tunnel list | No tab switching; status always visible |

## New Files

- `src/RsvpEngine.h` — `UpdateRsvp` declaration only (includes `Device.h`)
- `src/RsvpEngine.cpp` — CSPF, tunnel state computation, label allocation

## Data Structures

### `TeLfibEntry` (new, in `Device.h` alongside `LdpBinding`)

```cpp
struct TeLfibEntry {
    uint32_t inLabel  = 0;
    uint32_t outLabel = 0;   // 0 = pop (PHP)
    int      outPort  = -1;
    int      tunnelId = 0;
};
```

Keyed by `inLabel`. Kept separate from the LDP `lfib` map so the existing forwarding path is untouched.

### `TeTunnel` (new, in `Device.h` alongside `LdpBinding`)

```cpp
struct TeTunnel {
    int         id           = 0;        // 1–255, unique per router
    std::string destIp;                  // tail-end router-id or loopback IP

    uint32_t    bandwidth    = 0;        // required Mbps

    bool        useExplicit  = false;
    std::vector<std::string> explicitHopIps;  // human-readable IPs (UI storage)
    std::vector<int>         explicitHops;    // resolved node IDs (engine use)

    // computed each tick by UpdateRsvp
    bool        isUp         = false;
    std::vector<int> activePath;         // node IDs head→tail
    uint32_t    headLabel    = 0;        // label imposed at head-end ingress
    std::string statusMsg;               // "Up", "No CSPF path", "BW insufficient"
};
```

### `DeviceNode` additions (in `Device.h`)

```cpp
bool        rsvpEnabled  = false;
uint32_t    portBw[PORTS_PER_NODE] = {1000, 1000, 1000, 1000};  // Mbps per port
uint32_t    nextTeLabel  = 16000;   // label allocator; increments per tunnel
std::vector<TeTunnel>   teTunnels;
std::unordered_map<uint32_t, TeLfibEntry> teLfib;  // key = inLabel

// Transient failure buffer — tunnels re-signal from here before going Down
std::vector<TeTunnel>   pendingTunnels;
```

## `RsvpEngine.cpp` — `UpdateRsvp(nodes, cables)`

### Phase 1 — Available Bandwidth Map

For every cable, compute:

```
availableBw(cable) = min(nodeA.portBw[portA], nodeB.portBw[portB])
                   − sum of headLabel tunnel BW reservations on this cable
```

Build a `map<pair<int,int>, uint32_t>` keyed by `{minId, maxId}` node pair.

### Phase 2 — Tunnel Computation

For each `rsvpEnabled` router, for each tunnel in `teTunnels`:

**Explicit path:** Walk `explicitHops`. For each consecutive pair, look up available BW.
If any link has less than `tunnel.bandwidth` available → `isUp = false`, `statusMsg = "BW insufficient on explicit path"`.
Otherwise → assign labels, populate `teLfib`, set `isUp = true`.

**CSPF:** Run Dijkstra on the OSPF adjacency graph (reuse the SPF graph already built by `OspfEngine`), pruning edges where available BW < `tunnel.bandwidth`. If no path exists → check `pendingTunnels` for a previous path and hold it for one extra tick before marking Down (prevents flicker on transient failures). If path found → assign labels, write `teLfib`, set `isUp = true`.

### Label Allocation

At the top of each `UpdateRsvp` call, each router's `nextTeLabel` resets to 16000. Tunnels are processed in `teTunnels` order, so label assignment is deterministic each tick:

```
headLabel = node.nextTeLabel++
```

Transit nodes: `inLabel = upstream's outLabel`, `outLabel = node.nextTeLabel++`.
Egress (penultimate) node: `outLabel = MPLS_IMPLICIT_NULL` (PHP).

This matches the LDP pattern — full LFIB rebuilt from scratch each tick.

### `teLfib` Population

- **Head-end:** `teLfib[headLabel] = { headLabel, nextHopLabel, outPort, tunnelId }`
- **Transit:** `teLfib[inLabel] = { inLabel, outLabel, outPort, tunnelId }`
- **Egress:** `teLfib[inLabel] = { inLabel, MPLS_IMPLICIT_NULL, outPort, tunnelId }`

## `SimulationEngine.cpp` Changes

At the head-end router, before consulting the LDP `lfib`, check whether the destination IP matches any active `TeTunnel.destIp`. If a match exists:

1. Impose `tunnel.headLabel` as the MPLS label stack.
2. Set `HopDecision.tunnelId = tunnel.id` (new field, zero by default) so the trace modal can display "via Tunnel-1".

Transit hops consult `teLfib` before `lfib`; TE entries take precedence.

### `HopDecision` addition

```cpp
int tunnelId = 0;   // non-zero = this hop is inside a TE tunnel
```

## Canvas Rendering (`NetworkCanvas.cpp`)

After drawing all cables, for each `rsvpEnabled` node → each Up tunnel → its `activePath`:

1. For each consecutive node pair in `activePath`, find their shared cable.
2. Draw a bezier overlay using the same control points as the base cable, offset slightly perpendicular (±3px per tunnel index), alpha-blended at ~0.75.
3. Tunnel colors cycle through a fixed 6-color palette:
   `amber (#fbbf24), cyan (#22d3ee), magenta (#e879f9), lime (#a3e635), rose (#fb7185), sky (#38bdf8)`
4. A small label badge `"Tunnel-N • XMbps"` is drawn near the midpoint of each cable segment.

## ConfigPanel Changes (`ConfigPanel.cpp/.h`)

New section appended after the LDP toggle, visible only for ROUTER type nodes:

### RSVP-TE section

```
[rsvp-te] [ON/OFF toggle]

Per-port bandwidth (shown for ports with a configured IP):
  Gi0/0  [1000] Mbps
  Gi0/1  [ 100] Mbps
  ...

TE Tunnels:
  ▶ Tunnel-1  →10.0.5.1  200Mbps  CSPF  [UP]
  ▼ Tunnel-2  →10.0.9.1  500Mbps  CSPF  [DOWN]
     Dest:  [_________]
     BW:    [___] Mbps
     Mode:  [CSPF ▾]
     Hops:  [____________]  ← enabled only in Explicit mode
     [Simulate Setup]  [✕ Del]
  [+ Add Tunnel]
```

Fields stored in `TeTunnel`. Expand/collapse state is UI-only (not persisted). The `explicitHopIps` field stores the raw hop text; the engine resolves it to `explicitHops` (node IDs) at compute time.

## "Simulate Setup" Replay

When the user clicks **Simulate Setup** on an Up tunnel:

1. Spawn PATH packets (blue, `PacketType::RSVP_PATH`) one per hop along `activePath`, head→tail. Each carries metadata text: `"PATH Tunnel-N BW=XMbps"`.
2. Wait 0.8s after the final PATH packet reaches the tail-end.
3. Spawn RESV packets (green, `PacketType::RSVP_RESV`) one per hop, tail→head. Each carries: `"RESV Tunnel-N Label=XXXXX"`.

Reuses the existing `Packet` animation engine with no new state machine. The 0.8s hold is a simple timer in the replay sequence.

## Make-Before-Break (Simplified)

`UpdateRsvp` runs every simulation tick. When a link fails (via failure injection):

- Affected tunnels attempt CSPF on the new topology immediately.
- If a new path is found: `activePath` updates, `teLfib` rewrites, tunnel stays `isUp = true`. Traffic reroutes in the next forwarding tick — no Down state.
- If no path is found: tunnel is moved to `pendingTunnels` for one extra tick (prevents one-frame flicker), then marked `isUp = false`, `statusMsg = "No CSPF path after reroute"`.

## Build Integration

Add to `Makefile`:

```makefile
SRCS += src/RsvpEngine.cpp
```

`main.cpp` calls `UpdateRsvp(nodes, cables)` in the simulation update loop, after `UpdateOspf` and `UpdateLdp`.

## Out of Scope

- RSVP soft-state refresh (Hello keepalives) — state is recomputed each tick
- Multiple SRLG groups
- TE extensions to BGP (RSVP-TE is OSPF-topology-only in this implementation)
- IS-IS TE extensions (IS-IS not yet implemented)
