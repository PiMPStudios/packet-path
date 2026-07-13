#include "SceneSerializer.h"

#include "Level.h"
#include "RsvpEngine.h"
#include "UI.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <limits>
#include <unordered_set>
#include <utility>

using json = nlohmann::json;

bool LoadLevel(const std::string& path, LevelDef& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j;
        f >> j;
        if (!j.is_object()) return false;

        LevelDef parsed;
        parsed.id       = j.value("id",       0);
        parsed.title    = j.value("title",    "");
        parsed.briefing = j.value("briefing", "");

        const json devices = j.value("devices", json::array());
        if (!devices.is_array()) return false;

        std::unordered_set<int> deviceIds;
        int maxId = 0;
        for (const auto& d : devices) {
            if (!d.is_object()) return false;

            DeviceNode n;
            n.id = d.value("id", 0);
            if (n.id <= 0 || !deviceIds.insert(n.id).second) return false;

            n.label = d.value("label", "");
            const std::string typeStr = d.value("type", "PC");
            if      (typeStr == "ROUTER") n.type = ROUTER;
            else if (typeStr == "SWITCH") n.type = SWITCH;
            else if (typeStr == "PC")     n.type = PC;
            else return false;

            n.position = {d.value("x", 0.0f), d.value("y", 0.0f)};
            n.mgmtIp   = d.value("mgmtIp",   "");
            n.routerId = d.value("routerId", "");

            for (int i = 0; i < PORTS_PER_NODE; ++i) {
                const std::string suffix = std::to_string(i);
                n.portIp[i] = d.value("portIp" + suffix, "");
                n.ospfPortArea[i] = d.value("ospfArea" + suffix, 0u);

                const std::string vlanKey = "vlanPort" + suffix;
                if (d.contains(vlanKey)) {
                    if (!d[vlanKey].is_object()) return false;
                    const std::string mode = d[vlanKey].value("mode", "access");
                    if (mode != "access" && mode != "trunk") return false;
                    const int vlan = d[vlanKey].value("vlan", 1);
                    if (vlan < 1 || vlan > 4094) return false;
                    n.vlanPorts[i].mode = mode == "trunk" ? VLAN_TRUNK : VLAN_ACCESS;
                    n.vlanPorts[i].accessVlan = vlan;
                }

                const uint64_t bandwidth = d.value("portBandwidth" + suffix, 1000ull);
                if (bandwidth == 0 || bandwidth > std::numeric_limits<uint32_t>::max())
                    return false;
                n.portBandwidth[i] = static_cast<uint32_t>(bandwidth);
                n.sdwanLatencyMs[i] = d.value("sdwanLatency" + suffix, 0.f);
                n.sdwanJitterMs[i]  = d.value("sdwanJitter" + suffix, 0.f);
                n.sdwanLossPct[i]   = d.value("sdwanLoss" + suffix, 0.f);
                if (n.sdwanLatencyMs[i] < 0.f || n.sdwanJitterMs[i] < 0.f ||
                    n.sdwanLossPct[i] < 0.f || n.sdwanLossPct[i] > 100.f) return false;
            }

            n.ospfEnabled = d.value("ospfEnabled", false);
            n.ldpEnabled  = d.value("ldpEnabled",  false);
            n.crashed     = d.value("crashed",     false);
            n.bgpEnabled       = d.value("bgpEnabled",       false);
            n.isRouteReflector = d.value("isRouteReflector", false);
            n.localAsn         = d.value("localAsn", 0u);

            const json bgpNetworks = d.value("bgpNetworks", json::array());
            if (!bgpNetworks.is_array()) return false;
            for (const auto& network : bgpNetworks)
                n.bgpNetworks.push_back(network.get<std::string>());

            n.vxlanEnabled = d.value("vxlanEnabled", false);
            n.evpnEnabled  = d.value("evpnEnabled",  false);
            n.vni          = d.value("vni", 0u);
            n.vtepIp       = d.value("vtepIp", "");
            if (n.vni > 16777215u) return false;

            n.aclInPort  = d.value("aclInPort",  -1);
            n.aclOutPort = d.value("aclOutPort", -1);
            if (n.aclInPort < -1 || n.aclInPort >= PORTS_PER_NODE ||
                n.aclOutPort < -1 || n.aclOutPort >= PORTS_PER_NODE) return false;

            const json aclRules = d.value("aclRules", json::array());
            if (!aclRules.is_array()) return false;
            for (const auto& ar : aclRules) {
                if (!ar.is_object()) return false;
                AclRule rule;
                rule.seq = ar.value("seq", 10);
                const std::string action = ar.value("action", std::string("permit"));
                if (action != "permit" && action != "deny") return false;
                rule.action  = action == "permit" ? ACL_PERMIT : ACL_DENY;
                rule.srcCidr = ar.value("src", std::string("any"));
                rule.dstCidr = ar.value("dst", std::string("any"));
                rule.dstPort = ar.value("port", 0);
                if (rule.dstPort < 0 || rule.dstPort > 65535) return false;
                n.aclRules.push_back(rule);
            }

            n.natEnabled      = d.value("natEnabled", false);
            n.natInsidePort   = d.value("natInsidePort", -1);
            n.natOutsidePort  = d.value("natOutsidePort", -1);
            n.natInsidePrefix = d.value("natInsidePrefix", std::string{});
            if (n.natInsidePort < -1 || n.natInsidePort >= PORTS_PER_NODE ||
                n.natOutsidePort < -1 || n.natOutsidePort >= PORTS_PER_NODE) return false;

            const json subIfaces = d.value("subIfaces", json::array());
            if (!subIfaces.is_array()) return false;
            for (const auto& si : subIfaces) {
                if (!si.is_object()) return false;
                SubInterface subInterface;
                subInterface.parentPort = si.value("port", 0);
                subInterface.vlanId     = si.value("vlan", 0);
                subInterface.ip         = si.value("ip", "");
                if (subInterface.parentPort < 0 ||
                    subInterface.parentPort >= PORTS_PER_NODE ||
                    subInterface.vlanId < 1 || subInterface.vlanId > 4094) return false;
                n.subIfaces.push_back(subInterface);
            }

            const json staticRoutes = d.value("staticRoutes", json::array());
            if (!staticRoutes.is_array()) return false;
            for (const auto& sr : staticRoutes) {
                if (!sr.is_object()) return false;
                RouteEntry route;
                route.dest    = sr.value("dest", "");
                route.nextHop = sr.value("nextHop", "");
                route.outPort = -1;
                route.src     = ROUTE_STATIC;
                n.staticRoutes.push_back(route);
            }

            n.rsvpEnabled = d.value("rsvpEnabled", false);
            const json teTunnels = d.value("teTunnels", json::array());
            if (!teTunnels.is_array()) return false;
            std::unordered_set<int> tunnelIds;
            for (const auto& tj : teTunnels) {
                if (!tj.is_object()) return false;
                TeTunnel tunnel;
                tunnel.id = tj.value("id", 0);
                if (tunnel.id < 1 || tunnel.id > 255 ||
                    !tunnelIds.insert(tunnel.id).second) return false;
                tunnel.destIp      = tj.value("destIp", "");
                tunnel.bandwidth   = tj.value("bandwidth", 0u);
                tunnel.useExplicit = tj.value("useExplicit", false);
                const json hops = tj.value("explicitHopIps", json::array());
                if (!hops.is_array()) return false;
                for (const auto& hop : hops)
                    tunnel.explicitHopIps.push_back(hop.get<std::string>());
                n.teTunnels.push_back(std::move(tunnel));
            }

            n.srEnabled = d.value("srEnabled", false);
            n.nodeSid   = d.value("nodeSid", 0u);
            if (n.nodeSid >= SRGB_SIZE) return false;
            const json srPolicies = d.value("srPolicies", json::array());
            if (!srPolicies.is_array()) return false;
            std::unordered_set<int> policyIds;
            for (const auto& pj : srPolicies) {
                if (!pj.is_object()) return false;
                SrPolicy policy;
                policy.id = pj.value("id", 0);
                if (policy.id < 1 || policy.id > 255 ||
                    !policyIds.insert(policy.id).second) return false;
                policy.destIp = pj.value("destIp", "");
                const json segments = pj.value("segmentIps", json::array());
                if (!segments.is_array()) return false;
                for (const auto& segment : segments)
                    policy.segmentIps.push_back(segment.get<std::string>());
                n.srPolicies.push_back(std::move(policy));
            }

            n.srv6Enabled = d.value("srv6Enabled", false);
            n.srv6Sid = d.value("srv6Sid", std::string{});
            const json srv6Policies = d.value("srv6Policies", json::array());
            if (!srv6Policies.is_array()) return false;
            std::unordered_set<int> srv6PolicyIds;
            for (const auto& pj : srv6Policies) {
                if (!pj.is_object()) return false;
                Srv6Policy policy;
                policy.id = pj.value("id", 0);
                if (policy.id < 1 || policy.id > 255 ||
                    !srv6PolicyIds.insert(policy.id).second) return false;
                policy.destIp = pj.value("destIp", "");
                const json segments = pj.value("segmentSids", json::array());
                if (!segments.is_array()) return false;
                for (const auto& segment : segments)
                    policy.segmentSids.push_back(segment.get<std::string>());
                n.srv6Policies.push_back(std::move(policy));
            }

            n.sdwanEnabled = d.value("sdwanEnabled", false);
            const json sdwanPolicies = d.value("sdwanPolicies", json::array());
            if (!sdwanPolicies.is_array()) return false;
            std::unordered_set<int> sdwanPolicyIds;
            for (const auto& pj : sdwanPolicies) {
                if (!pj.is_object()) return false;
                SdwanPolicy policy;
                policy.id = pj.value("id", 0);
                if (policy.id < 1 || policy.id > 255 ||
                    !sdwanPolicyIds.insert(policy.id).second) return false;
                policy.destIp = pj.value("destIp", "");
                policy.preferredPort = pj.value("preferredPort", -1);
                policy.backupPort = pj.value("backupPort", -1);
                policy.maxLatencyMs = pj.value("maxLatencyMs", 0.f);
                policy.maxJitterMs = pj.value("maxJitterMs", 0.f);
                policy.maxLossPct = pj.value("maxLossPct", 0.f);
                if (policy.preferredPort < -1 || policy.preferredPort >= PORTS_PER_NODE ||
                    policy.backupPort < -1 || policy.backupPort >= PORTS_PER_NODE ||
                    policy.maxLatencyMs < 0.f || policy.maxJitterMs < 0.f ||
                    policy.maxLossPct < 0.f || policy.maxLossPct > 100.f) return false;
                n.sdwanPolicies.push_back(std::move(policy));
            }

            maxId = std::max(maxId, n.id);
            parsed.devices.push_back(std::move(n));
        }

        for (auto& node : parsed.devices)
            for (auto& tunnel : node.teTunnels)
                ResolveExplicitHops(tunnel, parsed.devices);

        const json cables = j.value("cables", json::array());
        if (!cables.is_array()) return false;
        std::unordered_set<uint64_t> occupiedPorts;
        for (const auto& c : cables) {
            if (!c.is_object()) return false;
            Cable cable;
            cable.fromId   = c.value("from", 0);
            cable.fromPort = c.value("fromPort", 0);
            cable.toId     = c.value("to", 0);
            cable.toPort   = c.value("toPort", 0);
            cable.broken   = c.value("broken", false);
            if (!deviceIds.count(cable.fromId) || !deviceIds.count(cable.toId) ||
                cable.fromId == cable.toId || cable.fromPort < 0 ||
                cable.fromPort >= PORTS_PER_NODE || cable.toPort < 0 ||
                cable.toPort >= PORTS_PER_NODE) return false;

            const uint64_t fromKey = (static_cast<uint64_t>(cable.fromId) << 32) |
                                     static_cast<uint32_t>(cable.fromPort);
            const uint64_t toKey = (static_cast<uint64_t>(cable.toId) << 32) |
                                   static_cast<uint32_t>(cable.toPort);
            if (!occupiedPorts.insert(fromKey).second ||
                !occupiedPorts.insert(toKey).second) return false;
            parsed.cables.push_back(cable);
        }

        const json winConditions = j.value("winConditions", json::array());
        if (!winConditions.is_array()) return false;
        for (const auto& wc : winConditions) {
            if (!wc.is_object()) return false;
            WinCondition condition;
            condition.srcLabel    = wc.value("src", "");
            condition.dstLabel    = wc.value("dst", "");
            condition.description = wc.value("description", "");
            condition.requiresFix = wc.value("requiresFix", false);
            condition.requiresNatOnDevice =
                wc.value("requiresNatOnDevice", std::string{});
            condition.requiresTeTunnelOnDevice =
                wc.value("requiresTeTunnelOnDevice", std::string{});
            condition.requiresTeTunnelId = wc.value("requiresTeTunnelId", 0);
            const uint64_t minimumBandwidth =
                wc.value("requiresTeMinBandwidth", 0ull);
            if (minimumBandwidth > std::numeric_limits<uint32_t>::max()) return false;
            condition.requiresTeMinBandwidth =
                static_cast<uint32_t>(minimumBandwidth);
            condition.requiresTePathVia =
                wc.value("requiresTePathVia", std::string{});
            condition.requiresTeMode = wc.value("requiresTeMode", std::string{});
            condition.requiresSrPolicyOnDevice =
                wc.value("requiresSrPolicyOnDevice", std::string{});
            condition.requiresSrPolicyId = wc.value("requiresSrPolicyId", 0);
            const json requiredSegments =
                wc.value("requiresSrSegments", json::array());
            if (!requiredSegments.is_array()) return false;
            for (const auto& segment : requiredSegments)
                condition.requiresSrSegments.push_back(segment.get<std::string>());
            condition.requiresSrPathVia =
                wc.value("requiresSrPathVia", std::string{});
            condition.requiresSrv6PolicyOnDevice =
                wc.value("requiresSrv6PolicyOnDevice", std::string{});
            condition.requiresSrv6PolicyId = wc.value("requiresSrv6PolicyId", 0);
            const json requiredSrv6Segments =
                wc.value("requiresSrv6Segments", json::array());
            if (!requiredSrv6Segments.is_array()) return false;
            for (const auto& segment : requiredSrv6Segments)
                condition.requiresSrv6Segments.push_back(segment.get<std::string>());
            condition.requiresSrv6PathVia =
                wc.value("requiresSrv6PathVia", std::string{});
            condition.requiresSdwanPolicyOnDevice =
                wc.value("requiresSdwanPolicyOnDevice", std::string{});
            condition.requiresSdwanPolicyId = wc.value("requiresSdwanPolicyId", 0);
            condition.requiresSdwanPreferredPort = wc.value("requiresSdwanPreferredPort", -1);
            condition.requiresSdwanBackupPort = wc.value("requiresSdwanBackupPort", -1);
            condition.requiresSdwanSelectedPort = wc.value("requiresSdwanSelectedPort", -1);
            condition.requiresSdwanMaxLatencyMs = wc.value("requiresSdwanMaxLatencyMs", 0.f);
            condition.requiresSdwanMaxJitterMs = wc.value("requiresSdwanMaxJitterMs", 0.f);
            condition.requiresSdwanMaxLossPct = wc.value("requiresSdwanMaxLossPct", 0.f);
            if (condition.requiresTeTunnelId < 0 ||
                condition.requiresTeTunnelId > 255 ||
                (!condition.requiresTeMode.empty() &&
                 condition.requiresTeMode != "cspf" &&
                 condition.requiresTeMode != "explicit") ||
                (condition.requiresTeTunnelOnDevice.empty() &&
                 (condition.requiresTeTunnelId != 0 ||
                  condition.requiresTeMinBandwidth != 0 ||
                  !condition.requiresTePathVia.empty() ||
                  !condition.requiresTeMode.empty()))) {
                return false;
            }
            if (condition.requiresSrPolicyId < 0 ||
                condition.requiresSrPolicyId > 255 ||
                (condition.requiresSrPolicyOnDevice.empty() &&
                 (condition.requiresSrPolicyId != 0 ||
                  !condition.requiresSrSegments.empty() ||
                  !condition.requiresSrPathVia.empty()))) {
                return false;
            }
            if (condition.requiresSrv6PolicyId < 0 ||
                condition.requiresSrv6PolicyId > 255 ||
                (condition.requiresSrv6PolicyOnDevice.empty() &&
                 (condition.requiresSrv6PolicyId != 0 ||
                  !condition.requiresSrv6Segments.empty() ||
                  !condition.requiresSrv6PathVia.empty()))) return false;
            if (condition.requiresSdwanPolicyId < 0 || condition.requiresSdwanPolicyId > 255 ||
                condition.requiresSdwanPreferredPort < -1 ||
                condition.requiresSdwanPreferredPort >= PORTS_PER_NODE ||
                condition.requiresSdwanBackupPort < -1 ||
                condition.requiresSdwanBackupPort >= PORTS_PER_NODE ||
                condition.requiresSdwanSelectedPort < -1 ||
                condition.requiresSdwanSelectedPort >= PORTS_PER_NODE ||
                (condition.requiresSdwanPolicyOnDevice.empty() &&
                 (condition.requiresSdwanPolicyId != 0 ||
                  condition.requiresSdwanPreferredPort != -1 ||
                  condition.requiresSdwanBackupPort != -1 ||
                  condition.requiresSdwanSelectedPort != -1 ||
                  condition.requiresSdwanMaxLatencyMs != 0.f ||
                  condition.requiresSdwanMaxJitterMs != 0.f ||
                  condition.requiresSdwanMaxLossPct != 0.f))) return false;
            parsed.winConditions.push_back(std::move(condition));
        }

        out = std::move(parsed);
        SetNextId(maxId + 1);
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool SaveScene(const std::string& path,
               const std::vector<DeviceNode>& nodes,
               const std::vector<Cable>& cables)
{
    json j;
    j["id"]            = 0;
    j["title"]         = "Saved Scene";
    j["briefing"]      = "";
    j["winConditions"] = json::array();

    json devArr = json::array();
    for (const auto& n : nodes) {
        json d;
        d["id"]    = n.id;
        d["label"] = n.label;
        d["x"]     = n.position.x;
        d["y"]     = n.position.y;
        std::string typeStr = "PC";
        if      (n.type == ROUTER) typeStr = "ROUTER";
        else if (n.type == SWITCH) typeStr = "SWITCH";
        d["type"] = typeStr;

        if (!n.mgmtIp.empty())   d["mgmtIp"]   = n.mgmtIp;
        if (!n.routerId.empty()) d["routerId"] = n.routerId;

        for (int i = 0; i < PORTS_PER_NODE; ++i)
            if (!n.portIp[i].empty())
                d["portIp" + std::to_string(i)] = n.portIp[i];

        if (n.ospfEnabled) d["ospfEnabled"] = true;
        if (n.ldpEnabled)  d["ldpEnabled"]  = true;
        if (n.crashed)     d["crashed"]     = true;

        if (n.bgpEnabled) {
            d["bgpEnabled"] = true;
            d["localAsn"]   = n.localAsn;
            if (n.isRouteReflector) d["isRouteReflector"] = true;
            if (!n.bgpNetworks.empty()) {
                json nets = json::array();
                for (const auto& net : n.bgpNetworks) nets.push_back(net);
                d["bgpNetworks"] = nets;
            }
        }

        if (n.vxlanEnabled) {
            d["vxlanEnabled"] = true;
            d["vni"]          = n.vni;
            if (!n.vtepIp.empty()) d["vtepIp"] = n.vtepIp;
            if (n.evpnEnabled) d["evpnEnabled"] = true;
        }

        if (n.aclInPort >= 0 || n.aclOutPort >= 0 || !n.aclRules.empty()) {
            if (n.aclInPort >= 0)  d["aclInPort"]  = n.aclInPort;
            if (n.aclOutPort >= 0) d["aclOutPort"] = n.aclOutPort;
            if (!n.aclRules.empty()) {
                json aclArr = json::array();
                for (const auto& r : n.aclRules) {
                    json rj;
                    rj["seq"]    = r.seq;
                    rj["action"] = (r.action == ACL_PERMIT) ? "permit" : "deny";
                    rj["src"]    = r.srcCidr;
                    rj["dst"]    = r.dstCidr;
                    if (r.dstPort > 0) rj["port"] = r.dstPort;
                    aclArr.push_back(rj);
                }
                d["aclRules"] = aclArr;
            }
        }

        if (n.natEnabled) {
            d["natEnabled"]      = true;
            d["natInsidePort"]   = n.natInsidePort;
            d["natOutsidePort"]  = n.natOutsidePort;
            if (!n.natInsidePrefix.empty()) d["natInsidePrefix"] = n.natInsidePrefix;
        }

        bool hasCustomBandwidth = false;
        for (int i = 0; i < PORTS_PER_NODE; ++i)
            hasCustomBandwidth = hasCustomBandwidth || n.portBandwidth[i] != 1000u;
        if (n.rsvpEnabled || hasCustomBandwidth || !n.teTunnels.empty()) {
            if (n.rsvpEnabled) d["rsvpEnabled"] = true;
            for (int i = 0; i < PORTS_PER_NODE; ++i)
                if (n.portBandwidth[i] != 1000u)
                    d["portBandwidth" + std::to_string(i)] = n.portBandwidth[i];

            if (!n.teTunnels.empty()) {
                json tunnels = json::array();
                for (const auto& tunnel : n.teTunnels) {
                    json tj;
                    tj["id"]          = tunnel.id;
                    tj["destIp"]      = tunnel.destIp;
                    tj["bandwidth"]   = tunnel.bandwidth;
                    tj["useExplicit"] = tunnel.useExplicit;
                    if (!tunnel.explicitHopIps.empty())
                        tj["explicitHopIps"] = tunnel.explicitHopIps;
                    tunnels.push_back(std::move(tj));
                }
                d["teTunnels"] = std::move(tunnels);
            }
        }

        if (n.srEnabled || n.nodeSid != 0 || !n.srPolicies.empty()) {
            if (n.srEnabled) d["srEnabled"] = true;
            if (n.nodeSid != 0) d["nodeSid"] = n.nodeSid;
            if (!n.srPolicies.empty()) {
                json policies = json::array();
                for (const auto& policy : n.srPolicies) {
                    json pj;
                    pj["id"]     = policy.id;
                    pj["destIp"] = policy.destIp;
                    if (!policy.segmentIps.empty())
                        pj["segmentIps"] = policy.segmentIps;
                    policies.push_back(std::move(pj));
                }
                d["srPolicies"] = std::move(policies);
            }
        }

        if (n.srv6Enabled || !n.srv6Sid.empty() || !n.srv6Policies.empty()) {
            if (n.srv6Enabled) d["srv6Enabled"] = true;
            if (!n.srv6Sid.empty()) d["srv6Sid"] = n.srv6Sid;
            if (!n.srv6Policies.empty()) {
                json policies = json::array();
                for (const auto& policy : n.srv6Policies) {
                    json pj;
                    pj["id"] = policy.id;
                    pj["destIp"] = policy.destIp;
                    if (!policy.segmentSids.empty())
                        pj["segmentSids"] = policy.segmentSids;
                    policies.push_back(std::move(pj));
                }
                d["srv6Policies"] = std::move(policies);
            }
        }

        if (n.sdwanEnabled || !n.sdwanPolicies.empty()) {
            if (n.sdwanEnabled) d["sdwanEnabled"] = true;
            for (int i = 0; i < PORTS_PER_NODE; ++i) {
                const std::string suffix = std::to_string(i);
                if (n.sdwanLatencyMs[i] != 0.f) d["sdwanLatency" + suffix] = n.sdwanLatencyMs[i];
                if (n.sdwanJitterMs[i] != 0.f) d["sdwanJitter" + suffix] = n.sdwanJitterMs[i];
                if (n.sdwanLossPct[i] != 0.f) d["sdwanLoss" + suffix] = n.sdwanLossPct[i];
            }
            if (!n.sdwanPolicies.empty()) {
                json policies = json::array();
                for (const auto& policy : n.sdwanPolicies) {
                    policies.push_back({
                        {"id", policy.id}, {"destIp", policy.destIp},
                        {"preferredPort", policy.preferredPort},
                        {"backupPort", policy.backupPort},
                        {"maxLatencyMs", policy.maxLatencyMs},
                        {"maxJitterMs", policy.maxJitterMs},
                        {"maxLossPct", policy.maxLossPct},
                    });
                }
                d["sdwanPolicies"] = std::move(policies);
            }
        }

        for (int i = 0; i < PORTS_PER_NODE; ++i)
            if (n.ospfPortArea[i] != 0)
                d["ospfArea" + std::to_string(i)] = n.ospfPortArea[i];

        for (int i = 0; i < PORTS_PER_NODE; ++i) {
            const auto& vp = n.vlanPorts[i];
            if (vp.mode != VLAN_ACCESS || vp.accessVlan != 1) {
                json vpj;
                vpj["mode"] = (vp.mode == VLAN_TRUNK) ? "trunk" : "access";
                vpj["vlan"] = vp.accessVlan;
                d["vlanPort" + std::to_string(i)] = vpj;
            }
        }

        if (!n.subIfaces.empty()) {
            json siArr = json::array();
            for (const auto& si : n.subIfaces) {
                json sij;
                sij["port"] = si.parentPort;
                sij["vlan"] = si.vlanId;
                sij["ip"]   = si.ip;
                siArr.push_back(sij);
            }
            d["subIfaces"] = siArr;
        }

        if (!n.staticRoutes.empty()) {
            json srArr = json::array();
            for (const auto& sr : n.staticRoutes) {
                if (sr.src != ROUTE_STATIC) continue;
                json srj;
                srj["dest"]    = sr.dest;
                srj["nextHop"] = sr.nextHop;
                srArr.push_back(srj);
            }
            if (!srArr.empty()) d["staticRoutes"] = srArr;
        }

        devArr.push_back(d);
    }
    j["devices"] = devArr;

    json cableArr = json::array();
    for (const auto& c : cables) {
        json cj;
        cj["from"]     = c.fromId;
        cj["fromPort"] = c.fromPort;
        cj["to"]       = c.toId;
        cj["toPort"]   = c.toPort;
        if (c.broken) cj["broken"] = true;
        cableArr.push_back(cj);
    }
    j["cables"] = cableArr;

    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << j.dump(2);
    f.close();
    return !f.fail();
}
