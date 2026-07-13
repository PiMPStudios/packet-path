#include "ProtocolPanelController.h"

#include "Layout.h"
#include "RsvpEngine.h"
#include "SrEngine.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace {

DeviceNode* SelectedRouter(int selectedId, std::vector<DeviceNode>& nodes) {
    DeviceNode* node = FindNodeMut(nodes, selectedId);
    return node && node->type == ROUTER ? node : nullptr;
}

std::string MetricBuffer(float value) {
    if (value == 0.f) return {};
    std::ostringstream output;
    output << value;
    return output.str();
}

}  // namespace

void HandleTrafficEngineeringPanelClick(Vector2 mouse, int selectedId,
                                        std::vector<DeviceNode>& nodes,
                                        PanelState& panel,
                                        RsvpReplayState& replay) {
    DeviceNode* node = SelectedRouter(selectedId, nodes);
    if (!node) return;

    if (CheckCollisionPointRec(mouse, PnlTeToggleRect())) {
        node->rsvpEnabled = !node->rsvpEnabled;
        if (!node->rsvpEnabled) {
            node->teLfib.clear();
            node->teTunnels.clear();
            node->pendingTunnels.clear();
            panel.teExpandedIdx = -1;
            replay.active = false;
        }
    }
    if (!node->rsvpEnabled) return;

    for (int port = 0; port < PORTS_PER_NODE; ++port) {
        if (node->portIp[port].empty()) continue;
        if (CheckCollisionPointRec(mouse, PnlTePbwRect(port))) {
            panel.tePbwActivePort = panel.tePbwActivePort == port ? -1 : port;
            panel.tePbwBuf = std::to_string(node->portBandwidth[port]);
        }
    }

    for (int index = 0; index < static_cast<int>(node->teTunnels.size()); ++index) {
        if (!CheckCollisionPointRec(mouse, PnlTeTunnelRowRect(index))) continue;
        panel.teExpandedIdx = panel.teExpandedIdx == index ? -1 : index;
        panel.teActiveField = -1;
        if (panel.teExpandedIdx == index) {
            const auto& tunnel = node->teTunnels[index];
            panel.teDestBuf = tunnel.destIp;
            panel.teBwBuf   = std::to_string(tunnel.bandwidth);
            panel.teHopsBuf.clear();
            for (const auto& hop : tunnel.explicitHopIps)
                panel.teHopsBuf += hop + " ";
        }
    }

    if (panel.teExpandedIdx >= 0 &&
        panel.teExpandedIdx < static_cast<int>(node->teTunnels.size())) {
        auto& tunnel = node->teTunnels[panel.teExpandedIdx];
        float y = TeListBaseY() + panel.teExpandedIdx * TeRowH() + TeRowH();

        if (CheckCollisionPointRec(mouse,
            {(float)(CANVAS_W() + 56), y - 2.f, (float)(PANEL_W - 60), 20.f}))
            panel.teActiveField = 0;
        y += 24.f;
        if (CheckCollisionPointRec(mouse,
            {(float)(CANVAS_W() + 56), y - 2.f, (float)(PANEL_W - 60), 20.f}))
            panel.teActiveField = 1;
        y += 24.f;
        if (CheckCollisionPointRec(mouse,
            {(float)(CANVAS_W() + 56), y, 80.f, 20.f})) {
            tunnel.useExplicit = !tunnel.useExplicit;
            tunnel.headLabel   = 0;
        }
        y += 24.f;
        if (tunnel.useExplicit) {
            if (CheckCollisionPointRec(mouse,
                {(float)(CANVAS_W() + 56), y - 2.f, (float)(PANEL_W - 60), 20.f}))
                panel.teActiveField = 2;
            y += 24.f;
        }

        const float deleteWidth = (PANEL_W - 8.f) * 0.40f;
        const Rectangle simulate = {(float)(CANVAS_W() + 12), y,
                                    (PANEL_W - 8.f) * 0.55f, 22.f};
        const Rectangle remove = {(float)(CANVAS_W() + 12 + PANEL_W - 24) - deleteWidth,
                                  y, deleteWidth, 22.f};
        if (tunnel.isUp && CheckCollisionPointRec(mouse, simulate))
            StartRsvpReplay(replay, tunnel);
        if (CheckCollisionPointRec(mouse, remove)) {
            node->teTunnels.erase(node->teTunnels.begin() + panel.teExpandedIdx);
            panel.teExpandedIdx = -1;
            panel.teActiveField = -1;
            return;
        }
    }

    const int visible = static_cast<int>(node->teTunnels.size())
                      + (panel.teExpandedIdx >= 0 ? 1 : 0);
    if (CheckCollisionPointRec(mouse, PnlTeAddBtnRect(visible))) {
        TeTunnel tunnel;
        for (const auto& existing : node->teTunnels)
            tunnel.id = std::max(tunnel.id, existing.id);
        if (++tunnel.id <= 255) node->teTunnels.push_back(tunnel);
    }
}

void HandleSegmentRoutingPanelClick(Vector2 mouse, int selectedId,
                                    std::vector<DeviceNode>& nodes,
                                    PanelState& panel) {
    DeviceNode* node = SelectedRouter(selectedId, nodes);
    if (!node) return;

    if (CheckCollisionPointRec(mouse, PnlSrToggleRect())) {
        node->srEnabled = !node->srEnabled;
        return;
    }
    if (CheckCollisionPointRec(mouse, PnlSrNodeSidRect())) {
        panel.srNodeSidEditing = true;
        panel.srNodeSidBuf = node->nodeSid > 0 ? std::to_string(node->nodeSid) : "";
        return;
    }

    for (int index = 0; index < static_cast<int>(node->srPolicies.size()); ++index) {
        const int expansionAbove = panel.srExpandedIdx >= 0 && panel.srExpandedIdx < index ? 1 : 0;
        Rectangle row = PnlSrPolicyRowRect(index);
        row.y += expansionAbove * SrFormH();
        if (!CheckCollisionPointRec(mouse, row)) continue;

        if (panel.srExpandedIdx == index) {
            panel.srExpandedIdx = -1;
            panel.srActiveField = -1;
        } else {
            panel.srExpandedIdx = index;
            panel.srActiveField = -1;
            panel.srDestBuf = node->srPolicies[index].destIp;
            panel.srSegsBuf.clear();
            for (int segment = 0;
                 segment < static_cast<int>(node->srPolicies[index].segmentIps.size());
                 ++segment) {
                if (segment > 0) panel.srSegsBuf += ", ";
                panel.srSegsBuf += node->srPolicies[index].segmentIps[segment];
            }
        }
        return;
    }

    if (panel.srExpandedIdx >= 0 &&
        panel.srExpandedIdx < static_cast<int>(node->srPolicies.size())) {
        float y = PnlSrPolicyRowRect(panel.srExpandedIdx).y + SrRowH();
        const float x = CANVAS_W() + 12.f;
        const float width = PANEL_W - 24.f;
        if (CheckCollisionPointRec(mouse, {x + 52.f, y - 2.f, width - 56.f, 20.f})) {
            panel.srActiveField = 0;
            return;
        }
        y += 24.f;
        if (CheckCollisionPointRec(mouse, {x + 52.f, y - 2.f, width - 56.f, 20.f})) {
            panel.srActiveField = 1;
            return;
        }
        y += 24.f;

        const auto& policy = node->srPolicies[panel.srExpandedIdx];
        if (!policy.labelStack.empty() || !policy.segmentIps.empty()) y += 16.f;
        const float deleteWidth = (width - 8.f) * 0.40f;
        if (CheckCollisionPointRec(mouse,
            {x + width - deleteWidth, y, deleteWidth, 22.f})) {
            node->srPolicies.erase(node->srPolicies.begin() + panel.srExpandedIdx);
            panel.srExpandedIdx = -1;
            panel.srActiveField = -1;
            return;
        }
    }

    const int visible = static_cast<int>(node->srPolicies.size())
                      + (panel.srExpandedIdx >= 0 ? 1 : 0);
    if (CheckCollisionPointRec(mouse, PnlSrAddBtnRect(visible))) {
        SrPolicy policy;
        for (const auto& existing : node->srPolicies)
            policy.id = std::max(policy.id, existing.id);
        if (++policy.id <= 255) {
            node->srPolicies.push_back(policy);
            panel.srExpandedIdx = static_cast<int>(node->srPolicies.size()) - 1;
            panel.srActiveField = 0;
            panel.srDestBuf.clear();
            panel.srSegsBuf.clear();
        }
    }
}

void UpdateTrafficEngineeringPanelInput(int selectedId,
                                        std::vector<DeviceNode>& nodes,
                                        PanelState& panel) {
    DeviceNode* node = SelectedRouter(selectedId, nodes);
    if (!node) return;

    if (panel.tePbwActivePort >= 0) {
        UpdateTextField(panel.tePbwBuf, 7);
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) {
            const int port = panel.tePbwActivePort;
            node->portBandwidth[port] = static_cast<uint32_t>(
                std::max(1, std::atoi(panel.tePbwBuf.c_str())));
            panel.tePbwActivePort = -1;
        }
    }

    if (panel.teExpandedIdx < 0 ||
        panel.teExpandedIdx >= static_cast<int>(node->teTunnels.size())) return;
    auto& tunnel = node->teTunnels[panel.teExpandedIdx];
    if (panel.teActiveField == 0) {
        UpdateTextField(panel.teDestBuf, 15);
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) {
            tunnel.destIp = panel.teDestBuf;
            tunnel.headLabel = 0;
            panel.teActiveField = -1;
        }
    } else if (panel.teActiveField == 1) {
        UpdateTextField(panel.teBwBuf, 7);
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) {
            tunnel.bandwidth = static_cast<uint32_t>(
                std::max(0, std::atoi(panel.teBwBuf.c_str())));
            tunnel.headLabel = 0;
            panel.teActiveField = -1;
        }
    } else if (panel.teActiveField == 2) {
        UpdateTextField(panel.teHopsBuf, 120);
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) {
            tunnel.explicitHopIps.clear();
            std::istringstream input(panel.teHopsBuf);
            std::string token;
            while (input >> token) tunnel.explicitHopIps.push_back(token);
            ResolveExplicitHops(tunnel, nodes);
            tunnel.headLabel = 0;
            panel.teActiveField = -1;
        }
    }
}

void UpdateSegmentRoutingPanelInput(int selectedId,
                                    std::vector<DeviceNode>& nodes,
                                    const std::vector<Cable>& cables,
                                    PanelState& panel) {
    DeviceNode* node = SelectedRouter(selectedId, nodes);
    if (!node) return;

    if (panel.srNodeSidEditing) {
        for (int key = GetCharPressed(); key > 0; key = GetCharPressed()) {
            if (key >= '0' && key <= '9' && panel.srNodeSidBuf.size() < 3)
                panel.srNodeSidBuf += static_cast<char>(key);
        }
        if (IsKeyPressed(KEY_BACKSPACE) && !panel.srNodeSidBuf.empty())
            panel.srNodeSidBuf.pop_back();
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
            if (!panel.srNodeSidBuf.empty()) {
                uint32_t sid = static_cast<uint32_t>(std::stoul(panel.srNodeSidBuf));
                node->nodeSid = std::clamp(sid, 1u, SRGB_SIZE - 1);
            }
            panel.srNodeSidEditing = false;
        }
        return;
    }

    if (panel.srExpandedIdx < 0 ||
        panel.srExpandedIdx >= static_cast<int>(node->srPolicies.size()) ||
        panel.srActiveField < 0) return;
    auto& policy = node->srPolicies[panel.srExpandedIdx];

    std::string* buffer = panel.srActiveField == 0 ? &panel.srDestBuf : &panel.srSegsBuf;
    const std::size_t limit = panel.srActiveField == 0 ? 15 : 160;
    for (int key = GetCharPressed(); key > 0; key = GetCharPressed()) {
        if (key >= 32 && key <= 126 && buffer->size() < limit)
            *buffer += static_cast<char>(key);
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !buffer->empty()) buffer->pop_back();

    if (panel.srActiveField == 0) {
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) {
            policy.destIp = panel.srDestBuf;
            panel.srActiveField = 1;
        } else if (IsKeyPressed(KEY_ESCAPE)) {
            policy.destIp = panel.srDestBuf;
            panel.srActiveField = -1;
        }
        return;
    }

    policy.segmentIps.clear();
    std::istringstream input(panel.srSegsBuf);
    std::string token;
    while (std::getline(input, token, ',')) {
        const auto start = token.find_first_not_of(" \t");
        const auto end   = token.find_last_not_of(" \t");
        if (start != std::string::npos)
            policy.segmentIps.push_back(token.substr(start, end - start + 1));
    }
    policy.segmentsResolved = false;
    ResolveSrSegments(policy, nodes, cables);
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ESCAPE))
        panel.srActiveField = -1;
}

void HandleSrv6PanelClick(Vector2 mouse, int selectedId,
                          std::vector<DeviceNode>& nodes, PanelState& panel) {
    DeviceNode* node = SelectedRouter(selectedId, nodes);
    if (!node) return;
    if (CheckCollisionPointRec(mouse, PnlSrv6ToggleRect())) {
        node->srv6Enabled = !node->srv6Enabled;
        return;
    }
    if (!node->srv6Enabled) return;
    if (CheckCollisionPointRec(mouse, PnlSrv6SidRect())) {
        panel.srv6SidEditing = true;
        panel.srv6SidBuf = node->srv6Sid;
        return;
    }

    for (int index = 0; index < static_cast<int>(node->srv6Policies.size()); ++index) {
        Rectangle row = PnlSrv6PolicyRowRect(index);
        if (panel.srv6ExpandedIdx >= 0 && panel.srv6ExpandedIdx < index)
            row.y += Srv6FormH();
        if (!CheckCollisionPointRec(mouse, row)) continue;
        if (panel.srv6ExpandedIdx == index) {
            panel.srv6ExpandedIdx = -1;
            panel.srv6ActiveField = -1;
        } else {
            panel.srv6ExpandedIdx = index;
            panel.srv6ActiveField = -1;
            panel.srv6DestBuf = node->srv6Policies[index].destIp;
            panel.srv6SegsBuf.clear();
            for (std::size_t segment = 0;
                 segment < node->srv6Policies[index].segmentSids.size(); ++segment) {
                if (segment) panel.srv6SegsBuf += ", ";
                panel.srv6SegsBuf += node->srv6Policies[index].segmentSids[segment];
            }
        }
        return;
    }

    if (panel.srv6ExpandedIdx >= 0 &&
        panel.srv6ExpandedIdx < static_cast<int>(node->srv6Policies.size())) {
        float y = PnlSrv6PolicyRowRect(panel.srv6ExpandedIdx).y + Srv6RowH();
        const float x = CANVAS_W() + 12.f;
        const float width = PANEL_W - 24.f;
        if (CheckCollisionPointRec(mouse, {x + 52.f, y - 2.f, width - 56.f, 20.f})) {
            panel.srv6ActiveField = 0;
            return;
        }
        y += 24.f;
        if (CheckCollisionPointRec(mouse, {x + 52.f, y - 2.f, width - 56.f, 20.f})) {
            panel.srv6ActiveField = 1;
            return;
        }
        y += 16.f;
        const float deleteWidth = (width - 8.f) * 0.40f;
        if (CheckCollisionPointRec(mouse,
            {x + width - deleteWidth, y, deleteWidth, 22.f})) {
            node->srv6Policies.erase(node->srv6Policies.begin() + panel.srv6ExpandedIdx);
            panel.srv6ExpandedIdx = -1;
            panel.srv6ActiveField = -1;
            return;
        }
    }

    const int visible = static_cast<int>(node->srv6Policies.size()) +
                        (panel.srv6ExpandedIdx >= 0 ? 1 : 0);
    Rectangle addButton = PnlSrv6AddBtnRect(visible);
    if (panel.srv6ExpandedIdx >= 0)
        addButton.y += Srv6FormH() - Srv6RowH();
    if (CheckCollisionPointRec(mouse, addButton)) {
        Srv6Policy policy;
        for (const auto& existing : node->srv6Policies)
            policy.id = std::max(policy.id, existing.id);
        if (++policy.id <= 255) {
            node->srv6Policies.push_back(policy);
            panel.srv6ExpandedIdx = static_cast<int>(node->srv6Policies.size()) - 1;
            panel.srv6ActiveField = 0;
            panel.srv6DestBuf.clear();
            panel.srv6SegsBuf.clear();
        }
    }
}

void UpdateSrv6PanelInput(int selectedId, std::vector<DeviceNode>& nodes,
                          PanelState& panel) {
    DeviceNode* node = SelectedRouter(selectedId, nodes);
    if (!node) return;
    if (panel.srv6SidEditing) {
        UpdateTextField(panel.srv6SidBuf, 48);
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ESCAPE)) {
            node->srv6Sid = panel.srv6SidBuf;
            panel.srv6SidEditing = false;
        }
        return;
    }
    if (panel.srv6ExpandedIdx < 0 ||
        panel.srv6ExpandedIdx >= static_cast<int>(node->srv6Policies.size()) ||
        panel.srv6ActiveField < 0) return;

    auto& policy = node->srv6Policies[panel.srv6ExpandedIdx];
    std::string& buffer = panel.srv6ActiveField == 0
        ? panel.srv6DestBuf : panel.srv6SegsBuf;
    UpdateTextField(buffer, panel.srv6ActiveField == 0 ? 15 : 220);
    if (panel.srv6ActiveField == 0) {
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) {
            policy.destIp = panel.srv6DestBuf;
            panel.srv6ActiveField = 1;
        } else if (IsKeyPressed(KEY_ESCAPE)) {
            policy.destIp = panel.srv6DestBuf;
            panel.srv6ActiveField = -1;
        }
        return;
    }

    policy.segmentSids.clear();
    std::istringstream input(panel.srv6SegsBuf);
    std::string token;
    while (std::getline(input, token, ',')) {
        const auto start = token.find_first_not_of(" \t");
        const auto end = token.find_last_not_of(" \t");
        if (start != std::string::npos)
            policy.segmentSids.push_back(token.substr(start, end - start + 1));
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ESCAPE))
        panel.srv6ActiveField = -1;
}

void HandleSdwanPanelClick(Vector2 mouse, int selectedId,
                           std::vector<DeviceNode>& nodes, PanelState& panel) {
    DeviceNode* node = SelectedRouter(selectedId, nodes);
    if (!node) return;
    if (CheckCollisionPointRec(mouse, PnlSdwanToggleRect())) {
        node->sdwanEnabled = !node->sdwanEnabled;
        if (node->sdwanEnabled && node->sdwanPolicies.empty()) {
            SdwanPolicy policy;
            policy.id = 1;
            node->sdwanPolicies.push_back(policy);
        }
        return;
    }
    if (!node->sdwanEnabled) return;
    if (node->sdwanPolicies.empty()) {
        if (CheckCollisionPointRec(mouse, PnlSdwanAddRect())) {
            SdwanPolicy policy;
            policy.id = 1;
            node->sdwanPolicies.push_back(policy);
        }
        return;
    }
    auto& policy = node->sdwanPolicies.front();
    for (int field = 0; field < 4; ++field) {
        if (!CheckCollisionPointRec(mouse, PnlSdwanFieldRect(field))) continue;
        panel.sdwanActiveField = field;
        panel.sdwanDestBuf = policy.destIp;
        panel.sdwanLatencyBuf = MetricBuffer(policy.maxLatencyMs);
        panel.sdwanJitterBuf = MetricBuffer(policy.maxJitterMs);
        panel.sdwanLossBuf = MetricBuffer(policy.maxLossPct);
        return;
    }
    if (CheckCollisionPointRec(mouse, PnlSdwanPreferredRect()))
        policy.preferredPort = (policy.preferredPort + 1) % PORTS_PER_NODE;
    if (CheckCollisionPointRec(mouse, PnlSdwanBackupRect()))
        policy.backupPort = (policy.backupPort + 1) % PORTS_PER_NODE;
}

void UpdateSdwanPanelInput(int selectedId, std::vector<DeviceNode>& nodes,
                           PanelState& panel) {
    DeviceNode* node = SelectedRouter(selectedId, nodes);
    if (!node || node->sdwanPolicies.empty() || panel.sdwanActiveField < 0) return;
    std::string* buffer = &panel.sdwanDestBuf;
    if (panel.sdwanActiveField == 1) buffer = &panel.sdwanLatencyBuf;
    if (panel.sdwanActiveField == 2) buffer = &panel.sdwanJitterBuf;
    if (panel.sdwanActiveField == 3) buffer = &panel.sdwanLossBuf;
    UpdateTextField(*buffer, panel.sdwanActiveField == 0 ? 15 : 8);
    if (!IsKeyPressed(KEY_ENTER) && !IsKeyPressed(KEY_TAB) &&
        !IsKeyPressed(KEY_ESCAPE)) return;
    auto& policy = node->sdwanPolicies.front();
    try {
        if (panel.sdwanActiveField == 0) policy.destIp = panel.sdwanDestBuf;
        if (panel.sdwanActiveField == 1)
            policy.maxLatencyMs = std::max(0.f, std::stof(panel.sdwanLatencyBuf));
        if (panel.sdwanActiveField == 2)
            policy.maxJitterMs = std::max(0.f, std::stof(panel.sdwanJitterBuf));
        if (panel.sdwanActiveField == 3)
            policy.maxLossPct = std::clamp(std::stof(panel.sdwanLossBuf), 0.f, 100.f);
    } catch (...) {
        // Keep the previous valid value; the engine status remains actionable.
    }
    panel.sdwanActiveField = -1;
}
