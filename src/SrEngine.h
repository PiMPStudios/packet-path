#pragma once
#include <vector>
#include "Device.h"
#include "Cable.h"

void UpdateSr(std::vector<DeviceNode>& nodes, const std::vector<Cable>& cables);
void ResolveSrSegments(SrPolicy& policy, const std::vector<DeviceNode>& nodes,
                       const std::vector<Cable>& cables);
