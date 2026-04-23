#pragma once
#include "raylib.h"
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <unordered_map>

// ── Device geometry constants ─────────────────────────────────────────────
static const int   PORTS_PER_NODE = 4;
static const float NODE_W         = 120.0f;
static const float NODE_H         =  60.0f;
static const int   NODE_FONT_SZ   =  14;
static const float PORT_RADIUS    =   6.0f;

// ── Routing types ─────────────────────────────────────────────────────────
enum RouteSource { ROUTE_CONNECTED, ROUTE_STATIC, ROUTE_OSPF, ROUTE_OSPF_IA };

struct RouteEntry {
    std::string dest;
    std::string nextHop;
    int         outPort;
    RouteSource src;
    uint32_t    area = 0;   // OSPF area (set by SPF; 0 for non-OSPF routes)
};

// ── OSPF types ────────────────────────────────────────────────────────────
enum OspfState { OSPF_DOWN, OSPF_INIT, OSPF_TWOWAY, OSPF_FULL };

struct OspfAdjacency {
    std::string neighborRouterId;
    int         cost = 1;
};

struct RouterLsa {
    std::string                routerId;
    uint32_t                   area = 0;
    std::vector<OspfAdjacency> adjacencies;
    std::vector<std::string>   networks;   // CIDR subnets in this area
};

struct OspfNeighbor {
    std::string neighborRouterId;
    std::string neighborIp;      // neighbor's port IP on the link facing us
    int         neighborNodeId = -1;
    int         localPort      = -1;
    OspfState   state          = OSPF_DOWN;
    float       deadTimer      = 0.f;
    uint32_t    area           = 0;   // area this adjacency was formed in
};

// ── ARP & log types ───────────────────────────────────────────────────────
enum LogType { LOG_FORWARD, LOG_ARP_REQ, LOG_ARP_REPLY, LOG_ARP_HIT, LOG_OSPF };

struct ArpEvent {
    int         nodeId   = 0;
    std::string ip;
    std::string mac;
    bool        cacheHit = false;
};

struct HopDecision {
    int         nodeId;
    std::string nodeLabel;
    std::string routeType;   // "C"=connected, "S"=static, "O"=OSPF, "O IA"=inter-area
    std::string destPrefix;  // matched route prefix, e.g. "10.0.1.0/24"
    std::string nextHopIp;   // next-hop IP, or "delivered" for connected routes
    int         outPort;     // egress port index, -1 if delivered
};

struct ForwardResult {
    bool                     success = false;
    std::vector<int>         path;
    std::string              reason;
    std::vector<ArpEvent>    arpEvents;
    std::vector<HopDecision> hops;
};

struct LogEntry {
    bool          success     = false;
    std::string   pathStr;
    std::string   reason;
    float         timestamp   = 0.f;
    LogType       type        = LOG_FORWARD;
    ForwardResult traceResult;   // populated for LOG_FORWARD entries only
};

// ── Device types & node struct ────────────────────────────────────────────
enum DeviceType { PC, ROUTER, SWITCH };

struct DeviceNode {
    int         id       = 0;
    DeviceType  type     = PC;
    Vector2     position = {0.0f, 0.0f};
    std::string label;
    bool        selected = false;
    std::string mgmtIp;
    std::string portIp[PORTS_PER_NODE];
    std::vector<RouteEntry> staticRoutes;
    std::unordered_map<std::string, std::string> arpTable;
    // OSPF state (routers only)
    bool        ospfEnabled  = false;
    std::string routerId;
    float       helloTimer   = 0.f;
    uint32_t    ospfPortArea[PORTS_PER_NODE] = {};  // area config per port (source of truth); OspfEngine copies to OspfNeighbor::area at adjacency formation
    std::vector<OspfNeighbor>                                      ospfNeighbors;
    std::unordered_map<uint32_t,
        std::unordered_map<std::string, RouterLsa>>                areaLsdbs;
    std::vector<RouteEntry>                                        ospfRoutes;
};

// ── Device geometry helpers (no draw calls) ───────────────────────────────
Color       GetDeviceColor(DeviceType t);
Rectangle   GetNodeRect(const DeviceNode& n);
Vector2     GetPortPosition(const DeviceNode& n, int port);
std::string GetPortName(DeviceType type, int port);
std::vector<RouteEntry> GetRoutingTable(const DeviceNode& n);
bool        IsAbr(const DeviceNode& node);

// ── IP / MAC utilities (no raylib) ────────────────────────────────────────
std::string NetworkAddress(const std::string& cidr);
bool        IpInSubnet(const std::string& ip, const std::string& subnet);
bool        ValidateIPOnly(const std::string& ip);
int         PrefixLen(const std::string& cidr);
bool        ValidateIP(const std::string& ip);
std::string GetDeviceMac(int id);
