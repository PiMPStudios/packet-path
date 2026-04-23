# Claude Project Context - Packet Path Network Simulator

## Current Project
Packet Path — Educational network simulator built in C++ with raylib.

## Core Objective
Build an interactive, visual network simulator where users drag-and-drop network devices onto a canvas, configure protocols via a side panel, and watch color-coded packets animate through the network in real time.

## Key Resources
- Main roadmap: `docs/packet-path-game-roadmap.md`
- RFCs & specs: `docs/RFCs/` (27 RFCs downloaded — IPv4 through SRv6)
- MemPalace wing: `packet-path` (rooms: overview, rfcs, architecture, skills)

## Project Structure
```
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
├── docs/
│   ├── packet-path-game-roadmap.md
│   └── RFCs/
├── Makefile
└── README.md
```

## Development Order
1. Basic raylib window + draggable nodes
2. Cable system between nodes
3. Device classes + simple IP config
4. Packet simulator with visual movement
5. Routing table + basic forwarding logic
6. MPLS label swapping (first advanced feature)

**Start with a single `main.cpp` for the first 2–3 weeks. Move to multi-file structure once basic dragging + packets are working. Keep all game logic in plain C++ classes — no raylib calls inside Device/Packet classes.**

## Active Skills
### Domain
- `network-sim` — Protocol simulation engine (OSPF, BGP, MPLS, forwarding plane, state machines)
- `raylib-development` — C++/raylib rendering, input, camera, animation, audio
- `educational-game-design` — Pedagogy, curriculum map, feedback systems, tutorial design

### Game Design
- `game-feel` — Packet animation polish, micro-interactions, visual juice
- `level-designer` — Scenario design, win conditions
- `game-gdd` — Full GDD authoring
- `sound-design` — SFX, music

### Engineering
- `software-architect` — C++ class hierarchy, system design
- `code-writer` — C++ implementation
- `code-reviewer` — Code quality review
- `andrej-karpathy-skills:karpathy-guidelines` — LLM coding mistake prevention

### Publishing
- `steam-publishing` — Steam store, wishlist, launch strategy

### Workflow
- `superpowers:brainstorming`
- `superpowers:writing-plans`
- `superpowers:executing-plans`
- `superpowers:subagent-driven-development`
- `superpowers:test-driven-development`
- `superpowers:systematic-debugging`
- `superpowers:verification-before-completion`
- `superpowers:finishing-a-development-branch`
- `superpowers:requesting-code-review`
- `superpowers:receiving-code-review`

## Memory Instructions
When "load Packet Path context" is requested:
- Pull relevant information from the `packet-path` MemPalace wing only
- Focus on current task and recent decisions
- Keep technical details (protocol behavior, architecture choices, implementation notes)
- Ignore unrelated projects

## Decision Log Rule
When a design or implementation decision is made, create a short entry with:
- What we chose
- Why we chose it
- Any trade-offs considered

Say "Context saved" after updating MemPalace.
