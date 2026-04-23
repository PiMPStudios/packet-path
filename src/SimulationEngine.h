#pragma once
#include "Device.h"
#include "Cable.h"
#include <string>
#include <vector>

ForwardResult SimulateForward(int srcId, const std::string& destIp,
                              const std::vector<DeviceNode>& nodes,
                              const std::vector<Cable>& cables);
