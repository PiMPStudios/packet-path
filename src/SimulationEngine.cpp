#include "SimulationEngine.h"
#include "AclEngine.h"
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
            if (nextId == -1 || visited.count(nextId)) continue;
            if (cable.broken) continue;

            const DeviceNode* next = FindNode(nodes, nextId);
            if (!next) continue;
            if (next->crashed && nextId != dstId) continue;

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

// Follows a specific egress port through an optional switch fabric to the next
// Layer-3 device. Label forwarding uses this path instead of the IP next hop.
static std::vector<int> FindL2PathViaPort(int srcId, int outPort,
                                         const std::vector<DeviceNode>& nodes,
                                         const std::vector<Cable>& cables)
{
    if (outPort < 0 || outPort >= PORTS_PER_NODE) return {};

    struct State {
        int nodeId;
        std::vector<int> path;
    };

    std::queue<State> queue;
    std::unordered_set<int> visited = {srcId};

    for (const auto& cable : cables) {
        if (cable.broken) continue;
        int nextId = -1;
        if (cable.fromId == srcId && cable.fromPort == outPort) nextId = cable.toId;
        if (cable.toId   == srcId && cable.toPort   == outPort) nextId = cable.fromId;
        if (nextId < 0) continue;

        const DeviceNode* next = FindNode(nodes, nextId);
        if (!next || next->crashed) continue;
        if (next->type != SWITCH) return {srcId, nextId};

        visited.insert(nextId);
        queue.push({nextId, {srcId, nextId}});
    }

    while (!queue.empty()) {
        State state = std::move(queue.front());
        queue.pop();

        for (const auto& cable : cables) {
            if (cable.broken) continue;
            int nextId = -1;
            if (cable.fromId == state.nodeId) nextId = cable.toId;
            if (cable.toId   == state.nodeId) nextId = cable.fromId;
            if (nextId < 0 || visited.count(nextId)) continue;

            const DeviceNode* next = FindNode(nodes, nextId);
            if (!next || next->crashed) continue;

            auto path = state.path;
            path.push_back(nextId);
            if (next->type != SWITCH) return path;

            visited.insert(nextId);
            queue.push({nextId, std::move(path)});
        }
    }

    return {};
}

struct LabelDecision {
    LabelOp  operation     = LABEL_NONE;
    uint32_t inLabel       = 0;
    uint32_t outLabel      = 0;
    int      forcedOutPort = -1;
    int      tunnelId      = 0;
    int      policyId      = 0;
    int      segmentIndex  = 0;
};

static LabelDecision ResolveLabelDecision(
    const DeviceNode& current,
    const RouteEntry& route,
    const std::string& destinationIp,
    uint32_t& currentLabel,
    std::vector<uint32_t>& srLabelStack,
    int& srSegmentIndex,
    int& srPolicyId)
{
    LabelDecision decision;

    if (currentLabel == 0 && current.srEnabled) {
        for (const auto& policy : current.srPolicies) {
            if (!policy.isActive || policy.destIp != destinationIp ||
                policy.labelStack.empty()) {
                continue;
            }
            srLabelStack   = policy.labelStack;
            currentLabel   = srLabelStack.back();
            srSegmentIndex = 0;
            srPolicyId     = policy.id;

            decision.operation    = LABEL_PUSH;
            decision.outLabel     = currentLabel;
            decision.policyId     = policy.id;
            decision.segmentIndex = 0;
            auto fib = current.srFib.find(currentLabel);
            if (fib != current.srFib.end()) decision.forcedOutPort = fib->second.outPort;
            return decision;
        }
    }

    if (currentLabel == 0 && current.rsvpEnabled) {
        for (const auto& tunnel : current.teTunnels) {
            if (!tunnel.isUp || tunnel.destIp != destinationIp || tunnel.headLabel == 0)
                continue;

            decision.tunnelId  = tunnel.id;
            auto fib = current.teLfib.find(tunnel.headLabel);
            if (fib == current.teLfib.end()) continue;

            decision.forcedOutPort = fib->second.outPort;
            if (fib->second.outLabel == MPLS_IMPLICIT_NULL) {
                // The head-end is also the penultimate hop; PHP means the
                // packet leaves without an MPLS label.
                currentLabel = 0;
            } else {
                currentLabel       = fib->second.outLabel;
                decision.operation = LABEL_PUSH;
                decision.outLabel  = currentLabel;
            }
            return decision;
        }
    }

    if (currentLabel != 0 && current.srEnabled) {
        auto fib = current.srFib.find(currentLabel);
        if (fib != current.srFib.end()) {
            const SrLfibEntry& entry = fib->second;
            decision.inLabel      = currentLabel;
            decision.policyId     = srPolicyId;
            decision.segmentIndex = srSegmentIndex;
            decision.forcedOutPort = entry.outPort;

            if (entry.outLabel == MPLS_IMPLICIT_NULL) {
                decision.operation = LABEL_POP;
                if (!srLabelStack.empty()) srLabelStack.pop_back();
                currentLabel = srLabelStack.empty() ? 0 : srLabelStack.back();
                ++srSegmentIndex;
            } else {
                decision.operation = LABEL_SWAP;
                decision.outLabel  = entry.outLabel;
                currentLabel       = entry.outLabel;
            }
            return decision;
        }
    }

    if (currentLabel != 0 && current.rsvpEnabled) {
        auto fib = current.teLfib.find(currentLabel);
        if (fib != current.teLfib.end()) {
            const TeLfibEntry& entry = fib->second;
            decision.inLabel       = currentLabel;
            decision.tunnelId      = entry.tunnelId;
            decision.forcedOutPort = entry.outPort;
            if (entry.outLabel == MPLS_IMPLICIT_NULL) {
                decision.operation = LABEL_POP;
                currentLabel = 0;
            } else {
                decision.operation = LABEL_SWAP;
                decision.outLabel  = entry.outLabel;
                currentLabel       = entry.outLabel;
            }
            return decision;
        }
    }

    if (current.ldpEnabled) {
        auto fib = current.lfib.find(NetworkAddress(route.dest));
        if (fib != current.lfib.end()) {
            const uint32_t nextLabel = fib->second.outLabel;
            decision.inLabel = currentLabel;
            if (currentLabel == 0) {
                decision.operation = LABEL_PUSH;
                decision.outLabel  = nextLabel;
                currentLabel       = nextLabel;
            } else if (nextLabel == MPLS_IMPLICIT_NULL) {
                decision.operation = LABEL_POP;
                currentLabel       = 0;
            } else {
                decision.operation = LABEL_SWAP;
                decision.outLabel  = nextLabel;
                currentLabel       = nextLabel;
            }
        } else if (currentLabel != 0) {
            currentLabel = 0;
        }
    } else if (currentLabel != 0) {
        currentLabel = 0;
    }

    return decision;
}

// ── Forwarding engine ─────────────────────────────────────────────────────
ForwardResult SimulateForward(int srcId, const std::string& destIp,
                              const std::vector<DeviceNode>& nodes,
                              const std::vector<Cable>& cables,
                              const std::string& srcIp,
                              int               dstPort)
{
    const DeviceNode* srcNode = FindNode(nodes, srcId);
    if (!srcNode)
        return {false, {}, "source node not found", {}, {}};
    if (srcNode->crashed)
        return {false, {srcId},
                srcNode->label + " is crashed \xe2\x80\x94 device offline", {}, {}};

    if (!ValidateIPOnly(destIp))
        return {false, {srcId}, "invalid destination", {}, {}};

    static constexpr int MAX_HOPS = 16;

    ForwardResult result;
    result.path = {srcId};
    int currentId = srcId;
    std::unordered_set<int> visited = {srcId};

    uint32_t currentLabel = 0;
    int lastIngressPort = -1;   // port on currentId where packet arrived
    std::vector<uint32_t> srLabelStack;   // SR label stack (innermost first, outermost at back())
    int  srSegmentIdx = 0;
    int  srPolicyId   = 0;
    int  srv6PolicyId = 0;
    int  srv6SegmentIdx = 0;
    std::size_t srv6PathIndex = 0;
    std::vector<std::string> srv6SegmentSids;
    std::vector<int> srv6SegmentHops;
    std::vector<int> srv6Path;
    bool sdwanPolicyApplied = false;

    for (int i = 0; i < MAX_HOPS; ++i) {
        const DeviceNode* cur = FindNode(nodes, currentId);
        if (!cur)         { result.reason = "node not found"; return result; }
        if (cur->crashed) { result.reason = cur->label + " is crashed \xe2\x80\x94 device offline"; return result; }

        // ── ACL inbound check ──────────────────────────────────────────────
        if (!cur->aclRules.empty() && cur->aclInPort >= 0
            && cur->aclInPort == lastIngressPort) {
            const AclRule* m = MatchAcl(cur->aclRules, srcIp, destIp, dstPort);
            if (!m || m->action == ACL_DENY) {
                std::string why = m
                    ? ("ACL seq " + std::to_string(m->seq) + " deny")
                    : "ACL implicit deny";
                result.reason = why + ": " + srcIp + " \xe2\x86\x92 " + destIp;
                return result;
            }
        }
        // ── end ACL inbound ────────────────────────────────────────────────

        // ── SD-WAN destination policy / SLA-selected egress ──────────────
        if (!sdwanPolicyApplied && cur->sdwanEnabled) {
            const SdwanPolicy* selectedPolicy = nullptr;
            for (const auto& policy : cur->sdwanPolicies) {
                if (policy.isActive && policy.destIp == destIp && policy.selectedPort >= 0) {
                    selectedPolicy = &policy;
                    break;
                }
            }
            if (selectedPolicy) {
                const auto l2Path = FindL2PathViaPort(
                    currentId, selectedPolicy->selectedPort, nodes, cables);
                if (l2Path.size() < 2) {
                    result.reason = "SD-WAN selected path unavailable";
                    return result;
                }
                const int neighborId = l2Path.back();
                if (visited.count(neighborId)) {
                    result.reason = "loop detected";
                    return result;
                }
                HopDecision hop;
                hop.nodeId = currentId;
                hop.nodeLabel = cur->label;
                hop.routeType = "SD-WAN";
                hop.destPrefix = destIp + "/32";
                const DeviceNode* neighbor = FindNode(nodes, neighborId);
                hop.nextHopIp = neighbor ? neighbor->label : "WAN next-hop";
                hop.outPort = selectedPolicy->selectedPort;
                hop.sdwanPolicyId = selectedPolicy->id;
                hop.sdwanSelectedPort = selectedPolicy->selectedPort;
                hop.sdwanUsingBackup = selectedPolicy->usingBackup;
                if (!cur->aclRules.empty() && cur->aclOutPort == hop.outPort) {
                    const AclRule* rule = MatchAcl(cur->aclRules, srcIp, destIp, dstPort);
                    if (!rule || rule->action == ACL_DENY) {
                        result.reason = rule
                            ? "ACL seq " + std::to_string(rule->seq) + " deny"
                            : "ACL implicit deny";
                        return result;
                    }
                    hop.aclResult = "PERMIT seq:" + std::to_string(rule->seq);
                }
                result.hops.push_back(hop);
                for (std::size_t index = 1; index + 1 < l2Path.size(); ++index) {
                    result.path.push_back(l2Path[index]);
                    visited.insert(l2Path[index]);
                }
                result.path.push_back(neighborId);
                visited.insert(neighborId);
                const Cable* ingress = FindCableL2(
                    cables, l2Path[l2Path.size() - 2], neighborId);
                lastIngressPort = ingress
                    ? (ingress->fromId == neighborId ? ingress->fromPort : ingress->toPort)
                    : -1;
                currentId = neighborId;
                sdwanPolicyApplied = true;
                continue;
            }
        }
        // ── end SD-WAN steering ──────────────────────────────────────────

        // ── SRv6 policy encapsulation / SRH steering ─────────────────────
        if (srv6PolicyId == 0 && cur->srv6Enabled) {
            for (const auto& policy : cur->srv6Policies) {
                if (!policy.isActive || policy.destIp != destIp ||
                    policy.activePath.size() < 2) continue;
                srv6PolicyId    = policy.id;
                srv6SegmentSids = policy.segmentSids;
                srv6SegmentHops = policy.segmentHops;
                srv6Path        = policy.activePath;
                srv6PathIndex   = 0;
                srv6SegmentIdx  = 0;
                break;
            }
        }

        if (srv6PolicyId != 0 && srv6PathIndex + 1 < srv6Path.size()) {
            while (srv6SegmentIdx < static_cast<int>(srv6SegmentHops.size()) &&
                   currentId == srv6SegmentHops[srv6SegmentIdx]) {
                ++srv6SegmentIdx;
            }
            if (srv6SegmentIdx >= static_cast<int>(srv6SegmentSids.size())) {
                result.reason = "SRv6 policy ended before its path";
                return result;
            }

            const int nextId = srv6Path[srv6PathIndex + 1];
            const auto l2Path = FindL2Path(currentId, nextId, nodes, cables);
            if (l2Path.size() < 2) {
                result.reason = "SRv6 underlay path unavailable";
                return result;
            }
            if (visited.count(nextId)) {
                result.reason = "loop detected";
                return result;
            }

            const Cable* firstCable = FindCableL2(cables, l2Path[0], l2Path[1]);
            HopDecision hop;
            hop.nodeId          = currentId;
            hop.nodeLabel       = cur->label;
            hop.routeType       = "SRv6";
            hop.destPrefix      = destIp + "/32";
            const DeviceNode* nextNode = FindNode(nodes, nextId);
            hop.nextHopIp       = nextNode ? nextNode->label : "SRv6 next-hop";
            hop.outPort         = firstCable
                ? (firstCable->fromId == currentId ? firstCable->fromPort : firstCable->toPort)
                : -1;
            hop.srv6PolicyId     = srv6PolicyId;
            hop.srv6SegmentIndex = srv6SegmentIdx;
            hop.srv6SegmentsLeft = static_cast<int>(srv6SegmentSids.size()) -
                                   srv6SegmentIdx - 1;
            hop.srv6ActiveSid    = srv6SegmentSids[srv6SegmentIdx];

            if (!cur->aclRules.empty() && cur->aclOutPort == hop.outPort) {
                const AclRule* rule = MatchAcl(cur->aclRules, srcIp, destIp, dstPort);
                if (!rule || rule->action == ACL_DENY) {
                    const std::string why = rule
                        ? "ACL seq " + std::to_string(rule->seq) + " deny"
                        : "ACL implicit deny";
                    result.reason = why + ": " + srcIp + " \xe2\x86\x92 " + destIp;
                    return result;
                }
                hop.aclResult = "PERMIT seq:" + std::to_string(rule->seq);
            }
            result.hops.push_back(hop);

            for (std::size_t index = 1; index + 1 < l2Path.size(); ++index) {
                const int switchId = l2Path[index];
                const int switchNextId = l2Path[index + 1];
                const DeviceNode* switchNode = FindNode(nodes, switchId);
                const DeviceNode* switchNext = FindNode(nodes, switchNextId);
                const Cable* cable = FindCableL2(cables, switchId, switchNextId);
                HopDecision switchHop;
                switchHop.nodeId = switchId;
                switchHop.nodeLabel = switchNode ? switchNode->label : "";
                switchHop.routeType = "SW";
                switchHop.destPrefix = destIp + "/32";
                switchHop.nextHopIp = switchNext ? switchNext->label : "";
                if (cable) switchHop.outPort = cable->fromId == switchId
                    ? cable->fromPort : cable->toPort;
                result.hops.push_back(switchHop);
                result.path.push_back(switchId);
                visited.insert(switchId);
            }

            result.path.push_back(nextId);
            visited.insert(nextId);
            const Cable* ingress = FindCableL2(cables, l2Path[l2Path.size() - 2], nextId);
            lastIngressPort = ingress
                ? (ingress->fromId == nextId ? ingress->fromPort : ingress->toPort)
                : -1;
            currentId = nextId;
            ++srv6PathIndex;
            continue;
        }
        // ── end SRv6 steering ─────────────────────────────────────────────

        // Labeled transit is driven by the LFIB and must not require an IP
        // route to the payload destination on intermediate LSRs.
        const bool hasSrTransit = currentLabel != 0 && cur->srEnabled &&
                                  cur->srFib.count(currentLabel) > 0;
        const bool hasTeTransit = currentLabel != 0 && cur->rsvpEnabled &&
                                  cur->teLfib.count(currentLabel) > 0;
        if (hasSrTransit || hasTeTransit) {
            RouteEntry labelRoute;
            labelRoute.dest = destIp + "/32";
            LabelDecision decision = ResolveLabelDecision(
                *cur, labelRoute, destIp, currentLabel, srLabelStack,
                srSegmentIdx, srPolicyId);

            if (decision.forcedOutPort >= 0) {
                const auto l2Path = FindL2PathViaPort(
                    currentId, decision.forcedOutPort, nodes, cables);
                if (l2Path.size() < 2) {
                    result.reason = "label forwarding: no live neighbor on " +
                                    GetPortName(cur->type, decision.forcedOutPort);
                    return result;
                }

                const int neighborId = l2Path.back();
                if (visited.count(neighborId)) {
                    result.reason = "loop detected";
                    return result;
                }

                HopDecision hop;
                hop.nodeId        = currentId;
                hop.nodeLabel     = cur->label;
                hop.routeType     = hasSrTransit ? "SR" : "TE";
                hop.destPrefix    = labelRoute.dest;
                const DeviceNode* neighbor = FindNode(nodes, neighborId);
                hop.nextHopIp     = neighbor ? neighbor->label : "label next-hop";
                hop.outPort       = decision.forcedOutPort;
                hop.labelOp       = decision.operation;
                hop.inLabel       = decision.inLabel;
                hop.outLabel      = decision.outLabel;
                hop.tunnelId      = decision.tunnelId;
                hop.policyId      = decision.policyId;
                hop.segmentIndex  = decision.segmentIndex;

                if (!cur->aclRules.empty() && cur->aclOutPort == hop.outPort) {
                    const AclRule* rule = MatchAcl(cur->aclRules, srcIp, destIp, dstPort);
                    if (!rule || rule->action == ACL_DENY) {
                        const std::string why = rule
                            ? "ACL seq " + std::to_string(rule->seq) + " deny"
                            : "ACL implicit deny";
                        result.reason = why + ": " + srcIp + " â " + destIp;
                        return result;
                    }
                    hop.aclResult = "PERMIT seq:" + std::to_string(rule->seq);
                }
                result.hops.push_back(hop);

                for (size_t pathIndex = 1; pathIndex + 1 < l2Path.size(); ++pathIndex) {
                    const int switchId = l2Path[pathIndex];
                    const int nextId   = l2Path[pathIndex + 1];
                    const DeviceNode* switchNode = FindNode(nodes, switchId);
                    const DeviceNode* nextNode   = FindNode(nodes, nextId);
                    const Cable* cable = FindCableL2(cables, switchId, nextId);

                    HopDecision switchHop;
                    switchHop.nodeId     = switchId;
                    switchHop.nodeLabel  = switchNode ? switchNode->label : "";
                    switchHop.routeType  = "SW";
                    switchHop.destPrefix = labelRoute.dest;
                    switchHop.nextHopIp  = nextNode ? nextNode->label : "";
                    if (cable)
                        switchHop.outPort = cable->fromId == switchId
                            ? cable->fromPort : cable->toPort;
                    result.hops.push_back(switchHop);
                    result.path.push_back(switchId);
                    visited.insert(switchId);
                }

                result.path.push_back(neighborId);
                visited.insert(neighborId);
                const Cable* ingressCable = FindCableL2(
                    cables, l2Path[l2Path.size() - 2], neighborId);
                lastIngressPort = ingressCable
                    ? (ingressCable->fromId == neighborId
                        ? ingressCable->fromPort : ingressCable->toPort)
                    : -1;
                currentId = neighborId;
                continue;
            }
        }

        auto table = GetRoutingTable(*cur);
        std::sort(table.begin(), table.end(),
            [](const RouteEntry& a, const RouteEntry& b) {
                int pa = PrefixLen(a.dest), pb = PrefixLen(b.dest);
                if (pa != pb) return pa > pb;
                auto rank = [](RouteSource s) -> int {
                    switch (s) {
                        case ROUTE_CONNECTED: return 0;
                        case ROUTE_STATIC:   return 1;
                        case ROUTE_EVPN:     return 2;
                        case ROUTE_BGP:      return 3;
                        case ROUTE_OSPF:     return 4;
                        case ROUTE_OSPF_IA:  return 5;
                        default:             return 6;
                    }
                };
                return rank(a.src) < rank(b.src);
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

                if (destNode->crashed) {
                    HopDecision hd;
                    hd.nodeId     = l2.back();
                    hd.nodeLabel  = destNode->label;
                    hd.routeType  = "C"; hd.destPrefix = route.dest;
                    hd.nextHopIp  = "crashed"; hd.outPort = -1; hd.vlanTag = 0;
                    result.hops.push_back(hd);
                    result.reason = destNode->label + " is crashed \xe2\x80\x94 device offline";
                    return result;
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
                ForwardResult lo = SimulateForward(remoteVtepId, destIp, nodes, cables, srcIp, dstPort);
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

            LabelDecision labelDecision = ResolveLabelDecision(
                *cur, route, destIp, currentLabel, srLabelStack,
                srSegmentIdx, srPolicyId);

            int neighborId = -1;
            std::vector<int> l2nh;
            if (labelDecision.forcedOutPort >= 0) {
                l2nh = FindL2PathViaPort(currentId, labelDecision.forcedOutPort,
                                         nodes, cables);
                if (!l2nh.empty()) neighborId = l2nh.back();
                if (neighborId < 0) {
                    result.reason = "label forwarding: no live neighbor on " +
                                    GetPortName(cur->type, labelDecision.forcedOutPort);
                    return result;
                }
            } else {
                const bool arpHit = cur->arpTable.count(route.nextHop) > 0;
                const std::string cachedMac = arpHit ? cur->arpTable.at(route.nextHop) : "";
                const DeviceNode* nextHopNode = FindNodeOwningIp(nodes, route.nextHop);
                neighborId = nextHopNode ? nextHopNode->id : -1;
                const std::string resolvedMac = neighborId >= 0
                    ? GetDeviceMac(neighborId) : "";

                if (arpHit) {
                    result.arpEvents.push_back({currentId, route.nextHop, cachedMac, true});
                    if (neighborId < 0) {
                        result.reason = "ARP: stale cache entry for " + route.nextHop;
                        return result;
                    }
                } else if (neighborId >= 0) {
                    result.arpEvents.push_back({currentId, route.nextHop, resolvedMac, false});
                } else {
                    result.arpEvents.push_back({currentId, route.nextHop, "", false});
                    result.reason = "ARP: who has " + route.nextHop + "? — no reply";
                    return result;
                }

                l2nh = FindL2Path(currentId, neighborId, nodes, cables);
                if (l2nh.empty()) {
                    result.reason = "VLAN mismatch — no L2 path to " + route.nextHop;
                    return result;
                }
            }

            if (visited.count(neighborId)) {
                result.reason = "loop detected";
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
                else if (route.src == ROUTE_EVPN)    hd.routeType = "VX";
                else                                  hd.routeType = "?";
                hd.destPrefix = route.dest;
                hd.nextHopIp  = route.nextHop;
                // Compute outPort from first hop in L2 path
                if (l2nh.size() >= 2) {
                    const Cable* c = FindCableL2(cables, l2nh[0], l2nh[1]);
                    if (c) hd.outPort = (c->fromId == l2nh[0]) ? c->fromPort : c->toPort;
                }
                if (labelDecision.forcedOutPort >= 0)
                    hd.outPort = labelDecision.forcedOutPort;
                hd.labelOp      = labelDecision.operation;
                hd.inLabel      = labelDecision.inLabel;
                hd.outLabel     = labelDecision.outLabel;
                hd.tunnelId     = labelDecision.tunnelId;
                hd.policyId     = labelDecision.policyId;
                hd.segmentIndex = labelDecision.segmentIndex;
                // ── ACL outbound check ─────────────────────────────────────
                if (!cur->aclRules.empty() && cur->aclOutPort >= 0
                    && cur->aclOutPort == hd.outPort) {
                    const AclRule* m = MatchAcl(cur->aclRules, srcIp, destIp, dstPort);
                    if (!m || m->action == ACL_DENY) {
                        std::string why = m
                            ? ("ACL seq " + std::to_string(m->seq) + " deny")
                            : "ACL implicit deny";
                        result.reason = why + ": " + srcIp + " \xe2\x86\x92 " + destIp;
                        return result;
                    }
                    hd.aclResult = "PERMIT seq:" + std::to_string(m->seq);
                }
                // ── NAT annotation ─────────────────────────────────────────
                if (cur->natEnabled && cur->natOutsidePort >= 0
                    && hd.outPort == cur->natOutsidePort
                    && !srcIp.empty()
                    && !cur->natInsidePrefix.empty()
                    && AclMatchPrefix(srcIp, cur->natInsidePrefix)) {
                    const std::string& outsideCidr = cur->portIp[cur->natOutsidePort];
                    if (!outsideCidr.empty()) {
                        auto slash = outsideCidr.find('/');
                        std::string outsideIp = (slash != std::string::npos)
                            ? outsideCidr.substr(0, slash) : outsideCidr;
                        hd.natResult = srcIp + " \xe2\x86\x92 " + outsideIp;
                    }
                }
                // ── end ACL/NAT ────────────────────────────────────────────
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
            // Track which port on neighborId the packet enters from (for next iteration's inbound ACL)
            {
                int nextIngress = -1;
                if (l2nh.size() >= 2) {
                    const Cable* nc = FindCableL2(cables, l2nh[l2nh.size()-2], neighborId);
                    if (nc) nextIngress = (nc->fromId == neighborId)
                                       ? nc->fromPort : nc->toPort;
                }
                lastIngressPort = nextIngress;
            }
            currentId = neighborId;
            matched   = true;
            break;
        }

        if (!matched) { result.reason = "no route to " + destIp; return result; }
    }

    result.reason = "ttl exceeded";
    return result;  // hops is partial; callers must check success/reason
}
