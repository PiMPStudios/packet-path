#pragma once
#include "Device.h"
#include "raylib.h"
#include <string>
#include <vector>

enum PanelTab { TAB_CONFIG, TAB_ROUTES, TAB_ARP, TAB_OSPF, TAB_MPLS, TAB_BGP, TAB_VLAN, TAB_SUB };

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
Rectangle PnlRouteDeleteRect(int rowIdx);
Rectangle PnlRouteDestRect();
Rectangle PnlRouteNextRect();
Rectangle PnlRouteAddBtnRect();

// Keyboard input handlers (no draw calls)
void UpdateTextField(std::string& text, int maxLen);
void UpdateRoutesTab(DeviceNode* n, PanelState& ps);
