# Router-on-a-Stick (Inter-VLAN Routing) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `SubInterface` structs to router nodes so one physical port (with a trunk) can route between multiple VLANs — teaching the real Cisco `GigabitEthernet0/0.10` concept.

**Architecture:** New `SubInterface { parentPort, vlanId, ip }` struct stored as a `vector<SubInterface>` on `DeviceNode`. `GetRoutingTable` emits `ROUTE_CONNECTED` entries (with `subVlanId` set) for each subinterface. The forwarding engine uses `subVlanId` as the startVlan for `FindL2Path` egress and extends next-hop resolution to traverse switches via L2 BFS. A new "Sub" config tab lets students add/remove subinterfaces on routers. Level 10 presents a classic router-on-a-stick scenario.

**Tech Stack:** C++17, raylib 5.x, nlohmann/json; build via `make`

---

## File Map

| File | Change |
|------|--------|
| `src/Device.h` | New `SubInterface` struct; `subVlanId` field on `RouteEntry`; `subIfaces` vector on `DeviceNode`; `TAB_SUB` enum value; new `PanelState` fields |
| `src/Device.cpp` | Update `GetRoutingTable` to emit subinterface routes |
| `src/Level.cpp` | Parse `subIfaces` JSON array |
| `levels/level_10.json` | New RoaS teaching scenario |
| `src/SimulationEngine.cpp` | `FindNodeOwningIp`; `FindL2Path` startVlan param; route-connected uses subVlanId; non-connected next-hop resolution traverses switches |
| `src/ConfigPanel.h` | `TAB_SUB`; new `PanelState` fields; new rect helper declarations |
| `src/ConfigPanel.cpp` | Update `PnlTabW()` for 8 tabs; `PnlSubTabRect()`; sub-tab layout rects |
| `src/NetworkCanvas.h` | `DrawSubIfaceTab` declaration; sub-tab layout constants |
| `src/NetworkCanvas.cpp` | `DrawSubIfaceTab`; 8-tab bar; router port subif indicator |
| `src/main.cpp` | Sub-tab click handler; subif form text input; field resets; level count 9→10 |

---

## Task 1: Data Model — SubInterface Struct + RouteEntry + DeviceNode

**Files:**
- Modify: `src/Device.h` (after `VlanPortConfig`, before the `MPLS types` comment block)
- Modify: `src/Device.h` (`RouteEntry` struct — add one field)
- Modify: `src/Device.h` (`DeviceNode` struct — add one field after `vlanPorts`)
- Modify: `src/Device.cpp` (`GetRoutingTable`)

- [ ] **Step 1: Add `SubInterface` struct to Device.h**

Insert immediately after the `VlanPortConfig` struct (around line 16), before the `// ── MPLS types` comment:

```cpp
struct SubInterface {
    int         parentPort = 0;   // physical port index (0-3)
    int         vlanId     = 0;   // 802.1Q VLAN ID (1-4094)
    std::string ip;               // CIDR e.g. "10.10.0.1/24"
};
```

- [ ] **Step 2: Add `subVlanId` to RouteEntry**

In `RouteEntry` (currently around line 56–62), add one field after `area`:

```cpp
struct RouteEntry {
    std::string dest;
    std::string nextHop;
    int         outPort;
    RouteSource src;
    uint32_t    area      = 0;
    int         subVlanId = 0;  // non-zero: route exits via tagged subinterface
};
```

- [ ] **Step 3: Add `subIfaces` vector to DeviceNode**

In `DeviceNode`, after `VlanPortConfig vlanPorts[PORTS_PER_NODE];` (the last field, currently around line 163), add:

```cpp
    VlanPortConfig            vlanPorts[PORTS_PER_NODE];
    std::vector<SubInterface> subIfaces;  // routers only
};
```

- [ ] **Step 4: Update `GetRoutingTable` in Device.cpp**

In `GetRoutingTable` (Device.cpp, line 80), add subinterface routes immediately after the `portIp` loop (before `staticRoutes`):

```cpp
std::vector<RouteEntry> GetRoutingTable(const DeviceNode& n) {
    std::vector<RouteEntry> table;
    if (ValidateIP(n.mgmtIp))
        table.push_back({NetworkAddress(n.mgmtIp), "direct", -1, ROUTE_CONNECTED});
    for (int i = 0; i < PORTS_PER_NODE; ++i)
        if (ValidateIP(n.portIp[i]))
            table.push_back({NetworkAddress(n.portIp[i]), "direct", i, ROUTE_CONNECTED});
    for (const auto& si : n.subIfaces)
        if (ValidateIP(si.ip)) {
            RouteEntry re;
            re.dest      = NetworkAddress(si.ip);
            re.nextHop   = "direct";
            re.outPort   = si.parentPort;
            re.src       = ROUTE_CONNECTED;
            re.subVlanId = si.vlanId;
            table.push_back(re);
        }
    for (const auto& r : n.staticRoutes)
        table.push_back(r);
    for (const auto& r : n.ospfRoutes)
        table.push_back(r);
    for (const auto& r : n.bgpRoutes)
        table.push_back({r.prefix, r.nextHop, -1, ROUTE_BGP});
    return table;
}
```

- [ ] **Step 5: Build and verify compile**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | head -30
```

Expected: no errors (new fields are zero-initialized by default).

- [ ] **Step 6: Commit**

```bash
git add src/Device.h src/Device.cpp
git commit -m "feat: add SubInterface struct, subVlanId on RouteEntry, subIfaces on DeviceNode"
```

---

## Task 2: Level JSON Parsing + Level 10

**Files:**
- Modify: `src/Level.cpp` (add subIfaces parsing after vlanPort loop)
- Create: `levels/level_10.json`

- [ ] **Step 1: Add subIfaces parsing in Level.cpp**

In `LoadLevel`, after the `vlanPort` parsing loop (around line 58, currently reading `vlanPort0`–`vlanPort3`), add:

```cpp
        if (d.contains("subIfaces") && d["subIfaces"].is_array())
            for (const auto& si : d["subIfaces"])
                n.subIfaces.push_back({
                    si.value("port", 0),
                    si.value("vlan", 0),
                    si.value("ip",   "")
                });
```

Full context (the block goes between the vlanPort loop and the staticRoutes loop):

```cpp
        for (int i = 0; i < PORTS_PER_NODE; ++i) {
            std::string key = "vlanPort" + std::to_string(i);
            if (d.contains(key) && d[key].is_object()) {
                std::string modeStr = d[key].value("mode", "access");
                n.vlanPorts[i].mode       = (modeStr == "trunk") ? VLAN_TRUNK : VLAN_ACCESS;
                n.vlanPorts[i].accessVlan = d[key].value("vlan", 1);
            }
        }

        // NEW: subinterfaces (routers only)
        if (d.contains("subIfaces") && d["subIfaces"].is_array())
            for (const auto& si : d["subIfaces"])
                n.subIfaces.push_back({
                    si.value("port", 0),
                    si.value("vlan", 0),
                    si.value("ip",   "")
                });

        for (const auto& sr : d.value("staticRoutes", json::array())) {
```

- [ ] **Step 2: Create levels/level_10.json**

Topology: PC-A (VLAN 10) and PC-B (VLAN 20) connect to SW1. SW1's trunk port connects to R1. Student must: (1) set SW1 port 2 to trunk, (2) add subinterfaces on R1.

```json
{
  "id": 10,
  "title": "Router-on-a-Stick",
  "briefing": "PC-A (VLAN 10) and PC-B (VLAN 20) are on the same switch but different VLANs. A router-on-a-stick configuration can route between them. Set SW1 port 2 to trunk, then add subinterfaces on R1: port 0, VLAN 10, 10.10.0.1/24 and port 0, VLAN 20, 10.20.0.1/24.",
  "devices": [
    {
      "id": 1, "type": "PC", "label": "PC-A",
      "x": 120, "y": 280,
      "portIp0": "10.10.0.2/24",
      "staticRoutes": [{"dest": "0.0.0.0/0", "nextHop": "10.10.0.1"}]
    },
    {
      "id": 2, "type": "PC", "label": "PC-B",
      "x": 120, "y": 440,
      "portIp0": "10.20.0.2/24",
      "staticRoutes": [{"dest": "0.0.0.0/0", "nextHop": "10.20.0.1"}]
    },
    {
      "id": 3, "type": "SWITCH", "label": "SW1",
      "x": 400, "y": 360,
      "vlanPort0": {"mode": "access", "vlan": 10},
      "vlanPort1": {"mode": "access", "vlan": 20},
      "vlanPort2": {"mode": "access", "vlan": 1},
      "vlanPort3": {"mode": "access", "vlan": 1}
    },
    {
      "id": 4, "type": "ROUTER", "label": "R1",
      "x": 680, "y": 360
    }
  ],
  "cables": [
    {"from": 1, "fromPort": 0, "to": 3, "toPort": 0},
    {"from": 2, "fromPort": 0, "to": 3, "toPort": 1},
    {"from": 3, "fromPort": 2, "to": 4, "toPort": 0}
  ],
  "winConditions": [
    {
      "src": "PC-A",
      "dst": "PC-B",
      "description": "PC-A reaches PC-B (inter-VLAN via router-on-a-stick)"
    }
  ]
}
```

Notes on the starting (broken) state:
- SW1 port 2 is `access VLAN 1` — student must change to trunk
- R1 has no `subIfaces` — student must add them via the Sub tab
- PC-A and PC-B have static default routes pre-configured so they can send; the gateway IPs (10.10.0.1, 10.20.0.1) are the subinterface IPs the student must create

- [ ] **Step 3: Build and verify compile**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | head -20
```

Expected: clean compile. The new level file is not loaded yet (level count still shows 1-9 in main.cpp).

- [ ] **Step 4: Commit**

```bash
git add src/Level.cpp levels/level_10.json
git commit -m "feat: parse subIfaces from level JSON; add level 10 RoaS scenario"
```

---

## Task 3: Forwarding Engine — Subinterface-Aware Routing

**Files:**
- Modify: `src/SimulationEngine.cpp`

This task is the core of router-on-a-stick. Three changes:

1. **`FindL2Path` gains `startVlan=0`** — allows a subinterface egress to seed the frame's VLAN tag.
2. **New `FindNodeOwningIp`** — like `FindNodeByIp` but also checks `subIfaces` (needed so next-hop resolution can find R1 via its subinterface IP "10.10.0.1").
3. **`ROUTE_CONNECTED` handler** — passes `route.subVlanId` as startVlan to `FindL2Path`.
4. **Non-connected route handler** — replaces the direct-cable neighbor loop with `FindNodeOwningIp` + `FindL2Path` (so R1's ARP for 10.10.0.1 traverses SW1).

- [ ] **Step 1: Add `startVlan` parameter to `FindL2Path`**

Change the function signature from:

```cpp
static std::vector<int> FindL2Path(int srcId, int dstId,
                                    const std::vector<DeviceNode>& nodes,
                                    const std::vector<Cable>& cables)
```

To:

```cpp
static std::vector<int> FindL2Path(int srcId, int dstId,
                                    const std::vector<DeviceNode>& nodes,
                                    const std::vector<Cable>& cables,
                                    int startVlan = 0)
```

And change the initial queue push (currently `q.push({srcId, 0, {srcId}})`) to:

```cpp
    q.push({srcId, startVlan, {srcId}});
```

- [ ] **Step 2: Add `FindNodeOwningIp` helper**

Add this function immediately after `FindNodeByIp` (around line 18):

```cpp
// Like FindNodeByIp but also checks subinterface IPs.
static const DeviceNode* FindNodeOwningIp(const std::vector<DeviceNode>& nodes,
                                           const std::string& ip)
{
    // strip mask if present
    auto slash = ip.find('/');
    std::string plain = (slash != std::string::npos) ? ip.substr(0, slash) : ip;

    for (const auto& n : nodes) {
        for (int p = 0; p < PORTS_PER_NODE; ++p) {
            auto s = n.portIp[p].find('/');
            std::string portPlain = (s != std::string::npos)
                                    ? n.portIp[p].substr(0, s) : n.portIp[p];
            if (portPlain == plain) return &n;
        }
        for (const auto& si : n.subIfaces) {
            auto s = si.ip.find('/');
            std::string siPlain = (s != std::string::npos) ? si.ip.substr(0, s) : si.ip;
            if (siPlain == plain) return &n;
        }
    }
    return nullptr;
}
```

- [ ] **Step 3: Update ROUTE_CONNECTED handler to use subVlanId**

In the `ROUTE_CONNECTED` branch of `SimulateForward` (around line 146), change the `FindL2Path` call from:

```cpp
                std::vector<int> l2 = FindL2Path(currentId, destNode->id, nodes, cables);
```

To:

```cpp
                std::vector<int> l2 = FindL2Path(currentId, destNode->id, nodes, cables,
                                                  route.subVlanId);
```

This seeds the BFS with the subinterface's VLAN so the egress through SW1's trunk port is tagged correctly. For ordinary `portIp`-based ROUTE_CONNECTED, `subVlanId` is 0 (unchanged behavior).

- [ ] **Step 4: Replace non-connected next-hop resolution with L2-aware BFS**

The existing code (around line 222–258) does a direct-cable loop to find `neighborId`. Replace it entirely. The full replacement:

**Remove** this block (direct-cable neighbor search loop):

```cpp
            // Find the directly-connected neighbor that owns route.nextHop
            int         neighborId  = -1;
            std::string resolvedMac;
            for (const auto& cable : cables) {
                int candidateId = -1;
                if      (cable.fromId == currentId) candidateId = cable.toId;
                else if (cable.toId   == currentId) candidateId = cable.fromId;
                if (candidateId == -1) continue;

                const DeviceNode* neighbor = FindNode(nodes, candidateId);
                if (!neighbor) continue;

                auto nbTable = GetRoutingTable(*neighbor);
                for (const auto& nbRoute : nbTable) {
                    if (nbRoute.src == ROUTE_CONNECTED &&
                        IpInSubnet(route.nextHop, nbRoute.dest)) {
                        neighborId  = candidateId;
                        resolvedMac = GetDeviceMac(candidateId);
                        break;
                    }
                }
                if (neighborId != -1) break;
            }
```

**Replace with:**

```cpp
            // Find the node owning route.nextHop (checks portIp + subIfaces)
            const DeviceNode* nextHopNode = FindNodeOwningIp(nodes, route.nextHop);
            int         neighborId  = nextHopNode ? nextHopNode->id : -1;
            std::string resolvedMac = (neighborId != -1) ? GetDeviceMac(neighborId) : "";
```

- [ ] **Step 5: Update the hop-building code after neighborId is found**

After the ARP event code (which stays unchanged), find the block that currently does:

```cpp
            {
                HopDecision hd;
                hd.nodeId     = currentId;
                ...
                hd.outPort    = route.outPort;
                // MPLS...
                result.hops.push_back(hd);
            }
            visited.insert(neighborId);
            result.path.push_back(neighborId);
            currentId = neighborId;
            matched   = true;
            break;
```

Replace the entire block from the opening `{` through `break;` with the following. This adds switch hop decisions when the path to the next L3 hop passes through switches:

```cpp
            // Find L2 path to neighbor (handles switches between L3 routers/PCs)
            std::vector<int> l2nh = FindL2Path(currentId, neighborId, nodes, cables);
            if (l2nh.empty()) {
                result.reason = "VLAN mismatch — no L2 path to " + route.nextHop;
                return result;
            }

            {
                HopDecision hd;
                hd.nodeId     = currentId;
                hd.nodeLabel  = cur->label;
                if      (route.src == ROUTE_STATIC)  hd.routeType = "S";
                else if (route.src == ROUTE_OSPF)    hd.routeType = "O";
                else if (route.src == ROUTE_OSPF_IA) hd.routeType = "O IA";
                else if (route.src == ROUTE_BGP)     hd.routeType = "B";
                else                                  hd.routeType = "?";
                hd.destPrefix = route.dest;
                hd.nextHopIp  = route.nextHop;
                // outPort: first hop in the L2 path (may differ from route.outPort for static routes)
                if (l2nh.size() >= 2) {
                    const Cable* c = FindCableL2(cables, l2nh[0], l2nh[1]);
                    if (c) hd.outPort = (c->fromId == l2nh[0]) ? c->fromPort : c->toPort;
                }
                // MPLS label operation
                if (cur->ldpEnabled) {
                    auto it = cur->lfib.find(NetworkAddress(route.dest));
                    if (it != cur->lfib.end()) {
                        uint32_t nextOut = it->second.outLabel;
                        if (currentLabel == 0) {
                            hd.labelOp    = LABEL_PUSH;
                            hd.inLabel    = 0;
                            hd.outLabel   = nextOut;
                            currentLabel  = nextOut;
                        } else if (nextOut == MPLS_IMPLICIT_NULL) {
                            hd.labelOp    = LABEL_POP;
                            hd.inLabel    = currentLabel;
                            hd.outLabel   = 0;
                            currentLabel  = 0;
                        } else {
                            hd.labelOp    = LABEL_SWAP;
                            hd.inLabel    = currentLabel;
                            hd.outLabel   = nextOut;
                            currentLabel  = nextOut;
                        }
                    } else if (currentLabel != 0) {
                        currentLabel = 0;
                    }
                } else if (currentLabel != 0) {
                    currentLabel = 0;
                }
                result.hops.push_back(hd);
            }

            // Intermediate switch hops (l2nh[1] .. l2nh[n-2])
            {
                int frameVlan = 0;
                for (int pi = 1; pi + 1 < (int)l2nh.size(); ++pi) {
                    int stepId   = l2nh[pi];
                    int nextStId = l2nh[pi + 1];
                    const DeviceNode* stepNode = FindNode(nodes, stepId);
                    const DeviceNode* nextNode = FindNode(nodes, nextStId);
                    const Cable* cab = FindCableL2(cables, stepId, nextStId);
                    int outPort = -1, inPort = -1;
                    if (cab) {
                        outPort = (cab->fromId == stepId)   ? cab->fromPort : cab->toPort;
                        inPort  = (cab->fromId == nextStId) ? cab->fromPort : cab->toPort;
                    }
                    int prevFrameVlan = frameVlan;
                    if (nextNode && nextNode->type == SWITCH && inPort >= 0) {
                        const VlanPortConfig& inp = nextNode->vlanPorts[inPort];
                        if (inp.mode == VLAN_ACCESS) frameVlan = inp.accessVlan;
                    } else {
                        frameVlan = 0;
                    }
                    bool trunkEgress = stepNode && stepNode->type == SWITCH
                                       && outPort >= 0
                                       && stepNode->vlanPorts[outPort].mode == VLAN_TRUNK;
                    HopDecision swHd;
                    swHd.nodeId     = stepId;
                    swHd.nodeLabel  = stepNode ? stepNode->label : "";
                    swHd.routeType  = "SW";
                    swHd.destPrefix = route.dest;
                    swHd.nextHopIp  = nextNode ? nextNode->label : "";
                    swHd.outPort    = outPort;
                    swHd.vlanTag    = trunkEgress ? prevFrameVlan : 0;
                    result.hops.push_back(swHd);
                    result.path.push_back(stepId);
                    visited.insert(stepId);
                }
            }

            visited.insert(neighborId);
            result.path.push_back(neighborId);
            currentId = neighborId;
            matched   = true;
            break;
```

- [ ] **Step 6: Build and verify compile**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | head -30
```

Expected: clean compile.

- [ ] **Step 7: Manual smoke test — RoaS forwarding**

Run the game (`./PacketPath`), load level 10 (press `0` on keyboard — see note below; level 10 won't be accessible via number key yet, but you can manually set currentLevel=9 in main.cpp for testing). Alternatively, create a debug route in a simpler level. For now, verify that existing levels 1–9 still work — no regressions in OSPF, BGP, or static routing.

- [ ] **Step 8: Commit**

```bash
git add src/SimulationEngine.cpp
git commit -m "feat: FindNodeOwningIp; FindL2Path startVlan; subinterface-aware forwarding"
```

---

## Task 4: Config Panel — "Sub" Tab Declaration

**Files:**
- Modify: `src/ConfigPanel.h`
- Modify: `src/ConfigPanel.cpp`

- [ ] **Step 1: Add `TAB_SUB` to `PanelTab` enum in ConfigPanel.h**

Change:

```cpp
enum PanelTab { TAB_CONFIG, TAB_ROUTES, TAB_ARP, TAB_OSPF, TAB_MPLS, TAB_BGP, TAB_VLAN };
```

To:

```cpp
enum PanelTab { TAB_CONFIG, TAB_ROUTES, TAB_ARP, TAB_OSPF, TAB_MPLS, TAB_BGP, TAB_VLAN, TAB_SUB };
```

- [ ] **Step 2: Add sub-tab PanelState fields to ConfigPanel.h**

In `PanelState`, after `vlanPortBuf`, add:

```cpp
    // Sub (subinterface) tab state
    int         subFormPort    = 0;    // selected parent port for add form (0-3)
    int         subActiveField = -1;   // 0=VLAN field, 1=IP field, -1=none
    std::string subVlanBuf;            // digit buffer for VLAN ID
    std::string subIpBuf;              // buffer for IP/CIDR entry
```

- [ ] **Step 3: Add sub-tab rect helper declarations to ConfigPanel.h**

After `PnlVlanPortIdRect`, add:

```cpp
Rectangle PnlSubTabRect();
Rectangle PnlSubPortBtnRect(int port);   // 4 buttons, port 0-3
Rectangle PnlSubVlanFieldRect();
Rectangle PnlSubIpFieldRect();
Rectangle PnlSubAddBtnRect();
Rectangle PnlSubRowDeleteRect(int rowIdx);
```

- [ ] **Step 4: Update `PnlTabW()` in ConfigPanel.cpp for 8 tabs**

Change:

```cpp
float PnlTabW() { return (PANEL_W - 24.0f - 6.0f * 4.0f) / 7.0f; }
```

To:

```cpp
float PnlTabW() { return (PANEL_W - 24.0f - 7.0f * 4.0f) / 8.0f; }
```

`PANEL_W=280`: new tab width = (280 − 24 − 28) / 8 = **28.5 px**.

- [ ] **Step 5: Add `PnlSubTabRect()` to ConfigPanel.cpp**

Add immediately after `PnlVlanTabRect`:

```cpp
Rectangle PnlSubTabRect() {
    return {(float)(CANVAS_W + 12) + 7.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
```

- [ ] **Step 6: Add sub-tab layout rect helpers to ConfigPanel.cpp**

```cpp
// Sub-tab layout — list rows start at y=148, add-form below y=380
static const int SUB_ROW_Y0   = 148;
static const int SUB_ROW_H    = 22;
static const int SUB_FORM_Y0  = 380;

Rectangle PnlSubPortBtnRect(int port) {
    // Four buttons side-by-side: port 0–3
    float btnW = 28.0f, gap = 4.0f;
    float x0 = (float)(CANVAS_W + 64);
    return {x0 + port * (btnW + gap), (float)(SUB_FORM_Y0), btnW, 22.0f};
}
Rectangle PnlSubVlanFieldRect() {
    return {(float)(CANVAS_W + 64), (float)(SUB_FORM_Y0 + 30), 60.0f, 22.0f};
}
Rectangle PnlSubIpFieldRect() {
    return {(float)(CANVAS_W + 64), (float)(SUB_FORM_Y0 + 60), (float)(PANEL_W - 76), 22.0f};
}
Rectangle PnlSubAddBtnRect() {
    return {(float)(CANVAS_W + 12), (float)(SUB_FORM_Y0 + 92), (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlSubRowDeleteRect(int rowIdx) {
    return {(float)(CANVAS_W + PANEL_W - 22),
            (float)(SUB_ROW_Y0 + rowIdx * SUB_ROW_H + 4), 16.0f, 14.0f};
}
```

These constants (`SUB_ROW_Y0`, `SUB_ROW_H`, `SUB_FORM_Y0`) need to be visible to `NetworkCanvas.cpp`. Move them to `NetworkCanvas.h` as `static const int` values (Task 5 step 1 handles this).

- [ ] **Step 7: Build and verify compile**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | head -30
```

Expected: clean compile. The SUB tab isn't rendered yet.

- [ ] **Step 8: Commit**

```bash
git add src/ConfigPanel.h src/ConfigPanel.cpp
git commit -m "feat: TAB_SUB enum, PanelState sub fields, rect helpers, 8-tab PnlTabW"
```

---

## Task 5: Canvas — DrawSubIfaceTab + Tab Bar + Router Port Indicators

**Files:**
- Modify: `src/NetworkCanvas.h`
- Modify: `src/NetworkCanvas.cpp`

- [ ] **Step 1: Add layout constants and `DrawSubIfaceTab` declaration to NetworkCanvas.h**

In `NetworkCanvas.h`, after the `RTE_BTN_Y` constant, add:

```cpp
// ── Subinterface tab layout ───────────────────────────────────────────────
static const int SUB_ROW_Y0   = 148;
static const int SUB_ROW_H    = 22;
static const int SUB_FORM_Y0  = 380;
```

Remove the temporary `static const int` definitions from `ConfigPanel.cpp` (Step 6 of Task 4) — replace them with `#include "NetworkCanvas.h"` references since `ConfigPanel.cpp` already includes `NetworkCanvas.h`.

Add the function declaration after `DrawVlanTab`:

```cpp
void DrawSubIfaceTab(const DeviceNode* n, const PanelState& ps);
```

- [ ] **Step 2: Implement `DrawSubIfaceTab` in NetworkCanvas.cpp**

Add after `DrawVlanTab`. The function guards for null or non-router:

```cpp
void DrawSubIfaceTab(const DeviceNode* n, const PanelState& ps) {
    if (!n || n->type != ROUTER) {
        int tw = MeasureText("Select a router to configure subinterfaces.", 10);
        DrawText("Select a router to configure subinterfaces.",
                 CANVAS_W + (PANEL_W - tw) / 2, 200, 10, Color{100, 116, 139, 255});
        return;
    }

    // ── Existing subinterface list ─────────────────────────────────────────
    DrawLine(CANVAS_W + 12, 128, CANVAS_W + PANEL_W - 12, 128, Color{51, 65, 85, 255});
    DrawText("Subinterfaces", CANVAS_W + 12, 132, 10, Color{148, 163, 184, 255});

    if (n->subIfaces.empty()) {
        DrawText("(none configured)", CANVAS_W + 20, SUB_ROW_Y0, 10, Color{100, 116, 139, 255});
    } else {
        for (int i = 0; i < (int)n->subIfaces.size(); ++i) {
            const SubInterface& si = n->subIfaces[i];
            int y = SUB_ROW_Y0 + i * SUB_ROW_H;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Gi0/%d.%d  V%d  %s",
                          si.parentPort, si.vlanId, si.vlanId, si.ip.c_str());
            DrawText(buf, CANVAS_W + 12, y + 4, 10, Color{203, 213, 225, 255});

            // Delete button [×]
            Rectangle delR = PnlSubRowDeleteRect(i);
            DrawRectangleRec(delR, Color{127, 29, 29, 255});
            DrawText("×", (int)(delR.x + 3), (int)(delR.y + 1), 11, WHITE);
        }
    }

    // ── Add subinterface form ──────────────────────────────────────────────
    DrawLine(CANVAS_W + 12, SUB_FORM_Y0 - 12,
             CANVAS_W + PANEL_W - 12, SUB_FORM_Y0 - 12, Color{51, 65, 85, 255});
    DrawText("Add Subinterface", CANVAS_W + 12, SUB_FORM_Y0 - 8, 10, Color{148, 163, 184, 255});

    // Port selector buttons (0-3)
    DrawText("Port:", CANVAS_W + 12, SUB_FORM_Y0 + 4, 10, Color{148, 163, 184, 255});
    for (int p = 0; p < PORTS_PER_NODE; ++p) {
        Rectangle btn = PnlSubPortBtnRect(p);
        bool sel = (ps.subFormPort == p);
        DrawRectangleRec(btn, sel ? Color{234, 88, 12, 255} : Color{30, 41, 59, 255});
        DrawRectangleLinesEx(btn, 1.0f, Color{51, 65, 85, 255});
        char lbl[4]; std::snprintf(lbl, sizeof(lbl), "%d", p);
        int tw = MeasureText(lbl, 10);
        DrawText(lbl, (int)(btn.x + (btn.width - tw) * 0.5f),
                 (int)(btn.y + 5), 10, WHITE);
    }

    // VLAN field
    DrawText("VLAN:", CANVAS_W + 12, SUB_FORM_Y0 + 34, 10, Color{148, 163, 184, 255});
    DrawTextField(PnlSubVlanFieldRect(), nullptr, "10",
                  ps.subVlanBuf, ps.subActiveField == 0,
                  !ps.subVlanBuf.empty());

    // IP field
    DrawText("IP:", CANVAS_W + 12, SUB_FORM_Y0 + 64, 10, Color{148, 163, 184, 255});
    DrawTextField(PnlSubIpFieldRect(), nullptr, "10.10.0.1/24",
                  ps.subIpBuf, ps.subActiveField == 1,
                  ValidateIP(ps.subIpBuf));

    // Add button
    Rectangle addBtn = PnlSubAddBtnRect();
    bool canAdd = !ps.subVlanBuf.empty() && ValidateIP(ps.subIpBuf);
    DrawRectangleRec(addBtn, canAdd ? Color{234, 88, 12, 200} : Color{30, 41, 59, 255});
    DrawRectangleLinesEx(addBtn, 1.0f, Color{51, 65, 85, 255});
    int tw2 = MeasureText("Add Subinterface", 10);
    DrawText("Add Subinterface",
             (int)(addBtn.x + (addBtn.width - tw2) * 0.5f),
             (int)(addBtn.y + 7), 10, WHITE);
}
```

- [ ] **Step 3: Update the tab bar in DrawPanel to render 8 tabs**

In `DrawPanel` (NetworkCanvas.cpp), find the tab rendering block. Currently it draws 7 tab labels. Update it to draw 8, adding "Sub" at position 7. Labels at font size 10:

Tab labels (in order): `"Cfg"`, `"Rte"`, `"Arp"`, `"Osp"`, `"Mpl"`, `"Bgp"`, `"VLN"`, `"Sub"`

Tab rect functions (in order):
`PnlConfigTabRect()`, `PnlRoutesTabRect()`, `PnlArpTabRect()`, `PnlOspfTabRect()`, `PnlMplsTabRect()`, `PnlBgpTabRect()`, `PnlVlanTabRect()`, `PnlSubTabRect()`

Active tab underline color for `TAB_SUB`: orange `Color{234, 88, 12, 255}` (matches router/subinterface color).

The tab dispatch block (currently the `if/else if` chain at the bottom of `DrawPanel`) needs a new arm:

```cpp
else if (ps.activeTab == TAB_SUB) DrawSubIfaceTab(n, ps);
```

- [ ] **Step 4: Add subinterface port indicator to `DrawDeviceNode`**

In `DrawDeviceNode`, in the port loop (after the SWITCH block that shows "T"/VLAN labels), add:

```cpp
        if (n.type == ROUTER) {
            for (const auto& si : n.subIfaces) {
                if (si.parentPort != i) continue;
                char lbl[8];
                std::snprintf(lbl, sizeof(lbl), ".%d", si.vlanId);
                int lw = MeasureText(lbl, 8);
                DrawText(lbl, (int)(pp.x - lw * 0.5f), (int)(pp.y + PORT_RADIUS + 2),
                         8, Color{249, 115, 22, 255});
                break;  // show first subinterface's VLAN ID; multiple subifs on one port merge
            }
        }
```

- [ ] **Step 5: Build and verify compile**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | head -30
```

Expected: clean compile. Run the game; click a router; confirm "Sub" tab appears; confirm existing tabs still render correctly.

- [ ] **Step 6: Commit**

```bash
git add src/NetworkCanvas.h src/NetworkCanvas.cpp
git commit -m "feat: DrawSubIfaceTab, 8-tab bar, router port subinterface indicators"
```

---

## Task 6: main.cpp — Click Handlers, Text Input, Level Count

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Update level count from 9 to 10**

There are three occurrences: the level loading loop guard, the "next level" button condition, and the status bar label. Find and update all three:

1. `k <= 9` → `k <= 10` (level preload loop)
2. `currentLevel < 9` (appears twice — for "Next Level" button visibility and overlay arrow) → `currentLevel < 10` (both occurrences)
3. `"1-9=Level"` → `"1-9,0=Level"` in the status bar hint (press `0` for level 10; raylib maps `KEY_ZERO` to key code 48)

For the level-key input block, add handling for `KEY_ZERO` → load level 10:

```cpp
        if      (IsKeyPressed(KEY_ONE))   { currentLevel = 1;  ApplyLevel(...); }
        else if (IsKeyPressed(KEY_TWO))   { currentLevel = 2;  ApplyLevel(...); }
        // ... (keys 3-9 already present) ...
        else if (IsKeyPressed(KEY_ZERO))  { currentLevel = 10; ApplyLevel(...); }
```

And the path string:
```cpp
        std::string lpath = "levels/level_" + (currentLevel < 10 ? "0" : "") +
                            std::to_string(currentLevel) + ".json";
```
Wait — the existing levels use `level_09.json` format (two digits). Check:

```bash
ls /Users/tweaver/Developer/GitRepos/Packet-Path/levels/
```

If the naming is `level_09.json`, then `level_10.json` already matches (two digits). The path format is likely `"levels/level_" + std::to_string(currentLevel < 10 ? 0 : "") + ...`. Find the exact string format in main.cpp and ensure level 10 loads `levels/level_10.json`.

- [ ] **Step 2: Add TAB_SUB click handler**

Find the TAB_BGP click block in the mouse-click handler and add after it:

```cpp
        else if (CheckCollisionPointRec(screenMouse, PnlSubTabRect())) {
            ps.activeTab        = TAB_SUB;
            ps.activeField      = -1;
            ps.activeRouteField = -1;
            ps.activePortAreaField = -1;
            ps.bgpAsnField      = -1;
            ps.vlanPortField    = -1;
            ps.subActiveField   = -1;
        }
```

- [ ] **Step 3: Add Sub tab port button click handler**

After the TAB_SUB tab-click block, add handling for the port selector buttons and delete buttons when TAB_SUB is active and a router is selected:

```cpp
        else if (ps.activeTab == TAB_SUB && selectedId != -1) {
            DeviceNode* n = FindNodeMut(nodes, selectedId);
            if (n && n->type == ROUTER) {
                // Port selector buttons
                for (int p = 0; p < PORTS_PER_NODE; ++p) {
                    if (CheckCollisionPointRec(screenMouse, PnlSubPortBtnRect(p))) {
                        ps.subFormPort = p;
                    }
                }
                // VLAN field
                if (CheckCollisionPointRec(screenMouse, PnlSubVlanFieldRect())) {
                    ps.subActiveField = 0;
                }
                // IP field
                if (CheckCollisionPointRec(screenMouse, PnlSubIpFieldRect())) {
                    ps.subActiveField = 1;
                }
                // Add button
                if (CheckCollisionPointRec(screenMouse, PnlSubAddBtnRect())) {
                    int vlan = std::atoi(ps.subVlanBuf.c_str());
                    if (vlan >= 1 && vlan <= 4094 && ValidateIP(ps.subIpBuf)) {
                        n->subIfaces.push_back({ps.subFormPort, vlan, ps.subIpBuf});
                        ps.subVlanBuf.clear();
                        ps.subIpBuf.clear();
                        ps.subActiveField = -1;
                    }
                }
                // Delete buttons
                for (int i = 0; i < (int)n->subIfaces.size(); ++i) {
                    if (CheckCollisionPointRec(screenMouse, PnlSubRowDeleteRect(i))) {
                        n->subIfaces.erase(n->subIfaces.begin() + i);
                        break;
                    }
                }
            }
        }
```

Note: `FindNodeMut` is the non-const version (check if it already exists in main.cpp; if not, it returns `DeviceNode*` by finding by ID in the mutable `nodes` vector).

- [ ] **Step 4: Add sub-tab text input handler**

Find the keyboard text-input section (currently has blocks for `activeField`, `activeRouteField`, `activePortAreaField`, `bgpAsnField`, `vlanPortField`). Add after the `vlanPortField` block:

```cpp
        else if (ps.subActiveField != -1 && selectedId != -1) {
            if (ps.subActiveField == 0) {
                // VLAN field: digits only, max 4 chars
                UpdateTextField(ps.subVlanBuf, 4);
                // strip non-digits
                ps.subVlanBuf.erase(
                    std::remove_if(ps.subVlanBuf.begin(), ps.subVlanBuf.end(),
                                   [](char c){ return !std::isdigit(c); }),
                    ps.subVlanBuf.end());
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB))
                    ps.subActiveField = 1;
                if (IsKeyPressed(KEY_ESCAPE))
                    ps.subActiveField = -1;
            } else if (ps.subActiveField == 1) {
                // IP field: allow digits, dots, slash
                UpdateTextField(ps.subIpBuf, 18);
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE))
                    ps.subActiveField = -1;
            }
        }
```

- [ ] **Step 5: Add sub-form field resets on tab switch**

In every existing `ps.activeTab = TAB_XXX` assignment block, add:
```cpp
ps.subActiveField = -1;
```

Also in the TAB_SUB click handler from Step 2 (already included `ps.subActiveField = -1`).

- [ ] **Step 6: Add sub-form reset on selection change**

Find the block that resets fields when `selectedId` changes (currently resets `activeField`, `activeRouteField`, etc.). Add:

```cpp
ps.subActiveField = -1;
// Note: preserve subFormPort and subVlanBuf/subIpBuf (student may be mid-entry)
```

- [ ] **Step 7: Add ESC handler for subActiveField**

In the ESC key handler block, add after the `vlanPortField` ESC arm:

```cpp
        else if (ps.subActiveField != -1) {
            ps.subActiveField = -1;
        }
```

- [ ] **Step 8: Build and run full smoke test**

```bash
cd /Users/tweaver/Developer/GitRepos/Packet-Path && make 2>&1 | head -20
./PacketPath
```

Full test sequence:
1. Press `0` → level 10 loads (PC-A, PC-B, SW1, R1 on canvas)
2. Click SW1 → VLAN tab → set port 2 to trunk
3. Click R1 → Sub tab → add port=0, VLAN=10, IP=10.10.0.1/24 → click "Add Subinterface" → entry appears in list
4. Add port=0, VLAN=20, IP=10.20.0.1/24 → second entry appears
5. Click PC-A → ping 10.20.0.2 (however the ping is triggered in the game — check main.cpp for the send-packet shortcut)
6. Packet should animate: PC-A → SW1 → R1 → SW1 → PC-B → win overlay appears

Regression tests:
- Level 1–9 still work (especially levels with OSPF and BGP)
- VLAN tab still works on switches (level 9)
- Delete subinterface works (click × next to a sub entry)
- ESC in a sub text field deactivates it without clearing the buffer

- [ ] **Step 9: Commit**

```bash
git add src/main.cpp
git commit -m "feat: Sub tab click handlers, subinterface text input, level count 1-10"
```

---

## Post-Implementation Self-Review Checklist

After all tasks are complete:

- [ ] **Spec coverage:**
  - SubInterface struct with parentPort, vlanId, ip ✓
  - UI in config panel (new Sub tab) ✓
  - Forwarding engine with subVlanId egress tagging ✓
  - Forwarding engine with switch-traversal next-hop resolution ✓
  - Router canvas indicator (.vlanId) ✓
  - Level 10 RoaS scenario ✓

- [ ] **Regression check:** Run levels 1–9 and verify no change in behavior for OSPF, BGP, static routing, VLAN forwarding.

- [ ] **Type consistency:**
  - `SubInterface.parentPort` matches `PnlSubPortBtnRect(int port)` index
  - `RouteEntry.subVlanId` is used as `startVlan` in `FindL2Path` call
  - `PanelState.subFormPort` used in port button highlight and "Add" action
  - `SUB_ROW_Y0`, `SUB_ROW_H`, `SUB_FORM_Y0` defined in `NetworkCanvas.h`, referenced in `ConfigPanel.cpp` and `NetworkCanvas.cpp`

---

**Plan complete.** Two execution options:

**1. Subagent-Driven (recommended)** — Fresh subagent per task, two-stage review (spec compliance then code quality) after each.

**2. Inline Execution** — Execute tasks in this session using executing-plans skill, batch with checkpoints.

Which approach?
