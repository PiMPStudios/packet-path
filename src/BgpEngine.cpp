#include "BgpEngine.h"
#include <unordered_map>
#include <string>

// Returns the port IP of `node` on this cable, or "" if not connected
static std::string FaceIp(const DeviceNode& node, const Cable& cable)
{
    int port = -1;
    if      (cable.fromId == node.id) port = cable.fromPort;
    else if (cable.toId   == node.id) port = cable.toPort;
    if (port < 0 || port >= PORTS_PER_NODE) return "";
    return node.portIp[port];
}

static std::string StripMask(const std::string& ip)
{
    auto s = ip.find('/');
    return (s != std::string::npos) ? ip.substr(0, s) : ip;
}

void UpdateBgp(std::vector<DeviceNode>& nodes,
               const std::vector<Cable>& cables)
{
    // Clear existing BGP state every frame
    for (auto& n : nodes) {
        n.bgpNeighbors.clear();
        n.bgpRoutes.clear();
    }

    // ── Phase 1: Form BGP sessions (eBGP = different AS, iBGP = same AS) ─
    // One session per cable where both endpoints: ROUTER, bgpEnabled, localAsn != 0.
    // Same AS → iBGP (full mesh, direct cable); different AS → eBGP.
    for (const auto& cable : cables) {
        DeviceNode* a = FindNodeMut(nodes, cable.fromId);
        DeviceNode* b = FindNodeMut(nodes, cable.toId);
        if (!a || !b) continue;
        if (a->type != ROUTER || b->type != ROUTER) continue;
        if (!a->bgpEnabled || !b->bgpEnabled) continue;
        if (a->localAsn == 0 || b->localAsn == 0) continue;

        std::string aIp = FaceIp(*a, cable);
        std::string bIp = FaceIp(*b, cable);
        if (aIp.empty() || bIp.empty()) continue;

        bool sameAs = (a->localAsn == b->localAsn);

        BgpNeighbor nbA;
        nbA.neighborIp     = StripMask(bIp);
        nbA.neighborNodeId = b->id;
        nbA.neighborAsn    = b->localAsn;
        nbA.established    = true;
        nbA.ibgp           = sameAs;
        a->bgpNeighbors.push_back(nbA);

        BgpNeighbor nbB;
        nbB.neighborIp     = StripMask(aIp);
        nbB.neighborNodeId = a->id;
        nbB.neighborAsn    = a->localAsn;
        nbB.established    = true;
        nbB.ibgp           = sameAs;
        b->bgpNeighbors.push_back(nbB);
    }

    // ── Phase 2a: Each router advertises its own networks to eBGP peers ──
    // Collect into pending list before applying so writes don't affect reads.
    struct PendingRoute { int targetId; BgpRoute route; };
    std::vector<PendingRoute> pending;

    for (const auto& n : nodes) {
        if (!n.bgpEnabled || n.localAsn == 0 || n.bgpNeighbors.empty()) continue;

        // What to advertise: explicit bgpNetworks, or all connected prefixes if empty
        std::vector<std::string> toAdvertise;
        if (!n.bgpNetworks.empty()) {
            for (const auto& net : n.bgpNetworks)
                toAdvertise.push_back(NetworkAddress(net));
        } else {
            auto table = GetRoutingTable(n);
            for (const auto& r : table)
                if (r.src == ROUTE_CONNECTED)
                    toAdvertise.push_back(r.dest);
        }

        for (const auto& nb : n.bgpNeighbors) {
            if (!nb.established) continue;

            // Find this router's IP facing the neighbor
            std::string myFaceIp;
            for (const auto& cable : cables) {
                if (!((cable.fromId == n.id && cable.toId   == nb.neighborNodeId) ||
                      (cable.toId   == n.id && cable.fromId == nb.neighborNodeId))) continue;
                myFaceIp = StripMask(FaceIp(n, cable));
                break;
            }
            if (myFaceIp.empty()) continue;

            for (const auto& prefix : toAdvertise) {
                BgpRoute r;
                r.prefix         = prefix;
                r.nextHop        = myFaceIp;
                r.asPath         = {n.localAsn};
                r.neighborNodeId = n.id;
                pending.push_back({nb.neighborNodeId, r});
            }
        }
    }
    for (auto& [tid, r] : pending)
        if (auto* nd = FindNodeMut(nodes, tid)) nd->bgpRoutes.push_back(r);

    // ── Phase 2b: Relay routes learned in 2a to other peers ──────────────
    // Enables 3-AS linear chains (one relay hop: AS100→AS200→AS300).
    pending.clear();
    for (const auto& n : nodes) {
        if (!n.bgpEnabled || n.localAsn == 0 || n.bgpRoutes.empty()) continue;

        for (const auto& nb : n.bgpNeighbors) {
            if (!nb.established) continue;

            std::string myFaceIp;
            for (const auto& cable : cables) {
                if (!((cable.fromId == n.id && cable.toId   == nb.neighborNodeId) ||
                      (cable.toId   == n.id && cable.fromId == nb.neighborNodeId))) continue;
                myFaceIp = StripMask(FaceIp(n, cable));
                break;
            }
            if (myFaceIp.empty()) continue;

            for (const auto& learned : n.bgpRoutes) {
                if (learned.neighborNodeId == nb.neighborNodeId) continue;  // don't reflect

                // AS path loop prevention: skip if peer's ASN is already in path
                bool loop = false;
                for (auto asn : learned.asPath)
                    if (asn == nb.neighborAsn) { loop = true; break; }
                if (loop) continue;

                BgpRoute relay;
                relay.prefix         = learned.prefix;
                relay.nextHop        = myFaceIp;
                relay.asPath         = {n.localAsn};
                for (auto asn : learned.asPath) relay.asPath.push_back(asn);
                relay.neighborNodeId = n.id;
                pending.push_back({nb.neighborNodeId, relay});
            }
        }
    }
    for (auto& [tid, r] : pending)
        if (auto* nd = FindNodeMut(nodes, tid)) nd->bgpRoutes.push_back(r);

    // ── Deduplicate: keep shortest AS path per prefix per router ─────────
    for (auto& n : nodes) {
        if (n.bgpRoutes.empty()) continue;
        std::unordered_map<std::string, BgpRoute> best;
        for (const auto& r : n.bgpRoutes) {
            auto it = best.find(r.prefix);
            if (it == best.end() || r.asPath.size() < it->second.asPath.size())
                best[r.prefix] = r;
        }
        n.bgpRoutes.clear();
        for (auto& [prefix, r] : best)
            n.bgpRoutes.push_back(r);
    }
}
