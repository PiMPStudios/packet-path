#pragma once
#include "Device.h"
#include "Cable.h"
#include <vector>
#include <string>

inline constexpr float OSPF_HELLO_INTERVAL = 2.0f;
inline constexpr float OSPF_DEAD_INTERVAL  = 8.0f;

// Called once per frame. Advances Hello timers, runs the adjacency FSM,
// rebuilds per-area LSDbs and runs SPF when adjacency state changes.
// Returns human-readable log events (empty most frames).
std::vector<std::string> UpdateOspf(float dt,
                                    std::vector<DeviceNode>& nodes,
                                    const std::vector<Cable>& cables);
