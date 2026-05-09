#include "RsvpEngine.h"
#include <unordered_map>
#include <queue>
#include <climits>
#include <algorithm>
#include <cstdio>

std::vector<std::string> UpdateRsvp(std::vector<DeviceNode>& nodes,
                                     const std::vector<Cable>& cables)
{
    std::vector<std::string> log;
    (void)nodes; (void)cables;
    return log;
}

void ResolveExplicitHops(TeTunnel& t, const std::vector<DeviceNode>& nodes)
{
    (void)t; (void)nodes;
}
