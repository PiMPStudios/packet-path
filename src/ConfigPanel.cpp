#include "ConfigPanel.h"
#include "NetworkCanvas.h"

Rectangle PnlFieldRect(int yOffset) {
    return {(float)(CANVAS_W + 12), (float)yOffset, (float)(PANEL_W - 24), 26.0f};
}

Rectangle PnlPortFieldRect(int port) {
    return {(float)(CANVAS_W + 80), (float)(CFG_PORT_Y0 + port * CFG_PORT_STRIDE),
            126.0f, 24.0f};
}

Rectangle PnlPortAreaFieldRect(int port) {
    return {(float)(CANVAS_W + 224), (float)(CFG_PORT_Y0 + port * CFG_PORT_STRIDE),
            44.0f, 24.0f};
}

float PnlTabW() { return (PANEL_W - 24.0f - 7.0f * 4.0f) / 8.0f; }
Rectangle PnlConfigTabRect() {
    return {(float)(CANVAS_W + 12), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlRoutesTabRect() {
    return {(float)(CANVAS_W + 12) + PnlTabW() + 4.0f, 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlArpTabRect() {
    return {(float)(CANVAS_W + 12) + 2.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlOspfTabRect() {
    return {(float)(CANVAS_W + 12) + 3.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlOspfEnableRect() {
    return {(float)(CANVAS_W + 12), 120.0f, (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlMplsTabRect() {
    return {(float)(CANVAS_W + 12) + 4.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlMplsToggleRect() {
    return {(float)(CANVAS_W + 12), 120.0f, (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlBgpTabRect() {
    return {(float)(CANVAS_W + 12) + 5.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlBgpToggleRect() {
    return {(float)(CANVAS_W + 12), 120.0f, (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlBgpAsnRect() {
    return {(float)(CANVAS_W + 56), 152.0f, (float)(PANEL_W - 68), 22.0f};
}
Rectangle PnlBgpRrRect() {
    return {(float)(CANVAS_W + 12), 182.0f, (float)(PANEL_W - 24), 22.0f};
}
Rectangle PnlRouteDeleteRect(int rowIdx) {
    return {(float)(CANVAS_W + PANEL_W - 22),
            (float)(RTE_ROW_Y0 + rowIdx * RTE_ROW_H + 4), 16.0f, 14.0f};
}
Rectangle PnlRouteDestRect()   { return {(float)(CANVAS_W+12),(float)RTE_DEST_Y,(float)(PANEL_W-24),26.0f}; }
Rectangle PnlRouteNextRect()   { return {(float)(CANVAS_W+12),(float)RTE_NEXT_Y,(float)(PANEL_W-24),26.0f}; }
Rectangle PnlRouteAddBtnRect() { return {(float)(CANVAS_W+12),(float)RTE_BTN_Y, (float)(PANEL_W-24),28.0f}; }
Rectangle PnlVlanTabRect() {
    return {(float)(CANVAS_W + 12) + 6.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlSubTabRect() {
    return {(float)(CANVAS_W + 12) + 7.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlSubPortBtnRect(int port) {
    float btnW = 28.0f, gap = 4.0f;
    float x0 = (float)(CANVAS_W + 64);
    return {x0 + port * (btnW + gap), (float)(SUB_FORM_Y0), btnW, 22.0f};
}
Rectangle PnlSubVlanFieldRect() {
    return {(float)(CANVAS_W + 64), (float)(SUB_FORM_Y0 + 30), 60.0f, 22.0f};
}
Rectangle PnlSubIpFieldRect() {
    return {(float)(CANVAS_W + 64), (float)(SUB_FORM_Y0 + 60), (float)(PANEL_W - 76), 22.0f};
}
Rectangle PnlSubAddBtnRect() {
    return {(float)(CANVAS_W + 12), (float)(SUB_FORM_Y0 + 92), (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlSubRowDeleteRect(int rowIdx) {
    return {(float)(CANVAS_W + PANEL_W - 22),
            (float)(SUB_ROW_Y0 + rowIdx * SUB_ROW_H + 4), 16.0f, 14.0f};
}
Rectangle PnlVlanPortModeRect(int port) {
    return {(float)(CANVAS_W + 12), (float)(152 + port * 34), 70.0f, 22.0f};
}
Rectangle PnlVlanPortIdRect(int port) {
    return {(float)(CANVAS_W + 90), (float)(152 + port * 34), 48.0f, 22.0f};
}

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
