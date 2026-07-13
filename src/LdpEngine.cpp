#include "LdpEngine.h"
#include <unordered_map>
#include <string>

// Returns the node ID of the directly-connected neighbor that owns nextHopIp.
static int FindNbrNodeId(int nodeId, const std::string& nextHopIp,
                          const std::vector<DeviceNode>& nodes,
                          const std::vector<Cable>& cables)
{
    for (const auto& c : cables) {
        if (c.broken) continue;
        int candidateId = -1;
        if      (c.fromId == nodeId) candidateId = c.toId;
        else if (c.toId   == nodeId) candidateId = c.fromId;
        if (candidateId == -1) continue;

        const DeviceNode* nb = FindNode(nodes, candidateId);
        if (!nb || nb->crashed) continue;
        for (int i = 0; i < PORTS_PER_NODE; ++i) {
            const auto& ip = nb->portIp[i];
            if (ip.empty()) continue;
            auto slash = ip.find('/');
            std::string plain = (slash != std::string::npos)
                                ? ip.substr(0, slash) : ip;
            if (plain == nextHopIp) return candidateId;
        }
    }
    return -1;
}

void UpdateLdp(std::vector<DeviceNode>& nodes,
               const std::vector<Cable>& cables)
{
    // Clear all existing LFIB tables
    for (auto& n : nodes) n.lfib.clear();

    // Phase 1: build routerId → {networkAddr → localLabel}
    // CONNECTED prefixes advertise MPLS_IMPLICIT_NULL (signals PHP to prev hop).
    // Non-CONNECTED prefixes get node.id*100 + per-router index.
    std::unordered_map<std::string,
        std::unordered_map<std::string, uint32_t>> bindingMap;

    for (const auto& n : nodes) {
        if (n.type != ROUTER || !n.ospfEnabled || !n.ldpEnabled || n.crashed) continue;
        if (n.routerId.empty()) continue;

        uint32_t localBase = (uint32_t)n.id * 100u;  // up to 100 non-CONNECTED prefixes per router
        uint32_t idx       = 0;
        auto table = GetRoutingTable(n);

        for (const auto& route : table) {
            std::string prefix = NetworkAddress(route.dest);
            if (prefix.empty()) continue;
            if (bindingMap[n.routerId].count(prefix)) continue;  // deduplicate

            if (route.src == ROUTE_CONNECTED) {
                bindingMap[n.routerId][prefix] = MPLS_IMPLICIT_NULL;
            } else {
                bindingMap[n.routerId][prefix] = localBase + idx++;
            }
        }
    }

    // Phase 2: for each ldpEnabled router, build LFIB
    for (auto& n : nodes) {
        if (n.type != ROUTER || !n.ospfEnabled || !n.ldpEnabled || n.crashed) continue;
        if (n.routerId.empty()) continue;

        auto myBindings = bindingMap.find(n.routerId);
        if (myBindings == bindingMap.end()) continue;

        auto table = GetRoutingTable(n);
        for (const auto& route : table) {
            if (route.src == ROUTE_CONNECTED) continue;

            std::string prefix = NetworkAddress(route.dest);
            if (prefix.empty()) continue;

            auto myLbl = myBindings->second.find(prefix);
            if (myLbl == myBindings->second.end()) continue;

            // Resolve next-hop router
            int nbrId = FindNbrNodeId(n.id, route.nextHop, nodes, cables);
            if (nbrId == -1) continue;
            const DeviceNode* nbr = FindNode(nodes, nbrId);
            if (!nbr || !nbr->ldpEnabled || nbr->routerId.empty()) continue;

            auto nbrBindings = bindingMap.find(nbr->routerId);
            if (nbrBindings == bindingMap.end()) continue;
            auto nbrLbl = nbrBindings->second.find(prefix);
            if (nbrLbl == nbrBindings->second.end()) continue;

            LdpBinding binding;
            binding.localLabel = myLbl->second;
            binding.outLabel   = nbrLbl->second;
            n.lfib[prefix]     = binding;
        }
    }
}
