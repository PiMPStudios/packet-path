# Packet Path Status Notes

Updated: 2026-07-12

The original six-phase MVP plan is complete. Packet Path currently ships 20 JSON-driven scenarios covering IP forwarding, VLANs, OSPF, BGP, ACL/NAT, VXLAN/EVPN, MPLS/LDP, RSVP-TE, SR-MPLS, SRv6, SD-WAN, and failure troubleshooting. The level selector discovers validated `level_*.json` files, so later scenarios require no code catalog update.

## Current protocol status

| Area | Status | Notes |
| --- | --- | --- |
| IPv4 / ARP / static routing | Implemented and tested | Includes IP validation and longest-prefix matching |
| VLAN / 802.1Q | Implemented and tested | Access/trunk forwarding and mismatch rejection |
| OSPF | Implemented and tested | Single/multi-area, SPF, adjacency and broken-link withdrawal |
| BGP | Implemented and tested | eBGP, iBGP, route reflection, link-failure withdrawal |
| MPLS / LDP | Implemented and tested | Push, transit labels, and implicit-null/PHP |
| RSVP-TE | Implemented and tested | OSPF-based CSPF, explicit paths, bandwidth reservations, setup replay, event logs, TE-aware level objectives |
| SR-MPLS | Implemented and tested | Node SIDs, duplicate detection, adjacency SID steering, policy forwarding, SR-aware level objectives |
| SRv6 | Implemented and tested | IPv6 SID validation, duplicate detection, SRH segment steering, trace state, SRv6-aware objectives |
| SD-WAN | Implemented and tested | Latency/jitter/loss probes, preferred/backup SLA selection, policy forwarding, objective validation |
| IS-IS | Not implemented | Longer-term roadmap |

## Engineering status

- The automated simulator suite covers IP parsing/LPM, packet forwarding, VLANs, OSPF/BGP failure behavior, LDP, RSVP, SR-MPLS, SRv6, scene validation, save/load round trips, dynamic level discovery, and audio fallback.
- CMake builds, tests, installs resources, and creates ZIP packages. GitHub Actions is configured for macOS, Ubuntu, and Windows.
- `main.cpp` has dedicated units for level discovery, scene serialization, RSVP replay, and TE/SR panel input. TE/SR world overlays have moved out of `NetworkCanvas.cpp`.
- Audio is optional: a missing device disables sound calls without stopping the simulator.

## Next product work

1. Playtest Levels 17–19 for briefing clarity and panel-input pacing.
2. Define the Level 21 complex multi-failure troubleshooting contract.
3. Continue extracting generic input and remaining protocol panel rendering from `main.cpp` and `NetworkCanvas.cpp`.
