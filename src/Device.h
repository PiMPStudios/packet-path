#pragma once
#include "raylib.h"
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <unordered_map>

// ── VLAN / 802.1Q types ───────────────────────────────────────────────────
enum VlanPortMode { VLAN_ACCESS, VLAN_TRUNK };

struct VlanPortConfig {
    VlanPortMode mode      = VLAN_ACCESS;
    int          accessVlan = 1;   // VLAN ID for access mode (1-4094); ignored when trunk
};

struct SubInterface {
    int         parentPort = 0;   // physical port index (0-3)
    int         vlanId     = 0;   // 802.1Q VLAN ID (1-4094)
    std::string ip;               // CIDR e.g. "10.10.0.1/24"
};

// ── MPLS types ────────────────────────────────────────────────────────────
static const uint32_t MPLS_IMPLICIT_NULL = 3;  // RFC 3032 §2.1

constexpr uint32_t SRGB_BASE = 17000;   // above LDP (n.id*100) and RSVP-TE (16000+)
constexpr uint32_t SRGB_SIZE = 1000;
constexpr uint32_t SRGB_END  = SRGB_BASE + SRGB_SIZE;   // exclusive upper bound

enum LabelOp { LABEL_NONE, LABEL_PUSH, LABEL_SWAP, LABEL_POP };

struct LdpBinding {
    uint32_t localLabel = 0;  // label this router advertises for this prefix
    uint32_t outLabel   = 0;  // label expected by next-hop router (or IMPLICIT_NULL)
};

struct TeLfibEntry {
    uint32_t inLabel  = 0;
    uint32_t outLabel = 0;   // MPLS_IMPLICIT_NULL = PHP at penultimate hop
    int      outPort  = -1;
    int      tunnelId = 0;
};

struct SrLfibEntry {
    uint32_t inLabel  = 0;
    uint32_t outLabel = 0;   // MPLS_IMPLICIT_NULL = PHP/self-pop; same as inLabel = transit
    int      outPort  = -1;  // -1 = egress self-pop (let IP routing determine outPort)
    int      policyId = 0;   // always 0 in srFib; carried on HopDecision for display
};

struct TeTunnel {
    int         id          = 0;       // 1–255, unique per router
    std::string destIp;                // tail-end router IP (no mask)

    uint32_t    bandwidth   = 0;       // required Mbps

    bool        useExplicit = false;
    std::vector<std::string> explicitHopIps;  // human-readable (UI storage)
    std::vector<int>         explicitHops;    // resolved node IDs (engine use)

    // computed each tick by UpdateRsvp — do not set from UI
    bool             isUp      = false;
    std::vector<int> activePath;        // node IDs head→tail
    uint32_t         headLabel = 0;     // 0 = not yet allocated
    std::string      statusMsg;         // "Up" / "No CSPF path" / "BW insufficient"
};

struct SrPolicy {
    int         id          = 0;        // 1–255, unique per router
    std::string destIp;                 // tail-end destination IP (no mask)

    std::vector<std::string> segmentIps;    // hop IPs as typed (UI storage)
    std::vector<int>         segmentHops;   // resolved endpoint node IDs (engine use)
    std::vector<int>         segmentOwners; // node that owns each SID
    std::vector<int>         segmentOutPorts; // -1 for Node SID; local port for Adj SID
    std::vector<uint32_t>    labelStack;    // innermost first, outermost at back()
                                            // e.g. segs [R2→R4]: {17004, 17002}
                                            // back() = 17002 (R2's label, processed first)
    bool             segmentsResolved = false;   // cleared on edit; set by ResolveSrSegments

    bool             isActive  = false;
    std::vector<int> activePath;    // full OSPF path head→tail through all waypoints
    std::string      statusMsg;     // "Active" / "Segment N unreachable" / "No SID for X"
};

struct Srv6Policy {
    int         id = 0;                 // 1-255, unique per router
    std::string destIp;                 // encapsulated IPv4 payload destination
    std::vector<std::string> segmentSids;  // policy order, first SID processed first

    // Computed by UpdateSrv6; never serialized as configuration.
    std::vector<int> segmentHops;
    bool             isActive = false;
    std::vector<int> activePath;
    std::string      statusMsg;
};

struct SdwanPolicy {
    int         id = 0;
    std::string destIp;
    int         preferredPort = -1;
    int         backupPort = -1;
    float       maxLatencyMs = 0.f;
    float       maxJitterMs = 0.f;
    float       maxLossPct = 0.f;

    bool        isActive = false;
    int         selectedPort = -1;
    bool        usingBackup = false;
    std::string statusMsg;
};

// ── BGP types ─────────────────────────────────────────────────────────────
struct BgpNeighbor {
    std::string neighborIp;        // peer's port IP on shared cable (no mask)
    int         neighborNodeId = -1;
    uint32_t    neighborAsn    = 0;
    bool        established    = false;
    bool        ibgp           = false;  // same-AS peer (iBGP session)
};

struct BgpRoute {
    std::string           prefix;           // CIDR e.g. "10.0.0.0/24"
    std::string           nextHop;          // peer's facing IP (no mask)
    std::vector<uint32_t> asPath;           // ASNs, closest first
    int                   neighborNodeId = -1;  // node that sent this route
    uint32_t              originatorId   = 0;        // RFC 4456: first originating client node ID
    std::vector<uint32_t> clusterList;               // RFC 4456: RR cluster IDs traversed
};

// ── Device geometry constants ─────────────────────────────────────────────
static const int   PORTS_PER_NODE = 4;
static const float NODE_W         = 120.0f;
static const float NODE_H         =  60.0f;
static const int   NODE_FONT_SZ   =  14;
static const float PORT_RADIUS    =   6.0f;

// ── Routing types ─────────────────────────────────────────────────────────
enum RouteSource { ROUTE_CONNECTED, ROUTE_STATIC, ROUTE_OSPF, ROUTE_OSPF_IA, ROUTE_BGP, ROUTE_EVPN };

// ── ACL types ─────────────────────────────────────────────────────────────
enum AclAction { ACL_PERMIT, ACL_DENY };

struct AclRule {
    int         seq     = 10;          // sequence number (10, 20, 30...)
    AclAction   action  = ACL_PERMIT;
    std::string srcCidr = "any";       // "any" or "x.x.x.x/n"
    std::string dstCidr = "any";
    int         dstPort = 0;           // 0 = any port
};

struct RouteEntry {
    std::string dest;
    std::string nextHop;
    int         outPort;
    RouteSource src;
    uint32_t    area = 0;   // OSPF area (set by SPF; 0 for non-OSPF routes)
    int         subVlanId = 0;  // non-zero: route exits via tagged subinterface
    uint32_t    vni       = 0;   // non-zero for ROUTE_EVPN entries
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
enum LogType { LOG_FORWARD, LOG_ARP_REQ, LOG_ARP_REPLY, LOG_ARP_HIT, LOG_OSPF, LOG_RSVP,
               LOG_LINK_DOWN, LOG_DEVICE_CRASH, LOG_RESTORED };

struct ArpEvent {
    int         nodeId   = 0;
    std::string ip;
    std::string mac;
    bool        cacheHit = false;
};

struct HopDecision {
    int         nodeId    = -1;
    std::string nodeLabel;
    std::string routeType;   // "C"=connected, "S"=static, "O"=OSPF, "O IA"=inter-area
    std::string destPrefix;  // matched route prefix, e.g. "10.0.1.0/24"
    std::string nextHopIp;   // next-hop IP, or "delivered" for connected routes
    int         outPort   = -1;
    // MPLS label operation (LABEL_NONE when MPLS not active on this hop)
    LabelOp  labelOp  = LABEL_NONE;
    uint32_t inLabel  = 0;   // label arriving at this router (0 = unlabeled)
    uint32_t outLabel = 0;   // label leaving this router (0 = unlabeled after POP)
    int      vlanTag  = 0;   // 802.1Q VLAN on the cable leaving this hop (0 = untagged)
    uint32_t vxlanVni = 0;   // non-zero = hop is inside a VXLAN tunnel
    std::string aclResult;   // non-empty = ACL matched this hop, e.g. "PERMIT seq:10"
    std::string natResult;   // non-empty = NAT translated this hop, e.g. "192.168.1.2 → 10.0.0.1"
    int tunnelId = 0;   // non-zero = this hop is inside a named TE tunnel
    int policyId     = 0;   // non-zero = this hop is inside a named SR policy
    int segmentIndex = 0;   // which segment of the policy this hop belongs to (0-based)
    int         srv6PolicyId    = 0;  // non-zero = SRv6 policy encapsulation is active
    int         srv6SegmentIndex = -1;
    int         srv6SegmentsLeft = -1;
    std::string srv6ActiveSid;
    int         sdwanPolicyId = 0;
    int         sdwanSelectedPort = -1;
    bool        sdwanUsingBackup = false;
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
    bool        crashed  = false;
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
    // LDP / MPLS state (routers only)
    bool ldpEnabled = false;
    std::unordered_map<std::string, LdpBinding> lfib;  // key = CIDR prefix e.g. "10.0.1.0/24" (NetworkAddress() form)
    // BGP state (routers only)
    bool                     bgpEnabled       = false;
    bool                     isRouteReflector = false;   // RR-centric toggle; all iBGP peers are clients
    uint32_t                 localAsn         = 0;
    std::vector<std::string> bgpNetworks;    // prefixes to advertise; empty = auto-advertise connected
    std::vector<BgpNeighbor> bgpNeighbors;
    std::vector<BgpRoute>    bgpRoutes;      // received BGP routes (RIB-in)
    // VLAN state (switches only)
    VlanPortConfig vlanPorts[PORTS_PER_NODE];
    std::vector<SubInterface> subIfaces;  // routers only
    // VXLAN / BGP EVPN (routers/leaves only)
    bool        vxlanEnabled = false;
    bool        evpnEnabled  = false;
    uint32_t    vni          = 0;        // VNI (1-16777215)
    std::string vtepIp;                  // must equal one of this node's portIpN values
    std::vector<RouteEntry> evpnRoutes;  // populated by EvpnEngine each frame
    // ACL (routers only)
    std::vector<AclRule> aclRules;
    int                  aclInPort  = -1;  // port ACL applied inbound  (-1 = not applied)
    int                  aclOutPort = -1;  // port ACL applied outbound (-1 = not applied)
    // NAT — source overload / PAT (routers only)
    bool        natEnabled      = false;
    int         natInsidePort   = -1;          // port facing inside network (-1 = not set)
    int         natOutsidePort  = -1;          // port facing outside / ISP  (-1 = not set)
    std::string natInsidePrefix;               // CIDR of inside subnet, e.g. "192.168.1.0/24"
    // RSVP-TE (routers only)
    bool        rsvpEnabled  = false;
    uint32_t    portBandwidth[PORTS_PER_NODE] = {1000, 1000, 1000, 1000};  // Mbps
    uint32_t    nextTeLabel  = 16000;   // monotonic allocator — never resets between ticks
    std::vector<TeTunnel>   teTunnels;
    std::unordered_map<uint32_t, TeLfibEntry> teLfib;  // key = inLabel
    std::vector<TeTunnel>   pendingTunnels;  // MBB hold buffer
    // SR-MPLS (routers only)
    bool     srEnabled = false;
    uint32_t nodeSid   = 0;      // 1–(SRGB_SIZE-1); 0 = not configured
    std::unordered_map<uint32_t, SrLfibEntry> srFib;      // key = inLabel
    std::vector<SrPolicy>                     srPolicies;
    std::unordered_map<int, uint32_t>         adjSids;    // key = port index; value = adj SID label
    // SRv6 (routers only). The IPv4 underlay carries an IPv6/SRH encapsulated payload.
    bool                    srv6Enabled = false;
    std::string             srv6Sid;
    std::vector<Srv6Policy> srv6Policies;
    // SD-WAN SLA probes and destination policy (routers only).
    bool                    sdwanEnabled = false;
    float                   sdwanLatencyMs[PORTS_PER_NODE] = {};
    float                   sdwanJitterMs[PORTS_PER_NODE] = {};
    float                   sdwanLossPct[PORTS_PER_NODE] = {};
    std::vector<SdwanPolicy> sdwanPolicies;
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
