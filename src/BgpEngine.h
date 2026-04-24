#pragma once
#include "Device.h"
#include "Cable.h"
#include <vector>

void UpdateBgp(std::vector<DeviceNode>& nodes,
               const std::vector<Cable>& cables);
