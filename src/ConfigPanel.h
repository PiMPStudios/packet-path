#pragma once
#include "Device.h"
#include "raylib.h"
#include <string>
#include <vector>

enum PanelTab { TAB_CONFIG, TAB_ROUTES, TAB_ARP, TAB_OSPF, TAB_MPLS, TAB_BGP,
                TAB_VLAN, TAB_SUB, TAB_VXLAN, TAB_ACL, TAB_NAT, TAB_TE, TAB_SR,
                TAB_SRV6, TAB_SDWAN };

struct TabInfo {
    PanelTab    tab;
    const char* label;
    Color       activeUnder;
    Color       activeTxt;
};
int            PnlTabCount(DeviceType t);
float          PnlTabWFor(DeviceType t);
const TabInfo* PnlTabList(DeviceType t);
Rectangle      PnlTabRect(DeviceType t, int visIdx);

struct PanelState {
    int         activeField      = -1;
    PanelTab    activeTab        = TAB_CONFIG;
    std::string newRouteDest;
    std::string newRouteNext;
    int         activeRouteField = -1;
    int         activePortAreaField = -1;  // 0..3 = which port's area field is active
    std::string portAreaBuf;               // edit buffer for area number
    int         bgpAsnField = -1;    // nodeId being edited (-1 = inactive)
    std::string bgpAsnBuf;
    int         vlanPortField = -1;   // 0..3 = which port's VLAN ID field is active, -1 = none
    std::string vlanPortBuf;          // edit buffer for VLAN ID digits

    // Sub (subinterface) tab
    int         subFormPort    = 0;    // selected parent port for add form (0-3)
    int         subActiveField = -1;   // 0=VLAN field, 1=IP field, -1=none
    std::string subVlanBuf;            // digit buffer for VLAN ID
    std::string subIpBuf;              // buffer for IP/CIDR entry

    // VXLAN tab
    int         vxlanField   = -1;   // 0=VNI editing, 1=VTEP IP editing
    std::string vxlanVniBuf;         // digit buffer for VNI
    std::string vxlanVtepBuf;        // text buffer for VTEP IP

    // ACL tab
    int         aclFormAction  = 0;    // 0=PERMIT, 1=DENY for the add-rule form
    int         aclActiveField = -1;   // 0=srcCidr, 1=dstCidr, 2=dstPort, -1=none
    std::string aclSrcBuf;             // edit buffer for source CIDR
    std::string aclDstBuf;             // edit buffer for dest CIDR
    std::string aclPortBuf;            // edit buffer for dst port (digits only)

    // NAT tab
    int         natField       = -1;   // 0=inside prefix editing, -1=none
    std::string natInsideBuf;          // edit buffer for inside prefix CIDR

    // TE tab
    int         teExpandedIdx   = -1;   // index in teTunnels currently expanded (-1=none)
    int         teActiveField   = -1;   // 0=dest, 1=bw, 2=hops
    std::string teDestBuf;
    std::string teBwBuf;
    std::string teHopsBuf;
    int         tePbwActivePort = -1;   // 0-3: which port BW field is active
    std::string tePbwBuf;               // edit buffer for port BW

    // SR tab
    bool        srNodeSidEditing = false;
    std::string srNodeSidBuf;
    int         srExpandedIdx    = -1;
    int         srActiveField    = -1;
    std::string srDestBuf;
    std::string srSegsBuf;

    // SRv6 tab
    bool        srv6SidEditing = false;
    std::string srv6SidBuf;
    int         srv6ExpandedIdx = -1;
    int         srv6ActiveField = -1;
    std::string srv6DestBuf;
    std::string srv6SegsBuf;

    int         sdwanActiveField = -1;  // 0=dest, 1=latency, 2=jitter, 3=loss
    std::string sdwanDestBuf;
    std::string sdwanLatencyBuf;
    std::string sdwanJitterBuf;
    std::string sdwanLossBuf;
};

// Layout rect helpers
Rectangle PnlFieldRect(int yOffset);
Rectangle PnlPortFieldRect(int port);
Rectangle PnlPortAreaFieldRect(int port);
float     PnlTabW();
Rectangle PnlConfigTabRect();
Rectangle PnlRoutesTabRect();
Rectangle PnlArpTabRect();
Rectangle PnlOspfTabRect();
Rectangle PnlOspfEnableRect();
Rectangle PnlMplsTabRect();
Rectangle PnlMplsToggleRect();
Rectangle PnlBgpTabRect();
Rectangle PnlBgpToggleRect();
Rectangle PnlBgpAsnRect();
Rectangle PnlBgpRrRect();
Rectangle PnlVlanTabRect();
Rectangle PnlVlanPortModeRect(int port);
Rectangle PnlVlanPortIdRect(int port);
Rectangle PnlSubTabRect();
Rectangle PnlSubPortBtnRect(int port);
Rectangle PnlSubVlanFieldRect();
Rectangle PnlSubIpFieldRect();
Rectangle PnlSubAddBtnRect();
Rectangle PnlSubRowDeleteRect(int rowIdx);
Rectangle PnlVxlanTabRect();
Rectangle PnlVxlanToggleRect();
Rectangle PnlVxlanVniRect();
Rectangle PnlVxlanVtepRect();
Rectangle PnlVxlanEvpnRect();
// ACL tab
Rectangle PnlAclTabRect();
Rectangle PnlAclToggleRect();
Rectangle PnlAclInPortBtnRect(int port);    // port 0-3 = ports, 4 = "—" (none)
Rectangle PnlAclOutPortBtnRect(int port);
Rectangle PnlAclRuleDeleteRect(int rowIdx);
Rectangle PnlAclFormActionRect();
Rectangle PnlAclFormSrcRect();
Rectangle PnlAclFormDstRect();
Rectangle PnlAclFormPortRect();
Rectangle PnlAclFormAddBtnRect();
// NAT tab
Rectangle PnlNatTabRect();
Rectangle PnlNatToggleRect();
Rectangle PnlNatInsidePortBtnRect(int port);   // port 0-3
Rectangle PnlNatOutsidePortBtnRect(int port);
Rectangle PnlNatInsidePrefixRect();
// TE tab
float TeListBaseY();
float TeRowH();
float TeFormH();
Rectangle PnlTeTabRect();
Rectangle PnlTeToggleRect();
Rectangle PnlTePbwRect(int port);
Rectangle PnlTeTunnelRowRect(int idx);
Rectangle PnlTeAddBtnRect(int tunnelCount);
Rectangle PnlTeSimBtnRect();
Rectangle PnlTeDelBtnRect();
// SR tab
float SrListBaseY();
float SrRowH();
float SrFormH();
Rectangle PnlSrTabRect();
Rectangle PnlSrToggleRect();
Rectangle PnlSrNodeSidRect();
Rectangle PnlSrPolicyRowRect(int idx);
Rectangle PnlSrAddBtnRect(int count);
// SRv6 tab
float     Srv6ListBaseY();
float     Srv6RowH();
float     Srv6FormH();
Rectangle PnlSrv6ToggleRect();
Rectangle PnlSrv6SidRect();
Rectangle PnlSrv6PolicyRowRect(int idx);
Rectangle PnlSrv6AddBtnRect(int count);
// SD-WAN tab
Rectangle PnlSdwanToggleRect();
Rectangle PnlSdwanFieldRect(int field);
Rectangle PnlSdwanPreferredRect();
Rectangle PnlSdwanBackupRect();
Rectangle PnlSdwanAddRect();
Rectangle PnlRouteDeleteRect(int rowIdx);
Rectangle PnlRouteDestRect();
Rectangle PnlRouteNextRect();
Rectangle PnlRouteAddBtnRect();

// Keyboard input handlers (no draw calls)
void UpdateTextField(std::string& text, int maxLen);
void UpdateRoutesTab(DeviceNode* n, PanelState& ps);
