#include "Level.h"
#include "SimulationEngine.h"
#include "UI.h"
#include <algorithm>

namespace {

const DeviceNode* FindNodeByLabel(const std::vector<DeviceNode>& nodes,
                                  const std::string& label) {
    const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const auto& node) {
        return node.label == label;
    });
    return found == nodes.end() ? nullptr : &*found;
}

bool MeetsTeRequirement(const WinCondition& condition,
                        const ForwardResult& result,
                        const std::vector<DeviceNode>& nodes) {
    if (condition.requiresTeTunnelOnDevice.empty()) return true;

    const DeviceNode* head = FindNodeByLabel(nodes, condition.requiresTeTunnelOnDevice);
    if (!head) return false;

    int requiredViaId = -1;
    if (!condition.requiresTePathVia.empty()) {
        const DeviceNode* via = FindNodeByLabel(nodes, condition.requiresTePathVia);
        if (!via) return false;
        requiredViaId = via->id;
    }

    for (const auto& tunnel : head->teTunnels) {
        if (!tunnel.isUp ||
            (condition.requiresTeTunnelId != 0 &&
             tunnel.id != condition.requiresTeTunnelId) ||
            tunnel.bandwidth < condition.requiresTeMinBandwidth ||
            (condition.requiresTeMode == "cspf" && tunnel.useExplicit) ||
            (condition.requiresTeMode == "explicit" && !tunnel.useExplicit)) {
            continue;
        }
        if (requiredViaId >= 0 &&
            std::find(tunnel.activePath.begin(), tunnel.activePath.end(), requiredViaId) ==
                tunnel.activePath.end()) {
            continue;
        }

        const bool usedForForwarding =
            std::any_of(result.hops.begin(), result.hops.end(), [&](const auto& hop) {
                return hop.nodeId == head->id && hop.tunnelId == tunnel.id;
            });
        if (usedForForwarding) return true;
    }
    return false;
}

bool MeetsSrRequirement(const WinCondition& condition,
                        const ForwardResult& result,
                        const std::vector<DeviceNode>& nodes) {
    if (condition.requiresSrPolicyOnDevice.empty()) return true;

    const DeviceNode* head = FindNodeByLabel(nodes, condition.requiresSrPolicyOnDevice);
    if (!head) return false;

    int requiredViaId = -1;
    if (!condition.requiresSrPathVia.empty()) {
        const DeviceNode* via = FindNodeByLabel(nodes, condition.requiresSrPathVia);
        if (!via) return false;
        requiredViaId = via->id;
    }

    for (const auto& policy : head->srPolicies) {
        if (!policy.isActive ||
            (condition.requiresSrPolicyId != 0 &&
             policy.id != condition.requiresSrPolicyId) ||
            (!condition.requiresSrSegments.empty() &&
             policy.segmentIps != condition.requiresSrSegments)) {
            continue;
        }
        if (requiredViaId >= 0 &&
            std::find(policy.activePath.begin(), policy.activePath.end(), requiredViaId) ==
                policy.activePath.end()) {
            continue;
        }

        const bool usedForForwarding =
            std::any_of(result.hops.begin(), result.hops.end(), [&](const auto& hop) {
                return hop.nodeId == head->id && hop.policyId == policy.id;
            });
        if (usedForForwarding) return true;
    }
    return false;
}

bool MeetsSrv6Requirement(const WinCondition& condition,
                          const ForwardResult& result,
                          const std::vector<DeviceNode>& nodes) {
    if (condition.requiresSrv6PolicyOnDevice.empty()) return true;
    const DeviceNode* head = FindNodeByLabel(nodes, condition.requiresSrv6PolicyOnDevice);
    if (!head) return false;

    int requiredViaId = -1;
    if (!condition.requiresSrv6PathVia.empty()) {
        const DeviceNode* via = FindNodeByLabel(nodes, condition.requiresSrv6PathVia);
        if (!via) return false;
        requiredViaId = via->id;
    }

    for (const auto& policy : head->srv6Policies) {
        if (!policy.isActive ||
            (condition.requiresSrv6PolicyId != 0 &&
             policy.id != condition.requiresSrv6PolicyId) ||
            (!condition.requiresSrv6Segments.empty() &&
             policy.segmentSids != condition.requiresSrv6Segments)) continue;
        if (requiredViaId >= 0 &&
            std::find(policy.activePath.begin(), policy.activePath.end(), requiredViaId) ==
                policy.activePath.end()) continue;
        const bool used = std::any_of(result.hops.begin(), result.hops.end(),
            [&](const HopDecision& hop) {
                return hop.nodeId == head->id && hop.srv6PolicyId == policy.id;
            });
        if (used) return true;
    }
    return false;
}

}  // namespace

void ApplyLevel(const LevelDef& def,
                std::vector<DeviceNode>& nodes,
                std::vector<Cable>& cables,
                int& selectedId) {
    nodes      = def.devices;
    cables     = def.cables;
    selectedId = -1;
    int maxId  = 0;
    for (const auto& n : nodes) maxId = std::max(maxId, n.id);
    SetNextId(maxId + 1);
}

int CheckWinConditions(const LevelDef& def,
                       const std::vector<DeviceNode>& nodes,
                       const std::vector<Cable>& cables) {
    int passed = 0;
    for (const auto& wc : def.winConditions) {
        const DeviceNode* src = nullptr;
        const DeviceNode* dst = nullptr;
        for (const auto& n : nodes) {
            if (n.label == wc.srcLabel) src = &n;
            if (n.label == wc.dstLabel) dst = &n;
        }
        if (!src || !dst) continue;
        std::string dstIp = GetFirstValidIp(*dst);
        if (dstIp.empty()) continue;
        std::string wcsrcIp = GetFirstValidIp(*src);
        ForwardResult fr = SimulateForward(src->id, dstIp, nodes, cables, wcsrcIp);
        if (fr.success) {
            if (!MeetsTeRequirement(wc, fr, nodes)) continue;
            if (!MeetsSrRequirement(wc, fr, nodes)) continue;
            if (!MeetsSrv6Requirement(wc, fr, nodes)) continue;
            if (!wc.requiresNatOnDevice.empty()) {
                bool natOk = false;
                for (const auto& nd : nodes)
                    if (nd.label == wc.requiresNatOnDevice && nd.natEnabled)
                        { natOk = true; break; }
                if (natOk) ++passed;
            } else {
                ++passed;
            }
        }
    }
    return passed;
}

int ComputeStars(int failedAttempts) {
    if (failedAttempts == 0) return 3;
    if (failedAttempts <= 2) return 2;
    return 1;
}
