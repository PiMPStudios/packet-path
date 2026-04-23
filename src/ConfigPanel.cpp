#include "ConfigPanel.h"
#include "NetworkCanvas.h"

Rectangle PnlFieldRect(int yOffset) {
    return {(float)(CANVAS_W + 12), (float)yOffset, (float)(PANEL_W - 24), 26.0f};
}

Rectangle PnlPortFieldRect(int port) {
    return {(float)(CANVAS_W + 80), (float)(CFG_PORT_Y0 + port * CFG_PORT_STRIDE),
            (float)(PANEL_W - 92), 24.0f};
}

float PnlTabW() { return (PANEL_W - 24 - 8) / 3.0f; }
Rectangle PnlConfigTabRect() {
    return {(float)(CANVAS_W + 12), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlRoutesTabRect() {
    return {(float)(CANVAS_W + 12) + PnlTabW() + 4.0f, 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlArpTabRect() {
    return {(float)(CANVAS_W + 12) + 2.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
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
