#include "ProtocolOverlays.h"

#include "Font.h"
#include "Packet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

void DrawTeTunnelOverlays(const std::vector<DeviceNode>& nodes,
                          const std::vector<Cable>& cables) {
    static const Color palette[] = {
        {251, 191, 36, 200}, {34, 211, 238, 200}, {232, 121, 249, 200},
        {163, 230, 53, 200}, {251, 113, 133, 200}, {56, 189, 248, 200},
    };

    int colorIndex = 0;
    for (const auto& node : nodes) {
        if (!node.rsvpEnabled) continue;
        for (const auto& tunnel : node.teTunnels) {
            if (!tunnel.isUp || tunnel.activePath.size() < 2) {
                ++colorIndex;
                continue;
            }
            const Color color = palette[colorIndex % 6];
            ++colorIndex;

            for (size_t hop = 0; hop + 1 < tunnel.activePath.size(); ++hop) {
                const int fromId = tunnel.activePath[hop];
                const int toId   = tunnel.activePath[hop + 1];
                const DeviceNode* from = FindNode(nodes, fromId);
                const DeviceNode* to   = FindNode(nodes, toId);
                const Cable* cable = FindCable(cables, fromId, toId);
                if (!from || !to || !cable) continue;

                const int fromPort = cable->fromId == fromId ? cable->fromPort : cable->toPort;
                const int toPort   = cable->fromId == toId ? cable->fromPort : cable->toPort;
                const Vector2 start = GetPortPosition(*from, fromPort);
                const Vector2 end   = GetPortPosition(*to, toPort);
                const Vector2 control1 = BezierCtrl(start, fromPort);
                const Vector2 control2 = BezierCtrl(end, toPort);

                const float offset = static_cast<float>(((colorIndex - 1) % 3) - 1) * 3.f;
                const Vector2 direction = {end.x - start.x, end.y - start.y};
                const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
                const Vector2 perpendicular = length > 0.001f
                    ? Vector2{-direction.y / length, direction.x / length}
                    : Vector2{};
                const Vector2 delta = {perpendicular.x * offset, perpendicular.y * offset};
                const Vector2 p0 = {start.x + delta.x, start.y + delta.y};
                const Vector2 p3 = {end.x + delta.x, end.y + delta.y};
                const Vector2 p1 = {control1.x + delta.x, control1.y + delta.y};
                const Vector2 p2 = {control2.x + delta.x, control2.y + delta.y};
                DrawSplineSegmentBezierCubic(p0, p1, p2, p3, 3.f, color);

                const Vector2 middle = {
                    (p0.x + 3.f * p1.x + 3.f * p2.x + p3.x) * 0.125f,
                    (p0.y + 3.f * p1.y + 3.f * p2.y + p3.y) * 0.125f,
                };
                char badge[32];
                std::snprintf(badge, sizeof(badge), "T%d.%uM", tunnel.id, tunnel.bandwidth);
                const float width = TW(badge, 9) + 8.f;
                DrawRectangleRounded({middle.x - width * 0.5f, middle.y - 9.f, width, 14.f},
                                     0.5f, 4, Color{15, 23, 42, 210});
                DrawTextEx(GFont(), badge,
                           {middle.x - width * 0.5f + 4.f, middle.y - 7.f},
                           FS(9), Sp(FS(9)), color);
            }
        }
    }
}

void DrawSrPolicyOverlays(const std::vector<DeviceNode>& nodes,
                          const std::vector<Cable>& cables) {
    static const Color colors[6] = {
        {59, 130, 246, 204}, {139, 92, 246, 204}, {16, 185, 129, 204},
        {249, 115, 22, 204}, {236, 72, 153, 204}, {234, 179, 8, 204},
    };

    for (const auto& node : nodes) {
        if (!node.srEnabled) continue;
        for (const auto& policy : node.srPolicies) {
            if (!policy.isActive || policy.activePath.size() < 2 || policy.id < 1) continue;
            const Color color = colors[(policy.id - 1) % 6];

            for (size_t hop = 0; hop + 1 < policy.activePath.size(); ++hop) {
                const int fromId = policy.activePath[hop];
                const int toId   = policy.activePath[hop + 1];
                const DeviceNode* from = FindNode(nodes, fromId);
                const DeviceNode* to   = FindNode(nodes, toId);
                const Cable* cable = FindCable(cables, fromId, toId);
                if (!from || !to || !cable) continue;

                const int fromPort = cable->fromId == fromId ? cable->fromPort : cable->toPort;
                const int toPort   = cable->fromId == toId ? cable->fromPort : cable->toPort;
                const Vector2 start = GetPortPosition(*from, fromPort);
                const Vector2 end   = GetPortPosition(*to, toPort);
                const Vector2 control1 = BezierCtrl(start, fromPort);
                const Vector2 control2 = BezierCtrl(end, toPort);

                for (int segment = 0; segment < 24; segment += 2) {
                    const Vector2 a = EvaluateCubicBezier(start, control1, control2, end,
                                                          segment / 24.f);
                    const Vector2 b = EvaluateCubicBezier(start, control1, control2, end,
                                                          (segment + 1) / 24.f);
                    DrawLineEx(a, b, 5.f, color);
                }
            }

            if (policy.labelStack.empty()) continue;
            const DeviceNode* head = FindNode(nodes, policy.activePath.front());
            if (!head) continue;

            std::string labels = "[ ";
            for (int index = static_cast<int>(policy.labelStack.size()) - 1; index >= 0; --index) {
                if (index < static_cast<int>(policy.labelStack.size()) - 1) labels += " | ";
                labels += std::to_string(policy.labelStack[index]);
            }
            labels += " ]";
            char badge[128];
            std::snprintf(badge, sizeof(badge), "Policy-%d push", policy.id);
            const float x = head->position.x;
            const float y = head->position.y - NODE_H * 0.5f - 36.f;
            const float width = std::max(TW(badge, 10), TW(labels.c_str(), 10)) + 10.f;
            const Rectangle box = {x - width * 0.5f, y, width, 26.f};
            DrawRectangleRounded(box, 0.4f, 4, Color{15, 23, 42, 230});
            DrawRectangleRoundedLinesEx(box, 0.4f, 4, 1.5f, color);
            DrawTextEx(GFont(), badge,
                       {(float)(int)(x - TW(badge, 10) * 0.5f), (float)(int)(y + 3.f)},
                       FS(10), Sp(FS(10)), color);
            DrawTextEx(GFont(), labels.c_str(),
                       {(float)(int)(x - TW(labels.c_str(), 10) * 0.5f), (float)(int)(y + 14.f)},
                       FS(10), Sp(FS(10)), WHITE);
        }
    }
}

void DrawSrv6PolicyOverlays(const std::vector<DeviceNode>& nodes,
                            const std::vector<Cable>& cables) {
    const Color color = {217, 70, 239, 210};
    for (const auto& node : nodes) {
        if (!node.srv6Enabled) continue;
        for (const auto& policy : node.srv6Policies) {
            if (!policy.isActive || policy.activePath.size() < 2) continue;
            for (std::size_t hop = 0; hop + 1 < policy.activePath.size(); ++hop) {
                const int fromId = policy.activePath[hop];
                const int toId = policy.activePath[hop + 1];
                const DeviceNode* from = FindNode(nodes, fromId);
                const DeviceNode* to = FindNode(nodes, toId);
                const Cable* cable = FindCable(cables, fromId, toId);
                if (!from || !to || !cable) continue;
                const int fromPort = cable->fromId == fromId ? cable->fromPort : cable->toPort;
                const int toPort = cable->fromId == toId ? cable->fromPort : cable->toPort;
                const Vector2 start = GetPortPosition(*from, fromPort);
                const Vector2 end = GetPortPosition(*to, toPort);
                const Vector2 control1 = BezierCtrl(start, fromPort);
                const Vector2 control2 = BezierCtrl(end, toPort);
                for (int part = 1; part < 24; part += 3) {
                    const Vector2 a = EvaluateCubicBezier(start, control1, control2, end,
                                                          part / 24.f);
                    const Vector2 b = EvaluateCubicBezier(start, control1, control2, end,
                                                          (part + 1) / 24.f);
                    DrawLineEx(a, b, 4.f, color);
                }
            }
            const DeviceNode* head = FindNode(nodes, policy.activePath.front());
            if (!head) continue;
            char badge[64];
            std::snprintf(badge, sizeof(badge), "SRv6 Policy-%d  SRH:%zu",
                          policy.id, policy.segmentSids.size());
            const float width = TW(badge, 10) + 10.f;
            const Rectangle box = {head->position.x - width * 0.5f,
                                   head->position.y + NODE_H * 0.5f + 10.f,
                                   width, 18.f};
            DrawRectangleRounded(box, 0.4f, 4, Color{15,23,42,230});
            DrawRectangleRoundedLinesEx(box, 0.4f, 4, 1.5f, color);
            DrawTextEx(GFont(), badge, {box.x + 5.f, box.y + 4.f},
                       FS(10), Sp(FS(10)), color);
        }
    }
}

void DrawSdwanPolicyOverlays(const std::vector<DeviceNode>& nodes,
                             const std::vector<Cable>& cables) {
    for (const auto& node : nodes) {
        if (!node.sdwanEnabled) continue;
        for (const auto& policy : node.sdwanPolicies) {
            if (!policy.isActive) continue;
            for (const auto& cable : cables) {
                int port = -1;
                int neighborId = -1;
                if (cable.fromId == node.id) { port = cable.fromPort; neighborId = cable.toId; }
                if (cable.toId == node.id) { port = cable.toPort; neighborId = cable.fromId; }
                if (port != policy.preferredPort && port != policy.selectedPort) continue;
                const DeviceNode* neighbor = FindNode(nodes, neighborId);
                if (!neighbor) continue;
                const int neighborPort = cable.fromId == neighborId ? cable.fromPort : cable.toPort;
                const Vector2 start = GetPortPosition(node, port);
                const Vector2 end = GetPortPosition(*neighbor, neighborPort);
                const Vector2 c1 = BezierCtrl(start, port);
                const Vector2 c2 = BezierCtrl(end, neighborPort);
                const Color color = port == policy.selectedPort
                    ? Color{34,197,94,220} : Color{239,68,68,190};
                if (port == policy.selectedPort)
                    DrawSplineSegmentBezierCubic(start,c1,c2,end,5.f,color);
                else for (int part=0; part<24; part+=3)
                    DrawLineEx(EvaluateCubicBezier(start,c1,c2,end,part/24.f),
                               EvaluateCubicBezier(start,c1,c2,end,(part+1)/24.f),4.f,color);
            }
        }
    }
}
