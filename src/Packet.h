#pragma once
#include "Device.h"
#include "Cable.h"
#include <string>
#include <vector>

static const float HOP_DURATION = 0.4f;  // seconds per hop segment

enum SimMode { SIM_IDLE, SIM_SELECTING_DST, SIM_ANIMATING };

struct PacketAnim {
    ForwardResult result;
    int      hop          = 0;
    float    t            = 0.f;
    bool     done         = false;
    float    failPulse    = 0.f;
    float    successPulse = 0.f;
    uint32_t currentLabel = 0;
    int      currentVlan  = 0;
    uint32_t currentVni   = 0;
    bool     paused       = false;
    float    speedMult    = 1.f;
};

struct SimState {
    SimMode    mode  = SIM_IDLE;
    int        srcId = -1;
    PacketAnim anim;
};

std::string  GetFirstValidIp(const DeviceNode& n);
const Cable* FindCable(const std::vector<Cable>& cables, int a, int b);
Vector2      EvaluateCubicBezier(Vector2 p0, Vector2 c1, Vector2 c2, Vector2 p3, float t);
std::string  BuildPathStr(const std::vector<int>& path,
                          const std::vector<DeviceNode>& nodes);
void         UpdatePacketAnim(PacketAnim& anim, float dt,
                              const std::vector<DeviceNode>& nodes,
                              const std::vector<Cable>& cables);
void    StepForwardAnim(PacketAnim& anim);
Vector2 GetPacketWorldPos(const PacketAnim& anim,
                          const std::vector<DeviceNode>& nodes,
                          const std::vector<Cable>& cables);
