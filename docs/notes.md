# Packet Path Status Notes

Updated: 2026-07-12

The original six-phase MVP plan is complete. Packet Path currently ships 17 JSON-driven scenarios covering IP forwarding, VLANs, OSPF, BGP, ACL/NAT, VXLAN/EVPN, MPLS/LDP, RSVP-TE, and failure troubleshooting. The level selector discovers validated `level_*.json` files, so later scenarios require no code catalog update.

## Current protocol status

| Area | Status | Notes |
| --- | --- | --- |
| IPv4 / ARP / static routing | Implemented and tested | Includes IP validation and longest-prefix matching |
| VLAN / 802.1Q | Implemented and tested | Access/trunk forwarding and mismatch rejection |
| OSPF | Implemented and tested | Single/multi-area, SPF, adjacency and broken-link withdrawal |
| BGP | Implemented and tested | eBGP, iBGP, route reflection, link-failure withdrawal |
| MPLS / LDP | Implemented and tested | Push, transit labels, and implicit-null/PHP |
| RSVP-TE | Implemented and tested | OSPF-based CSPF, explicit paths, bandwidth reservations, setup replay, event logs, TE-aware level objectives |
| SR-MPLS | Implemented, in testing | Node SIDs, duplicate detection, adjacency SID steering, policy forwarding |
| SRv6 | Not implemented | Planned after SR-MPLS scenarios |
| IS-IS / SD-WAN | Not implemented | Longer-term roadmap |

## Engineering status

- The automated simulator suite covers IP parsing/LPM, packet forwarding, VLANs, OSPF/BGP failure behavior, LDP, RSVP, SR, scene validation, save/load round trips, dynamic level discovery, and audio fallback.
- CMake builds, tests, installs resources, and creates ZIP packages. GitHub Actions is configured for macOS, Ubuntu, and Windows.
- `main.cpp` has dedicated units for level discovery, scene serialization, RSVP replay, and TE/SR panel input. TE/SR world overlays have moved out of `NetworkCanvas.cpp`.
- Audio is optional: a missing device disables sound calls without stopping the simulator.

## Next product work

1. Add Level 18 for SR-MPLS, teaching Node SID versus adjacency SID steering.
2. Continue extracting generic input and remaining protocol panel rendering from `main.cpp` and `NetworkCanvas.cpp`.
3. Rerun the three-platform CI workflow after the next push to verify the Windows C++17 portability fix.
4. Add SRv6 only after the SR-MPLS scenarios and tests are stable.
