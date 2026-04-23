# Phase 3a: Routing Table + Forwarding Engine — Design Spec

**Date:** 2026-04-23  
**Branch:** `phase-3a-routing-table` (to be created from `main`)  
**Builds on:** Phase 2 (`main` @ `fd2720a`, 675 lines, `src/main.cpp`)

---

## Goal

Give every simulated device a real routing table: connected routes auto-derived from configured interface IPs, user-editable static routes in a new "Routes" panel tab, and a pure-logic forwarding engine (longest-prefix match) that Phase 3b will drive for packet animation.

Fix the deferred Phase 2 quality issue (hoverItem computed in draw phase) as housekeeping before the main work.

---

## Design Decisions (locked)

| Decision | Choice | Rationale |
|---|---|---|
| Panel layout | Tab-based (Config / Routes) | Scales to Phase 4+ without layout rework |
| Connected routes | Auto-derived on-the-fly from portIp + mgmtIp | Always reflects current IP config; never stale |
| Static route fields | Destination prefix + next-hop IP (no metric) | Sufficient for Phase 3a; metric added in Phase 4 |
| Add-route UX | Always-visible form at bottom of Routes tab | Simpler state; reuses existing DrawTextField |
| Forwarding engine | Pure logic function, returns path + result | Phase 3b animates; Phase 3a only computes |
| File structure | Single `src/main.cpp` | Packets not yet working; split deferred to Phase 3b/4 |

---

## Architecture

### What changes

| Symbol | Change |
|---|---|
| `UpdateContextMenuHover` | **New** — moves hoverItem computation to input phase (Fix #3) |
| `DrawContextMenu` | **Modified** — becomes `const`, no longer sets hoverItem |
| `RouteSource` | **New enum** — `ROUTE_CONNECTED`, `ROUTE_STATIC` |
| `RouteEntry` | **New struct** — dest, nextHop, outPort, src |
| `DeviceNode` | **Extended** — add `staticRoutes` vector |
| `NetworkAddress` | **New** — "10.0.1.5/24" → "10.0.1.0/24" |
| `IpInSubnet` | **New** — returns true if an IP falls within a subnet |
| `GetRoutingTable` | **New** — connected routes + staticRoutes combined |
| `PanelTab` | **New enum** — `TAB_CONFIG`, `TAB_ROUTES` |
| `PanelState` | **Extended** — activeTab, newRouteDest, newRouteNext, activeRouteField |
| `DrawPanel` | **Modified** — tab header + dispatch to Config or Routes tab renderer |
| `DrawConfigTab` | **New** (extracted from DrawPanel) — existing hostname/IP fields unchanged |
| `DrawRoutesTab` | **New** — routing table rows + delete buttons + add-form |
| `UpdateRoutesTab` | **New** — keyboard input for add-form fields |
| `ForwardResult` | **New struct** — success, path (node ID list), reason string |
| `SimulateForward` | **New** — longest-prefix match forwarding engine |

### Estimated size

675 lines (Phase 2) + ~280 new lines = **~955 lines** after Phase 3a.

---

## Data Model

### RouteEntry

```cpp
enum RouteSource { ROUTE_CONNECTED, ROUTE_STATIC };

struct RouteEntry {
    std::string dest;     // network prefix e.g. "10.0.0.0/24" or "0.0.0.0/0"
    std::string nextHop;  // "direct" for connected; "10.0.0.1" for static
    int         outPort;  // 0-3 for port-connected; -1 for mgmt or static
    RouteSource src;      // ROUTE_CONNECTED or ROUTE_STATIC
};
```

### DeviceNode extension

```cpp
struct DeviceNode {
    // ... existing fields unchanged ...
    std::vector<RouteEntry> staticRoutes;  // user-added; persists with node
};
```

Connected routes are **never stored in DeviceNode** — they are computed on-the-fly by `GetRoutingTable()` so they always reflect current `portIp[]` and `mgmtIp` values.

---

## Include Change

Add `#include <cstdint>` to the includes block in `src/main.cpp` (needed for `uint32_t` in `NetworkAddress` and `IpInSubnet`).

---

## New Free Functions (above `main()`)

### NetworkAddress

Strips host bits from a CIDR string.

```cpp
std::string NetworkAddress(const std::string& cidr) {
    int a, b, c, d, prefix;
    if (std::sscanf(cidr.c_str(), "%d.%d.%d.%d/%d", &a, &b, &c, &d, &prefix) != 5)
        return cidr;
    uint32_t ip   = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
                    ((uint32_t)c << 8)  |  (uint32_t)d;
    uint32_t mask = prefix ? (~0u << (32 - prefix)) : 0u;
    uint32_t net  = ip & mask;
    return std::to_string((net >> 24) & 0xFF) + "." +
           std::to_string((net >> 16) & 0xFF) + "." +
           std::to_string((net >>  8) & 0xFF) + "." +
           std::to_string( net        & 0xFF) + "/" +
           std::to_string(prefix);
}
```

### IpInSubnet

Returns true if a plain IP ("10.0.1.5") falls within a subnet ("10.0.1.0/24").

```cpp
bool IpInSubnet(const std::string& ip, const std::string& subnet) {
    int a1, b1, c1, d1, a2, b2, c2, d2, prefix;
    if (std::sscanf(ip.c_str(),     "%d.%d.%d.%d",     &a1, &b1, &c1, &d1) != 4) return false;
    if (std::sscanf(subnet.c_str(), "%d.%d.%d.%d/%d",  &a2, &b2, &c2, &d2, &prefix) != 5) return false;
    uint32_t ipBits  = ((uint32_t)a1 << 24) | ((uint32_t)b1 << 16) |
                       ((uint32_t)c1 <<  8) |  (uint32_t)d1;
    uint32_t netBits = ((uint32_t)a2 << 24) | ((uint32_t)b2 << 16) |
                       ((uint32_t)c2 <<  8) |  (uint32_t)d2;
    uint32_t mask    = prefix ? (~0u << (32 - prefix)) : 0u;
    return (ipBits & mask) == (netBits & mask);
}
```

### ValidateIPOnly

Validates a plain IP address (no prefix) — used for the next-hop field.

```cpp
bool ValidateIPOnly(const std::string& ip) {
    if (ip.empty()) return false;
    int a, b, c, d, consumed = 0;
    std::sscanf(ip.c_str(), "%d.%d.%d.%d%n", &a, &b, &c, &d, &consumed);
    return (consumed == (int)ip.size() &&
            a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
            c >= 0 && c <= 255 && d >= 0 && d <= 255);
}
```

### PrefixLen

Extracts prefix length from a CIDR string — used for longest-prefix-match sorting.

```cpp
int PrefixLen(const std::string& cidr) {
    int prefix = 0;
    const char* slash = std::strchr(cidr.c_str(), '/');
    if (slash) prefix = std::atoi(slash + 1);
    return prefix;
}
```

### GetRoutingTable

Returns connected routes (derived from portIp[] and mgmtIp) followed by staticRoutes.

```cpp
std::vector<RouteEntry> GetRoutingTable(const DeviceNode& n) {
    std::vector<RouteEntry> table;
    // Connected: management IP
    if (ValidateIP(n.mgmtIp))
        table.push_back({NetworkAddress(n.mgmtIp), "direct", -1, ROUTE_CONNECTED});
    // Connected: port IPs
    for (int i = 0; i < PORTS_PER_NODE; ++i)
        if (ValidateIP(n.portIp[i]))
            table.push_back({NetworkAddress(n.portIp[i]), "direct", i, ROUTE_CONNECTED});
    // Static routes
    for (const auto& r : n.staticRoutes)
        table.push_back(r);
    return table;
}
```

---

## PanelState Changes

```cpp
enum PanelTab { TAB_CONFIG, TAB_ROUTES };

struct PanelState {
    int         activeField      = -1;          // Config tab: -1=none 0=label 1=mgmtIp 2-5=portIp[0-3]
    PanelTab    activeTab        = TAB_CONFIG;
    std::string newRouteDest;                   // Routes tab add-form: destination field
    std::string newRouteNext;                   // Routes tab add-form: next-hop field
    int         activeRouteField = -1;          // -1=none, 0=newRouteDest, 1=newRouteNext
};
```

**Tab switching rules:**
- Clicking the tab header switches `activeTab` and clears `activeField` / `activeRouteField`
- Selecting a new device resets `activeTab = TAB_CONFIG`
- `activeField` is only valid when `activeTab == TAB_CONFIG`
- `activeRouteField` is only valid when `activeTab == TAB_ROUTES`

---

## Panel Rendering

### Tab header (drawn at top of every panel render)

Two buttons side by side: `[Config]` and `[Routes]`. Active tab has a highlighted bottom border (same blue as text field active border). Clicking switches tab and clears focus.

### Config tab

Extracted verbatim from current `DrawPanel` — hostname, mgmt IP, 4 port IP rows. No changes to existing behavior.

### Routes tab

```
Type  Destination      Next-Hop        Via
C     10.0.0.0/24      direct          Gi0/0
C     10.0.1.0/24      direct          Gi0/1
S     0.0.0.0/0        10.0.0.254      —        [×]
────────────────────────────────────────────
Destination:  [________________]
Next-Hop:     [________________]
              [    Add Route   ]
```

- **C rows** (green) — connected, no delete button
- **S rows** (blue) — static, `[×]` delete button on right; clicking removes from `staticRoutes`
- If routing table is empty: dim placeholder text "No routes configured"
- **Add form** always visible at bottom:
  - `Destination` field — validates as network prefix (reuses `ValidateIP`)
  - `Next-Hop` field — validates as plain IP (uses `ValidateIPOnly`)
  - `[Add]` button — active (highlighted) only when both fields are valid; clicking appends `RouteEntry` to `n->staticRoutes` and clears both fields
- `KEY_TAB` cycles focus between the two add-form fields (only when `activeTab == TAB_ROUTES`)
- **Delete click:** handled in the panel click-to-focus block in `main()` — when `activeTab == TAB_ROUTES` and `!inCanvas`, check if the click lands on a `[×]` button rect for a static route row; if so, erase that entry from `n->staticRoutes`
- **Add click:** same block — if click lands on the `[Add]` button rect and both fields are valid, append `RouteEntry` and clear `newRouteDest`/`newRouteNext`/`activeRouteField`

---

## Fix #3: hoverItem to Input Phase

### New function

```cpp
void UpdateContextMenuHover(ContextMenu& menu, Vector2 screenMouse) {
    if (!menu.visible) { menu.hoverItem = -1; return; }

    static const char* nodeItems[]   = {"Rename", "Delete", nullptr};
    static const char* cableItems[]  = {"Delete Cable", nullptr};
    static const char* canvasItems[] = {"Add PC Here", "Add Router Here",
                                        "Add Switch Here", "Reset View", nullptr};
    const char** items = nullptr;
    if      (menu.ctx == CTX_NODE)   items = nodeItems;
    else if (menu.ctx == CTX_CABLE)  items = cableItems;
    else if (menu.ctx == CTX_CANVAS) items = canvasItems;
    else { menu.hoverItem = -1; return; }

    int count = 0;
    while (items[count]) ++count;

    float h = (float)(count * MENU_ITEM_H + 8);
    float x = std::min(menu.screenPos.x, (float)(CANVAS_W - CONTEXT_MENU_W - 4));
    float y = std::min(menu.screenPos.y, (float)(SCREEN_H - (int)h - 4));

    menu.hoverItem = -1;
    for (int i = 0; i < count; ++i) {
        Rectangle ir = {x + 4, y + 4 + (float)(i * MENU_ITEM_H),
                        (float)(CONTEXT_MENU_W - 8), (float)MENU_ITEM_H};
        if (CheckCollisionPointRec(screenMouse, ir)) { menu.hoverItem = i; break; }
    }
}
```

### DrawContextMenu signature change

```cpp
void DrawContextMenu(const ContextMenu& menu, Vector2 screenMouse)
```

The `screenMouse` parameter is kept for future use (tooltip highlighting etc.) but `hoverItem` is read, not written. The draw function no longer sets `menu.hoverItem = -1` at the top.

### Call site in main()

`UpdateContextMenuHover(contextMenu, screenMouse)` is called **once per frame in the input phase**, immediately before the LMB pressed block. `DrawContextMenu` is unchanged in position (after `DrawPanel`).

---

## Forwarding Engine

### ForwardResult

```cpp
struct ForwardResult {
    bool             success = false;
    std::vector<int> path;    // node IDs from src to dst (inclusive)
    std::string      reason;  // "delivered" | "no route to X" | "loop detected"
};
```

### SimulateForward

```cpp
ForwardResult SimulateForward(int srcId, const std::string& destIp,
                              const std::vector<DeviceNode>& nodes,
                              const std::vector<Cable>& cables);
```

**Algorithm:**

1. Validate `destIp` with `ValidateIPOnly`. If invalid → `{false, {srcId}, "invalid destination"}`.
2. Start: `currentId = srcId`, `path = {srcId}`.
3. Each hop (max 16):
   a. Get `GetRoutingTable(*currentNode)`.
   b. Sort routes by prefix length descending (longest-prefix match).
   c. For each route in order — if `IpInSubnet(destIp, route.dest)`:
      - **Connected** (`"direct"`) → `success = true`, `reason = "delivered"`. Done.
      - **Static** → find the neighbor node reachable via `route.nextHop`:
        - Search cables from `currentId`; for each neighbor, check if any of its interface IPs' subnets contain `route.nextHop` using `IpInSubnet`.
        - If found: append neighbor ID to `path`, `currentId = neighborId`, continue loop.
        - If not found → `{false, path, "next-hop unreachable: " + route.nextHop}`. Done.
   d. No route matched → `{false, path, "no route to " + destIp}`. Done.
4. Exceeded 16 hops → `{false, path, "loop detected"}`.

---

## Acceptance Criteria

### Fix #3
- `DrawContextMenu` compiles with `const ContextMenu&` parameter — confirmed by the build
- `UpdateContextMenuHover` is called in the input section, before the LMB block
- Context menu hover highlight still works correctly

### Routing table display
- Select a Router/Switch/PC with configured interface IPs → Routes tab shows connected routes in green with "C" prefix
- mgmtIp (if valid) also appears as a connected route
- Changing an interface IP live (type in Config tab) updates the Routes tab immediately (derived on-the-fly)

### Static route editing
- Enter a valid destination prefix + valid next-hop IP → `[Add]` button activates
- Clicking `[Add]` appends route (blue "S" row), clears both fields
- Clicking `[×]` on a static route removes it
- Invalid fields (red border) keep `[Add]` inactive

### Forwarding engine
- `SimulateForward(pc1, "10.0.1.5", nodes, cables)` where PC1 has a default route to a router which has a connected route to 10.0.1.0/24 → `{true, [pc1id, routerid], "delivered"}`
- No matching route → `{false, [srcid], "no route to X"}`
- Next-hop not reachable via cables → `{false, [srcid], "next-hop unreachable: X"}`
- 17-hop loop → `{false, [...], "loop detected"}`

---

## Out of Scope for Phase 3a

- Packet animation (Phase 3b)
- Simulation trigger (Space / Run button) (Phase 3b)
- Log console (Phase 3b)
- ARP, ICMP, OSPF, BGP
- Route metrics / administrative distance
- Multi-file split (deferred until Phase 3b is complete)
