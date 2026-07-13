# Packet Path Status Notes

Updated: 2026-07-13

The original six-phase MVP plan is complete. Packet Path currently ships 21 JSON-driven scenarios covering IP forwarding, VLANs, OSPF, BGP, ACL/NAT, VXLAN/EVPN, MPLS/LDP, RSVP-TE, SR-MPLS, SRv6, SD-WAN, and failure troubleshooting. The Build Your Own ISP campaign groups them into five persistent chapters, while the level selector still discovers validated `level_*.json` files for unrestricted Freeplay.

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

- The automated simulator suite covers IP parsing/LPM, packet forwarding, VLANs, OSPF/BGP failure behavior, LDP, RSVP, SR-MPLS, SRv6, SD-WAN, complete multi-failure recovery, campaign unlock/persistence behavior, scene validation, save/load round trips, dynamic level discovery, and audio fallback.
- CMake builds, tests, installs resources, and creates ZIP packages. GitHub Actions is configured for macOS, Ubuntu, and Windows.
- `main.cpp` has dedicated units for level discovery, scene serialization, RSVP replay, and TE/SR panel input. TE/SR world overlays have moved out of `NetworkCanvas.cpp`.
- Audio is optional: a missing device disables sound calls without stopping the simulator.

## Next product work

1. Playtest the full campaign for chapter pacing, briefing clarity, and reset/continue discoverability.
2. Decide whether Level 22 should introduce IS-IS or deepen the campaign with chapter-specific narrative events.
3. Continue extracting generic input and remaining protocol panel rendering from `main.cpp` and `NetworkCanvas.cpp`.
