#pragma once
#include "Device.h"
#include "raylib.h"
#include <string>
#include <vector>

enum PanelTab { TAB_CONFIG, TAB_ROUTES, TAB_ARP, TAB_OSPF, TAB_MPLS, TAB_BGP,
                TAB_VLAN, TAB_SUB, TAB_VXLAN, TAB_ACL, TAB_NAT };

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
Rectangle PnlRouteDeleteRect(int rowIdx);
Rectangle PnlRouteDestRect();
Rectangle PnlRouteNextRect();
Rectangle PnlRouteAddBtnRect();

// Keyboard input handlers (no draw calls)
void UpdateTextField(std::string& text, int maxLen);
void UpdateRoutesTab(DeviceNode* n, PanelState& ps);
