#pragma once

#include "Cable.h"
#include "ConfigPanel.h"
#include "Device.h"
#include "RsvpReplay.h"

#include "raylib.h"

#include <vector>

void HandleTrafficEngineeringPanelClick(Vector2 mouse, int selectedId,
                                        std::vector<DeviceNode>& nodes,
                                        PanelState& panel,
                                        RsvpReplayState& replay);
void HandleSegmentRoutingPanelClick(Vector2 mouse, int selectedId,
                                    std::vector<DeviceNode>& nodes,
                                    PanelState& panel);
void UpdateTrafficEngineeringPanelInput(int selectedId,
                                        std::vector<DeviceNode>& nodes,
                                        PanelState& panel);
void UpdateSegmentRoutingPanelInput(int selectedId,
                                    std::vector<DeviceNode>& nodes,
                                    const std::vector<Cable>& cables,
                                    PanelState& panel);
