#pragma once

#include "Cable.h"
#include "Device.h"
#include "Packet.h"

#include <vector>

struct RsvpReplayState {
    enum Phase { PATH_PHASE, HOLD_PHASE, RESV_PHASE };

    bool        active    = false;
    Phase       phase     = PATH_PHASE;
    int         hop       = 0;
    float       holdTimer = 0.f;
    std::vector<int> path;
    PacketAnim  packet;
    bool        packetActive = false;
    uint32_t    headLabel    = 0;
};

void StartRsvpReplay(RsvpReplayState& replay, const TeTunnel& tunnel);
void UpdateRsvpReplay(RsvpReplayState& replay, float dt,
                      const std::vector<DeviceNode>& nodes,
                      const std::vector<Cable>& cables);
