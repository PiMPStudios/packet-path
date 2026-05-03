# Networking Simulation Game - Learning Roadmap

**Goal**: Build an interactive game that visually teaches real networking concepts from basics to advanced enterprise/SP technologies using open standards.

## Core Technologies & Key RFCs

### 1. Foundational Networking

- IP Addressing & Subnetting — RFC 791 (IPv4), RFC 2460 (IPv6)
- ARP — RFC 826
- ICMP — RFC 792
- TCP/UDP — RFC 793, RFC 768

### 2. Routing Protocols

- OSPF — RFC 2328 (OSPFv2), RFC 5340 (OSPFv3)
- IS-IS — RFC 1195
- BGP — RFC 4271, RFC 4760, RFC 3107

### 3. Switching & VLANs

- VLANs & 802.1Q — IEEE 802.1Q
- VXLAN — RFC 7348
- BGP EVPN — RFC 7432, RFC 8365, RFC 9135

### 4. MPLS & Traffic Engineering

- MPLS Architecture — RFC 3031, RFC 3032
- LDP — RFC 5036
- RSVP-TE — RFC 3209, RFC 4090, RFC 8426
- Segment Routing — RFC 8402, RFC 8660, RFC 8754 (SRv6)

### 5. Advanced & Modern Topics

- SD-WAN — RFC 7426, BGP-based SD-WAN drafts
- DWDM / Optical — ITU-T G.694.1 (DWDM grid)
- Intent-Based Networking — RFC 9316

## Game Mechanics

**Core Loop**:

- Drag-and-drop devices (PC, Switch, Router, PE) and cables on a canvas
- Click a device to open a clean config panel (not full CLI — keep it readable)
- Click **"Apply Config"** or **"Test Network"** to start packet simulation
- Watch animated packets travel in real time — color-coded by type (green = success, red = dropped, yellow = ICMP error)
- Hover or click a packet to see exactly why it succeeded or failed

**Key Mechanics**:

- Visual packet flow with label swapping for MPLS
- Real-time routing table and LFIB viewer
- "Troubleshoot" mode that highlights broken paths
- Star rating system (1–3 stars) based on efficiency and cleanliness of your solution
- Scenario-based levels with clear win conditions ("Make all users reach the web server")

**UI Style**:

- Clean, modern node-graph look
- Side panel for device config
- Bottom console showing live logs ("ARP request sent", "Label 24000 swapped to 16001", etc.)
- Slow-motion replay button for complex scenarios

## Suggested Level Progression

### Beginner (CCNA level)

1. Basic IP addressing & ping
2. VLANs and trunking
3. Static routing and default gateway
4. OSPF single area

**Intermediate (Early CCNP)**
5. Multi-area OSPF + route summarization
6. BGP basics (eBGP peering and route advertisement)
7. ACLs and basic NAT
8. VLANs + VXLAN basics

**Advanced (Late CCNP / Expert)**
9. MPLS + LDP label distribution
10. RSVP-TE with traffic engineering and path constraints
11. Segment Routing (SR-MPLS and SRv6)
12. VXLAN + BGP EVPN fabric (leaf-spine data center)
13. SD-WAN policy-based routing and path selection
14. Complex troubleshooting (link failures, routing loops, blackholes)

### Bonus / Free-play

- Sandbox mode with no restrictions
- Failure injection (random link cuts, device crashes)
- "Build your own ISP" campaign mode

## Project Structure (C++ + raylib)

packet-path/
├── src/
│   ├── main.cpp
│   ├── Device.h / Device.cpp
│   ├── Router.h / Router.cpp
│   ├── Switch.h / Switch.cpp
│   ├── PC.h / PC.cpp
│   ├── Packet.h / Packet.cpp
│   ├── Cable.h / Cable.cpp
│   ├── NetworkCanvas.h / NetworkCanvas.cpp
│   ├── ConfigPanel.h / ConfigPanel.cpp
│   ├── SimulationEngine.h / SimulationEngine.cpp
│   └── UI.h / UI.cpp
├── include/
├── assets/
├── Makefile
└── README.md

**Recommended Approach**:

- Start with a single `main.cpp` file for the first 2-3 weeks
- Move to the multi-file structure once you have basic dragging + packets working
- Keep all game logic in plain C++ classes (no raylib calls inside Device/Packet classes)

## Next Steps & Development Order

1. Basic raylib window + draggable nodes
2. Cable system between nodes
3. Device classes + simple IP config
4. Packet simulator with visual movement
5. Routing table + basic forwarding logic
6. MPLS label swapping (first advanced feature)

---
