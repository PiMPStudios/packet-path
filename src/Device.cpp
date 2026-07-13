#include "Device.h"

#include <charconv>
#include <string_view>

namespace {

bool ParseIpv4(const std::string& text, uint32_t& value) {
    const std::string_view input(text);
    uint32_t result = 0;
    std::size_t start = 0;
    for (int octetIndex = 0; octetIndex < 4; ++octetIndex) {
        const std::size_t end = input.find('.', start);
        if ((octetIndex < 3 && end == std::string_view::npos) ||
            (octetIndex == 3 && end != std::string_view::npos)) return false;
        const std::size_t partEnd = end == std::string_view::npos ? input.size() : end;
        if (partEnd == start) return false;

        unsigned int octet = 0;
        const char* first = input.data() + start;
        const char* last  = input.data() + partEnd;
        const auto parsed = std::from_chars(first, last, octet);
        if (parsed.ec != std::errc{} || parsed.ptr != last || octet > 255) return false;
        result = (result << 8) | octet;
        start = partEnd + 1;
    }
    value = result;
    return true;
}

bool ParseCidr(const std::string& text, uint32_t& address, int& prefix) {
    const auto slash = text.find('/');
    if (slash == std::string::npos || text.find('/', slash + 1) != std::string::npos)
        return false;
    if (!ParseIpv4(text.substr(0, slash), address)) return false;
    const char* first = text.data() + slash + 1;
    const char* last  = text.data() + text.size();
    if (first == last) return false;
    const auto parsed = std::from_chars(first, last, prefix);
    return parsed.ec == std::errc{} && parsed.ptr == last && prefix >= 0 && prefix <= 32;
}

uint32_t PrefixMask(int prefix) {
    return prefix == 0 ? 0u : (~0u << (32 - prefix));
}

}  // namespace

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
    uint32_t ip = 0;
    int prefix = 0;
    if (!ParseCidr(cidr, ip, prefix)) return cidr;
    const uint32_t net = ip & PrefixMask(prefix);
    return std::to_string((net >> 24) & 0xFF) + "." +
           std::to_string((net >> 16) & 0xFF) + "." +
           std::to_string((net >>  8) & 0xFF) + "." +
           std::to_string( net        & 0xFF) + "/" +
           std::to_string(prefix);
}

bool IpInSubnet(const std::string& ip, const std::string& subnet) {
    uint32_t ipBits = 0, netBits = 0;
    int prefix = 0;
    if (!ParseIpv4(ip, ipBits) || !ParseCidr(subnet, netBits, prefix)) return false;
    const uint32_t mask = PrefixMask(prefix);
    return (ipBits & mask) == (netBits & mask);
}

bool ValidateIPOnly(const std::string& ip) {
    uint32_t value = 0;
    return ParseIpv4(ip, value);
}

int PrefixLen(const std::string& cidr) {
    uint32_t address = 0;
    int prefix = 0;
    return ParseCidr(cidr, address, prefix) ? prefix : 0;
}

bool ValidateIP(const std::string& ip) {
    uint32_t address = 0;
    int prefix = 0;
    return ParseCidr(ip, address, prefix);
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
    for (const auto& r : n.evpnRoutes)
        table.push_back(r);
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
