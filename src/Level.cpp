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

        for (int i = 0; i < PORTS_PER_NODE; ++i) {
            std::string key = "portIp" + std::to_string(i);
            n.portIp[i] = d.value(key, "");
        }

        n.ospfEnabled = d.value("ospfEnabled", false);
        n.ldpEnabled  = d.value("ldpEnabled",  false);
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
        out.cables.push_back(cable);
    }

    for (const auto& wc : j.value("winConditions", json::array())) {
        WinCondition w;
        w.srcLabel    = wc.value("src",         "");
        w.dstLabel    = wc.value("dst",         "");
        w.description = wc.value("description", "");
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
