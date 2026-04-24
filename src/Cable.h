#pragma once
#include "Device.h"
#include <vector>

struct Cable {
    int  fromId, fromPort;
    int  toId,   toPort;
    bool broken = false;
};

const DeviceNode* FindNode(const std::vector<DeviceNode>& nodes, int id);
DeviceNode*       FindNodeMut(std::vector<DeviceNode>& nodes, int id);
Vector2           BezierCtrl(Vector2 p, int port);
