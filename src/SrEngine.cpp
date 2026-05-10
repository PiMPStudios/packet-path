#include "SrEngine.h"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cstdint>

// ── Helpers ───────────────────────────────────────────────────────────────────

// Returns the outPort from src.ospfRoutes that covers dest, or -1 if not found.
// Matches against dest.routerId or any of dest's port IPs (bare, no mask).
static int NextHopPort(const DeviceNode& src, const DeviceNode& dest) {
    for (const auto& r : src.ospfRoutes) {
        // Match by routerId (exact)
        if (r.dest == dest.routerId) return r.outPort;

        // Match by any port IP of dest (strip mask for comparison)
        for (int i = 0; i < PORTS_PER_NODE; ++i) {
            if (dest.portIp[i].empty()) continue;
            std::string bareIp   = dest.portIp[i].substr(0, dest.portIp[i].find('/'));
            std::string bareDest = r.dest.substr(0, r.dest.find('/'));
            if (bareDest == bareIp) return r.outPort;
        }
    }
    return -1;
}

// Returns the node ID on the far end of the cable attached to src at port, or -1.
static int NextHopNodeId(const DeviceNode& src, int port,
                         const std::vector<Cable>& cables) {
    for (const auto& c : cables) {
        if (c.fromId == src.id && c.fromPort == port) return c.toId;
        if (c.toId   == src.id && c.toPort   == port) return c.fromId;
    }
    return -1;
}

// Traces OSPF hops from srcId to destId.
// Returns node ID sequence [src, ..., dest], or empty if unreachable.
// Caps at 16 hops to prevent infinite loops.
static std::vector<int> OspfPath(int srcId, int destId,
                                  const std::vector<DeviceNode>& nodes,
                                  const std::vector<Cable>& cables) {
    std::vector<int> path;
    std::unordered_set<int> visited;
    int cur = srcId;
    for (int hop = 0; hop < 16; ++hop) {
        if (!visited.insert(cur).second) return {};  // cycle detected
        path.push_back(cur);
        if (cur == destId) return path;

        const DeviceNode* n    = FindNode(nodes, cur);
        if (!n) return {};
        const DeviceNode* dest = FindNode(nodes, destId);
        if (!dest) return {};

        int port = NextHopPort(*n, *dest);
        if (port < 0) return {};

        int nextId = NextHopNodeId(*n, port, cables);
        if (nextId < 0) return {};

        cur = nextId;
    }
    return {};  // exceeded max hops
}

// ── ResolveSrSegments ─────────────────────────────────────────────────────────

void ResolveSrSegments(SrPolicy& policy, const std::vector<DeviceNode>& nodes) {
    policy.segmentHops.clear();
    policy.labelStack.clear();
    policy.segmentsResolved = false;

    for (const auto& ip : policy.segmentIps) {
        const DeviceNode* found = nullptr;

        for (const auto& n : nodes) {
            // Match by routerId
            if (n.routerId == ip) { found = &n; break; }

            // Match by any port IP (bare, no mask)
            for (int i = 0; i < PORTS_PER_NODE; ++i) {
                if (n.portIp[i].empty()) continue;
                std::string bare = n.portIp[i].substr(0, n.portIp[i].find('/'));
                if (bare == ip) { found = &n; break; }
            }
            if (found) break;
        }

        if (!found || !found->srEnabled || found->nodeSid == 0) {
            policy.statusMsg = "No Node SID for " + ip;
            return;
        }
        if (found->nodeSid >= SRGB_SIZE) {
            policy.statusMsg = "SID out of range for " + ip;
            return;
        }

        policy.segmentHops.push_back(found->id);
        policy.labelStack.push_back(SRGB_BASE + found->nodeSid);
    }

    // Reverse so back() is the outermost (first-processed) label.
    // e.g. segments [R2→R4]: labelStack = {17004, 17002}, back()=17002 pushed first.
    std::reverse(policy.labelStack.begin(), policy.labelStack.end());
    policy.segmentsResolved = true;
    policy.statusMsg = "";
}

// ── UpdateSr ─────────────────────────────────────────────────────────────────

void UpdateSr(std::vector<DeviceNode>& nodes, const std::vector<Cable>& cables) {

    // Phase 1: Build global SID maps (nodeId ↔ label).
    // Skip duplicate SIDs silently — first one wins.
    std::unordered_map<int, uint32_t> nodeSidToLabel;  // nodeId  -> label
    std::unordered_map<uint32_t, int> labelToNodeId;   // label   -> nodeId

    for (const auto& n : nodes) {
        if (!n.srEnabled || n.nodeSid == 0) continue;
        uint32_t label = SRGB_BASE + n.nodeSid;
        if (labelToNodeId.count(label)) continue;  // duplicate SID — skip
        nodeSidToLabel[n.id] = label;
        labelToNodeId[label] = n.id;
    }

    // Phase 2: Assign adjacency SIDs per port.
    // Formula: 5000 + nodeId*8 + portIndex (unique per node/port pair).
    for (auto& n : nodes) {
        if (!n.srEnabled) continue;
        n.adjSids.clear();
        for (const auto& c : cables) {
            if (c.fromId == n.id)
                n.adjSids[c.fromPort] = 5000u + static_cast<uint32_t>(n.id) * 8u
                                        + static_cast<uint32_t>(c.fromPort);
            if (c.toId == n.id)
                n.adjSids[c.toPort]   = 5000u + static_cast<uint32_t>(n.id) * 8u
                                        + static_cast<uint32_t>(c.toPort);
        }
    }

    // Phase 3: Build per-node srFib (shortest-path SR forwarding entries).
    for (auto& n : nodes) {
        if (!n.srEnabled) { n.srFib.clear(); continue; }
        n.srFib.clear();

        for (const auto& [label, destId] : labelToNodeId) {
            const DeviceNode* dest = FindNode(nodes, destId);
            if (!dest) continue;

            if (n.id == destId) {
                // Egress node: self-pop so IP routing takes over.
                n.srFib[label] = { label, MPLS_IMPLICIT_NULL, -1, 0 };
                continue;
            }

            int port = NextHopPort(n, *dest);
            if (port < 0) continue;

            int nextId = NextHopNodeId(n, port, cables);
            bool isPenultimate = (nextId == destId);

            if (isPenultimate) {
                // PHP: pop label at penultimate hop so egress sees raw IP.
                n.srFib[label] = { label, MPLS_IMPLICIT_NULL, port, 0 };
            } else {
                // Transit: forward with same label unchanged.
                n.srFib[label] = { label, label, port, 0 };
            }
        }
    }

    // Phase 4: Compute SR policies — resolve segments and verify reachability.
    for (auto& n : nodes) {
        if (!n.srEnabled) continue;

        for (auto& policy : n.srPolicies) {
            // Re-resolve whenever the UI clears segmentsResolved.
            if (!policy.segmentsResolved)
                ResolveSrSegments(policy, nodes);

            if (!policy.segmentsResolved || policy.segmentHops.empty()) {
                policy.isActive = false;
                continue;
            }

            // Verify reachability from head-end to first segment.
            {
                auto path = OspfPath(n.id, policy.segmentHops.front(), nodes, cables);
                if (path.empty()) {
                    policy.isActive  = false;
                    policy.statusMsg = "Segment 0 unreachable";
                    continue;
                }
            }

            // Verify reachability between consecutive segment hops.
            bool reachable = true;
            for (int i = 0; i + 1 < static_cast<int>(policy.segmentHops.size()); ++i) {
                auto path = OspfPath(policy.segmentHops[i], policy.segmentHops[i + 1],
                                     nodes, cables);
                if (path.empty()) {
                    policy.isActive  = false;
                    policy.statusMsg = "Segment " + std::to_string(i) + " unreachable";
                    reachable = false;
                    break;
                }
            }
            if (!reachable) continue;

            // Build activePath: head-end + OSPF paths concatenated through each waypoint.
            policy.activePath.clear();
            policy.activePath.push_back(n.id);

            for (int i = 0; i < static_cast<int>(policy.segmentHops.size()); ++i) {
                int fromId = (i == 0) ? n.id : policy.segmentHops[i - 1];
                auto seg = OspfPath(fromId, policy.segmentHops[i], nodes, cables);
                if (seg.size() > 1)
                    policy.activePath.insert(policy.activePath.end(),
                                             seg.begin() + 1, seg.end());
            }

            policy.isActive  = true;
            policy.statusMsg = "Active";
        }
    }
}
