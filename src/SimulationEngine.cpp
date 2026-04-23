#include "SimulationEngine.h"
#include <algorithm>
#include <unordered_set>

// ── Forwarding engine ─────────────────────────────────────────────────────
ForwardResult SimulateForward(int srcId, const std::string& destIp,
                              const std::vector<DeviceNode>& nodes,
                              const std::vector<Cable>& cables)
{
    if (!FindNode(nodes, srcId))
        return {false, {}, "source node not found", {}};

    if (!ValidateIPOnly(destIp))
        return {false, {srcId}, "invalid destination", {}};

    static constexpr int MAX_HOPS = 16;

    ForwardResult result;
    result.path = {srcId};
    int currentId = srcId;
    std::unordered_set<int> visited = {srcId};

    for (int i = 0; i < MAX_HOPS; ++i) {
        const DeviceNode* cur = FindNode(nodes, currentId);
        if (!cur) { result.reason = "node not found"; return result; }

        auto table = GetRoutingTable(*cur);
        std::sort(table.begin(), table.end(), [](const RouteEntry& a, const RouteEntry& b) {
            return PrefixLen(a.dest) > PrefixLen(b.dest);
        });

        bool matched = false;
        for (const auto& route : table) {
            if (!IpInSubnet(destIp, route.dest)) continue;

            if (route.src == ROUTE_CONNECTED) {
                result.success = true;
                result.reason  = "delivered";
                return result;
            }

            // ARP cache check for this next-hop
            bool        arpHit    = cur->arpTable.count(route.nextHop) > 0;
            std::string cachedMac = arpHit ? cur->arpTable.at(route.nextHop) : "";

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

            visited.insert(neighborId);
            result.path.push_back(neighborId);
            currentId = neighborId;
            matched   = true;
            break;
        }

        if (!matched) { result.reason = "no route to " + destIp; return result; }
    }

    result.reason = "ttl exceeded";
    return result;
}
