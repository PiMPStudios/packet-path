# Packet Path Decision Log

## 2026-07-12 — TE/SR forwarding and scene boundaries

- **What we chose:** SR-MPLS and RSVP-TE resolve their label-selected egress before ordinary IP next-hop resolution. Labeled transit uses the LFIB without requiring an IP route to the payload destination.
- **Why:** A configured policy must control the simulated packet path; changing only trace annotations teaches incorrect data-plane behavior.
- **Trade-offs:** LFIB entries currently identify an egress port rather than an explicit next-hop LSR, so shared multi-access MPLS segments still use the first reachable Layer-3 neighbor on that port.

- **What we chose:** RSVP bandwidth is recalculated sequentially, temporarily releasing each tunnel's own reservation before testing its replacement path.
- **Why:** This prevents a stable tunnel from rejecting itself while still preventing later tunnels in the same tick from overbooking the link.
- **Trade-offs:** Parallel cables between the same pair of nodes still share one node-pair bandwidth key.

- **What we chose:** Scene files persist TE/SR configuration but rebuild derived LFIB, reservation, and active-policy state after load. Structurally invalid scene data is rejected as a whole.
- **Why:** Derived protocol state can become stale, while configuration must round-trip safely and malformed port/device references must never reach fixed-size arrays.
- **Trade-offs:** A partially malformed scene cannot be loaded selectively; the user receives a load failure instead.

## 2026-07-12 — Stabilization boundaries and portable builds

- **What we chose:** Levels are discovered from validated `level_*.json` metadata and sorted numerically instead of being capped at 16.
- **Why:** Level 17 and later should require only a data file, not synchronized code arrays and progression constants.
- **Trade-offs:** Invalid level files are omitted from the selector rather than shown as disabled cards.

- **What we chose:** Adjacency SID policies use `adj:<router-ip>:<port>` segments. The simulator's automatically allocated adjacency labels are globally unique; transit routers carry the label to its owner, which pops it and forces the selected interface.
- **Why:** This makes the existing adjacency SID state operational while preserving the single-label policy model used by the forwarding engine.
- **Trade-offs:** Globally unique adjacency labels simplify real local-significance behavior and should be revisited if per-router label spaces are modeled.

- **What we chose:** Level discovery, scene serialization, RSVP replay, TE/SR input routing, and protocol overlays are separate units; protocol algorithms remain plain C++ engine code.
- **Why:** These are independently testable responsibilities and were the safest seams to extract from the large game-loop and canvas translation units.
- **Trade-offs:** `main.cpp` and `NetworkCanvas.cpp` are smaller but still substantial; the remaining generic canvas/panel input can be extracted incrementally.

- **What we chose:** CMake is the portable build and packaging source of truth, with a three-OS CI matrix. The Makefile remains a fast local Unix path.
- **Why:** Windows/Linux verification and packaging need reproducible dependency and resource handling.
- **Trade-offs:** CI fetches raylib 5.5, increasing cold-build time but avoiding runner-specific package drift.
