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
        if (vtep.vni == 0) continue;

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
