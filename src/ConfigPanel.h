#pragma once
#include "Device.h"
#include "raylib.h"
#include <string>
#include <vector>

enum PanelTab { TAB_CONFIG, TAB_ROUTES };

struct PanelState {
    int         activeField      = -1;
    PanelTab    activeTab        = TAB_CONFIG;
    std::string newRouteDest;
    std::string newRouteNext;
    int         activeRouteField = -1;
};

// Layout rect helpers
Rectangle PnlFieldRect(int yOffset);
Rectangle PnlPortFieldRect(int port);
float     PnlTabW();
Rectangle PnlConfigTabRect();
Rectangle PnlRoutesTabRect();
Rectangle PnlRouteDeleteRect(int rowIdx);
Rectangle PnlRouteDestRect();
Rectangle PnlRouteNextRect();
Rectangle PnlRouteAddBtnRect();

// Keyboard input handlers (no draw calls)
void UpdateTextField(std::string& text, int maxLen);
void UpdateRoutesTab(DeviceNode* n, PanelState& ps);
