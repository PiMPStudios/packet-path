#pragma once

#include "Cable.h"
#include "Device.h"

#include <vector>

void DrawTeTunnelOverlays(const std::vector<DeviceNode>& nodes,
                          const std::vector<Cable>& cables);
void DrawSrPolicyOverlays(const std::vector<DeviceNode>& nodes,
                          const std::vector<Cable>& cables);
void DrawSrv6PolicyOverlays(const std::vector<DeviceNode>& nodes,
                            const std::vector<Cable>& cables);
