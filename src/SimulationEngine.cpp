#include "SimulationEngine.h"
#include <algorithm>
#include <unordered_set>
#include <queue>

// Finds the node whose portIp[i] matches `ip` (plain, no mask).
static const DeviceNode* FindNodeByIp(const std::vector<DeviceNode>& nodes,
                                       const std::string& ip)
{
    for (const auto& n : nodes)
        for (int p = 0; p < PORTS_PER_NODE; ++p) {
            auto s = n.portIp[p].find('/');
            std::string plain = (s != std::string::npos)
                                ? n.portIp[p].substr(0, s) : n.portIp[p];
            if (plain == ip) return &n;
        }
    return nullptr;
}

// Like FindNodeByIp but also checks subinterface IPs (needed for next-hop resolution).
static const DeviceNode* FindNodeOwningIp(const std::vector<DeviceNode>& nodes,
                                           const std::string& ip)
{
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

static const Cable* FindCableL2(const std::vector<Cable>& cables, int a, int b)
{
    for (const auto& c : cables)
        if ((c.fromId == a && c.toId == b) || (c.fromId == b && c.toId == a))
            return &c;
    return nullptr;
}

// VLAN-aware BFS through the L2 fabric (switches).
// Returns [srcId, …, dstId] or {} if blocked.
// frameVlan starts at 0 (untagged at L3 boundary); access ports assign it,
// trunk ports preserve it.
static std::vector<int> FindL2Path(int srcId, int dstId,
                                    const std::vector<DeviceNode>& nodes,
                                    const std::vector<Cable>& cables,
                                    int startVlan = 0)
{
    if (srcId == dstId) return {srcId};

    struct State {
        int              nodeId;
        int              vlan;    // frame's VLAN tag inside the L2 domain (0 = untagged)
        std::vector<int> path;
    };

    std::unordered_set<int> visited;
    std::queue<State>       q;
    q.push({srcId, startVlan, {srcId}});
    visited.insert(srcId);

    while (!q.empty()) {
        auto [curId, curVlan, curPath] = q.front();
        q.pop();

        const DeviceNode* cur = FindNode(nodes, curId);
        if (!cur) continue;

        for (const auto& cable : cables) {
            int myPort = -1, nextId = -1, nextPort = -1;
            if      (cable.fromId == curId) { myPort = cable.fromPort; nextId = cable.toId;   nextPort = cable.toPort; }
            else if (cable.toId   == curId) { myPort = cable.toPort;   nextId = cable.fromId; nextPort = cable.fromPort; }
            if (cable.broken) continue;
            if (nextId == -1 || visited.count(nextId)) continue;

            const DeviceNode* next = FindNode(nodes, nextId);
            if (!next) continue;

            // ── Egress VLAN check on current switch ──────────────────
            int frameVlan = curVlan;
            if (cur->type == SWITCH) {
                const VlanPortConfig& ep = cur->vlanPorts[myPort];
                if (ep.mode == VLAN_ACCESS) {
                    if (ep.accessVlan != frameVlan) continue;  // VLAN mismatch: blocked
                }
                // trunk: all VLANs pass
            }

            // ── Ingress VLAN assignment entering next switch ──────────
            if (next->type == SWITCH) {
                const VlanPortConfig& ip = next->vlanPorts[nextPort];
                if (ip.mode == VLAN_ACCESS)
                    frameVlan = ip.accessVlan;          // access: assign VLAN
                else
                    frameVlan = (curVlan != 0) ? curVlan : 1;  // trunk: keep tag
            }

            std::vector<int> newPath = curPath;
            newPath.push_back(nextId);

            if (nextId == dstId) return newPath;

            if (next->type == SWITCH) {
                visited.insert(nextId);
                q.push({nextId, frameVlan, newPath});
            }
        }
    }
    return {};  // unreachable or blocked
}

// ── Forwarding engine ─────────────────────────────────────────────────────
ForwardResult SimulateForward(int srcId, const std::string& destIp,
                              const std::vector<DeviceNode>& nodes,
                              const std::vector<Cable>& cables)
{
    if (!FindNode(nodes, srcId))
        return {false, {}, "source node not found", {}, {}};

    {
        const DeviceNode* srcNode = FindNode(nodes, srcId);
        if (srcNode && srcNode->crashed)
            return {false, {srcId},
                    srcNode->label + " is crashed \xe2\x80\x94 device offline", {}, {}};
    }

    if (!ValidateIPOnly(destIp))
        return {false, {srcId}, "invalid destination", {}, {}};

    static constexpr int MAX_HOPS = 16;

    ForwardResult result;
    result.path = {srcId};
    int currentId = srcId;
    std::unordered_set<int> visited = {srcId};

    uint32_t currentLabel = 0;

    for (int i = 0; i < MAX_HOPS; ++i) {
        const DeviceNode* cur = FindNode(nodes, currentId);
        if (!cur)         { result.reason = "node not found"; return result; }
        if (cur->crashed) { result.reason = cur->label + " is crashed \xe2\x80\x94 device offline"; return result; }

        auto table = GetRoutingTable(*cur);
        std::sort(table.begin(), table.end(), [](const RouteEntry& a, const RouteEntry& b) {
            return PrefixLen(a.dest) > PrefixLen(b.dest);
        });

        bool matched = false;
        for (const auto& route : table) {
            if (!IpInSubnet(destIp, route.dest)) continue;

            if (route.src == ROUTE_CONNECTED) {
                const DeviceNode* destNode = FindNodeByIp(nodes, destIp);

                if (!destNode || destNode->id == currentId) {
                    // No switch hop needed — direct delivery
                    HopDecision hd;
                    hd.nodeId     = currentId; hd.nodeLabel  = cur->label;
                    hd.routeType  = "C";       hd.destPrefix = route.dest;
                    hd.nextHopIp  = "delivered"; hd.outPort  = -1;
                    result.hops.push_back(hd);
                    result.success = true; result.reason = "delivered";
                    return result;
                }

                // L2 BFS — find path through switches, VLAN-aware
                std::vector<int> l2 = FindL2Path(currentId, destNode->id, nodes, cables,
                                                  route.subVlanId);

                if (l2.empty()) {
                    HopDecision hd;
                    hd.nodeId     = currentId; hd.nodeLabel  = cur->label;
                    hd.routeType  = "C";       hd.destPrefix = route.dest;
                    hd.nextHopIp  = "blocked";  hd.outPort   = -1;
                    result.hops.push_back(hd);
                    result.reason = "VLAN mismatch — switch port blocked this frame";
                    return result;
                }

                // Build path + hop decisions for each L2 step
                // l2[0] == currentId (already in result.path)
                int frameVlan = 0;
                for (int pi = 0; pi + 1 < (int)l2.size(); ++pi) {
                    int stepId  = l2[pi];
                    int nextStId = l2[pi + 1];
                    const DeviceNode* stepNode = FindNode(nodes, stepId);
                    const DeviceNode* nextNode = FindNode(nodes, nextStId);
                    const Cable* cab = FindCableL2(cables, stepId, nextStId);
                    int outPort = -1, inPort = -1;
                    if (cab) {
                        outPort = (cab->fromId == stepId)   ? cab->fromPort : cab->toPort;
                        inPort  = (cab->fromId == nextStId) ? cab->fromPort : cab->toPort;
                    }

                    int prevFrameVlan = frameVlan;  // VLAN at egress from stepId

                    // Determine VLAN on this segment: access ingress assigns VLAN,
                    // trunk ingress keeps current tag.
                    if (nextNode && nextNode->type == SWITCH && inPort >= 0) {
                        const VlanPortConfig& inp = nextNode->vlanPorts[inPort];
                        if (inp.mode == VLAN_ACCESS) frameVlan = inp.accessVlan;
                        // trunk: frameVlan unchanged
                    } else {
                        frameVlan = 0;  // exiting switch domain
                    }

                    // vlanTag shows on the CABLE leaving stepId; only non-zero on trunk egress.
                    bool trunkEgress = stepNode && stepNode->type == SWITCH
                                       && outPort >= 0
                                       && stepNode->vlanPorts[outPort].mode == VLAN_TRUNK;

                    HopDecision hd;
                    hd.nodeId     = stepId;
                    hd.nodeLabel  = stepNode ? stepNode->label : "";
                    hd.routeType  = (stepNode && stepNode->type == SWITCH) ? "SW" : "C";
                    hd.destPrefix = route.dest;
                    hd.nextHopIp  = nextNode ? nextNode->label : "";
                    hd.outPort    = outPort;
                    hd.vlanTag    = trunkEgress ? prevFrameVlan : 0;
                    result.hops.push_back(hd);
                    result.path.push_back(nextStId);
                    visited.insert(nextStId);
                }

                // Final delivery hop at destination
                {
                    const DeviceNode* d = FindNode(nodes, l2.back());
                    HopDecision hd;
                    hd.nodeId     = l2.back();
                    hd.nodeLabel  = d ? d->label : "";
                    hd.routeType  = "C"; hd.destPrefix = route.dest;
                    hd.nextHopIp  = "delivered"; hd.outPort = -1; hd.vlanTag = 0;
                    result.hops.push_back(hd);
                }
                result.success = true; result.reason = "delivered";
                return result;
            }

            // ARP cache check for this next-hop
            bool        arpHit    = cur->arpTable.count(route.nextHop) > 0;
            std::string cachedMac = arpHit ? cur->arpTable.at(route.nextHop) : "";

            // Find the node owning route.nextHop (checks portIp + subIfaces)
            const DeviceNode* nextHopNode = FindNodeOwningIp(nodes, route.nextHop);
            int         neighborId  = nextHopNode ? nextHopNode->id : -1;
            std::string resolvedMac = (neighborId != -1) ? GetDeviceMac(neighborId) : "";

            // Emit ARP event
            if (arpHit) {
                result.arpEvents.push_back({currentId, route.nextHop, cachedMac, true});
                if (neighborId == -1) {
                    result.reason = "ARP: stale cache entry for " + route.nextHop;
                    return result;
                }
            } else if (neighborId != -1) {
                result.arpEvents.push_back({currentId, route.nextHop, resolvedMac, false});
            } else {
                result.arpEvents.push_back({currentId, route.nextHop, "", false});
                result.reason = "ARP: who has " + route.nextHop + "? — no reply";
                return result;
            }

            if (visited.count(neighborId)) {
                result.reason = "loop detected";
                return result;
            }

            // Find L2 path to neighbor (handles switches between L3 devices)
            std::vector<int> l2nh = {};
            if (neighborId != -1)
                l2nh = FindL2Path(currentId, neighborId, nodes, cables);
            if (neighborId == -1 || l2nh.empty()) {
                result.reason = l2nh.empty() && neighborId != -1
                    ? "VLAN mismatch — no L2 path to " + route.nextHop
                    : "ARP: who has " + route.nextHop + "? — no reply";
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
                // Compute outPort from first hop in L2 path
                if (l2nh.size() >= 2) {
                    const Cable* c = FindCableL2(cables, l2nh[0], l2nh[1]);
                    if (c) hd.outPort = (c->fromId == l2nh[0]) ? c->fromPort : c->toPort;
                }
                // MPLS label operation (preserve existing MPLS logic verbatim)
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

            // Add intermediate switch hops (l2nh[1] .. l2nh[n-2])
            {
                // Seed frameVlan: determine VLAN assigned at l2nh[1]'s ingress from l2nh[0].
                // Needed so trunk-egress hops correctly show the VLAN tag in the trace.
                int frameVlan = 0;
                if (l2nh.size() >= 3) {
                    const Cable*      c0 = FindCableL2(cables, l2nh[0], l2nh[1]);
                    const DeviceNode* sw = FindNode(nodes, l2nh[1]);
                    if (c0 && sw && sw->type == SWITCH) {
                        int inPt = (c0->fromId == l2nh[1]) ? c0->fromPort : c0->toPort;
                        if (sw->vlanPorts[inPt].mode == VLAN_ACCESS)
                            frameVlan = sw->vlanPorts[inPt].accessVlan;
                    }
                }
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
        }

        if (!matched) { result.reason = "no route to " + destIp; return result; }
    }

    result.reason = "ttl exceeded";
    return result;  // hops is partial; callers must check success/reason
}
