# VXLAN + BGP EVPN Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add VXLAN tunnel simulation with BGP EVPN control-plane semantics — VTEPs configured on routers/leaves, automatic EVPN route distribution between co-VNI peers, packet animation with VNI badge, and a new teaching level.

**Architecture:** Six-layer addition on top of the existing stack. New `EvpnEngine.cpp` builds EVPN routes into `DeviceNode::evpnRoutes` each frame. `SimulationEngine` detects `ROUTE_EVPN` matches and splices in a two-pass VXLAN path (underlay → decap → local delivery). A new `TAB_VXLAN` panel tab exposes VNI/VTEP-IP config. Packet animation shows an orange VNI badge on all tunnel hops. Level 14 demonstrates the full overlay-over-underlay teaching scenario.

**Tech Stack:** C++17, raylib 5.5, nlohmann/json, existing BgpEngine/OspfEngine frame-loop pattern.

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `src/Device.h` | Modify | `ROUTE_EVPN` enum value, `vni` on `RouteEntry`, `vxlanVni` on `HopDecision`, VXLAN fields on `DeviceNode`, `TAB_VXLAN` in `PanelTab`, VXLAN buffers in `PanelState`, `currentVni` in `PacketAnim` |
| `src/Device.cpp` | Modify | Include `evpnRoutes` in `GetRoutingTable()` |
| `src/EvpnEngine.h` | Create | `BuildEvpnRoutes()` declaration |
| `src/EvpnEngine.cpp` | Create | Distributes overlay subnets as `ROUTE_EVPN` entries between co-VNI VTEP peers |
| `src/SimulationEngine.cpp` | Modify | VXLAN two-pass forwarding: detect `ROUTE_EVPN`, run underlay forward to VTEP, splice marked hops, run local delivery |
| `src/NetworkCanvas.h` | Modify | `DrawVxlanTab()` declaration |
| `src/NetworkCanvas.cpp` | Modify | `DrawVxlanTab()` implementation, `DrawPacketAnim` VNI badge, `DrawPanel` tab bar + dispatch |
| `src/ConfigPanel.cpp` | Modify | VXLAN tab input handling (VNI digit input, VTEP IP text input, enable toggles) |
| `src/Level.cpp` | Modify | Load/save `vxlanEnabled`, `evpnEnabled`, `vni`, `vtepIp` fields |
| `levels/level_14.json` | Create | VXLAN overlay level: PC-A/Leaf-1/Spine/Leaf-2/PC-B, player enables EVPN on both leaves |
| `src/main.cpp` | Modify | `#include "EvpnEngine.h"`, call `BuildEvpnRoutes(nodes)` each frame after BgpEngine |

---

### Task 1: Data Model — Device.h additions

**Files:**
- Modify: `src/Device.h`

All pure struct/enum additions — no logic. Read the file first to get exact line numbers, then make targeted additions in the correct sections.

- [ ] **Step 1: Add `ROUTE_EVPN` to `RouteSource` enum**

Current (line ~60):
```cpp
enum RouteSource { ROUTE_CONNECTED, ROUTE_STATIC, ROUTE_OSPF, ROUTE_OSPF_IA, ROUTE_BGP };
```

Replace with:
```cpp
enum RouteSource { ROUTE_CONNECTED, ROUTE_STATIC, ROUTE_OSPF, ROUTE_OSPF_IA, ROUTE_BGP, ROUTE_EVPN };
```

- [ ] **Step 2: Add `vni` field to `RouteEntry` struct**

`RouteEntry` currently ends with `int subVlanId = 0;`. Add one line after it:
```cpp
    uint32_t    vni       = 0;   // non-zero for ROUTE_EVPN entries
```

- [ ] **Step 3: Add `vxlanVni` field to `HopDecision` struct**

`HopDecision` currently ends with `int vlanTag = 0;`. Add one line after it:
```cpp
    uint32_t vxlanVni = 0;   // non-zero = hop is inside a VXLAN tunnel
```

- [ ] **Step 4: Add VXLAN fields to `DeviceNode` struct**

After the last field in `DeviceNode` (`std::vector<SubInterface> subIfaces;`), add:
```cpp
    // VXLAN / BGP EVPN (routers/leaves only)
    bool        vxlanEnabled = false;
    bool        evpnEnabled  = false;
    uint32_t    vni          = 0;        // VNI (1–16777215)
    std::string vtepIp;                  // must equal one of this node's portIpN values
    std::vector<RouteEntry> evpnRoutes;  // populated by EvpnEngine each frame
```

- [ ] **Step 5: Add `TAB_VXLAN` to `PanelTab` enum**

`PanelTab` currently ends with `TAB_SUB`. Append:
```cpp
enum PanelTab { TAB_CONFIG, TAB_ROUTES, TAB_ARP, TAB_OSPF, TAB_MPLS, TAB_BGP, TAB_VLAN, TAB_SUB, TAB_VXLAN };
```

- [ ] **Step 6: Add VXLAN input state to `PanelState` struct**

After the last field in `PanelState` (the `subIpBuf` line), add:
```cpp
    // VXLAN tab
    int         vxlanField   = -1;   // 0=VNI editing, 1=VTEP IP editing
    std::string vxlanVniBuf;         // digit buffer for VNI
    std::string vxlanVtepBuf;        // text buffer for VTEP IP
```

- [ ] **Step 7: Add `currentVni` to `PacketAnim` struct**

`PacketAnim` currently ends with `int currentVlan = 0;`. Add one line after it:
```cpp
    uint32_t currentVni  = 0;   // VXLAN VNI badge: non-zero while inside tunnel
```

- [ ] **Step 8: Build to confirm Device.h compiles**

```bash
make 2>&1 | grep -E "error:|warning:" | head -20
```

Expected: only pre-existing warnings, no new errors.

- [ ] **Step 9: Commit**

```bash
git add src/Device.h
git commit -m "feat: add VXLAN/EVPN data model to Device.h"
```

---

### Task 2: EvpnEngine — Route Distribution

**Files:**
- Create: `src/EvpnEngine.h`
- Create: `src/EvpnEngine.cpp`
- Modify: `src/Device.cpp` (add evpnRoutes to `GetRoutingTable`)

The Makefile uses `$(wildcard src/*.cpp)` — new files are auto-compiled. No Makefile changes needed.

**Design rules (implement exactly):**
1. Clear all `evpnRoutes` on every node at the start of each call (routes are rebuilt from scratch each frame)
2. For each VTEP (vxlanEnabled + evpnEnabled + non-empty vtepIp), find all OTHER nodes that are also vxlanEnabled + evpnEnabled + same vni
3. For each remote peer's portIp[i]: skip if the IP (stripped of mask) equals the peer's vtepIp — this prevents the underlay link subnet from being advertised as an overlay route
4. Otherwise, push a `ROUTE_EVPN` entry with dest=SubnetOf(portIp), nextHop=remote.vtepIp, vni=remote.vni
5. Also advertise the remote's sub-interface subnets (for router-on-a-stick VTEPs)

- [ ] **Step 1: Create `src/EvpnEngine.h`**

```cpp
#pragma once
#include "Device.h"
#include <vector>

void BuildEvpnRoutes(std::vector<DeviceNode>& nodes);
```

- [ ] **Step 2: Create `src/EvpnEngine.cpp`**

```cpp
#include "EvpnEngine.h"
#include <cstdio>
#include <cstdint>
#include <cstring>

// Zero the host bits of a CIDR address: "10.1.0.5/24" → "10.1.0.0/24"
static std::string SubnetOf(const std::string& cidr) {
    auto slash = cidr.find('/');
    if (slash == std::string::npos) return cidr;
    int prefix = std::stoi(cidr.substr(slash + 1));
    unsigned int a, b, c, d;
    if (sscanf(cidr.substr(0, slash).c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
        return cidr;
    uint32_t ipInt = (a << 24) | (b << 16) | (c << 8) | d;
    uint32_t mask  = prefix ? (0xFFFFFFFFu << (32 - prefix)) : 0u;
    uint32_t net   = ipInt & mask;
    char buf[32];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u/%d",
             (net >> 24) & 0xFF, (net >> 16) & 0xFF,
             (net >>  8) & 0xFF,  net & 0xFF, prefix);
    return std::string(buf);
}

// Strip "/prefix" from a CIDR string
static std::string StripMask(const std::string& cidr) {
    auto slash = cidr.find('/');
    return (slash != std::string::npos) ? cidr.substr(0, slash) : cidr;
}

void BuildEvpnRoutes(std::vector<DeviceNode>& nodes) {
    // Phase 1: clear existing EVPN routes on every node
    for (auto& n : nodes) n.evpnRoutes.clear();

    // Phase 2: for each VTEP, distribute remote overlay subnets
    for (auto& vtep : nodes) {
        if (!vtep.vxlanEnabled || !vtep.evpnEnabled || vtep.vtepIp.empty()) continue;

        for (const auto& remote : nodes) {
            if (remote.id == vtep.id) continue;
            if (!remote.vxlanEnabled || !remote.evpnEnabled) continue;
            if (remote.vni != vtep.vni) continue;
            if (remote.vtepIp.empty()) continue;

            // Advertise remote's portIp subnets (skip the vtepIp port itself)
            for (int i = 0; i < PORTS_PER_NODE; ++i) {
                if (remote.portIp[i].empty()) continue;
                // Skip the underlay port whose IP is the vtepIp
                if (StripMask(remote.portIp[i]) == remote.vtepIp) continue;

                RouteEntry re;
                re.dest    = SubnetOf(remote.portIp[i]);
                re.nextHop = remote.vtepIp;
                re.outPort = -1;
                re.src     = ROUTE_EVPN;
                re.vni     = remote.vni;
                vtep.evpnRoutes.push_back(re);
            }

            // Advertise remote's sub-interface subnets (router-on-a-stick VTEPs)
            for (const auto& si : remote.subIfaces) {
                if (si.ip.empty()) continue;
                RouteEntry re;
                re.dest    = SubnetOf(si.ip);
                re.nextHop = remote.vtepIp;
                re.outPort = -1;
                re.src     = ROUTE_EVPN;
                re.vni     = remote.vni;
                vtep.evpnRoutes.push_back(re);
            }
        }
    }
}
```

- [ ] **Step 3: Add `evpnRoutes` to `GetRoutingTable` in `src/Device.cpp`**

Read `src/Device.cpp`. Find the `GetRoutingTable` function (around line 80). After the `bgpRoutes` lines:
```cpp
    for (const auto& r : n.bgpRoutes)
        table.push_back({r.prefix, r.nextHop, -1, ROUTE_BGP});
```

Add immediately after:
```cpp
    for (const auto& r : n.evpnRoutes)
        table.push_back(r);
```

- [ ] **Step 4: Build**

```bash
make 2>&1 | grep -E "error:" | head -10
```

Expected: no errors.

- [ ] **Step 5: Commit**

```bash
git add src/EvpnEngine.h src/EvpnEngine.cpp src/Device.cpp
git commit -m "feat: add EvpnEngine with VXLAN EVPN route distribution"
```

---

### Task 3: SimulationEngine — VXLAN Two-Pass Forwarding

**Files:**
- Modify: `src/SimulationEngine.cpp`

**Design:** When the route-selection loop matches a `ROUTE_EVPN` entry, the code performs two recursive calls instead of the normal hop-advance:

1. **Phase 1 (underlay):** `SimulateForward(currentId, route.nextHop, nodes, cables)` — traces the underlay IP path from the current VTEP to the remote VTEP IP. All hops in this result get `vxlanVni = route.vni` stamped on them.
2. **Phase 2 (local delivery):** `SimulateForward(remoteVtepId, destIp, nodes, cables)` — traces local delivery from the remote VTEP to the actual destination. Hops have vxlanVni = 0 (no tunnel).
3. Both results are spliced into the main `result` and the function returns immediately.

**Why this doesn't recurse infinitely:** The vtepIp (e.g., `10.0.2.1`) is in the underlay /30 subnet, which is NOT in any EVPN-advertised overlay subnet. So the underlay call never matches an EVPN route. Similarly, the local delivery call forwards to the access-side subnet (e.g., `10.200.0.2`) which is CONNECTED on the remote VTEP — no EVPN match there either.

- [ ] **Step 1: Read the relevant section of SimulationEngine.cpp**

Read `src/SimulationEngine.cpp` lines 155–410 to locate the inner route-selection loop and understand where to insert the EVPN case.

- [ ] **Step 2: Add `ROUTE_EVPN` case after the `ROUTE_CONNECTED` block**

Inside `SimulateForward`, the `for (const auto& route : table)` loop has a large `if (route.src == ROUTE_CONNECTED)` block (ending around line 258) followed by a `// ARP cache check` comment that begins the non-CONNECTED path. Insert the EVPN block between those two sections:

```cpp
            // ── VXLAN EVPN tunnel ─────────────────────────────────────────
            if (route.src == ROUTE_EVPN) {
                // Phase 1: trace underlay from this VTEP to remote VTEP IP
                ForwardResult ul = SimulateForward(currentId, route.nextHop, nodes, cables);
                if (!ul.success) {
                    result.reason = "VXLAN underlay: " + ul.reason;
                    return result;
                }
                // Mark all underlay hops as VXLAN-encapsulated
                for (auto& h : ul.hops) h.vxlanVni = route.vni;
                // Splice underlay hops + path (skip ul.path[0] = currentId, already recorded)
                for (auto& h : ul.hops)  result.hops.push_back(h);
                for (int k = 1; k < (int)ul.path.size(); ++k) result.path.push_back(ul.path[k]);

                // Phase 2: local delivery from remote VTEP to actual destination
                int remoteVtepId = ul.path.back();
                ForwardResult lo = SimulateForward(remoteVtepId, destIp, nodes, cables);
                if (!lo.success) {
                    result.reason = "VXLAN decap: " + lo.reason;
                    return result;
                }
                for (auto& h : lo.hops)  result.hops.push_back(h);
                for (int k = 1; k < (int)lo.path.size(); ++k) result.path.push_back(lo.path[k]);

                result.success = true;
                result.reason  = "delivered";
                return result;
            }
            // ── end VXLAN EVPN ───────────────────────────────────────────
```

- [ ] **Step 3: Add `ROUTE_EVPN` to the route-type label in the non-CONNECTED `HopDecision` block**

Find the block that sets `hd.routeType` (around line 304–308):
```cpp
                if      (route.src == ROUTE_STATIC)  hd.routeType = "S";
                else if (route.src == ROUTE_OSPF)    hd.routeType = "O";
                else if (route.src == ROUTE_OSPF_IA) hd.routeType = "O IA";
                else if (route.src == ROUTE_BGP)     hd.routeType = "B";
                else                                  hd.routeType = "?";
```

Add before the `else`:
```cpp
                else if (route.src == ROUTE_EVPN)    hd.routeType = "VX";
```

- [ ] **Step 4: Build**

```bash
make 2>&1 | grep -E "error:" | head -10
```

Expected: no errors.

- [ ] **Step 5: Manual trace verification**

Load the game. Create: PC-A (10.1.0.2, GW 10.1.0.1) → Router-A (portIp0=10.1.0.1/24, portIp1=10.0.1.1/30, vxlanEnabled, evpnEnabled, vni=100, vtepIp="10.0.1.1") → Router-B (portIp0=10.0.1.2/30, portIp1=10.2.0.1/24, vxlanEnabled, evpnEnabled, vni=100, vtepIp="10.0.1.2") → PC-B (10.2.0.2, GW 10.2.0.1).

Add OSPF to both routers so they see each other's subnets (or add static routes). Enable EVPN on both. Send a packet from PC-A to PC-B.

Expected trace in the Routes tab or log: PC-A → Router-A (S route) → Router-A (VX encap) → Router-B (VX decap) → PC-B.

- [ ] **Step 6: Commit**

```bash
git add src/SimulationEngine.cpp
git commit -m "feat: VXLAN two-pass forwarding in SimulationEngine"
```

---

### Task 4: Packet Animation — VNI Badge

**Files:**
- Modify: `src/NetworkCanvas.cpp`

Show an orange "VNI:NNN" badge on the animated packet while `hop.vxlanVni > 0`. The badge renders above the MPLS/VLAN badges using the same pill style.

- [ ] **Step 1: Read the DrawPacketAnim function**

Search for `DrawPacketAnim` in `src/NetworkCanvas.cpp` and read the full function. Identify:
- Where `anim.currentLabel` and `anim.currentVlan` are updated (when the hop index advances)
- Where the MPLS label badge is drawn
- Where the VLAN badge is drawn

- [ ] **Step 2: Update `currentVni` as hops advance**

Find the section inside `DrawPacketAnim` where `anim.currentLabel` and `anim.currentVlan` are set from the current hop. In the same block, add:

```cpp
anim.currentVni = (hopIdx < (int)anim.result.hops.size())
                  ? anim.result.hops[hopIdx].vxlanVni : 0;
```

- [ ] **Step 3: Draw the VNI badge**

Find where the MPLS badge is drawn (the block that checks `anim.currentLabel != 0`). Add the VNI badge immediately before or after it:

```cpp
// VNI badge — orange pill above MPLS/VLAN badges
if (anim.currentVni != 0) {
    char vniBuf[32];
    snprintf(vniBuf, sizeof(vniBuf), "VNI:%u", anim.currentVni);
    int    badgeW = MeasureText(vniBuf, 11) + 10;
    // Position: above the packet dot (or stack below MPLS/VLAN badge)
    float  badgeX = px - badgeW * 0.5f;
    float  badgeY = py - 34.0f;   // adjust offset to avoid overlap with existing badges
    DrawRectangleRounded({badgeX, badgeY, (float)badgeW, 16.f}, 0.5f, 6,
                         {255, 140, 0, 230});   // orange
    DrawText(vniBuf, (int)(badgeX + 5), (int)(badgeY + 2), 11, WHITE);
}
```

Adjust `badgeY` so it doesn't overlap with the MPLS label badge (typically at py - 20) or VLAN badge. Use `py - 38` if MPLS badge is at `py - 20`, stacking upward.

- [ ] **Step 4: Build and eyeball test**

```bash
make 2>&1 | grep -E "error:" | head -5
```

Run the game. Trigger a VXLAN tunnel packet (requires EVPN enabled on two co-VNI VTEPs with IP connectivity). Confirm the orange VNI badge appears on the packet dot during tunnel hops and disappears after decap.

- [ ] **Step 5: Commit**

```bash
git add src/NetworkCanvas.cpp
git commit -m "feat: VNI badge on packet animation during VXLAN tunnel hops"
```

---

### Task 5: VXLAN Config Tab UI

**Files:**
- Modify: `src/NetworkCanvas.h` (add `DrawVxlanTab` declaration)
- Modify: `src/NetworkCanvas.cpp` (implement `DrawVxlanTab`, update `DrawPanel`)
- Modify: `src/ConfigPanel.cpp` (VXLAN tab input handling)

The tab follows the exact same visual style as `DrawBgpTab` and `DrawVlanTab`: read those functions first to match font sizes, row heights, rectangle styles, and field colors.

- [ ] **Step 1: Read existing tab functions for style reference**

Read `DrawBgpTab` and `DrawVlanTab` in `src/NetworkCanvas.cpp`. Note:
- Panel bounds (x, width, typical y offsets)
- How text fields are drawn with `DrawTextField`
- How booleans are toggled (checkbox style or button)
- Row heights (~22–28px per field, separator lines)

Also read `DrawPanel` to see how the tab bar is rendered (tab labels, widths, click regions) and how tab content is dispatched.

- [ ] **Step 2: Add `DrawVxlanTab` declaration to `src/NetworkCanvas.h`**

After the `DrawSubIfaceTab` line, add:
```cpp
void DrawVxlanTab(const DeviceNode* n, const PanelState& ps);
```

- [ ] **Step 3: Implement `DrawVxlanTab` in `src/NetworkCanvas.cpp`**

```cpp
void DrawVxlanTab(const DeviceNode* n, const PanelState& ps) {
    if (!n) return;
    const int PX = SCREEN_W() - PANEL_W + 10;
    const int PW = PANEL_W - 20;

    // Section header
    DrawText("VXLAN / BGP EVPN", PX, 115, 12, RAYWHITE);
    DrawLine(PX, 131, PX + PW, 131, {51, 65, 85, 255});

    // VXLAN enabled toggle
    bool vxlan = n->vxlanEnabled;
    Rectangle vxBox = {(float)PX, 140, 14, 14};
    DrawRectangleRec(vxBox, vxlan ? (Color){34, 197, 94, 255} : (Color){51, 65, 85, 255});
    if (vxlan) DrawText("x", (int)vxBox.x + 3, (int)vxBox.y + 1, 11, WHITE);
    DrawText("VXLAN Enabled", PX + 20, 141, 12, RAYWHITE);

    if (!vxlan) {
        DrawText("Enable VXLAN to configure.", PX, 170, 11, GRAY);
        return;
    }

    // VNI field
    DrawText("VNI", PX, 172, 11, GRAY);
    char vniBuf[32];
    snprintf(vniBuf, sizeof(vniBuf), "%u", n->vni);
    bool vniActive = (ps.vxlanField == 0);
    DrawTextField({(float)PX, 185, (float)PW, 22}, nullptr, "1–16777215",
                  vniActive ? ps.vxlanVniBuf : std::string(vniBuf), vniActive, n->vni > 0);

    // VTEP IP field
    DrawText("VTEP IP  (must match a port IP)", PX, 215, 11, GRAY);
    bool vtepActive = (ps.vxlanField == 1);
    DrawTextField({(float)PX, 228, (float)PW, 22}, nullptr, "e.g. 10.0.1.1",
                  vtepActive ? ps.vxlanVtepBuf : n->vtepIp, vtepActive,
                  !n->vtepIp.empty());

    // EVPN enabled toggle
    DrawLine(PX, 260, PX + PW, 260, {51, 65, 85, 255});
    bool evpn = n->evpnEnabled;
    Rectangle evpnBox = {(float)PX, 268, 14, 14};
    DrawRectangleRec(evpnBox, evpn ? (Color){34, 197, 94, 255} : (Color){51, 65, 85, 255});
    if (evpn) DrawText("x", (int)evpnBox.x + 3, (int)evpnBox.y + 1, 11, WHITE);
    DrawText("EVPN Enabled", PX + 20, 269, 12, RAYWHITE);

    // Show learned EVPN route count
    if (evpn) {
        char routeBuf[64];
        snprintf(routeBuf, sizeof(routeBuf), "EVPN routes: %d", (int)n->evpnRoutes.size());
        DrawText(routeBuf, PX, 292, 11, {148, 163, 184, 255});
    }
}
```

- [ ] **Step 4: Add `TAB_VXLAN` to the tab bar in `DrawPanel`**

Read `DrawPanel` in `src/NetworkCanvas.cpp`. Find where the tab labels are listed (the array or sequence of tab names like "CFG", "RTE", "ARP", etc.). Add "VXL" (or "VXLAN") as the last tab after "SUB".

The tab bar click regions are computed from the tab count and panel width. Adding one more tab shrinks all tab widths proportionally — confirm the tab bar still fits at the current panel width (280px ÷ 9 tabs ≈ 31px per tab, which is tight but acceptable with 2–4 character labels).

In the tab content dispatch (the `if (ps.activeTab == TAB_CONFIG)` chain), add:
```cpp
else if (ps.activeTab == TAB_VXLAN) DrawVxlanTab(selectedNode, ps);
```

- [ ] **Step 5: Add VXLAN input handling to `src/ConfigPanel.cpp`**

Read `ConfigPanel.cpp` to understand how other tabs handle clicks and text input (look at `TAB_BGP` or `TAB_VLAN` handlers as reference). Add a `TAB_VXLAN` case that handles:

**Click on VXLAN enabled checkbox (y ≈ 140–154 panel-relative):**
```cpp
// Toggle vxlanEnabled
selectedNode->vxlanEnabled = !selectedNode->vxlanEnabled;
if (!selectedNode->vxlanEnabled) {
    selectedNode->evpnEnabled = false;
    selectedNode->vni = 0;
    selectedNode->vtepIp.clear();
    ps.vxlanField = -1;
}
```

**Click on VNI field (y ≈ 185–207):**
```cpp
ps.vxlanField = 0;
ps.vxlanVniBuf = std::to_string(selectedNode->vni);
```

**Click on VTEP IP field (y ≈ 228–250):**
```cpp
ps.vxlanField = 1;
ps.vxlanVtepBuf = selectedNode->vtepIp;
```

**Click on EVPN enabled checkbox (y ≈ 268–282):**
```cpp
selectedNode->evpnEnabled = !selectedNode->evpnEnabled;
```

**Text input while vxlanField == 0 (VNI):**
- Accept digits only
- On Enter/Tab: parse as uint32_t, clamp to [1, 16777215], store in `selectedNode->vni`, set vxlanField = -1

**Text input while vxlanField == 1 (VTEP IP):**
- Accept printable characters (IP address)
- On Enter/Tab: store in `selectedNode->vtepIp`, set vxlanField = -1

**Escape:** `ps.vxlanField = -1`

- [ ] **Step 6: Build**

```bash
make 2>&1 | grep -E "error:" | head -10
```

Expected: no errors.

- [ ] **Step 7: UI smoke test**

Run the game. Add a router, open its panel. Confirm a "VXL" (or equivalent) tab appears in the tab bar. Click it — confirm the VXLAN tab renders with the enabled checkbox, VNI field, VTEP IP field, and EVPN checkbox. Toggle "VXLAN Enabled" — confirm the form appears/hides. Enter a VNI number — confirm it saves and displays correctly.

- [ ] **Step 8: Commit**

```bash
git add src/NetworkCanvas.h src/NetworkCanvas.cpp src/ConfigPanel.cpp
git commit -m "feat: VXLAN config tab (VNI, VTEP IP, EVPN toggle)"
```

---

### Task 6: Level Serialization + Level 14 JSON

**Files:**
- Modify: `src/Level.cpp`
- Create: `levels/level_14.json`

**Level 14 topology:**
```
PC-A ── Leaf-1 ── Spine ── Leaf-2 ── PC-B
VLAN:  VNI 100  underlay  VNI 100
```

Player action: open VXLAN tab on Leaf-1 → enable EVPN; same on Leaf-2. Send ping. Watch orange VNI badge animate across Spine.

- [ ] **Step 1: Add VXLAN field loading to `LoadLevel` in `src/Level.cpp`**

Read `src/Level.cpp`. In the device-loading loop (after `ldpEnabled` and before or after `bgpEnabled`), add:

```cpp
n.vxlanEnabled = d.value("vxlanEnabled", false);
n.evpnEnabled  = d.value("evpnEnabled",  false);
n.vni          = (uint32_t)d.value("vni", 0);
n.vtepIp       = d.value("vtepIp", "");
```

- [ ] **Step 2: Add VXLAN field saving to `SaveScene` in `src/Level.cpp`**

In the device-saving loop (after the MPLS/BGP block), add:

```cpp
if (n.vxlanEnabled) {
    d["vxlanEnabled"] = true;
    d["vni"]          = n.vni;
    if (!n.vtepIp.empty()) d["vtepIp"] = n.vtepIp;
    if (n.evpnEnabled) d["evpnEnabled"] = true;
}
```

- [ ] **Step 3: Create `levels/level_14.json`**

IP plan:
- PC-A: 10.100.0.2/24, GW 10.100.0.1
- Leaf-1: portIp0=10.100.0.1/24 (to PC-A), portIp1=10.0.1.1/30 (underlay to Spine), vtepIp="10.0.1.1", vni=100, evpnEnabled=false
- Spine: portIp0=10.0.1.2/30, portIp1=10.0.2.2/30, OSPF only
- Leaf-2: portIp0=10.0.2.1/30 (underlay to Spine), portIp1=10.200.0.1/24 (to PC-B), vtepIp="10.0.2.1", vni=100, evpnEnabled=false
- PC-B: 10.200.0.2/24, GW 10.200.0.1

**Key constraint:** vtepIp on each leaf equals its underlay portIp (the link toward Spine). EvpnEngine skips this portIp when advertising overlay routes — so only the 10.100.x and 10.200.x overlay subnets get distributed via EVPN, not the /30 underlay links.

```json
{
  "id": 14,
  "title": "Level 14 — VXLAN Overlay",
  "briefing": "PC-A and PC-B live on the same VXLAN segment (VNI 100) but across different racks. OSPF already handles the underlay. Open the VXLAN tab on Leaf-1 and enable EVPN, then do the same on Leaf-2 — BGP EVPN will distribute the overlay routes automatically. Send a ping and watch the orange VNI:100 badge travel through the fabric.",
  "devices": [
    {
      "id": 1,
      "label": "PC-A",
      "type": "PC",
      "x": -500,
      "y": 0,
      "portIp0": "10.100.0.2/24",
      "staticRoutes": [
        { "dest": "0.0.0.0/0", "nextHop": "10.100.0.1" }
      ]
    },
    {
      "id": 2,
      "label": "Leaf-1",
      "type": "ROUTER",
      "x": -250,
      "y": 0,
      "portIp0": "10.100.0.1/24",
      "portIp1": "10.0.1.1/30",
      "ospfEnabled": true,
      "routerId": "1.1.1.1",
      "vxlanEnabled": true,
      "vni": 100,
      "vtepIp": "10.0.1.1",
      "evpnEnabled": false
    },
    {
      "id": 3,
      "label": "Spine",
      "type": "ROUTER",
      "x": 0,
      "y": 0,
      "portIp0": "10.0.1.2/30",
      "portIp1": "10.0.2.2/30",
      "ospfEnabled": true,
      "routerId": "2.2.2.2"
    },
    {
      "id": 4,
      "label": "Leaf-2",
      "type": "ROUTER",
      "x": 250,
      "y": 0,
      "portIp0": "10.0.2.1/30",
      "portIp1": "10.200.0.1/24",
      "ospfEnabled": true,
      "routerId": "3.3.3.3",
      "vxlanEnabled": true,
      "vni": 100,
      "vtepIp": "10.0.2.1",
      "evpnEnabled": false
    },
    {
      "id": 5,
      "label": "PC-B",
      "type": "PC",
      "x": 500,
      "y": 0,
      "portIp0": "10.200.0.2/24",
      "staticRoutes": [
        { "dest": "0.0.0.0/0", "nextHop": "10.200.0.1" }
      ]
    }
  ],
  "cables": [
    { "from": 1, "fromPort": 0, "to": 2, "toPort": 0 },
    { "from": 2, "fromPort": 1, "to": 3, "toPort": 0 },
    { "from": 3, "fromPort": 1, "to": 4, "toPort": 0 },
    { "from": 4, "fromPort": 1, "to": 5, "toPort": 0 }
  ],
  "winConditions": [
    {
      "src": "PC-A",
      "dst": "PC-B",
      "description": "VXLAN overlay carries PC-A traffic to PC-B across the fabric",
      "requiresFix": false
    },
    {
      "src": "PC-B",
      "dst": "PC-A",
      "description": "VXLAN overlay carries PC-B traffic to PC-A across the fabric",
      "requiresFix": false
    }
  ]
}
```

- [ ] **Step 4: Validate JSON**

```bash
python3 -m json.tool levels/level_14.json > /dev/null && echo "OK"
```

- [ ] **Step 5: Build**

```bash
make 2>&1 | grep -E "error:" | head -5
```

- [ ] **Step 6: Commit**

```bash
git add src/Level.cpp levels/level_14.json
git commit -m "feat: VXLAN serialization + level 14 (VXLAN overlay)"
```

---

### Task 7: main.cpp Integration

**Files:**
- Modify: `src/main.cpp`

Three additions: include the new header, call `BuildEvpnRoutes` in the per-frame engine loop, extend the "Next Level" cap from 13 to 14.

- [ ] **Step 1: Add `#include "EvpnEngine.h"`**

Read `src/main.cpp` lines 1–20. Find the block of `#include` lines. Add after the BgpEngine include (or at the end of the project header block):

```cpp
#include "EvpnEngine.h"
```

- [ ] **Step 2: Call `BuildEvpnRoutes` in the per-frame engine loop**

Search for `BuildBgpRoutes` in `src/main.cpp`. It is called once per frame in the game loop. Add the EVPN call immediately after it:

```cpp
BuildEvpnRoutes(nodes);
```

The call order must be: OspfEngine → BgpEngine → EvpnEngine. EVPN builds on BGP-established state (and in a future iteration could filter by BGP session). Currently it finds co-VNI peers directly, so order is not strictly critical, but keep EVPN last.

- [ ] **Step 3: Extend "Next Level" cap from 13 to 14**

Search for `currentLevel < 13` in `src/main.cpp`. There should be exactly 2 occurrences (Next Level button handler and `DrawWinOverlay` argument). Change both to `currentLevel < 14`:

```bash
grep -n "currentLevel < 13" src/main.cpp
sed -i '' 's/currentLevel < 13/currentLevel < 14/g' src/main.cpp
grep -n "currentLevel < 14" src/main.cpp   # expect exactly 2 lines
grep -n "currentLevel < 13" src/main.cpp   # expect no output
```

- [ ] **Step 4: Build**

```bash
make 2>&1 | tail -5
```

Expected: build completes with no new errors.

- [ ] **Step 5: Full integration smoke test**

Load level 14. Confirm:
1. Leaf-1 and Leaf-2 appear with "VXL" tab visible in their config panel.
2. Open Leaf-1 VXLAN tab — VNI shows 100, VTEP IP shows "10.0.1.1", EVPN disabled.
3. Enable EVPN on Leaf-1 → Leaf-1's EVPN routes count shows 1 (10.200.0.0/24 from Leaf-2). Enable EVPN on Leaf-2 → Leaf-2's EVPN routes count shows 1 (10.100.0.0/24 from Leaf-1).
4. Select PC-A, click PC-B to send a packet. Packet animates: PC-A → Leaf-1 (normal) → Spine (orange VNI:100 badge) → Leaf-2 (VNI badge) → PC-B (VNI gone).
5. Win overlay appears.
6. "Next Level →" button does NOT appear (level 14 is the last level).
7. After winning level 13 (load it first), "Next Level →" leads to level 14.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "feat: integrate EvpnEngine into main loop, extend level cap to 14"
```

---

## Verification Checklist

After all 7 tasks complete:

- [ ] `make` builds clean (zero new errors)
- [ ] `python3 -m json.tool levels/level_14.json` exits 0
- [ ] VXLAN tab appears in the panel tab bar for routers
- [ ] VNI (0–16777215) and VTEP IP fields accept and persist input
- [ ] EVPN toggle appears only when VXLAN is enabled
- [ ] EVPN route count display shows correct number after enabling EVPN on both peers
- [ ] Send packet PC-A → PC-B with EVPN enabled: packet shows orange VNI:100 badge on Spine and Leaf-2 hops
- [ ] Send packet PC-A → PC-B with EVPN disabled on either leaf: packet fails ("no route to 10.200.0.2")
- [ ] Bidirectional win conditions both trigger after EVPN enabled
- [ ] VNI badge disappears after decap (local delivery hops show no badge)
- [ ] "Next Level →" appears after levels 13 win, leads to level 14
- [ ] "Next Level →" does NOT appear after level 14 win
- [ ] Ctrl+S saves vxlanEnabled, evpnEnabled, vni, vtepIp; Ctrl+O reloads them correctly

---

## Self-Review

**Spec coverage:**
- VTEPs on routers ✓ (vxlanEnabled + vtepIp + vni on DeviceNode)
- EVPN control plane using BGP semantics ✓ (EvpnEngine distributes overlay routes; player enables "EVPN" matching BGP EVPN vocabulary)
- VXLAN tunnel creation ✓ (two-pass SimulateForward with ROUTE_EVPN case)
- VNI badge on packet animation ✓ (hops.vxlanVni → anim.currentVni → orange badge)
- Config tab for VXLAN ✓ (TAB_VXLAN, DrawVxlanTab, ConfigPanel input)
- New level 14 ✓ (overlay-over-underlay, player enables EVPN)

**Placeholder scan:** All code is complete. SubnetOf helper is a concrete implementation. DrawVxlanTab badge offsets are explicit values. All fields are named.

**Infinite recursion guard:** The EVPN two-pass calls `SimulateForward` twice recursively. Recursion terminates because:
1. Underlay call: vtepIp (e.g., `10.0.2.1`) is in the underlay /30 subnet. No EVPN route matches this address (EVPN routes are for overlay /24 subnets). OSPF/CONNECTED routes handle the underlay hop.
2. Local delivery call: destIp (e.g., `10.200.0.2`) is the actual destination in the overlay subnet, which is CONNECTED on the remote VTEP. No EVPN re-match.

**vtepIp constraint:** vtepIp must equal one of the VTEP node's portIpN values. This is required for `FindNodeOwningIp` to locate the remote VTEP, and for `FindNodeByIp` to identify the destination in CONNECTED route handling. The level_14.json satisfies this: Leaf-1 vtepIp="10.0.1.1"=portIp1, Leaf-2 vtepIp="10.0.2.1"=portIp0.

**EvpnEngine underlay exclusion:** The vtepIp port is skipped when advertising overlay routes. In level_14.json, Leaf-2's portIp0="10.0.2.1/30" is skipped (matches vtepIp="10.0.2.1"), leaving only portIp1="10.200.0.1/24" to be advertised. This prevents the underlay /30 subnet from appearing as an EVPN route on Leaf-1.

**Type consistency:** `uint32_t vni` used consistently in RouteEntry, DeviceNode, EvpnEngine, and HopDecision. `ROUTE_EVPN` added at end of enum — existing code that does not handle it falls through to `hd.routeType = "?"` (caught by the Step 3 fix adding the `else if (route.src == ROUTE_EVPN) hd.routeType = "VX"` branch).
