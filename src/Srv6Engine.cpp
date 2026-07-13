#include "Srv6Engine.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

bool ParseGroups(const std::string& text, std::vector<uint16_t>& groups) {
    if (text.empty()) return true;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(':', start);
        const std::string token = text.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (token.empty() || token.size() > 4) return false;
        uint16_t value = 0;
        for (const unsigned char ch : token) {
            if (!std::isxdigit(ch)) return false;
            value = static_cast<uint16_t>(value * 16u +
                (ch >= '0' && ch <= '9' ? ch - '0' :
                 std::tolower(ch) - 'a' + 10));
        }
        groups.push_back(value);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

int NextHopNodeId(const DeviceNode& node, int port,
                  const std::vector<Cable>& cables) {
    for (const auto& cable : cables) {
        if (cable.broken) continue;
        if (cable.fromId == node.id && cable.fromPort == port) return cable.toId;
        if (cable.toId == node.id && cable.toPort == port) return cable.fromId;
    }
    return -1;
}

int NextHopPort(const DeviceNode& source, const DeviceNode& destination) {
    std::vector<std::string> addresses;
    if (ValidateIPOnly(destination.routerId)) addresses.push_back(destination.routerId);
    for (int port = 0; port < PORTS_PER_NODE; ++port) {
        if (destination.portIp[port].empty()) continue;
        const auto slash = destination.portIp[port].find('/');
        addresses.push_back(destination.portIp[port].substr(0, slash));
    }

    int bestPort = -1;
    int bestPrefix = -1;
    for (const auto& route : GetRoutingTable(source)) {
        if (route.src != ROUTE_CONNECTED && route.src != ROUTE_OSPF &&
            route.src != ROUTE_OSPF_IA) continue;
        for (const auto& address : addresses) {
            if (!IpInSubnet(address, route.dest)) continue;
            const int prefix = PrefixLen(route.dest);
            if (prefix > bestPrefix) {
                bestPrefix = prefix;
                bestPort = route.outPort;
            }
        }
    }
    return bestPort;
}

std::vector<int> OspfPath(int sourceId, int destinationId,
                          const std::vector<DeviceNode>& nodes,
                          const std::vector<Cable>& cables) {
    std::vector<int> path;
    std::unordered_set<int> visited;
    int current = sourceId;
    for (int hop = 0; hop < 16; ++hop) {
        if (!visited.insert(current).second) return {};
        path.push_back(current);
        if (current == destinationId) return path;
        const DeviceNode* node = FindNode(nodes, current);
        const DeviceNode* destination = FindNode(nodes, destinationId);
        if (!node || !destination) return {};
        const int port = NextHopPort(*node, *destination);
        if (port < 0) return {};
        current = NextHopNodeId(*node, port, cables);
        if (current < 0) return {};
    }
    return {};
}

}  // namespace

bool NormalizeIpv6Sid(const std::string& sid, std::string& key) {
    key.clear();
    if (sid.empty() || sid.find('.') != std::string::npos) return false;
    const std::size_t compression = sid.find("::");
    if (compression != std::string::npos && sid.find("::", compression + 2) != std::string::npos)
        return false;

    std::vector<uint16_t> left;
    std::vector<uint16_t> right;
    if (compression == std::string::npos) {
        if (!ParseGroups(sid, left) || left.size() != 8) return false;
    } else {
        if (!ParseGroups(sid.substr(0, compression), left) ||
            !ParseGroups(sid.substr(compression + 2), right) ||
            left.size() + right.size() >= 8) return false;
        left.insert(left.end(), 8 - left.size() - right.size(), 0);
        left.insert(left.end(), right.begin(), right.end());
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const uint16_t group : left) output << std::setw(4) << group;
    key = output.str();
    return true;
}

void UpdateSrv6(std::vector<DeviceNode>& nodes, const std::vector<Cable>& cables) {
    std::unordered_map<std::string, int> sidOwners;
    std::unordered_set<std::string> duplicates;
    for (const auto& node : nodes) {
        if (!node.srv6Enabled || node.srv6Sid.empty()) continue;
        std::string key;
        if (!NormalizeIpv6Sid(node.srv6Sid, key)) continue;
        if (!sidOwners.emplace(key, node.id).second) duplicates.insert(key);
    }
    for (const auto& duplicate : duplicates) sidOwners.erase(duplicate);

    for (auto& head : nodes) {
        for (auto& policy : head.srv6Policies) {
            policy.segmentHops.clear();
            policy.activePath.clear();
            policy.isActive = false;
            policy.statusMsg.clear();
            if (!head.srv6Enabled) {
                policy.statusMsg = "SRv6 disabled";
                continue;
            }
            if (!ValidateIPOnly(policy.destIp)) {
                policy.statusMsg = "Invalid payload destination";
                continue;
            }
            if (policy.segmentSids.empty()) {
                policy.statusMsg = "Add at least one SID";
                continue;
            }

            bool invalid = false;
            for (const auto& sid : policy.segmentSids) {
                std::string key;
                if (!NormalizeIpv6Sid(sid, key)) {
                    policy.statusMsg = "Invalid IPv6 SID";
                    invalid = true;
                    break;
                }
                if (duplicates.count(key)) {
                    policy.statusMsg = "Duplicate SRv6 SID";
                    invalid = true;
                    break;
                }
                const auto owner = sidOwners.find(key);
                if (owner == sidOwners.end()) {
                    policy.statusMsg = "Unknown SRv6 SID";
                    invalid = true;
                    break;
                }
                policy.segmentHops.push_back(owner->second);
            }
            if (invalid) continue;

            policy.activePath.push_back(head.id);
            int current = head.id;
            for (std::size_t segment = 0; segment < policy.segmentHops.size(); ++segment) {
                const auto leg = OspfPath(current, policy.segmentHops[segment], nodes, cables);
                if (leg.empty()) {
                    policy.activePath.clear();
                    policy.statusMsg = "Segment " + std::to_string(segment + 1) + " unreachable";
                    invalid = true;
                    break;
                }
                policy.activePath.insert(policy.activePath.end(), leg.begin() + 1, leg.end());
                current = policy.segmentHops[segment];
            }
            if (invalid) continue;
            policy.isActive = true;
            policy.statusMsg = "Active";
        }
    }
}
