# Packet Path Decision Log

## 2026-07-13 — Level 21 multi-failure troubleshooting contract

- **What we chose:** Level objectives may opt into `requiresAllFaultsCleared`, which blocks completion while any device remains crashed or any link remains broken; older reachability-only failure levels retain their existing behavior.
- **Why:** Level 21 must prove a complete fault sweep, while Level 15 intentionally teaches that restoring either redundant path can recover service.
- **Trade-offs:** The requirement checks all visible scene fault flags rather than naming individual required repairs, which is simple and reusable but does not model hidden or causally dependent faults.

- **What we chose:** Level 21 separates three injected faults across an upper-core router, a lower-core link, and branch access, then validates bidirectional HQ and branch service against a shared server after OSPF reconvergence.
- **Why:** Distinct fault domains and two service paths prevent one lucky repair from masquerading as a complete diagnosis and make the topology itself communicate the troubleshooting sequence.
- **Trade-offs:** Troubleshoot Mode still exposes failure overlays for accessibility; the capstone tests isolation and recovery discipline rather than requiring command-line inference from telemetry alone.

## 2026-07-13 — Level 20 SD-WAN teaching contract

- **What we chose:** SD-WAN policies match an exact application destination, evaluate live per-egress latency, jitter, and loss against configured thresholds, prefer the primary when compliant, and otherwise select a compliant backup.
- **Why:** The lesson must distinguish application-aware path selection from static-route preference and prove that measured quality can override the nominal WAN path.
- **Trade-offs:** Probes are deterministic per-port measurements rather than sampled time-series, and the first slice supports preferred/backup paths rather than centralized controllers, overlays, or multi-application policy classes.

- **What we chose:** Level 20 preconfigures a reachable primary static route, two visibly separate ISP paths, measured WAN quality, and the primary/backup port roles; the player supplies the voice destination and the 80 ms / 20 ms / 1 percent SLA.
- **Why:** This isolates one new idea—SLA-driven failover—while forwarding traces and green/red path overlays make the policy outcome immediately legible.
- **Trade-offs:** The campaign objective requires exact threshold values to keep the teaching contract deterministic; sandbox SD-WAN policies may use any thresholds.

## 2026-07-12 — Level 19 SRv6 teaching contract

- **What we chose:** The first SRv6 slice encapsulates the simulator's existing IPv4 payload over an IPv4/OSPF underlay, maps configured 128-bit IPv6 SIDs to SRv6-enabled routers, and exposes the active SID and SRH Segments Left at each steered hop.
- **Why:** This isolates the SRv6 data-plane concept from a simultaneous native-IPv6 routing lesson while still making the configured segment list control real packet forwarding.
- **Trade-offs:** Native IPv6 forwarding, OSPFv3, SRv6 endpoint behaviors such as End.DX4, TLVs, HMAC, and reduced SRH are deferred.

- **What we chose:** Level 19 reuses Level 18's short upper path and longer lower path, with preconfigured unique router SIDs and a player-created three-SID policy through RTR-2, RTR-4, and RTR-5.
- **Why:** Reusing the topology makes the contrast explicit: SR-MPLS steers with a label stack, while SRv6 steers with an IPv6 destination and SRH state.
- **Trade-offs:** The objective requires the exact human-readable SID order even though RFC 8754 encodes the Segment List in reverse order on the wire.

## 2026-07-12 — Level 18 SR-MPLS teaching contract

- **What we chose:** SR-aware win conditions require an active policy on a named head-end, an optional policy ID, an exact ordered segment list, a required path waypoint, and proof that forwarding used the policy.
- **Why:** Ordinary OSPF reachability or a Node-SID-only policy must not complete a lesson about adjacency SID link steering.
- **Trade-offs:** Exact segment matching keeps campaign objectives deterministic; sandbox policies remain unrestricted.

- **What we chose:** Level 18 preconfigures OSPF, SR-MPLS, and unique Node SIDs, then asks the player to build one policy containing a Node SID, an adjacency SID, and a final Node SID.
- **Why:** The lesson focuses on the semantic difference between reaching a node along SPF and forcing a specific interface, without repeating protocol-enablement setup.
- **Trade-offs:** The adjacency segment uses the simulator's `adj:<router-ip>:<port>` syntax and globally unique adjacency labels rather than modeling per-router label significance.

## 2026-07-12 — Level 17 RSVP-TE teaching contract

- **What we chose:** RSVP CSPF reads the head-end router's live OSPF area LSDBs, uses advertised adjacency costs, and then prunes links that cannot satisfy the requested reservation.
- **Why:** A traffic-engineering lesson should build on the control-plane topology rather than treating every physical cable and device as a valid MPLS path.
- **Trade-offs:** Inter-area TE remains limited to topology visible in the head-end's area LSDBs, and parallel cables still share the existing node-pair bandwidth key.

- **What we chose:** Level win conditions can require an UP tunnel on a named head-end, a tunnel ID, minimum bandwidth, CSPF or explicit mode, an active-path waypoint, and proof that forwarding actually used the tunnel.
- **Why:** Reachability alone cannot prove that the player learned or configured RSVP-TE.
- **Trade-offs:** The schema intentionally validates the lesson's observable outcome rather than every RSVP signaling detail.

- **What we chose:** Level 17 teaches one new idea: a 400 Mbps CSPF tunnel must avoid a shorter 200 Mbps path and use the longer feasible route; PATH/RESV replay is the visual confirmation.
- **Why:** Bandwidth-constrained path selection is the clearest first RSVP-TE lesson and avoids combining explicit routing, failure recovery, and preemption in one scenario.
- **Trade-offs:** Explicit-path configuration and RSVP failure recovery remain available in the sandbox but are deferred as campaign objectives.

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
