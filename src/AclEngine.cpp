#include "AclEngine.h"
#include <cstdio>
#include <cstdint>

static uint32_t IpToU32(const std::string& ip) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
        return 0;
    return (a << 24) | (b << 16) | (c << 8) | d;
}

bool AclMatchPrefix(const std::string& ip, const std::string& cidr) {
    if (cidr == "any" || cidr.empty()) return true;
    if (ip.empty()) return true;   // empty srcIp = wildcard (underlay hops)
    auto slash = cidr.find('/');
    if (slash == std::string::npos) {
        // host match
        return ip == cidr;
    }
    int bits = 0;
    try { bits = std::stoi(cidr.substr(slash + 1)); } catch (...) { return false; }
    if (bits < 0 || bits > 32) return false;
    uint32_t mask = (bits == 0) ? 0u : (~0u << (32 - bits));
    uint32_t network = IpToU32(cidr.substr(0, slash)) & mask;
    return (IpToU32(ip) & mask) == network;
}

const AclRule* MatchAcl(const std::vector<AclRule>& rules,
                          const std::string& srcIp,
                          const std::string& dstIp,
                          int dstPort) {
    for (const auto& r : rules) {
        if (!AclMatchPrefix(srcIp, r.srcCidr))              continue;
        if (!AclMatchPrefix(dstIp, r.dstCidr))              continue;
        if (r.dstPort != 0 && r.dstPort != dstPort)         continue;
        return &r;
    }
    return nullptr;   // no match → implicit deny
}
