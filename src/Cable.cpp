#include "Cable.h"

const DeviceNode* FindNode(const std::vector<DeviceNode>& nodes, int id) {
    for (const auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

Vector2 BezierCtrl(Vector2 p, int port) {
    const float offset = 60.0f;
    switch (port) {
        case 0: return {p.x,           p.y - offset};
        case 1: return {p.x + offset,  p.y         };
        case 2: return {p.x,           p.y + offset};
        case 3: return {p.x - offset,  p.y         };
        default: return p;
    }
}
