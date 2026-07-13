#pragma once

#include "Cable.h"
#include "Device.h"

#include <string>
#include <vector>

struct LevelDef;

bool LoadLevel(const std::string& path, LevelDef& out);
bool SaveScene(const std::string& path,
               const std::vector<DeviceNode>& nodes,
               const std::vector<Cable>& cables);
