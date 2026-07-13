#include "RsvpReplay.h"

#include "SimulationEngine.h"

#include <utility>

void StartRsvpReplay(RsvpReplayState& replay, const TeTunnel& tunnel) {
    replay             = RsvpReplayState{};
    replay.active      = tunnel.isUp && tunnel.activePath.size() > 1;
    replay.path        = tunnel.activePath;
    replay.headLabel   = tunnel.headLabel;
}

void UpdateRsvpReplay(RsvpReplayState& replay, float dt,
                      const std::vector<DeviceNode>& nodes,
                      const std::vector<Cable>& cables) {
    if (!replay.active) return;

    if (replay.packetActive) {
        UpdatePacketAnim(replay.packet, dt, nodes, cables);
        if (replay.packet.done) replay.packetActive = false;
    }
    if (replay.packetActive) return;

    const int count = static_cast<int>(replay.path.size());
    if (count < 2) {
        replay.active = false;
        return;
    }

    if (replay.phase == RsvpReplayState::PATH_PHASE) {
        if (replay.hop < count - 1) {
            ForwardResult result;
            result.path    = {replay.path[replay.hop], replay.path[replay.hop + 1]};
            result.success = true;
            replay.packet               = PacketAnim{};
            replay.packet.result        = std::move(result);
            replay.packet.overrideColor = Color{59, 130, 246, 255};
            replay.packetActive         = true;
            ++replay.hop;
        } else {
            replay.phase     = RsvpReplayState::HOLD_PHASE;
            replay.holdTimer = 0.8f;
        }
        return;
    }

    if (replay.phase == RsvpReplayState::HOLD_PHASE) {
        replay.holdTimer -= dt;
        if (replay.holdTimer <= 0.f) {
            replay.phase = RsvpReplayState::RESV_PHASE;
            replay.hop   = 0;
        }
        return;
    }

    if (replay.hop < count - 1) {
        const int from = replay.path[count - 1 - replay.hop];
        const int to   = replay.path[count - 2 - replay.hop];
        ForwardResult result;
        result.path    = {from, to};
        result.success = true;
        replay.packet              = PacketAnim{};
        replay.packet.result       = std::move(result);
        replay.packet.currentLabel = replay.headLabel
                                     + static_cast<uint32_t>(count - 2 - replay.hop);
        replay.packetActive = true;
        ++replay.hop;
    } else {
        replay.active = false;
    }
}
