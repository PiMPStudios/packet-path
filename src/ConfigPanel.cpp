#include "ConfigPanel.h"
#include "NetworkCanvas.h"

Rectangle PnlFieldRect(int yOffset) {
    return {(float)(CANVAS_W() + 12), (float)yOffset, (float)(PANEL_W - 24), 26.0f};
}

Rectangle PnlPortFieldRect(int port) {
    return {(float)(CANVAS_W() + 80), (float)(CFG_PORT_Y0 + port * CFG_PORT_STRIDE),
            126.0f, 24.0f};
}

Rectangle PnlPortAreaFieldRect(int port) {
    return {(float)(CANVAS_W() + 224), (float)(CFG_PORT_Y0 + port * CFG_PORT_STRIDE),
            44.0f, 24.0f};
}

float PnlTabW() { return (PANEL_W - 24.0f - 11.0f * 4.0f) / 12.0f; }
Rectangle PnlConfigTabRect() {
    return {(float)(CANVAS_W() + 12), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlRoutesTabRect() {
    return {(float)(CANVAS_W() + 12) + PnlTabW() + 4.0f, 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlArpTabRect() {
    return {(float)(CANVAS_W() + 12) + 2.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlOspfTabRect() {
    return {(float)(CANVAS_W() + 12) + 3.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlOspfEnableRect() {
    return {(float)(CANVAS_W() + 12), 120.0f, (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlMplsTabRect() {
    return {(float)(CANVAS_W() + 12) + 4.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlMplsToggleRect() {
    return {(float)(CANVAS_W() + 12), 120.0f, (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlBgpTabRect() {
    return {(float)(CANVAS_W() + 12) + 5.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlBgpToggleRect() {
    return {(float)(CANVAS_W() + 12), 120.0f, (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlBgpAsnRect() {
    return {(float)(CANVAS_W() + 56), 152.0f, (float)(PANEL_W - 68), 22.0f};
}
Rectangle PnlBgpRrRect() {
    return {(float)(CANVAS_W() + 12), 182.0f, (float)(PANEL_W - 24), 22.0f};
}
Rectangle PnlRouteDeleteRect(int rowIdx) {
    return {(float)(CANVAS_W() + PANEL_W - 22),
            (float)(RTE_ROW_Y0 + rowIdx * RTE_ROW_H + 4), 16.0f, 14.0f};
}
Rectangle PnlRouteDestRect()   { return {(float)(CANVAS_W()+12),(float)RTE_DEST_Y,(float)(PANEL_W-24),26.0f}; }
Rectangle PnlRouteNextRect()   { return {(float)(CANVAS_W()+12),(float)RTE_NEXT_Y,(float)(PANEL_W-24),26.0f}; }
Rectangle PnlRouteAddBtnRect() { return {(float)(CANVAS_W()+12),(float)RTE_BTN_Y, (float)(PANEL_W-24),28.0f}; }
Rectangle PnlVlanTabRect() {
    return {(float)(CANVAS_W() + 12) + 6.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlSubTabRect() {
    return {(float)(CANVAS_W() + 12) + 7.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlVxlanTabRect() {
    return {(float)(CANVAS_W() + 12) + 8.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlVxlanToggleRect() {
    return {(float)(CANVAS_W() + 12), 120.0f, (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlVxlanVniRect() {
    return {(float)(CANVAS_W() + 12), 158.0f, (float)(PANEL_W - 24), 22.0f};
}
Rectangle PnlVxlanVtepRect() {
    return {(float)(CANVAS_W() + 12), 194.0f, (float)(PANEL_W - 24), 22.0f};
}
Rectangle PnlVxlanEvpnRect() {
    return {(float)(CANVAS_W() + 12), 230.0f, (float)(PANEL_W - 24), 22.0f};
}
// ── ACL tab rects ──────────────────────────────────────────────────────────
Rectangle PnlAclTabRect() {
    return {(float)(CANVAS_W()+12) + 9.0f*(PnlTabW()+4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlAclToggleRect() {
    return {(float)(CANVAS_W()+12), 120.0f, (float)(PANEL_W-24), 26.0f};
}
Rectangle PnlAclInPortBtnRect(int p) {
    return {(float)(CANVAS_W()+12) + p*52.0f, 154.0f, 48.0f, 22.0f};
}
Rectangle PnlAclOutPortBtnRect(int p) {
    return {(float)(CANVAS_W()+12) + p*52.0f, 180.0f, 48.0f, 22.0f};
}
Rectangle PnlAclRuleDeleteRect(int i) {
    return {(float)(CANVAS_W()+12+PANEL_W-24-20), 226.0f + i*26.0f, 20.0f, 22.0f};
}
Rectangle PnlAclFormActionRect() {
    return {(float)(CANVAS_W()+12), 370.0f, (float)(PANEL_W-24), 26.0f};
}
Rectangle PnlAclFormSrcRect() {
    return {(float)(CANVAS_W()+12), 400.0f, (float)(PANEL_W-24), 22.0f};
}
Rectangle PnlAclFormDstRect() {
    return {(float)(CANVAS_W()+12), 426.0f, (float)(PANEL_W-24), 22.0f};
}
Rectangle PnlAclFormPortRect() {
    return {(float)(CANVAS_W()+12), 452.0f, 80.0f, 22.0f};
}
Rectangle PnlAclFormAddBtnRect() {
    return {(float)(CANVAS_W()+12), 480.0f, (float)(PANEL_W-24), 28.0f};
}
// ── NAT tab rects ──────────────────────────────────────────────────────────
Rectangle PnlNatTabRect() {
    return {(float)(CANVAS_W()+12) + 10.0f*(PnlTabW()+4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlNatToggleRect() {
    return {(float)(CANVAS_W()+12), 120.0f, (float)(PANEL_W-24), 26.0f};
}
Rectangle PnlNatInsidePortBtnRect(int p) {   // p=0..3
    return {(float)(CANVAS_W()+12) + p*52.0f, 154.0f, 48.0f, 22.0f};
}
Rectangle PnlNatOutsidePortBtnRect(int p) {
    return {(float)(CANVAS_W()+12) + p*52.0f, 180.0f, 48.0f, 22.0f};
}
Rectangle PnlNatInsidePrefixRect() {
    return {(float)(CANVAS_W()+12), 216.0f, (float)(PANEL_W-24), 22.0f};
}
Rectangle PnlSubPortBtnRect(int port) {
    float btnW = 28.0f, gap = 4.0f;
    float x0 = (float)(CANVAS_W() + 64);
    return {x0 + port * (btnW + gap), (float)(SUB_FORM_Y0), btnW, 22.0f};
}
Rectangle PnlSubVlanFieldRect() {
    return {(float)(CANVAS_W() + 64), (float)(SUB_FORM_Y0 + 30), 60.0f, 22.0f};
}
Rectangle PnlSubIpFieldRect() {
    return {(float)(CANVAS_W() + 64), (float)(SUB_FORM_Y0 + 60), (float)(PANEL_W - 76), 22.0f};
}
Rectangle PnlSubAddBtnRect() {
    return {(float)(CANVAS_W() + 12), (float)(SUB_FORM_Y0 + 92), (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlSubRowDeleteRect(int rowIdx) {
    return {(float)(CANVAS_W() + PANEL_W - 22),
            (float)(SUB_ROW_Y0 + rowIdx * SUB_ROW_H + 4), 16.0f, 14.0f};
}
Rectangle PnlVlanPortModeRect(int port) {
    return {(float)(CANVAS_W() + 12), (float)(152 + port * 34), 70.0f, 22.0f};
}
Rectangle PnlVlanPortIdRect(int port) {
    return {(float)(CANVAS_W() + 90), (float)(152 + port * 34), 48.0f, 22.0f};
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

// ── Device-type-aware tab layout ─────────────────────────────────────────────

static const TabInfo kPcTabs[] = {
    {TAB_CONFIG, "Cfg",  Color{59,130,246,255}, WHITE},
    {TAB_ROUTES, "Rte",  Color{59,130,246,255}, WHITE},
    {TAB_ARP,    "ARP",  Color{59,130,246,255}, WHITE},
};
static const TabInfo kSwTabs[] = {
    {TAB_CONFIG, "Cfg",  Color{59,130,246,255}, WHITE},
    {TAB_VLAN,   "VLAN", Color{245,158,11,255}, Color{245,158,11,255}},
    {TAB_ACL,    "ACL",  Color{59,130,246,255}, Color{239,68,68,255}},
};
// Router: 12 tabs (VLAN omitted — routers use Sub-ifaces for inter-VLAN)
static const TabInfo kRtTabs[] = {
    {TAB_CONFIG, "Cfg",  Color{59,130,246,255}, WHITE},
    {TAB_ROUTES, "Rte",  Color{59,130,246,255}, WHITE},
    {TAB_ARP,    "ARP",  Color{59,130,246,255}, WHITE},
    {TAB_OSPF,   "OSP",  Color{59,130,246,255}, WHITE},
    {TAB_MPLS,   "MLS",  Color{59,130,246,255}, WHITE},
    {TAB_BGP,    "BGP",  Color{34,197,94,255},  Color{34,197,94,255}},
    {TAB_SUB,    "Sub",  Color{234,88,12,255},  Color{234,88,12,255}},
    {TAB_VXLAN,  "VXL",  Color{20,184,166,255}, Color{20,184,166,255}},
    {TAB_ACL,    "ACL",  Color{59,130,246,255}, Color{239,68,68,255}},
    {TAB_NAT,    "NAT",  Color{59,130,246,255}, Color{234,179,8,255}},
    {TAB_TE,     "TE",   Color{251,191,36,255}, Color{251,191,36,255}},
    {TAB_SR,     "SR",   Color{59,130,246,255}, Color{59,130,246,255}},
};

int PnlTabCount(DeviceType t) {
    if (t == PC)     return 3;
    if (t == SWITCH) return 3;
    return 12;
}
float PnlTabWFor(DeviceType t) {
    int n = PnlTabCount(t);
    return (PANEL_W - 24.0f - (float)(n - 1) * 4.0f) / (float)n;
}
const TabInfo* PnlTabList(DeviceType t) {
    if (t == PC)     return kPcTabs;
    if (t == SWITCH) return kSwTabs;
    return kRtTabs;
}
Rectangle PnlTabRect(DeviceType t, int visIdx) {
    float w = PnlTabWFor(t);
    return {(float)(CANVAS_W() + 12) + (float)visIdx * (w + 4.0f), 88.0f, w, 26.0f};
}

// ── TE tab helpers and rects ──────────────────────────────────────────────
float TeListBaseY() { return 260.0f; }
float TeRowH()      { return 28.0f;  }
float TeFormH()     { return 128.0f; }

Rectangle PnlTeTabRect() {
    return {(float)(CANVAS_W() + 12) + 10.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlTeToggleRect() {
    return {(float)(CANVAS_W() + 12), 120.0f, (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlTePbwRect(int port) {
    float y = 152.0f + (float)port * 26.0f;
    return {(float)(CANVAS_W() + PANEL_W - 80), y, 60.0f, 22.0f};
}
Rectangle PnlTeTunnelRowRect(int idx) {
    return {(float)(CANVAS_W() + 12), TeListBaseY() + (float)idx * TeRowH(),
            (float)(PANEL_W - 24), TeRowH() - 2.0f};
}
Rectangle PnlTeAddBtnRect(int tunnelCount) {
    float y = TeListBaseY() + (float)tunnelCount * TeRowH() + 4.0f;
    return {(float)(CANVAS_W() + 12), y, (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlTeSimBtnRect() {
    return {0.0f, 0.0f, (float)(PANEL_W - 24) * 0.6f, 22.0f};
}
Rectangle PnlTeDelBtnRect() {
    return {0.0f, 0.0f, (float)(PANEL_W - 24) * 0.35f, 22.0f};
}

// ── SR tab helpers and rects ──────────────────────────────────────────────
float SrListBaseY() { return 316.0f; }
float SrRowH()      { return 28.0f;  }
float SrFormH()     { return 132.0f; }

Rectangle PnlSrTabRect() {
    return {(float)(CANVAS_W() + 12) + 11.0f * (PnlTabW() + 4.0f), 88.0f, PnlTabW(), 26.0f};
}
Rectangle PnlSrToggleRect() {
    return {(float)(CANVAS_W() + 12), 120.0f, (float)(PANEL_W - 24), 26.0f};
}
Rectangle PnlSrNodeSidRect() {
    return {(float)(CANVAS_W() + 12), 152.0f, 40.0f, 20.0f};
}
Rectangle PnlSrPolicyRowRect(int idx) {
    return {(float)(CANVAS_W() + 12), SrListBaseY() + (float)idx * SrRowH(),
            (float)(PANEL_W - 24), SrRowH() - 2.0f};
}
Rectangle PnlSrAddBtnRect(int count) {
    float y = SrListBaseY() + (float)count * SrRowH() + 4.0f;
    return {(float)(CANVAS_W() + 12), y, (float)(PANEL_W - 24), 22.0f};
}
