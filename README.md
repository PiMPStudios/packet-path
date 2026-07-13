# Packet Path

A visual, interactive network simulator that teaches real networking concepts — from basic IP forwarding through OSPF, BGP, MPLS, VLANs, VXLANs, ACLs, and beyond. Built in C++17 with [raylib](https://www.raylib.com/).

> **Under active development.** Features are being added rapidly and things may break between commits. Not recommended for production use — but great for poking around.

---

![MPLS Label Switching — PC-A to PC-B across a 4-router MPLS core](docs/screenshots/gameplay-mpls.png)

![Multi-Area OSPF — ABR bridging Area 0 and Area 1, OSPF neighbors at Full state](docs/screenshots/gameplay-ospf-multiarea.png)

![VXLAN Overlay — leaf-spine fabric with BGP EVPN on Leaf-2](docs/screenshots/gameplay-vxlan.png)

---

## What It Is

Packet Path is a scenario-based learning game where you configure real network topologies and watch packets animate through them in real time. Every routing decision, label swap, ARP resolution, and ACL hit is shown visually and logged — so you can see exactly why a packet succeeded or was dropped.

It is not a toy model. The simulation engine runs real protocol behavior: OSPF SPF, BGP path selection, MPLS label distribution via LDP, 802.1Q VLAN tagging, VXLAN + BGP EVPN, stateful ACL evaluation, and NAT. The goal is to build intuition for how these protocols actually work, not just what they're called.

---

## Levels

| # | Title | Concepts |
| --- | --- | --- |
| 1 | Basic Ping | IP forwarding, ARP, static routes |
| 2 | Multi-Hop Routing | Default gateway, multi-hop paths |
| 3 | VLAN Trunking | 802.1Q, trunk vs access ports, VLAN isolation |
| 4 | Router-on-a-Stick | Inter-VLAN routing, subinterfaces |
| 5 | OSPF: Single Area | OSPF hello, adjacency FSM, SPF, RIB |
| 6 | Multi-Area OSPF: ABR | Area 0 backbone, ABRs, inter-area routes |
| 7 | eBGP Peering | AS numbers, eBGP sessions, prefix advertisement |
| 8 | iBGP Full Mesh | iBGP sessions, next-hop-self, OSPF underlay |
| 9 | BGP Route Reflection | RR topology, client/non-client, cluster IDs |
| 10 | ACL Firewall | Access control lists, permit/deny rules |
| 11 | NAT: Internet Access | PAT/overload NAT, address translation |
| 12 | VXLAN Overlay | VXLAN encapsulation, VNI, overlay vs underlay |
| 13 | MPLS Label Switching | LDP, label push/swap/pop, LSP |
| 14 | Link Down | Failure detection, OSPF reconvergence |
| 15 | Dual Failure | Multi-failure troubleshooting |
| 16 | VLAN Gateway Down | Failure isolation, partial connectivity |
| 17 | Bandwidth Detour | RSVP-TE, OSPF-based CSPF, bandwidth reservations, PATH/RESV replay |
| 18 | SID Steering | SR-MPLS, Node SIDs, adjacency SIDs, explicit link steering |
| 19 | Segments Left | SRv6, IPv6 SIDs, Segment Routing Header, segment-list steering |
| 20 | SLA Failover | SD-WAN, live path probes, application SLAs, dual-WAN failover |
| 21 | Fault Domain Sweep | Multi-failure isolation, OSPF reconvergence, complete service restoration |

Level 17 presents two OSPF paths to the same tail-end: a short 200 Mbps route and a longer 1 Gbps route. The player creates a 400 Mbps CSPF tunnel, watches RSVP PATH/RESV signaling, and verifies that labeled traffic follows the only bandwidth-feasible path.

Level 18 contrasts SR segment types. A Node SID carries traffic to an OSPF-selected waypoint, an adjacency SID forces one exact egress interface, and a final Node SID completes the label-stack journey to the tail-end.

Level 19 carries the existing IPv4 payload inside an SRv6 policy. The player enters an ordered IPv6 SID list, watches the active destination advance through the list, and sees the SRH Segments Left value count down while the packet avoids the ordinary OSPF path.

Level 20 introduces application-aware WAN selection. A nominal primary path violates latency, jitter, and loss thresholds, so the player builds an SLA policy that selects the compliant backup path and verifies the actual forwarding trace.

Level 21 is the troubleshooting capstone. Three independent faults affect the upper core, lower backup path, and branch access; the player must isolate and clear every fault, wait for OSPF reconvergence, and prove bidirectional recovery for both HQ and branch services.

Campaign mode connects all 21 scenarios into **Build Your Own ISP**, a five-chapter progression from first-customer LANs through backbone routing, managed services, provider-core engineering, and production operations. Completion unlocks the next mission, saves the best star rating per level, and resumes at the first incomplete mission; Freeplay and Sandbox remain unrestricted.

---

## Features

### Simulation

- Animated packet movement along bezier cable curves
- ARP resolution with request/reply/cache behavior
- Hop-by-hop forwarding with per-device RIB/FIB
- OSPF: hello timers, adjacency state machine (Down → Init → 2-Way → Full), LSDB, Dijkstra SPF
- BGP: eBGP/iBGP sessions, route reflection, path selection
- MPLS/LDP: label distribution, push/swap/pop at each LSR
- RSVP-TE: bandwidth reservations, CSPF/explicit paths, rerouting, and setup replay
- SR-MPLS: Node and adjacency SIDs, label-stack steering, and duplicate-SID validation
- SRv6: IPv6 SID validation, SRH segment-list steering, duplicate-SID detection, and Segments Left traces
- SD-WAN: per-path latency/jitter/loss probes, destination SLA policies, preferred/backup selection, and forwarding overrides
- 802.1Q: trunk/access ports, VLAN tag handling, frame forwarding
- VXLAN + BGP EVPN: overlay encapsulation, MAC/IP route advertisement
- ACL: ordered permit/deny rules applied per interface/direction
- NAT: PAT/overload translation with connection table

### UI

- Infinite pan/zoom canvas (Camera2D)
- Drag-and-drop devices (PC, Router, Switch, PE)
- Click-to-wire bezier cable connections
- Side config panel: IP addresses, routing, OSPF, BGP, ACL, NAT, VLAN, TE, SR-MPLS, and SRv6 per device
- Floating, draggable mission briefing card (collapsible, auto-sizing)
- Packet trace modal: step-by-step hop decisions with MPLS, SRH, ACL, and NAT annotations
- Log console with mouse-wheel scroll and auto-height
- Star rating (1–3) based on solution efficiency
- Persistent Build Your Own ISP campaign with chapter progression, mission locks, best-star replay, and Continue
- Slow-motion replay
- Save/load topology
- Sandbox mode (free play, no objectives)
- Failure injection (link cuts, device crashes)
- Sound effects on key events

---

## Building

### Requirements

- CMake 3.20+ and a C++17 compiler
- [raylib 5.5+](https://github.com/raysan5/raylib), or internet access for CMake to fetch it

### Install raylib (Homebrew)

```bash
brew install raylib
```

### Cross-platform build and run

```bash
git clone https://github.com/PiMPStudios/packet-path.git
cd packet-path
cmake -S . -B build -DPACKET_PATH_FETCH_RAYLIB=ON -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

Run `build/packet-path` on macOS/Linux or `build/Release/packet-path.exe` on Windows. The existing `make` workflow remains available for local Unix development. Create a redistributable ZIP with:

```bash
cmake --build build --config Release --target package
```

GitHub Actions builds, tests, and packages the project on macOS, Ubuntu, and Windows. The simulator test suite can also be run locally with `make test`.

---

## Controls

| Input | Action |
| --- | --- |
| `P` / `R` / `S` | Spawn PC / Router / Switch |
| Left-click | Select device, drag nodes and cables |
| Right-click | Context menu (send packet, delete, rename) |
| Middle-mouse drag | Pan canvas |
| Scroll wheel | Zoom canvas (canvas area) / scroll log (log area) |
| `B` | Toggle mission briefing card |
| `H` | Toggle help overlay |
| `M` | Return to menu |
| `ESC` | Close modal / return to menu |

---

## Architecture

```text
src/
├── main.cpp              — game loop, input, state machine
├── ProtocolPanelController — TE/SR input routing and form state
├── RsvpReplay            — RSVP PATH/RESV replay state machine
├── SimulationEngine      — packet forwarding, protocol dispatch
├── OspfEngine            — OSPF hello/LSDB/SPF
├── BgpEngine             — eBGP/iBGP/route reflection
├── LdpEngine             — MPLS label distribution
├── EvpnEngine            — VXLAN/BGP EVPN
├── AclEngine             — ACL evaluation
├── NetworkCanvas         — canvas, panels, and log console
├── ProtocolOverlays      — TE/SR world-space rendering
├── ConfigPanel           — device configuration UI
├── ProtocolPanels        — TE/SR protocol panel rendering
├── GameUI                — briefing card, overlays, HUD
├── TraceModal            — packet trace viewer
├── LevelCatalog          — discovered level metadata and progression
├── Level                 — level application and win conditions
├── SceneSerializer       — validated JSON load/save
├── Device / Cable / Packet — core data structures (no raylib)
├── Font / Layout         — shared rendering helpers
└── SoundEngine           — audio events
```

All protocol logic lives in `*Engine` classes with no raylib dependency. Rendering is isolated to `*Canvas`, `*Panel`, `*UI`, and `*Modal` files. Data structures (`Device`, `Cable`, `Packet`) are plain C++ structs.

---

## Protocol Coverage

The simulator is built against published RFCs. Key references in `docs/RFCs/`:

| RFC | Protocol |
| --- | --- |
| RFC 791 | IPv4 |
| RFC 826 | ARP |
| RFC 2328 / 5340 | OSPFv2 / OSPFv3 |
| RFC 4271 / 4760 | BGP-4 / Multiprotocol BGP |
| RFC 3031 / 3032 | MPLS Architecture / Label Stack |
| RFC 5036 | LDP |
| RFC 3209 / 4090 / 8426 | RSVP-TE, fast reroute, and MPLS traffic-engineering guidance |
| RFC 7348 | VXLAN |
| RFC 7432 / 8365 / 9135 | BGP EVPN |
| RFC 8402 / 8660 / 8754 | Segment Routing / SRv6 |
| IEEE 802.1Q | VLANs |

---

## Roadmap

The core engine, 21 levels, and all UI features listed above are implemented and working on macOS. Active development continues.

### Protocol engine

- [x] IPv4 forwarding, ARP, static routes
- [x] OSPF (single-area and multi-area, SPF, adjacency FSM)
- [x] BGP (eBGP, iBGP, route reflection)
- [x] MPLS / LDP (label push/swap/pop, LSP)
- [x] 802.1Q VLANs and trunking
- [x] VXLAN + BGP EVPN
- [x] ACLs and NAT
- [x] RSVP-TE — OSPF-based CSPF, explicit paths, bandwidth reservations, and setup replay
- [x] Segment Routing — SR-MPLS Node/adjacency SID allocation and label-stack steering
- [x] SRv6 — IPv4 payload encapsulation, IPv6 SID lists, SRH trace state, and policy steering
- [ ] IS-IS — L1/L2, TLVs, SPF
- [x] SD-WAN — policy-based preferred/backup selection with latency, jitter, and loss thresholds

### Levels & content

- [x] Levels 1–21 (beginner through advanced, see table above)
- [x] Level 17: RSVP-TE traffic engineering
- [x] Level 18: Segment Routing SR-MPLS
- [x] Level 19: SRv6 segment-list steering
- [x] Level 20: SD-WAN dual-WAN SLA policy
- [x] Level 21: Complex multi-failure troubleshooting
- [x] Campaign mode — "Build your own ISP" narrative arc with persistent unlocks and best-star progress

### Platform

- [x] macOS (primary dev platform, tested on 13+)
- [x] CMake and package targets for macOS, Windows, and Linux
- [x] GitHub Actions build/test/package matrix for all three platforms

---

## Contributors

**DaRealDaHoodie** — design, engineering, everything  
**Claude Sonnet 4.6** (Anthropic) — co-author, pair programmer, protocol consultant, and the one who kept the RFC citations honest

---

## License

Source available under the [MIT + Commons Clause](LICENSE) license — free to use, study, and modify for personal and educational purposes. Commercial use requires permission from PiMP Studios.
