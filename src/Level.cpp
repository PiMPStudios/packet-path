#include "Level.h"
#include "Packet.h"
#include "SimulationEngine.h"
#include "UI.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

using json = nlohmann::json;

bool LoadLevel(const std::string& path, LevelDef& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    json j;
    try { f >> j; } catch (...) { return false; }

    out          = LevelDef{};
    out.id       = j.value("id",       0);
    out.title    = j.value("title",    "");
    out.briefing = j.value("briefing", "");

    int maxId = 0;
    for (const auto& d : j.value("devices", json::array())) {
        DeviceNode n;
        n.id    = d.value("id",    0);
        n.label = d.value("label", "");
        std::string typeStr = d.value("type", "PC");
        if      (typeStr == "ROUTER") n.type = ROUTER;
        else if (typeStr == "SWITCH") n.type = SWITCH;
        else                          n.type = PC;
        n.position = {d.value("x", 0.0f), d.value("y", 0.0f)};
        n.mgmtIp   = d.value("mgmtIp",   "");
        n.routerId = d.value("routerId", "");

        for (int i = 0; i < PORTS_PER_NODE; ++i) {
            std::string key = "portIp" + std::to_string(i);
            n.portIp[i] = d.value(key, "");
        }

        n.ospfEnabled = d.value("ospfEnabled", false);
        n.ldpEnabled  = d.value("ldpEnabled",  false);
        n.crashed     = d.value("crashed",     false);
        n.bgpEnabled       = d.value("bgpEnabled",       false);
        n.isRouteReflector = d.value("isRouteReflector", false);
        n.localAsn         = (uint32_t)d.value("localAsn",  0);
        if (d.contains("bgpNetworks") && d["bgpNetworks"].is_array())
            for (const auto& net : d["bgpNetworks"])
                n.bgpNetworks.push_back(net.get<std::string>());
        for (int i = 0; i < PORTS_PER_NODE; ++i) {
            std::string key = "ospfArea" + std::to_string(i);
            n.ospfPortArea[i] = (uint32_t)d.value(key, 0);
        }

        for (int i = 0; i < PORTS_PER_NODE; ++i) {
            std::string key = "vlanPort" + std::to_string(i);
            if (d.contains(key) && d[key].is_object()) {
                std::string modeStr = d[key].value("mode", "access");
                n.vlanPorts[i].mode       = (modeStr == "trunk") ? VLAN_TRUNK : VLAN_ACCESS;
                n.vlanPorts[i].accessVlan = d[key].value("vlan", 1);
            }
        }

        if (d.contains("subIfaces") && d["subIfaces"].is_array())
            for (const auto& si : d["subIfaces"]) {
                SubInterface sif;
                sif.parentPort = si.value("port", 0);
                sif.vlanId     = si.value("vlan", 0);
                sif.ip         = si.value("ip",   "");
                n.subIfaces.push_back(sif);
            }

        for (const auto& sr : d.value("staticRoutes", json::array())) {
            RouteEntry re;
            re.dest    = sr.value("dest",    "");
            re.nextHop = sr.value("nextHop", "");
            re.outPort = -1;
            re.src     = ROUTE_STATIC;
            n.staticRoutes.push_back(re);
        }

        maxId = std::max(maxId, n.id);
        out.devices.push_back(n);
    }

    for (const auto& c : j.value("cables", json::array())) {
        Cable cable;
        cable.fromId   = c.value("from",     0);
        cable.fromPort = c.value("fromPort", 0);
        cable.toId     = c.value("to",       0);
        cable.toPort   = c.value("toPort",   0);
        cable.broken   = c.value("broken",   false);
        out.cables.push_back(cable);
    }

    for (const auto& wc : j.value("winConditions", json::array())) {
        WinCondition w;
        w.srcLabel    = wc.value("src",         "");
        w.dstLabel    = wc.value("dst",         "");
        w.description = wc.value("description", "");
        w.requiresFix = wc.value("requiresFix", false);
        out.winConditions.push_back(w);
    }

    SetNextId(maxId + 1);
    return true;
}

void ApplyLevel(const LevelDef& def,
                std::vector<DeviceNode>& nodes,
                std::vector<Cable>& cables,
                int& selectedId) {
    nodes      = def.devices;
    cables     = def.cables;
    selectedId = -1;
    int maxId  = 0;
    for (const auto& n : nodes) maxId = std::max(maxId, n.id);
    SetNextId(maxId + 1);
}

int CheckWinConditions(const LevelDef& def,
                       const std::vector<DeviceNode>& nodes,
                       const std::vector<Cable>& cables) {
    int passed = 0;
    for (const auto& wc : def.winConditions) {
        const DeviceNode* src = nullptr;
        const DeviceNode* dst = nullptr;
        for (const auto& n : nodes) {
            if (n.label == wc.srcLabel) src = &n;
            if (n.label == wc.dstLabel) dst = &n;
        }
        if (!src || !dst) continue;
        std::string dstIp = GetFirstValidIp(*dst);
        if (dstIp.empty()) continue;
        ForwardResult fr = SimulateForward(src->id, dstIp, nodes, cables);
        if (fr.success) ++passed;
    }
    return passed;
}

int ComputeStars(int failedAttempts) {
    if (failedAttempts == 0) return 3;
    if (failedAttempts <= 2) return 2;
    return 1;
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
