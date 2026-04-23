#include "Packet.h"
#include <algorithm>

// Returns first valid plain IP (no prefix) from a node's interfaces.
std::string GetFirstValidIp(const DeviceNode& n) {
    for (int i = 0; i < PORTS_PER_NODE; ++i) {
        const auto& ip = n.portIp[i];
        auto slash = ip.find('/');
        std::string plain = (slash != std::string::npos) ? ip.substr(0, slash) : ip;
        if (ValidateIPOnly(plain)) return plain;
    }
    auto slash = n.mgmtIp.find('/');
    std::string plain = (slash != std::string::npos)
                      ? n.mgmtIp.substr(0, slash) : n.mgmtIp;
    if (ValidateIPOnly(plain)) return plain;
    return "";
}

// Returns first cable connecting node a to node b (either direction).
const Cable* FindCable(const std::vector<Cable>& cables, int a, int b) {
    for (const auto& c : cables)
        if ((c.fromId == a && c.toId == b) || (c.fromId == b && c.toId == a))
            return &c;
    return nullptr;
}

Vector2 EvaluateCubicBezier(Vector2 p0, Vector2 c1, Vector2 c2, Vector2 p3, float t) {
    float it = 1.f - t;
    return {
        it*it*it*p0.x + 3*it*it*t*c1.x + 3*it*t*t*c2.x + t*t*t*p3.x,
        it*it*it*p0.y + 3*it*it*t*c1.y + 3*it*t*t*c2.y + t*t*t*p3.y
    };
}

std::string BuildPathStr(const std::vector<int>& path,
                         const std::vector<DeviceNode>& nodes) {
    std::string s;
    for (int i = 0; i < (int)path.size(); ++i) {
        if (i > 0) s += " \xe2\x86\x92 ";   // UTF-8 →
        const DeviceNode* n = FindNode(nodes, path[i]);
        s += n ? n->label : "?";
    }
    return s;
}

void UpdatePacketAnim(PacketAnim& anim, float dt,
                      const std::vector<DeviceNode>& nodes,
                      const std::vector<Cable>& cables)
{
    (void)nodes; (void)cables;
    if (anim.done) {
        anim.failPulse    = std::max(0.f, anim.failPulse    - dt);
        anim.successPulse = std::max(0.f, anim.successPulse - dt);
        return;
    }

    const auto& path = anim.result.path;
    if ((int)path.size() <= 1) {
        anim.done = true;
        if (anim.result.success) anim.successPulse = 0.5f;
        else                     anim.failPulse    = 0.5f;
        return;
    }

    anim.t += dt / HOP_DURATION;
    if (anim.t >= 1.f) {
        anim.t = 0.f;
        anim.hop++;
        if (anim.hop >= (int)path.size() - 1) {
            anim.done = true;
            if (anim.result.success) anim.successPulse = 0.5f;
            else                     anim.failPulse    = 0.5f;
        }
    }
}
