#pragma once
#include "Device.h"
#include <string>
#include <vector>

// Returns true if `ip` (plain, no mask) falls within `cidr`.
// "any" and "" both match everything.
bool AclMatchPrefix(const std::string& ip, const std::string& cidr);

// Returns pointer to first matching AclRule for (srcIp, dstIp, dstPort),
// or nullptr if no rule matches (caller treats nullptr as implicit deny).
// srcIp="" skips source prefix check (used for underlay hops).
const AclRule* MatchAcl(const std::vector<AclRule>& rules,
                         const std::string& srcIp,
                         const std::string& dstIp,
                         int dstPort = 0);
