#include "SimulationEngine.h"
#include <unordered_set>

// ── Forwarding engine ─────────────────────────────────────────────────────
ForwardResult SimulateForward(int srcId, const std::string& destIp,
                              const std::vector<DeviceNode>& nodes,
                              const std::vector<Cable>& cables)
{
    if (!FindNode(nodes, srcId))
        return {false, {}, "source node not found"};

    if (!ValidateIPOnly(destIp))
        return {false, {srcId}, "invalid destination"};

    static constexpr int MAX_HOPS = 16;

    int currentId = srcId;
    std::vector<int> path = {srcId};
    std::unordered_set<int> visited = {srcId};

    for (int i = 0; i < MAX_HOPS; ++i) {
        const DeviceNode* cur = FindNode(nodes, currentId);
        if (!cur) return {false, path, "node not found"};

        auto table = GetRoutingTable(*cur);
        std::sort(table.begin(), table.end(), [](const RouteEntry& a, const RouteEntry& b) {
            return PrefixLen(a.dest) > PrefixLen(b.dest);
        });

        bool matched = false;
        for (const auto& route : table) {
            if (!IpInSubnet(destIp, route.dest)) continue;

            if (route.src == ROUTE_CONNECTED) {
                return {true, path, "delivered"};
            }

            // Static route — find neighbor reachable via route.nextHop
            int neighborId = -1;
            for (const auto& cable : cables) {
                int candidateId = -1;
                if (cable.fromId == currentId) candidateId = cable.toId;
                else if (cable.toId == currentId) candidateId = cable.fromId;
                if (candidateId == -1) continue;

                const DeviceNode* neighbor = FindNode(nodes, candidateId);
                if (!neighbor) continue;

                auto nbTable = GetRoutingTable(*neighbor);
                for (const auto& nbRoute : nbTable) {
                    if (nbRoute.src == ROUTE_CONNECTED &&
                        IpInSubnet(route.nextHop, nbRoute.dest)) {
                        neighborId = candidateId;
                        break;
                    }
                }
                if (neighborId != -1) break;
            }

            if (neighborId == -1)
                return {false, path, "next-hop unreachable: " + route.nextHop};

            if (visited.count(neighborId))
                return {false, path, "loop detected"};

            visited.insert(neighborId);
            path.push_back(neighborId);
            currentId = neighborId;
            matched = true;
            break;
        }

        if (!matched)
            return {false, path, "no route to " + destIp};
    }

    return {false, path, "ttl exceeded"};
}
