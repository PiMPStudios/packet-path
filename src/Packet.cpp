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

    if (anim.paused) return;
    anim.t += dt * anim.speedMult / HOP_DURATION;
    if (anim.t >= 1.f) {
        anim.t = 0.f;
        anim.hop++;
        if (anim.hop < (int)anim.result.hops.size()) {
            uint32_t raw = anim.result.hops[anim.hop].outLabel;
            anim.currentLabel = (raw == MPLS_IMPLICIT_NULL) ? 0 : raw;
            anim.currentVlan  = anim.result.hops[anim.hop].vlanTag;
            anim.currentVni   = anim.result.hops[anim.hop].vxlanVni;
            anim.currentSrv6Sid = anim.result.hops[anim.hop].srv6ActiveSid;
            anim.currentSrv6SegmentsLeft = anim.result.hops[anim.hop].srv6SegmentsLeft;
        } else {
            anim.currentLabel = 0;
            anim.currentVlan  = 0;
            anim.currentVni   = 0;
            anim.currentSrv6Sid.clear();
            anim.currentSrv6SegmentsLeft = -1;
        }
        if (anim.hop >= (int)path.size() - 1) {
            anim.done = true;
            anim.currentLabel = 0;
            anim.currentVlan  = 0;
            anim.currentVni   = 0;
            anim.currentSrv6Sid.clear();
            anim.currentSrv6SegmentsLeft = -1;
            if (anim.result.success) anim.successPulse = 0.5f;
            else                     anim.failPulse    = 0.5f;
        }
    }
}

void StepForwardAnim(PacketAnim& anim) {
    if (anim.done) return;
    const auto& path = anim.result.path;
    if ((int)path.size() <= 1) {
        anim.done = true;
        if (anim.result.success) anim.successPulse = 0.5f;
        else                     anim.failPulse    = 0.5f;
        return;
    }
    anim.t = 0.f;
    anim.hop++;
    if (anim.hop < (int)anim.result.hops.size()) {
        uint32_t raw      = anim.result.hops[anim.hop].outLabel;
        anim.currentLabel = (raw == MPLS_IMPLICIT_NULL) ? 0 : raw;
        anim.currentVlan  = anim.result.hops[anim.hop].vlanTag;
        anim.currentVni   = anim.result.hops[anim.hop].vxlanVni;
        anim.currentSrv6Sid = anim.result.hops[anim.hop].srv6ActiveSid;
        anim.currentSrv6SegmentsLeft = anim.result.hops[anim.hop].srv6SegmentsLeft;
    } else {
        anim.currentLabel = 0;
        anim.currentVlan  = 0;
        anim.currentVni   = 0;
        anim.currentSrv6Sid.clear();
        anim.currentSrv6SegmentsLeft = -1;
    }
    if (anim.hop >= (int)path.size() - 1) {
        anim.done         = true;
        anim.currentLabel = 0;
        anim.currentVlan  = 0;
        anim.currentVni   = 0;
        anim.currentSrv6Sid.clear();
        anim.currentSrv6SegmentsLeft = -1;
        if (anim.result.success) anim.successPulse = 0.5f;
        else                     anim.failPulse    = 0.5f;
    }
}

Vector2 GetPacketWorldPos(const PacketAnim& anim,
                          const std::vector<DeviceNode>& nodes,
                          const std::vector<Cable>& cables)
{
    const auto& path = anim.result.path;
    if (path.empty() || anim.done || anim.hop >= (int)path.size() - 1)
        return {-99999.f, -99999.f};

    int fromId = path[anim.hop];
    int toId   = path[anim.hop + 1];
    const DeviceNode* fromNode = FindNode(nodes, fromId);
    const DeviceNode* toNode   = FindNode(nodes, toId);
    const Cable*      cable    = FindCable(cables, fromId, toId);
    if (!fromNode || !toNode || !cable) return {-99999.f, -99999.f};

    int fromPort = (cable->fromId == fromId) ? cable->fromPort : cable->toPort;
    int toPort   = (cable->fromId == toId)   ? cable->fromPort : cable->toPort;

    Vector2 p0 = GetPortPosition(*fromNode, fromPort);
    Vector2 p3 = GetPortPosition(*toNode,   toPort);
    Vector2 c1 = BezierCtrl(p0, fromPort);
    Vector2 c2 = BezierCtrl(p3, toPort);
    return EvaluateCubicBezier(p0, c1, c2, p3, anim.t);
}
