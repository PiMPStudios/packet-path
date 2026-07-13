#include "SdwanEngine.h"

namespace {

bool PortIsLive(const DeviceNode& node, int port,
                const std::vector<DeviceNode>& nodes,
                const std::vector<Cable>& cables) {
    if (port < 0 || port >= PORTS_PER_NODE || node.portIp[port].empty()) return false;
    for (const auto& cable : cables) {
        if (cable.broken) continue;
        int neighborId = -1;
        if (cable.fromId == node.id && cable.fromPort == port) neighborId = cable.toId;
        if (cable.toId == node.id && cable.toPort == port) neighborId = cable.fromId;
        const DeviceNode* neighbor = FindNode(nodes, neighborId);
        if (neighbor && !neighbor->crashed) return true;
    }
    return false;
}

bool MeetsSla(const DeviceNode& node, int port, const SdwanPolicy& policy,
              const std::vector<DeviceNode>& nodes,
              const std::vector<Cable>& cables) {
    return PortIsLive(node, port, nodes, cables) &&
           policy.maxLatencyMs > 0.f && policy.maxJitterMs > 0.f &&
           policy.maxLossPct > 0.f &&
           node.sdwanLatencyMs[port] <= policy.maxLatencyMs &&
           node.sdwanJitterMs[port] <= policy.maxJitterMs &&
           node.sdwanLossPct[port] <= policy.maxLossPct;
}

}  // namespace

void UpdateSdwan(std::vector<DeviceNode>& nodes, const std::vector<Cable>& cables) {
    for (auto& node : nodes) {
        for (auto& policy : node.sdwanPolicies) {
            policy.isActive = false;
            policy.selectedPort = -1;
            policy.usingBackup = false;
            if (!node.sdwanEnabled) {
                policy.statusMsg = "SD-WAN disabled";
                continue;
            }
            if (!ValidateIPOnly(policy.destIp)) {
                policy.statusMsg = "Invalid destination";
                continue;
            }
            if (policy.preferredPort == policy.backupPort || policy.preferredPort < 0 ||
                policy.backupPort < 0) {
                policy.statusMsg = "Choose two WAN ports";
                continue;
            }
            if (MeetsSla(node, policy.preferredPort, policy, nodes, cables)) {
                policy.selectedPort = policy.preferredPort;
                policy.statusMsg = "Primary meets SLA";
            } else if (MeetsSla(node, policy.backupPort, policy, nodes, cables)) {
                policy.selectedPort = policy.backupPort;
                policy.usingBackup = true;
                policy.statusMsg = "Backup selected";
            } else {
                policy.statusMsg = "No path meets SLA";
                continue;
            }
            policy.isActive = true;
        }
    }
}
