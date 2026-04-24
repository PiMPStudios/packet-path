#include "Device.h"

Color GetDeviceColor(DeviceType t) {
    switch (t) {
        case PC:     return {59,  130, 246, 255};
        case ROUTER: return {249, 115,  22, 255};
        case SWITCH: return {34,  197,  94, 255};
        default:     return WHITE;
    }
}

Rectangle GetNodeRect(const DeviceNode& n) {
    return {n.position.x - NODE_W / 2.0f,
            n.position.y - NODE_H / 2.0f,
            NODE_W, NODE_H};
}

Vector2 GetPortPosition(const DeviceNode& n, int port) {
    float hw = NODE_W / 2.0f, hh = NODE_H / 2.0f;
    switch (port) {
        case 0: return {n.position.x,       n.position.y - hh};  // top
        case 1: return {n.position.x + hw,  n.position.y      };  // right
        case 2: return {n.position.x,       n.position.y + hh};  // bottom
        case 3: return {n.position.x - hw,  n.position.y      };  // left
        default: return n.position;
    }
}

std::string NetworkAddress(const std::string& cidr) {
    int a, b, c, d, prefix;
    if (std::sscanf(cidr.c_str(), "%d.%d.%d.%d/%d", &a, &b, &c, &d, &prefix) != 5)
        return cidr;
    uint32_t ip   = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
                    ((uint32_t)c <<  8) |  (uint32_t)d;
    uint32_t mask = prefix ? (~0u << (32 - prefix)) : 0u;
    uint32_t net  = ip & mask;
    return std::to_string((net >> 24) & 0xFF) + "." +
           std::to_string((net >> 16) & 0xFF) + "." +
           std::to_string((net >>  8) & 0xFF) + "." +
           std::to_string( net        & 0xFF) + "/" +
           std::to_string(prefix);
}

bool IpInSubnet(const std::string& ip, const std::string& subnet) {
    int a1, b1, c1, d1, a2, b2, c2, d2, prefix;
    if (std::sscanf(ip.c_str(),     "%d.%d.%d.%d",    &a1,&b1,&c1,&d1) != 4) return false;
    if (std::sscanf(subnet.c_str(), "%d.%d.%d.%d/%d", &a2,&b2,&c2,&d2,&prefix) != 5) return false;
    uint32_t ipBits  = ((uint32_t)a1 << 24) | ((uint32_t)b1 << 16) |
                       ((uint32_t)c1 <<  8) |  (uint32_t)d1;
    uint32_t netBits = ((uint32_t)a2 << 24) | ((uint32_t)b2 << 16) |
                       ((uint32_t)c2 <<  8) |  (uint32_t)d2;
    uint32_t mask    = prefix ? (~0u << (32 - prefix)) : 0u;
    return (ipBits & mask) == (netBits & mask);
}

bool ValidateIPOnly(const std::string& ip) {
    if (ip.empty()) return false;
    int a, b, c, d, consumed = 0;
    std::sscanf(ip.c_str(), "%d.%d.%d.%d%n", &a, &b, &c, &d, &consumed);
    return (consumed == (int)ip.size() &&
            a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
            c >= 0 && c <= 255 && d >= 0 && d <= 255);
}

int PrefixLen(const std::string& cidr) {
    const char* slash = std::strchr(cidr.c_str(), '/');
    return slash ? std::atoi(slash + 1) : 0;
}

bool ValidateIP(const std::string& ip) {
    if (ip.empty()) return false;
    int a, b, c, d, prefix, consumed = 0;
    std::sscanf(ip.c_str(), "%d.%d.%d.%d/%d%n", &a, &b, &c, &d, &prefix, &consumed);
    return (consumed == (int)ip.size() &&
            a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
            c >= 0 && c <= 255 && d >= 0 && d <= 255 &&
            prefix >= 0 && prefix <= 32);
}

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

std::string GetPortName(DeviceType type, int port) {
    if (type == PC) {
        static_assert(PORTS_PER_NODE == 4, "Update GetPortName PC branch to match PORTS_PER_NODE");
        const char* names[] = {"eth0", "eth1", "eth2", "eth3"};
        return names[port];
    }
    return "Gi0/" + std::to_string(port);
}

std::string GetDeviceMac(int id) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "de:ad:be:ef:%02x:%02x",
                  (id >> 8) & 0xFF, id & 0xFF);
    return buf;
}

bool IsAbr(const DeviceNode& node) {
    bool     foundFirst = false;
    uint32_t firstArea  = 0;
    for (const auto& nbr : node.ospfNeighbors) {
        if (nbr.state != OSPF_FULL) continue;
        if (!foundFirst) { foundFirst = true; firstArea = nbr.area; continue; }
        if (nbr.area != firstArea) return true;
    }
    return false;
}
