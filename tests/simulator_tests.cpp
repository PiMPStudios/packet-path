#include "Cable.h"
#include "BgpEngine.h"
#include "LdpEngine.h"
#include "Level.h"
#include "LevelCatalog.h"
#include "OspfEngine.h"
#include "SceneSerializer.h"
#include "RsvpEngine.h"
#include "SimulationEngine.h"
#include "SoundEngine.h"
#include "SrEngine.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

DeviceNode MakeRouter(int id, const std::string& label) {
    DeviceNode node;
    node.id    = id;
    node.type  = ROUTER;
    node.label = label;
    return node;
}

DeviceNode MakePc(int id, const std::string& label) {
    DeviceNode node;
    node.id    = id;
    node.type  = PC;
    node.label = label;
    return node;
}

DeviceNode MakeSwitch(int id, const std::string& label) {
    DeviceNode node;
    node.id    = id;
    node.type  = SWITCH;
    node.label = label;
    return node;
}

Cable Link(int fromId, int fromPort, int toId, int toPort) {
    return {fromId, fromPort, toId, toPort, false};
}

std::string TempPath(const std::string& filename) {
    return (std::filesystem::temp_directory_path() / filename).string();
}

void TestIpParsingAndSubnetBoundaries() {
    Expect(ValidateIPOnly("192.0.2.1"), "A valid host address should parse");
    Expect(!ValidateIPOnly("192.0.2.999"), "An out-of-range octet must fail");
    Expect(ValidateIP("10.1.2.3/0") && ValidateIP("10.1.2.3/32"),
           "CIDR boundary prefix lengths should parse");
    Expect(!ValidateIP("10.1.2.3/33") && !ValidateIP("10.1.2.3/-1"),
           "Invalid prefix lengths must fail without shifting out of range");
    Expect(NetworkAddress("10.1.2.3/24") == "10.1.2.0/24",
           "NetworkAddress should mask host bits");
    Expect(NetworkAddress("10.1.2.3/0") == "0.0.0.0/0",
           "A /0 should normalize to the default route");
    Expect(IpInSubnet("10.1.2.3", "10.1.2.3/32"),
           "A host should match its /32");
    Expect(!IpInSubnet("10.1.2.4", "10.1.2.3/32"),
           "A neighboring host should not match a /32");
    Expect(!IpInSubnet("10.1.2.3", "10.0.0.0/99"),
           "Invalid subnet masks must fail safely");
}

void TestLongestPrefixMatchSelectsMostSpecificRoute() {
    auto r1 = MakeRouter(1, "R1");
    auto r2 = MakeRouter(2, "R2");
    auto r3 = MakeRouter(3, "R3");
    auto pc = MakePc(4, "PC-B");
    r1.portIp[0] = "192.0.2.1/30";
    r2.portIp[0] = "192.0.2.2/30";
    r1.portIp[1] = "198.51.100.1/30";
    r3.portIp[0] = "198.51.100.2/30";
    r3.portIp[1] = "10.1.2.1/24";
    pc.portIp[0] = "10.1.2.99/24";
    r1.staticRoutes.push_back({"10.0.0.0/8", "192.0.2.2", 0, ROUTE_STATIC});
    r1.staticRoutes.push_back({"10.1.2.0/24", "198.51.100.2", 1, ROUTE_STATIC});

    const std::vector<DeviceNode> nodes = {r1, r2, r3, pc};
    const std::vector<Cable> cables = {
        Link(1, 0, 2, 0), Link(1, 1, 3, 0), Link(3, 1, 4, 0),
    };
    const ForwardResult result = SimulateForward(1, "10.1.2.99", nodes, cables,
                                                  "198.51.100.1");
    Expect(result.success, "The specific route should deliver the packet");
    Expect(result.path.size() > 1 && result.path[1] == 3,
           "The /24 must win over the /8 regardless of insertion order");
}

void TestVlanAccessAndTrunkPaths() {
    auto a = MakePc(1, "PC-A");
    auto s1 = MakeSwitch(2, "SW-1");
    auto s2 = MakeSwitch(3, "SW-2");
    auto b = MakePc(4, "PC-B");
    a.portIp[0] = "192.168.10.2/24";
    b.portIp[0] = "192.168.10.3/24";
    s1.vlanPorts[0] = {VLAN_ACCESS, 10};
    s1.vlanPorts[1] = {VLAN_TRUNK, 1};
    s2.vlanPorts[0] = {VLAN_TRUNK, 1};
    s2.vlanPorts[1] = {VLAN_ACCESS, 10};

    std::vector<DeviceNode> nodes = {a, s1, s2, b};
    const std::vector<Cable> cables = {
        Link(1, 0, 2, 0), Link(2, 1, 3, 0), Link(3, 1, 4, 0),
    };
    ForwardResult result = SimulateForward(1, "192.168.10.3", nodes, cables,
                                            "192.168.10.2");
    Expect(result.success && result.path == std::vector<int>({1, 2, 3, 4}),
           "Matching access VLANs should cross a trunk");

    nodes[2].vlanPorts[1].accessVlan = 20;
    result = SimulateForward(1, "192.168.10.3", nodes, cables, "192.168.10.2");
    Expect(!result.success && result.reason.find("VLAN mismatch") != std::string::npos,
           "A mismatched destination access VLAN must block the frame");
}

void TestOspfConvergenceAndFailureRecovery() {
    auto r1 = MakeRouter(1, "R1");
    auto r2 = MakeRouter(2, "R2");
    r1.portIp[0] = "10.0.12.1/30";
    r2.portIp[0] = "10.0.12.2/30";
    r2.portIp[1] = "192.0.2.1/24";
    r1.ospfEnabled = r2.ospfEnabled = true;
    r1.routerId = "1.1.1.1";
    r2.routerId = "2.2.2.2";
    std::vector<DeviceNode> nodes = {r1, r2};
    std::vector<Cable> cables = {Link(1, 0, 2, 0)};

    UpdateOspf(OSPF_HELLO_INTERVAL + 0.1f, nodes, cables);
    Expect(!nodes[0].ospfNeighbors.empty() &&
               nodes[0].ospfNeighbors[0].state == OSPF_FULL,
           "OSPF neighbors should reach FULL on a live same-area link");
    Expect(!nodes[0].ospfRoutes.empty(), "OSPF should install the remote network");

    cables[0].broken = true;
    UpdateOspf(OSPF_DEAD_INTERVAL + 0.1f, nodes, cables);
    Expect(nodes[0].ospfNeighbors.empty(),
           "The dead timer should remove a neighbor across a broken link");
    Expect(nodes[0].ospfRoutes.empty(),
           "SPF should withdraw routes learned across the failed link");
}

void TestBgpSessionAndLinkFailure() {
    auto r1 = MakeRouter(1, "R1");
    auto r2 = MakeRouter(2, "R2");
    r1.portIp[0] = "10.0.12.1/30";
    r2.portIp[0] = "10.0.12.2/30";
    r1.bgpEnabled = r2.bgpEnabled = true;
    r1.localAsn = 64501;
    r2.localAsn = 64502;
    r1.bgpNetworks = {"192.0.2.1/24"};
    r2.bgpNetworks = {"198.51.100.1/24"};
    std::vector<DeviceNode> nodes = {r1, r2};
    std::vector<Cable> cables = {Link(1, 0, 2, 0)};

    UpdateBgp(nodes, cables);
    Expect(nodes[0].bgpNeighbors.size() == 1 && nodes[0].bgpNeighbors[0].established,
           "A live eBGP cable should establish a session");
    Expect(!nodes[0].bgpRoutes.empty() &&
               nodes[0].bgpRoutes[0].prefix == "198.51.100.0/24",
           "The peer network should enter the BGP RIB");

    cables[0].broken = true;
    UpdateBgp(nodes, cables);
    Expect(nodes[0].bgpNeighbors.empty() && nodes[0].bgpRoutes.empty(),
           "A broken cable should withdraw the BGP session and learned routes");
}

void TestLdpBuildsPushAndPhpBindings() {
    auto r1 = MakeRouter(1, "R1");
    auto r2 = MakeRouter(2, "R2");
    auto r3 = MakeRouter(3, "R3");
    r1.portIp[0] = "10.0.12.1/30";
    r2.portIp[0] = "10.0.12.2/30";
    r2.portIp[1] = "10.0.23.1/30";
    r3.portIp[0] = "10.0.23.2/30";
    r3.portIp[1] = "192.0.2.1/24";
    for (auto* router : {&r1, &r2, &r3}) {
        router->ospfEnabled = true;
        router->ldpEnabled = true;
    }
    r1.routerId = "1.1.1.1";
    r2.routerId = "2.2.2.2";
    r3.routerId = "3.3.3.3";
    r1.ospfRoutes.push_back({"192.0.2.0/24", "10.0.12.2", 0, ROUTE_OSPF});
    r2.ospfRoutes.push_back({"192.0.2.0/24", "10.0.23.2", 1, ROUTE_OSPF});
    std::vector<DeviceNode> nodes = {r1, r2, r3};
    std::vector<Cable> cables = {Link(1, 0, 2, 0), Link(2, 1, 3, 0)};

    UpdateLdp(nodes, cables);
    Expect(nodes[0].lfib.count("192.0.2.0/24") == 1 &&
               nodes[0].lfib.at("192.0.2.0/24").outLabel != MPLS_IMPLICIT_NULL,
           "The ingress LSR should push the transit router's label");
    Expect(nodes[1].lfib.count("192.0.2.0/24") == 1 &&
               nodes[1].lfib.at("192.0.2.0/24").outLabel == MPLS_IMPLICIT_NULL,
           "The penultimate LSR should receive implicit-null from the egress");
}

void TestSrPolicyResolvesOspfNetworkPrefixes() {
    auto r1 = MakeRouter(1, "R1");
    auto r2 = MakeRouter(2, "R2");
    auto r3 = MakeRouter(3, "R3");

    r1.portIp[0] = "10.0.12.1/30";
    r2.portIp[0] = "10.0.12.2/30";
    r2.portIp[1] = "10.0.23.1/30";
    r3.portIp[0] = "10.0.23.2/30";

    r1.ospfRoutes.push_back({"10.0.23.0/30", "10.0.12.2", 0, ROUTE_OSPF});

    r1.srEnabled = r2.srEnabled = r3.srEnabled = true;
    r1.nodeSid = 1;
    r2.nodeSid = 2;
    r3.nodeSid = 3;

    SrPolicy policy;
    policy.id         = 1;
    policy.destIp     = "192.0.2.2";
    policy.segmentIps = {"10.0.23.2"};
    r1.srPolicies.push_back(policy);

    std::vector<DeviceNode> nodes = {r1, r2, r3};
    std::vector<Cable> cables = {Link(1, 0, 2, 0), Link(2, 1, 3, 0)};

    UpdateSr(nodes, cables);

    const auto& resolved = nodes[0].srPolicies[0];
    Expect(resolved.isActive, "SR policy should resolve an OSPF-advertised subnet");
    Expect(resolved.activePath == std::vector<int>({1, 2, 3}),
           "SR active path should follow R1-R2-R3");
}

void TestSrPolicySteersTheForwardingPath() {
    auto r1 = MakeRouter(1, "R1");
    auto r2 = MakeRouter(2, "R2");
    auto r3 = MakeRouter(3, "R3");
    auto pc = MakePc(4, "PC-B");

    r1.portIp[0] = "10.0.12.1/30";
    r2.portIp[0] = "10.0.12.2/30";
    r1.portIp[1] = "10.0.13.1/30";
    r3.portIp[0] = "10.0.13.2/30";
    r3.portIp[1] = "192.0.2.1/24";
    pc.portIp[0] = "192.0.2.2/24";

    r1.staticRoutes.push_back({"192.0.2.0/24", "10.0.12.2", -1, ROUTE_STATIC});
    r1.srEnabled = r3.srEnabled = true;
    r1.nodeSid = 1;
    r3.nodeSid = 3;

    SrPolicy policy;
    policy.id         = 1;
    policy.destIp     = "192.0.2.2";
    policy.segmentIps = {"10.0.13.2"};
    r1.srPolicies.push_back(policy);

    std::vector<DeviceNode> nodes = {r1, r2, r3, pc};
    std::vector<Cable> cables = {
        Link(1, 0, 2, 0),
        Link(1, 1, 3, 0),
        Link(3, 1, 4, 0),
    };

    UpdateSr(nodes, cables);
    ForwardResult result = SimulateForward(1, "192.0.2.2", nodes, cables,
                                           "10.0.12.1");

    Expect(result.success, "SR-steered packet should reach PC-B");
    Expect(result.path.size() >= 2 && result.path[1] == 3,
           "SR policy should override the IP route through R2 and steer through R3");
}

void TestSrAdjacencySegmentForcesTheSelectedLink() {
    auto r1 = MakeRouter(1, "R1");
    auto r2 = MakeRouter(2, "R2");
    auto r3 = MakeRouter(3, "R3");
    r1.portIp[0] = "10.0.12.1/30";
    r2.portIp[0] = "10.0.12.2/30";
    r2.portIp[1] = "10.0.23.1/30";
    r3.portIp[0] = "10.0.23.2/30";
    r1.srEnabled = r2.srEnabled = true;
    r1.nodeSid = 1;
    r2.nodeSid = 2;
    r1.staticRoutes.push_back({"10.0.23.0/30", "10.0.12.2", 0, ROUTE_STATIC});

    SrPolicy policy;
    policy.id         = 1;
    policy.destIp     = "10.0.23.2";
    policy.segmentIps = {"adj:10.0.12.2:1"};
    r1.srPolicies.push_back(policy);
    std::vector<DeviceNode> nodes = {r1, r2, r3};
    const std::vector<Cable> cables = {Link(1, 0, 2, 0), Link(2, 1, 3, 0)};

    UpdateSr(nodes, cables);
    Expect(nodes[0].srPolicies[0].isActive,
           "A valid adjacency segment should activate the SR policy");
    Expect(nodes[0].srPolicies[0].activePath == std::vector<int>({1, 2, 3}),
           "The adjacency segment should terminate across its selected link");
    const ForwardResult result = SimulateForward(1, "10.0.23.2", nodes, cables,
                                                  "10.0.12.1");
    Expect(result.success, "Adjacency-SID forwarding should deliver the packet");
    Expect(result.hops.size() >= 2 && result.hops[0].labelOp == LABEL_PUSH &&
               result.hops[1].nodeId == 2 && result.hops[1].labelOp == LABEL_POP &&
               result.hops[1].outPort == 1,
           "The SID owner should pop the adjacency label and force port 1");
}

void TestTeTunnelSteersTheForwardingPath() {
    auto r1 = MakeRouter(1, "R1");
    auto r2 = MakeRouter(2, "R2");
    auto r3 = MakeRouter(3, "R3");
    auto r4 = MakeRouter(4, "R4");

    r1.portIp[0] = "10.0.12.1/30";
    r2.portIp[0] = "10.0.12.2/30";
    r2.portIp[1] = "10.0.23.1/30";
    r3.portIp[0] = "10.0.23.2/30";
    r1.portIp[1] = "10.0.14.1/30";
    r4.portIp[0] = "10.0.14.2/30";
    r4.portIp[1] = "10.0.43.1/30";
    r3.portIp[1] = "10.0.43.2/30";

    r1.staticRoutes.push_back({"10.0.23.2/32", "10.0.12.2", -1, ROUTE_STATIC});

    r1.rsvpEnabled = r2.rsvpEnabled = r3.rsvpEnabled = r4.rsvpEnabled = true;
    TeTunnel tunnel;
    tunnel.id           = 1;
    tunnel.destIp       = "10.0.23.2";
    tunnel.bandwidth    = 100;
    tunnel.useExplicit  = true;
    tunnel.explicitHops = {4, 3};
    r1.teTunnels.push_back(tunnel);

    std::vector<DeviceNode> nodes = {r1, r2, r3, r4};
    std::vector<Cable> cables = {
        Link(1, 0, 2, 0),
        Link(2, 1, 3, 0),
        Link(1, 1, 4, 0),
        Link(4, 1, 3, 1),
    };

    UpdateRsvp(nodes, cables);
    Expect(nodes[0].teTunnels[0].isUp,
           "Explicit R1-R4-R3 tunnel should come up");
    ForwardResult result = SimulateForward(1, "10.0.23.2", nodes, cables,
                                           "10.0.12.1");

    Expect(result.success, "TE-steered packet should reach R3");
    Expect(result.path.size() >= 3 && result.path[1] == 4,
           "TE tunnel should override the IP route through R2 and steer through R4");
    Expect(result.hops.size() >= 2 && result.hops[0].labelOp == LABEL_PUSH &&
               result.hops[0].outLabel == 16001,
           "TE head-end should impose the label expected by the first transit LSR");
    Expect(result.hops[1].nodeId == 4 && result.hops[1].labelOp == LABEL_POP &&
               result.hops[1].inLabel == 16001,
           "TE transit forwarding should use the LFIB without an IP route");
}

void TestRsvpReservationDoesNotCountItself() {
    auto r1 = MakeRouter(1, "R1");
    auto r2 = MakeRouter(2, "R2");
    r1.portIp[0] = "10.0.12.1/30";
    r2.portIp[0] = "10.0.12.2/30";
    r1.rsvpEnabled = true;

    TeTunnel tunnel;
    tunnel.id        = 1;
    tunnel.destIp    = "10.0.12.2";
    tunnel.bandwidth = 600;
    r1.teTunnels.push_back(tunnel);

    std::vector<DeviceNode> nodes = {r1, r2};
    std::vector<Cable> cables = {Link(1, 0, 2, 0)};

    bool sawUpEvent = false;
    for (int tick = 0; tick < 5; ++tick) {
        const auto events = UpdateRsvp(nodes, cables);
        sawUpEvent = sawUpEvent || !events.empty();
        Expect(nodes[0].teTunnels[0].isUp,
               "A 600 Mbps tunnel on a 1000 Mbps link should remain up");
    }
    Expect(sawUpEvent, "RSVP should emit a user-visible tunnel transition event");
}

void TestRsvpRejectsNonAdjacentExplicitHops() {
    auto r1 = MakeRouter(1, "R1");
    auto r2 = MakeRouter(2, "R2");
    r1.portIp[0] = "10.0.12.1/30";
    r2.portIp[0] = "10.0.12.2/30";
    r1.rsvpEnabled = true;

    TeTunnel tunnel;
    tunnel.id           = 1;
    tunnel.destIp       = "10.0.12.2";
    tunnel.bandwidth    = 100;
    tunnel.useExplicit  = true;
    tunnel.explicitHops = {2};
    r1.teTunnels.push_back(tunnel);

    std::vector<DeviceNode> nodes = {r1, r2};
    std::vector<Cable> cables;
    UpdateRsvp(nodes, cables);

    Expect(!nodes[0].teTunnels[0].isUp,
           "An explicit path with no physical cable must remain down");
}

void TestRsvpWithdrawsAfterLinkFailure() {
    auto r1 = MakeRouter(1, "R1");
    auto r2 = MakeRouter(2, "R2");
    r1.portIp[0] = "10.0.12.1/30";
    r2.portIp[0] = "10.0.12.2/30";
    r1.rsvpEnabled = true;
    TeTunnel tunnel;
    tunnel.id = 1;
    tunnel.destIp = "10.0.12.2";
    tunnel.bandwidth = 100;
    r1.teTunnels.push_back(tunnel);
    std::vector<DeviceNode> nodes = {r1, r2};
    std::vector<Cable> cables = {Link(1, 0, 2, 0)};
    UpdateRsvp(nodes, cables);
    Expect(nodes[0].teTunnels[0].isUp, "The RSVP tunnel should initially be up");

    cables[0].broken = true;
    UpdateRsvp(nodes, cables);  // make-before-break hold tick
    const auto events = UpdateRsvp(nodes, cables);
    Expect(!nodes[0].teTunnels[0].isUp && nodes[0].teLfib.empty(),
           "RSVP should withdraw forwarding state after the hold tick");
    Expect(!events.empty() && events[0].find("down:") != std::string::npos,
           "RSVP link failure should emit a down event");
}

void TestRsvpRejectsUnresolvedExplicitHops() {
    auto r1 = MakeRouter(1, "R1");
    auto r2 = MakeRouter(2, "R2");
    r1.portIp[0] = "10.0.12.1/30";
    r2.portIp[0] = "10.0.12.2/30";
    r1.rsvpEnabled = true;

    TeTunnel tunnel;
    tunnel.id              = 1;
    tunnel.destIp          = "10.0.12.2";
    tunnel.bandwidth       = 100;
    tunnel.useExplicit     = true;
    tunnel.explicitHopIps  = {"203.0.113.99"};
    ResolveExplicitHops(tunnel, {r1, r2});
    r1.teTunnels.push_back(tunnel);

    std::vector<DeviceNode> nodes = {r1, r2};
    std::vector<Cable> cables = {Link(1, 0, 2, 0)};
    UpdateRsvp(nodes, cables);

    Expect(!nodes[0].teTunnels[0].isUp,
           "An unresolved explicit waypoint must not be silently ignored");
}

void TestRsvpRetainsTransitLfibEntries() {
    auto r1 = MakeRouter(1, "R1");
    auto r2 = MakeRouter(2, "R2");
    auto r3 = MakeRouter(3, "R3");
    r1.portIp[0] = "10.0.12.1/30";
    r2.portIp[0] = "10.0.12.2/30";
    r2.portIp[1] = "10.0.23.1/30";
    r3.portIp[0] = "10.0.23.2/30";
    r1.rsvpEnabled = r2.rsvpEnabled = r3.rsvpEnabled = true;

    TeTunnel tunnel;
    tunnel.id        = 1;
    tunnel.destIp    = "10.0.23.2";
    tunnel.bandwidth = 100;
    r1.teTunnels.push_back(tunnel);

    std::vector<DeviceNode> nodes = {r1, r2, r3};
    std::vector<Cable> cables = {Link(1, 0, 2, 0), Link(2, 1, 3, 0)};
    UpdateRsvp(nodes, cables);

    Expect(nodes[0].teTunnels[0].isUp, "Multi-hop RSVP tunnel should come up");
    Expect(!nodes[1].teLfib.empty(),
           "RSVP rebuild should retain the transit router's LFIB entry");
}

void TestDuplicateSrNodeSidsAreRejected() {
    auto r1 = MakeRouter(1, "R1");
    auto r2 = MakeRouter(2, "R2");
    r1.portIp[0] = "10.0.12.1/30";
    r2.portIp[0] = "10.0.12.2/30";
    r1.srEnabled = r2.srEnabled = true;
    r1.nodeSid = r2.nodeSid = 10;

    SrPolicy policy;
    policy.id         = 1;
    policy.destIp     = "10.0.12.2";
    policy.segmentIps = {"10.0.12.2"};
    r1.srPolicies.push_back(policy);

    std::vector<DeviceNode> nodes = {r1, r2};
    std::vector<Cable> cables = {Link(1, 0, 2, 0)};
    UpdateSr(nodes, cables);

    Expect(!nodes[0].srPolicies[0].isActive,
           "A policy using a duplicate global Node SID must stay inactive");
    Expect(nodes[0].srPolicies[0].statusMsg == "Duplicate Node SID",
           "Duplicate Node SID should have an explicit status message");
}

void TestSceneRoundTripsTeAndSrConfiguration() {
    auto r1 = MakeRouter(1, "R1");
    r1.rsvpEnabled = true;
    r1.portBandwidth[0] = 250;
    TeTunnel tunnel;
    tunnel.id              = 7;
    tunnel.destIp          = "10.0.9.9";
    tunnel.bandwidth       = 125;
    tunnel.useExplicit     = true;
    tunnel.explicitHopIps  = {"10.0.2.2", "10.0.9.9"};
    r1.teTunnels.push_back(tunnel);

    r1.srEnabled = true;
    r1.nodeSid   = 42;
    SrPolicy policy;
    policy.id         = 3;
    policy.destIp     = "192.0.2.2";
    policy.segmentIps = {"10.0.2.2", "10.0.9.9"};
    r1.srPolicies.push_back(policy);

    const std::string path = TempPath("packet-path-roundtrip.json");
    Expect(SaveScene(path, {r1}, {}), "Scene save should succeed");

    LevelDef loaded;
    Expect(LoadLevel(path, loaded), "Saved scene should load");
    std::remove(path.c_str());

    Expect(loaded.devices.size() == 1, "Round-trip should preserve the router");
    const auto& restored = loaded.devices[0];
    Expect(restored.rsvpEnabled && restored.portBandwidth[0] == 250,
           "Round-trip should preserve RSVP interface configuration");
    Expect(restored.teTunnels.size() == 1 && restored.teTunnels[0].id == 7 &&
               restored.teTunnels[0].explicitHopIps.size() == 2,
           "Round-trip should preserve TE tunnels");
    Expect(restored.srEnabled && restored.nodeSid == 42,
           "Round-trip should preserve SR node configuration");
    Expect(restored.srPolicies.size() == 1 &&
               restored.srPolicies[0].segmentIps.size() == 2,
           "Round-trip should preserve SR policies");
}

void TestSceneRejectsInvalidCablePortsAndTypes() {
    const std::string badPortPath = TempPath("packet-path-bad-port.json");
    {
        std::ofstream file(badPortPath);
        file << R"({
            "devices": [
                {"id": 1, "type": "ROUTER"},
                {"id": 2, "type": "ROUTER"}
            ],
            "cables": [
                {"from": 1, "fromPort": 9, "to": 2, "toPort": 0}
            ]
        })";
    }
    LevelDef loaded;
    Expect(!LoadLevel(badPortPath, loaded),
           "Scene loader should reject out-of-range cable ports");
    std::remove(badPortPath.c_str());

    const std::string badTypePath = TempPath("packet-path-bad-type.json");
    {
        std::ofstream file(badTypePath);
        file << R"({"devices": [{"id": "router-one", "type": "ROUTER"}]})";
    }
    bool loadedBadType = false;
    bool threw = false;
    try {
        loadedBadType = LoadLevel(badTypePath, loaded);
    } catch (...) {
        threw = true;
    }
    std::remove(badTypePath.c_str());
    Expect(!threw && !loadedBadType,
           "Scene loader should fail gracefully on invalid JSON field types");
}

void TestBundledLevelsPassSceneValidation() {
    for (int level = 1; level <= 16; ++level) {
        char path[64];
        std::snprintf(path, sizeof(path), "levels/level_%02d.json", level);
        LevelDef loaded;
        Expect(LoadLevel(path, loaded),
               std::string("Bundled level should pass scene validation: ") + path);
    }
}

void TestLevelCatalogDiscoversMetadataWithoutFixedLimit() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "packet-path-level-catalog";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    {
        std::ofstream file(directory / "level_17.json");
        file << R"({"id":17,"title":"Dynamic Seventeen","devices":[],"cables":[]})";
    }
    {
        std::ofstream file(directory / "level_02.json");
        file << R"({"id":2,"title":"Dynamic Two","devices":[],"cables":[]})";
    }
    {
        std::ofstream file(directory / "level_99.json");
        file << "not json";
    }

    const auto catalog = DiscoverLevels(directory.string());
    std::filesystem::remove_all(directory);
    Expect(catalog.size() == 2, "Invalid discovered levels should be omitted");
    Expect(catalog[0].number == 2 && catalog[1].number == 17,
           "Discovered levels should be numerically sorted without a 16-level cap");
    Expect(FindNextLevel(catalog, 2) && FindNextLevel(catalog, 2)->number == 17,
           "Next-level lookup should follow discovered metadata");
}

void TestAudioCallsAreSafeWithoutADevice() {
    Expect(!InitSounds(), "Headless tests should fall back when no audio device exists");
    Expect(!IsSoundAvailable(), "Sound should remain disabled after fallback");
    PlayPacketSend();
    PlayPacketArrive();
    PlayPacketFail();
    UnloadSounds();
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests = {
        {"IP parsing and subnet boundaries", TestIpParsingAndSubnetBoundaries},
        {"Longest-prefix match", TestLongestPrefixMatchSelectsMostSpecificRoute},
        {"VLAN access/trunk forwarding", TestVlanAccessAndTrunkPaths},
        {"OSPF convergence and recovery", TestOspfConvergenceAndFailureRecovery},
        {"BGP session failure recovery", TestBgpSessionAndLinkFailure},
        {"LDP push and PHP", TestLdpBuildsPushAndPhpBindings},
        {"SR resolves OSPF prefixes", TestSrPolicyResolvesOspfNetworkPrefixes},
        {"SR steers forwarding", TestSrPolicySteersTheForwardingPath},
        {"SR adjacency steering", TestSrAdjacencySegmentForcesTheSelectedLink},
        {"TE steers forwarding", TestTeTunnelSteersTheForwardingPath},
        {"RSVP reservation stability", TestRsvpReservationDoesNotCountItself},
        {"RSVP explicit path validation", TestRsvpRejectsNonAdjacentExplicitHops},
        {"RSVP link failure withdrawal", TestRsvpWithdrawsAfterLinkFailure},
        {"RSVP unresolved hop validation", TestRsvpRejectsUnresolvedExplicitHops},
        {"RSVP transit LFIB rebuild", TestRsvpRetainsTransitLfibEntries},
        {"Duplicate SR SID validation", TestDuplicateSrNodeSidsAreRejected},
        {"TE/SR scene round-trip", TestSceneRoundTripsTeAndSrConfiguration},
        {"Scene validation", TestSceneRejectsInvalidCablePortsAndTypes},
        {"Bundled level validation", TestBundledLevelsPassSceneValidation},
        {"Dynamic level catalog", TestLevelCatalogDiscoversMetadataWithoutFixedLimit},
        {"Audio fallback", TestAudioCallsAreSafeWithoutADevice},
    };

    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::printf("[PASS] %s\n", name);
        } catch (const std::exception& error) {
            ++failures;
            std::fprintf(stderr, "[FAIL] %s: %s\n", name, error.what());
        }
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    std::printf("All %zu tests passed\n", tests.size());
    return 0;
}
