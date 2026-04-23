#include "ConfigPanel.h"

// Temporary layout constants — replaced by #include "NetworkCanvas.h" in Task 8
static const int PANEL_W         = 280;
static const int SCREEN_W        = 1280;
static const int CANVAS_W        = SCREEN_W - PANEL_W;
static const int LOG_H           = 90;
static const int SCREEN_H        = 720;
static const int CANVAS_H        = SCREEN_H - LOG_H;
static const int MENU_ITEM_H     = 28;
static const int CONTEXT_MENU_W  = 160;
static const int CFG_HOSTNAME_Y  = 158;
static const int CFG_MGMTIP_Y    = 210;
static const int CFG_IFACE_SEP_Y = 246;
static const int CFG_PORT_Y0     = 272;
static const int CFG_PORT_STRIDE = 44;
static const int RTE_ROW_Y0       = 142;
static const int RTE_HEADER_SEP_Y = 136;
static const int RTE_ROW_H        = 22;
static const int RTE_ADD_SEP_Y    = 420;
static const int RTE_DEST_Y       = 464;
static const int RTE_NEXT_Y       = 516;
static const int RTE_BTN_Y        = 554;

Rectangle PnlFieldRect(int yOffset) {
    return {(float)(CANVAS_W + 12), (float)yOffset, (float)(PANEL_W - 24), 26.0f};
}

Rectangle PnlPortFieldRect(int port) {
    return {(float)(CANVAS_W + 80), (float)(CFG_PORT_Y0 + port * CFG_PORT_STRIDE),
            (float)(PANEL_W - 92), 24.0f};
}

float PnlTabW() { return (PANEL_W - 24 - 4) / 2.0f; }
Rectangle PnlConfigTabRect() {
    return {(float)(CANVAS_W + 12), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlRoutesTabRect() {
    return {(float)(CANVAS_W + 12) + PnlTabW() + 4.0f, 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlRouteDeleteRect(int rowIdx) {
    return {(float)(CANVAS_W + PANEL_W - 22),
            (float)(RTE_ROW_Y0 + rowIdx * RTE_ROW_H + 4), 16.0f, 14.0f};
}
Rectangle PnlRouteDestRect()   { return {(float)(CANVAS_W+12),(float)RTE_DEST_Y,(float)(PANEL_W-24),26.0f}; }
Rectangle PnlRouteNextRect()   { return {(float)(CANVAS_W+12),(float)RTE_NEXT_Y,(float)(PANEL_W-24),26.0f}; }
Rectangle PnlRouteAddBtnRect() { return {(float)(CANVAS_W+12),(float)RTE_BTN_Y, (float)(PANEL_W-24),28.0f}; }

void UpdateTextField(std::string& text, int maxLen) {
    int ch;
    while ((ch = GetCharPressed()) > 0)
        if ((int)text.size() < maxLen && ch >= 32 && ch < 127)
            text += (char)ch;
    if (IsKeyPressed(KEY_BACKSPACE) && !text.empty())
        text.pop_back();
}

void UpdateRoutesTab(DeviceNode* n, PanelState& ps) {
    if (ps.activeRouteField == -1) return;
    if (ps.activeRouteField == 0) UpdateTextField(ps.newRouteDest, 18);
    if (ps.activeRouteField == 1) UpdateTextField(ps.newRouteNext, 15);

    // KEY_TAB cycles focus between the two fields
    if (IsKeyPressed(KEY_TAB)) {
        ps.activeRouteField = (ps.activeRouteField == 0) ? 1 : 0;
        while (GetCharPressed() > 0) {}  // flush the tab character itself
    }

    // KEY_ENTER commits if valid
    if (IsKeyPressed(KEY_ENTER)) {
        if (ValidateIP(ps.newRouteDest) && ValidateIPOnly(ps.newRouteNext)) {
            n->staticRoutes.push_back({ps.newRouteDest, ps.newRouteNext, -1, ROUTE_STATIC});
            ps.newRouteDest.clear();
            ps.newRouteNext.clear();
            ps.activeRouteField = -1;
        }
    }
}
