#include "ProtocolPanels.h"

#include "Font.h"
#include "Layout.h"

#include <algorithm>
#include <cstdio>
#include <string>

void DrawTeTab(const DeviceNode* n, const PanelState& ps)
{
    float px = (float)(CANVAS_W() + 12);
    float pw = (float)(PANEL_W - 24);
    Color DIM  = {100, 116, 139, 255};
    Color WHT  = WHITE;
    Color ON   = {34,  197, 94,  255};
    Color OFF  = {100, 116, 139, 255};

    if (!n) { DrawTextEx(GFont(), "No device selected", {px, 130.0f}, FS(11), Sp(FS(11)), DIM); return; }
    if (n->type != ROUTER) { DrawTextEx(GFont(), "TE: routers only", {px, 130.0f}, FS(11), Sp(FS(11)), DIM); return; }

    // Toggle row
    const char* lbl = n->rsvpEnabled ? "rsvp-te  ON" : "rsvp-te  OFF";
    Color       tcol = n->rsvpEnabled ? ON : OFF;
    DrawRectangleRoundedLines(PnlTeToggleRect(), 0.4f, 4, tcol);
    float tw = TW(lbl, 12);
    DrawTextEx(GFont(), lbl,
               {px + (pw - tw) * 0.5f, 126.0f}, FS(12), Sp(FS(12)), tcol);

    if (!n->rsvpEnabled) return;

    // Per-port bandwidth rows
    DrawTextEx(GFont(), "Interface Bandwidth", {px, 150.0f}, FS(10), Sp(FS(10)), DIM);
    for (int p = 0; p < PORTS_PER_NODE; ++p) {
        if (n->portIp[p].empty()) continue;
        float y = 152.0f + (float)p * 26.0f;
        std::string portName = GetPortName(n->type, p);
        DrawTextEx(GFont(), portName.c_str(), {px, y + 4.0f}, FS(11), Sp(FS(11)), WHT);

        Rectangle bwRect = PnlTePbwRect(p);
        bool active = (ps.tePbwActivePort == p);
        DrawRectangleRec(bwRect, Color{30, 41, 59, 255});
        DrawRectangleLinesEx(bwRect, 1.0f, active ? Color{59,130,246,255} : Color{51,65,85,255});
        const std::string& txt = active ? ps.tePbwBuf
                                        : std::to_string(n->portBandwidth[p]);
        DrawTextEx(GFont(), txt.c_str(), {bwRect.x + 4.0f, bwRect.y + 4.0f},
                   FS(11), Sp(FS(11)), WHT);
        DrawTextEx(GFont(), "Mbps", {bwRect.x + bwRect.width + 4.0f, bwRect.y + 4.0f},
                   FS(10), Sp(FS(10)), DIM);
    }

    // Tunnel list header
    DrawTextEx(GFont(), "TE Tunnels",
               {px, TeListBaseY() - 18.0f}, FS(10), Sp(FS(10)), DIM);

    float listY = TeListBaseY();
    for (int i = 0; i < (int)n->teTunnels.size(); ++i) {
        const auto& t  = n->teTunnels[i];
        bool expanded  = (ps.teExpandedIdx == i);
        float rowY     = listY;
        listY         += TeRowH();

        Color statusColor = t.isUp ? ON : Color{239,68,68,255};
        Color rowBg       = Color{21, 30, 47, 255};
        DrawRectangleRounded({px, rowY, pw, TeRowH() - 2.0f}, 0.3f, 4, rowBg);
        DrawRectangle((int)px, (int)rowY, 3, (int)(TeRowH() - 2.0f), Color{251,191,36,200});

        char rowLabel[64];
        std::snprintf(rowLabel, sizeof(rowLabel),
                      "%s Tunnel-%d  ->%s  %uMbps  %s",
                      expanded ? "v" : ">",
                      t.id,
                      t.destIp.empty() ? "?" : t.destIp.c_str(),
                      t.bandwidth,
                      t.useExplicit ? "Explicit" : "CSPF");
        DrawTextEx(GFont(), rowLabel, {px + 8.0f, rowY + 7.0f}, FS(10), Sp(FS(10)), WHT);
        const char* statusTxt = t.isUp ? "UP" : "DOWN";
        float sw = TW(statusTxt, 10);
        DrawTextEx(GFont(), statusTxt, {px + pw - sw - 4.0f, rowY + 7.0f},
                   FS(10), Sp(FS(10)), statusColor);

        if (!expanded) continue;
        listY += TeFormH();

        float fy = rowY + TeRowH();
        auto field = [&](const char* flbl, float y, const std::string& val, bool act) {
            DrawTextEx(GFont(), flbl, {px + 4.0f, y}, FS(10), Sp(FS(10)), DIM);
            Rectangle r = {px + 52.0f, y - 2.0f, pw - 56.0f, 20.0f};
            DrawRectangleRec(r, Color{30, 41, 59, 255});
            DrawRectangleLinesEx(r, 1.0f, act ? Color{59,130,246,255} : Color{51,65,85,255});
            DrawTextEx(GFont(), val.c_str(), {r.x + 4.0f, r.y + 3.0f}, FS(10), Sp(FS(10)), WHT);
        };
        bool destAct = (ps.teActiveField == 0);
        bool bwAct   = (ps.teActiveField == 1);
        bool hopAct  = (ps.teActiveField == 2);

        field("Dest:", fy, destAct ? ps.teDestBuf : t.destIp,                      destAct); fy += 24.0f;
        field("BW:",   fy, bwAct   ? ps.teBwBuf   : std::to_string(t.bandwidth),   bwAct);   fy += 24.0f;

        DrawTextEx(GFont(), "Mode:", {px + 4.0f, fy}, FS(10), Sp(FS(10)), DIM);
        const char* modeTxt = t.useExplicit ? "[Explicit]" : "[CSPF    ]";
        DrawTextEx(GFont(), modeTxt, {px + 52.0f, fy}, FS(10), Sp(FS(10)), Color{251,191,36,255});
        fy += 24.0f;

        if (t.useExplicit) {
            std::string hopsStr;
            for (const auto& h : t.explicitHopIps) hopsStr += h + " ";
            field("Hops:", fy, hopAct ? ps.teHopsBuf : hopsStr, hopAct);
            fy += 24.0f;
        }

        float btnW = (pw - 8.0f) * 0.55f;
        float delW = (pw - 8.0f) * 0.40f;
        bool canSim = t.isUp;
        Color simCol = canSim ? Color{59,130,246,255} : Color{51,65,85,255};
        DrawRectangleRounded({px, fy, btnW, 22.0f}, 0.4f, 4, simCol);
        DrawTextEx(GFont(), "Simulate Setup", {px + 4.0f, fy + 4.0f}, FS(10), Sp(FS(10)), WHT);
        DrawRectangleRounded({px + pw - delW, fy, delW, 22.0f}, 0.4f, 4, Color{127,29,29,255});
        DrawTextEx(GFont(), "Del", {px + pw - delW + 4.0f, fy + 4.0f}, FS(10), Sp(FS(10)), WHT);
    }

    // Add Tunnel button
    int visCount = (int)n->teTunnels.size() + (ps.teExpandedIdx >= 0 ? 1 : 0);
    Rectangle addBtn = PnlTeAddBtnRect(visCount);
    DrawRectangleRounded(addBtn, 0.4f, 4, Color{21,128,61,200});
    float atw = TW("+ Add Tunnel", 11);
    DrawTextEx(GFont(), "+ Add Tunnel",
               {addBtn.x + (addBtn.width - atw) * 0.5f, addBtn.y + 6.0f},
               FS(11), Sp(FS(11)), WHT);
}

void DrawSrTab(const DeviceNode* n, const PanelState& ps)
{
    float px   = (float)(CANVAS_W() + 12);
    float pw   = (float)(PANEL_W - 24);
    Color DIM      = {100, 116, 139, 255};
    Color WHT      = WHITE;
    Color ON       = {34,  197, 94,  255};
    Color OFF      = {100, 116, 139, 255};
    Color ELEC_BLUE = {59,  130, 246, 255};

    if (!n) { DrawTextEx(GFont(), "No device selected", {px, 130.0f}, FS(11), Sp(FS(11)), DIM); return; }
    if (n->type != ROUTER) { DrawTextEx(GFont(), "SR-MPLS: routers only", {px, 130.0f}, FS(11), Sp(FS(11)), DIM); return; }

    // ── Toggle row ────────────────────────────────────────────────────────
    const char* lbl  = n->srEnabled ? "sr-mpls  ON" : "sr-mpls  OFF";
    Color       tcol = n->srEnabled ? ON : OFF;
    DrawRectangleRoundedLines(PnlSrToggleRect(), 0.4f, 4, tcol);
    float tw = TW(lbl, 12);
    DrawTextEx(GFont(), lbl,
               {px + (pw - tw) * 0.5f, 126.0f}, FS(12), Sp(FS(12)), tcol);

    if (!n->srEnabled) return;

    // ── Node SID section ─────────────────────────────────────────────────
    DrawTextEx(GFont(), "Node SID:", {px, 152.0f}, FS(10), Sp(FS(10)), DIM);

    Rectangle sidRect = PnlSrNodeSidRect();
    // shift the field right of the label
    sidRect.x = px + TW("Node SID:", 10) + 6.0f;

    bool sidActive = ps.srNodeSidEditing;
    DrawRectangleRec(sidRect, Color{30, 41, 59, 255});
    DrawRectangleLinesEx(sidRect, 1.0f, sidActive ? ELEC_BLUE : Color{51, 65, 85, 255});

    std::string sidTxt = sidActive ? ps.srNodeSidBuf
                                   : (n->nodeSid > 0 ? std::to_string(n->nodeSid) : "");
    if (sidTxt.empty() && !sidActive) {
        DrawTextEx(GFont(), "0", {sidRect.x + 4.0f, sidRect.y + 3.0f}, FS(10), Sp(FS(10)), Color{51,65,85,255});
    } else {
        DrawTextEx(GFont(), sidTxt.c_str(), {sidRect.x + 4.0f, sidRect.y + 3.0f},
                   FS(10), Sp(FS(10)), WHT);
    }

    // Label preview below SID field
    float sidInfoY = sidRect.y + sidRect.height + 4.0f;
    if (n->nodeSid > 0) {
        std::string labelStr = "-> label: " + std::to_string(SRGB_BASE + n->nodeSid);
        DrawTextEx(GFont(), labelStr.c_str(), {px, sidInfoY}, FS(10), Sp(FS(10)), ON);
        sidInfoY += 14.0f;
    }
    DrawTextEx(GFont(), "SRGB: 17000-17999 (global)", {px, sidInfoY}, FS(9), Sp(FS(9)), DIM);

    // ── Adj SIDs section ─────────────────────────────────────────────────
    float adjY = sidInfoY + 18.0f;
    DrawTextEx(GFont(), "Adj SIDs (auto):", {px, adjY}, FS(10), Sp(FS(10)), DIM);
    adjY += 14.0f;

    if (n->adjSids.empty()) {
        DrawTextEx(GFont(), "(none)", {px + 8.0f, adjY}, FS(10), Sp(FS(10)), DIM);
        adjY += 14.0f;
    } else {
        for (const auto& kv : n->adjSids) {
            int      port   = kv.first;
            uint32_t adjSid = kv.second;
            char adjBuf[64];
            std::snprintf(adjBuf, sizeof(adjBuf),
                          "Gi0/%d  adj: %u", port, adjSid);
            DrawTextEx(GFont(), adjBuf, {px + 8.0f, adjY}, FS(10), Sp(FS(10)), WHT);
            adjY += 14.0f;
        }
    }
    DrawTextEx(GFont(), "Policy segment: adj:<router-ip>:<port>",
               {px, SrListBaseY() - 34.0f}, FS(8), Sp(FS(8)), DIM);

    // ── SR Policies section ──────────────────────────────────────────────
    DrawTextEx(GFont(), "SR Policies",
               {px, SrListBaseY() - 18.0f}, FS(10), Sp(FS(10)), DIM);

    for (int i = 0; i < (int)n->srPolicies.size(); ++i) {
        const SrPolicy& pol = n->srPolicies[i];
        bool            exp = (ps.srExpandedIdx == i);

        // Keep row geometry aligned with ProtocolPanelController hit testing.
        int   expansionAbove = (ps.srExpandedIdx >= 0 && ps.srExpandedIdx < i) ? 1 : 0;
        float rowY           = PnlSrPolicyRowRect(i).y + expansionAbove * SrFormH();

        // Row background + left accent (electric-blue)
        Color rowBg = {21, 30, 47, 255};
        DrawRectangleRounded({px, rowY, pw, SrRowH() - 2.0f}, 0.3f, 4, rowBg);
        DrawRectangle((int)px, (int)rowY, 3, (int)(SrRowH() - 2.0f), ELEC_BLUE);

        // Expand glyph + policy label + dest
        char rowLabel[128];
        std::snprintf(rowLabel, sizeof(rowLabel),
                      "%s Policy-%d  ->%s",
                      exp ? "v" : ">",
                      pol.id,
                      pol.destIp.empty() ? "?" : pol.destIp.c_str());
        DrawTextEx(GFont(), rowLabel, {px + 8.0f, rowY + 7.0f}, FS(10), Sp(FS(10)), WHT);

        // Status badge
        const char* statusTxt  = pol.isActive ? "ACTIVE" : pol.statusMsg.c_str();
        Color       statusColor = pol.isActive ? ON : Color{239, 68, 68, 255};
        if (!pol.isActive && pol.statusMsg.empty()) {
            statusTxt  = "DOWN";
        }
        float sw = TW(statusTxt, 10);
        DrawTextEx(GFont(), statusTxt, {px + pw - sw - 4.0f, rowY + 7.0f},
                   FS(10), Sp(FS(10)), statusColor);

        if (!exp) continue;

        // ── Expanded form ─────────────────────────────────────────────
        float fy = rowY + SrRowH();

        auto srField = [&](const char* flbl, float y, const std::string& val, bool act) {
            DrawTextEx(GFont(), flbl, {px + 4.0f, y}, FS(10), Sp(FS(10)), DIM);
            Rectangle r = {px + 52.0f, y - 2.0f, pw - 56.0f, 20.0f};
            DrawRectangleRec(r, Color{30, 41, 59, 255});
            DrawRectangleLinesEx(r, 1.0f, act ? ELEC_BLUE : Color{51, 65, 85, 255});
            DrawTextEx(GFont(), val.c_str(), {r.x + 4.0f, r.y + 3.0f},
                       FS(10), Sp(FS(10)), WHT);
        };

        bool destAct = (ps.srActiveField == 0);
        bool segsAct = (ps.srActiveField == 1);

        // Dest field
        srField("Dest:", fy,
                destAct ? ps.srDestBuf : pol.destIp,
                destAct);
        fy += 24.0f;

        // Segs field — joined segment IPs
        std::string segsDisplay;
        if (segsAct) {
            segsDisplay = ps.srSegsBuf;
        } else {
            for (size_t k = 0; k < pol.segmentIps.size(); ++k) {
                if (k) segsDisplay += ", ";
                segsDisplay += pol.segmentIps[k];
            }
        }
        srField("Segs:", fy, segsDisplay, segsAct);
        fy += 24.0f;

        // Live label preview — resolved label stack
        if (!pol.labelStack.empty()) {
            std::string preview;
            for (size_t k = 0; k < pol.labelStack.size(); ++k) {
                if (k) preview += "  ";
                uint32_t lbl2  = pol.labelStack[k];
                uint32_t sid2  = (lbl2 >= SRGB_BASE && lbl2 < SRGB_END)
                                     ? (lbl2 - SRGB_BASE) : lbl2;
                char seg[32];
                std::snprintf(seg, sizeof(seg), "SID:%u*label:%u", sid2, lbl2);
                preview += seg;
            }
            DrawTextEx(GFont(), ("-> " + preview).c_str(),
                       {px + 4.0f, fy}, FS(9), Sp(FS(9)), Color{147, 197, 253, 255});
            fy += 16.0f;
        } else if (!pol.segmentIps.empty()) {
            DrawTextEx(GFont(), "(segments unresolved)",
                       {px + 4.0f, fy}, FS(9), Sp(FS(9)), DIM);
            fy += 16.0f;
        }

        // Del button
        float delW = (pw - 8.0f) * 0.40f;
        DrawRectangleRounded({px + pw - delW, fy, delW, 22.0f}, 0.4f, 4,
                             Color{127, 29, 29, 255});
        DrawTextEx(GFont(), "x Del", {px + pw - delW + 4.0f, fy + 4.0f},
                   FS(10), Sp(FS(10)), WHT);
    }

    // ── Add Policy button ────────────────────────────────────────────────
    int visCount = (int)n->srPolicies.size() + (ps.srExpandedIdx >= 0 ? 1 : 0);
    Rectangle addBtn = PnlSrAddBtnRect(visCount);
    DrawRectangleRounded(addBtn, 0.4f, 4, Color{21, 128, 61, 200});
    float atw = TW("+ Add Policy", 11);
    DrawTextEx(GFont(), "+ Add Policy",
               {addBtn.x + (addBtn.width - atw) * 0.5f, addBtn.y + 6.0f},
               FS(11), Sp(FS(11)), WHT);
}
