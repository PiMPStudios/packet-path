#pragma once
#include "Device.h"
#include "Cable.h"
#include <vector>
#include <string>

// Delay between each PATH (or RESV) hop in the Simulate Setup replay.
// Tune during playtesting — 0.25s feels natural at 1× speed.
inline constexpr float RSVP_PATH_HOP_DELAY = 0.25f;

// Called once per simulation tick, after UpdateOspf and UpdateLdp.
// Recomputes all TE tunnel states and teLfib tables for every rsvp-enabled router.
// Returns human-readable log events (empty most ticks).
std::vector<std::string> UpdateRsvp(std::vector<DeviceNode>& nodes,
                                     const std::vector<Cable>& cables);

// Resolves TeTunnel::explicitHopIps → TeTunnel::explicitHops (node IDs).
// Call only when the user edits the hop string or toggles Explicit mode — not every tick.
void ResolveExplicitHops(TeTunnel& t, const std::vector<DeviceNode>& nodes);
