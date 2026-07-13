# Packet Path Status Notes

Updated: 2026-07-12

The original six-phase MVP plan is complete. Packet Path currently ships 16 JSON-driven scenarios covering IP forwarding, VLANs, OSPF, BGP, ACL/NAT, VXLAN/EVPN, MPLS/LDP, and failure troubleshooting. The level selector now discovers validated `level_*.json` files, so Level 17 no longer requires a code catalog update.

## Current protocol status

| Area | Status | Notes |
| --- | --- | --- |
| IPv4 / ARP / static routing | Implemented and tested | Includes IP validation and longest-prefix matching |
| VLAN / 802.1Q | Implemented and tested | Access/trunk forwarding and mismatch rejection |
| OSPF | Implemented and tested | Single/multi-area, SPF, adjacency and broken-link withdrawal |
| BGP | Implemented and tested | eBGP, iBGP, route reflection, link-failure withdrawal |
| MPLS / LDP | Implemented and tested | Push, transit labels, and implicit-null/PHP |
| RSVP-TE | Implemented, in testing | CSPF, explicit paths, bandwidth reservations, setup replay, event logs |
| SR-MPLS | Implemented, in testing | Node SIDs, duplicate detection, adjacency SID steering, policy forwarding |
| SRv6 | Not implemented | Planned after SR-MPLS scenarios |
| IS-IS / SD-WAN | Not implemented | Longer-term roadmap |

## Engineering status

- The automated simulator suite covers IP parsing/LPM, packet forwarding, VLANs, OSPF/BGP failure behavior, LDP, RSVP, SR, scene validation, save/load round trips, dynamic level discovery, and audio fallback.
- CMake builds, tests, installs resources, and creates ZIP packages. GitHub Actions is configured for macOS, Ubuntu, and Windows.
- `main.cpp` has dedicated units for level discovery, scene serialization, RSVP replay, and TE/SR panel input. TE/SR world overlays have moved out of `NetworkCanvas.cpp`.
- Audio is optional: a missing device disables sound calls without stopping the simulator.

## Next product work

1. Add Level 17 for RSVP-TE using the discovered level catalog.
2. Add an SR-MPLS scenario that teaches Node SID versus adjacency SID steering.
3. Continue extracting generic input and remaining protocol panel rendering from `main.cpp` and `NetworkCanvas.cpp`.
4. Run the new CI workflow in GitHub and address any platform-specific compiler or packaging failures.
5. Add SRv6 only after the SR-MPLS scenarios and tests are stable.
