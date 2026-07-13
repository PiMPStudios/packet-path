#pragma once

#include "Cable.h"
#include "Device.h"

#include <string>
#include <vector>

// Validates an IPv6 SID and returns an expanded, lowercase comparison key.
// IPv4-embedded IPv6 forms are intentionally outside the first SRv6 lesson.
bool NormalizeIpv6Sid(const std::string& sid, std::string& key);

// Resolves every configured SRv6 policy over the converged IPv4/OSPF underlay.
void UpdateSrv6(std::vector<DeviceNode>& nodes, const std::vector<Cable>& cables);
