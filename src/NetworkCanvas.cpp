#include "NetworkCanvas.h"
#include "AclEngine.h"

void DrawDeviceNode(const DeviceNode& n) {
    Rectangle r = GetNodeRect(n);
    Color     c = GetDeviceColor(n.type);
    DrawRectangleRounded({r.x + 3, r.y + 3, r.width, r.height}, 0.3f, 8,
                         Color{0, 0, 0, 80});
    DrawRectangleRounded(r, 0.3f, 8, c);
    if (n.selected)
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, 2.5f, WHITE);
    if (n.crashed) {
        DrawRectangleRounded(r, 0.3f, 8, Color{239, 68, 68, 50});
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, 2.0f, Color{239, 68, 68, 255});
        float bx = n.position.x;
        float by = n.position.y - NODE_H / 2.f - 18.f;
        DrawCircle((int)bx, (int)by, 12.f, Color{239, 68, 68, 255});
        const char* xmark = "\xe2\x9c\x97";
        int xw = MeasureText(xmark, 11);
        DrawText(xmark, (int)bx - xw / 2, (int)(by - 6.f), 11, WHITE);
    }
    int tw = MeasureText(n.label.c_str(), NODE_FONT_SZ);
    DrawText(n.label.c_str(),
             (int)(n.position.x - tw / 2.0f),
             (int)(n.position.y - NODE_FONT_SZ / 2.0f),
             NODE_FONT_SZ, WHITE);
    for (int i = 0; i < PORTS_PER_NODE; ++i) {
        Vector2 pp = GetPortPosition(n, i);
        DrawCircleV(pp, PORT_RADIUS,        Color{51,  65,  85, 255});
        DrawCircleV(pp, PORT_RADIUS - 2.0f, Color{100, 116, 139, 255});
        if (n.type == SWITCH) {
            const VlanPortConfig& vc = n.vlanPorts[i];
            char lbl[8];
            if (vc.mode == VLAN_TRUNK)
                std::snprintf(lbl, sizeof(lbl), "T");
            else
                std::snprintf(lbl, sizeof(lbl), "%d", vc.accessVlan);
            Color lblCol = (vc.mode == VLAN_TRUNK) ? Color{245,158,11,255}
                                                   : Color{96,165,250,255};
            int lw = MeasureText(lbl, 8);
            DrawText(lbl, (int)(pp.x - lw * 0.5f), (int)(pp.y + PORT_RADIUS + 2), 8, lblCol);
        }
        if (n.type == ROUTER) {
            int subY = (int)(pp.y + PORT_RADIUS + 2);
            for (const auto& si : n.subIfaces) {
                if (si.parentPort != i) continue;
                char lbl[8];
                std::snprintf(lbl, sizeof(lbl), ".%d", si.vlanId);
                int lw = MeasureText(lbl, 8);
                DrawText(lbl, (int)(pp.x - lw * 0.5f), subY,
                         8, Color{249, 115, 22, 255});
                subY += 10;
            }
        }
    }
}

// ── Dot-grid background (drawn inside BeginMode2D) ────────────────────────
void DrawDotGrid(const Camera2D& cam) {
    float spacing = 40.0f;
    Color dot     = {30, 41, 59, 255};

    Vector2 topLeft  = GetScreenToWorld2D({0.0f, 0.0f}, cam);
    Vector2 botRight = GetScreenToWorld2D({(float)CANVAS_W(), (float)CANVAS_H()}, cam);

    float startX = floorf(topLeft.x / spacing) * spacing;
    float startY = floorf(topLeft.y / spacing) * spacing;

    for (float x = startX; x <= botRight.x; x += spacing)
        for (float y = startY; y <= botRight.y; y += spacing)
            DrawCircleV({x, y}, 1.5f / cam.zoom, dot);
}

void DrawAllCables(const std::vector<Cable>& cables,
                   const std::vector<DeviceNode>& nodes)
{
    for (const auto& c : cables) {
        const DeviceNode* from = FindNode(nodes, c.fromId);
        const DeviceNode* to   = FindNode(nodes, c.toId);
        if (!from || !to) continue;

        Vector2 p0 = GetPortPosition(*from, c.fromPort);
        Vector2 p3 = GetPortPosition(*to,   c.toPort);

        if (c.broken) {
            DrawSplineSegmentBezierCubic(p0, BezierCtrl(p0, c.fromPort),
                                         BezierCtrl(p3, c.toPort), p3,
                                         3.0f, Color{239, 68, 68, 255});
            Vector2 mid = {(p0.x + p3.x) / 2.0f, (p0.y + p3.y) / 2.0f};
            DrawCircle((int)mid.x, (int)mid.y, 7.f, Color{239, 68, 68, 255});
            const char* xmark = "\xe2\x9c\x97";
            int xw = MeasureText(xmark, 9);
            DrawText(xmark, (int)mid.x - xw / 2, (int)(mid.y - 5.f), 9, WHITE);
            continue;   // skip normal color/OSPF/trunk logic for this cable
        }

        Color cableColor = Color{148, 163, 184, 255};  // default slate-gray

        if (from->ospfEnabled && to->ospfEnabled) {
            OspfState stateAB = OSPF_DOWN, stateBA = OSPF_DOWN;
            for (const auto& nbr : from->ospfNeighbors)
                if (nbr.neighborNodeId == to->id) { stateAB = nbr.state; break; }
            for (const auto& nbr : to->ospfNeighbors)
                if (nbr.neighborNodeId == from->id) { stateBA = nbr.state; break; }

            OspfState best = std::max(stateAB, stateBA);
            if (best == OSPF_FULL)
                cableColor = Color{34, 197, 94, 220};   // green
            else if (best >= OSPF_INIT)
                cableColor = Color{234, 179, 8, 220};   // yellow
        }

        // Trunk cable: amber if either endpoint switch port is trunk mode
        {
            bool isTrunk =
                (from->type == SWITCH && from->vlanPorts[c.fromPort].mode == VLAN_TRUNK) ||
                (to->type   == SWITCH && to->vlanPorts[c.toPort].mode   == VLAN_TRUNK);
            if (isTrunk)
                cableColor = Color{245, 158, 11, 220};
        }

        DrawSplineSegmentBezierCubic(p0, BezierCtrl(p0, c.fromPort),
                                     BezierCtrl(p3, c.toPort), p3,
                                     2.0f, cableColor);
    }
}

// Returns true and sets outNode/outPort if worldMouse is near any port.
// excludeId: skip this node's ports (prevents self-connect during connect mode).
bool HitTestPort(const std::vector<DeviceNode>& nodes, Vector2 worldMouse,
                 int excludeId, int& outNode, int& outPort)
{
    for (int i = (int)nodes.size() - 1; i >= 0; --i) {
        if (nodes[i].id == excludeId) continue;
        for (int p = 0; p < PORTS_PER_NODE; ++p) {
            Vector2 pp = GetPortPosition(nodes[i], p);
            if (CheckCollisionPointCircle(worldMouse, pp, PORT_RADIUS * 1.5f)) {
                outNode = nodes[i].id;
                outPort = p;
                return true;
            }
        }
    }
    return false;
}

// Returns cable index if worldMouse is within threshold of any cable bezier, else -1
int HitTestCable(const std::vector<Cable>& cables,
                 const std::vector<DeviceNode>& nodes,
                 Vector2 worldMouse, float threshold)
{
    for (int ci = 0; ci < (int)cables.size(); ++ci) {
        const Cable& c   = cables[ci];
        const DeviceNode* from = FindNode(nodes, c.fromId);
        const DeviceNode* to   = FindNode(nodes, c.toId);
        if (!from || !to) continue;

        Vector2 p0 = GetPortPosition(*from, c.fromPort);
        Vector2 p3 = GetPortPosition(*to,   c.toPort);

        Vector2 c1 = BezierCtrl(p0, c.fromPort);
        Vector2 c2 = BezierCtrl(p3, c.toPort);

        for (int s = 0; s <= 20; ++s) {
            float t  = (float)s / 20.0f;
            float it = 1.0f - t;
            Vector2 pt = {
                it*it*it*p0.x + 3*it*it*t*c1.x + 3*it*t*t*c2.x + t*t*t*p3.x,
                it*it*it*p0.y + 3*it*it*t*c1.y + 3*it*t*t*c2.y + t*t*t*p3.y
            };
            if (CheckCollisionPointCircle(worldMouse, pt, threshold))
                return ci;
        }
    }
    return -1;
}

void DrawTextField(Rectangle r, const char* topLabel, const char* placeholder,
                   const std::string& value, bool active, bool valid)
{
    if (topLabel && *topLabel)
        DrawText(topLabel, (int)r.x, (int)r.y - 16, 11, Color{100, 116, 139, 255});
    DrawRectangleRec(r, Color{15, 23, 42, 255});

    Color border;
    if (active)             border = WHITE;
    else if (value.empty()) border = Color{51, 65, 85, 255};
    else if (valid)         border = Color{34, 197, 94, 255};
    else                    border = Color{239, 68, 68, 255};
    DrawRectangleLinesEx(r, 1.5f, border);

    const int tx   = (int)r.x + 6;
    const int ty   = (int)r.y + (int)(r.height / 2) - 6;
    const int maxW = (int)r.width - 12;

    if (value.empty()) {
        if (placeholder && *placeholder && !active)
            DrawText(placeholder, tx, ty, 12, Color{51, 65, 85, 255});
        if (active && std::fmod(GetTime(), 1.0) < 0.5)
            DrawRectangle(tx, (int)r.y + 4, 2, (int)r.height - 8, WHITE);
    } else {
        int start = 0;
        while (start < (int)value.size() &&
               MeasureText(value.c_str() + start, 12) > maxW)
            ++start;
        DrawText(value.c_str() + start, tx, ty, 12, WHITE);
        if (active && std::fmod(GetTime(), 1.0) < 0.5) {
            int curX = tx + MeasureText(value.c_str() + start, 12);
            DrawRectangle(curX, (int)r.y + 4, 2, (int)r.height - 8, WHITE);
        }
    }
}

void DrawPacketAnim(const PacketAnim& anim,
                    const std::vector<DeviceNode>& nodes,
                    const std::vector<Cable>& cables)
{
    const auto& path = anim.result.path;
    if (path.empty()) return;

    // Success pulse — green ring expanding on the destination node
    if (anim.successPulse > 0.f) {
        const DeviceNode* destNode = FindNode(nodes, path.back());
        if (destNode) {
            float frac = anim.successPulse / 0.5f;   // 1..0 as pulse fades
            float r    = 30.f + 20.f * (1.f - frac);
            DrawCircleV(destNode->position, r,
                        Color{34, 197, 94, (unsigned char)(frac * 80.f)});
            DrawCircleLinesV(destNode->position, r, Color{34, 197, 94, (unsigned char)(frac * 180.f)});
        }
        return;
    }

    // Failure pulse — red ring expanding on the last node
    if (anim.failPulse > 0.f) {
        const DeviceNode* failNode = FindNode(nodes, path.back());
        if (failNode) {
            float frac = anim.failPulse / 0.5f;   // 1..0 as pulse fades
            float r    = 30.f + 20.f * (1.f - frac);
            DrawCircleV(failNode->position, r,
                        Color{239, 68, 68, (unsigned char)(frac * 80.f)});
            DrawCircleLinesV(failNode->position, r, Color{239, 68, 68, (unsigned char)(frac * 180.f)});
        }
        return;
    }

    if (anim.done) return;
    if (anim.hop >= (int)path.size() - 1) return;

    int fromId = path[anim.hop];
    int toId   = path[anim.hop + 1];

    const DeviceNode* fromNode = FindNode(nodes, fromId);
    const DeviceNode* toNode   = FindNode(nodes, toId);
    const Cable*      cable    = FindCable(cables, fromId, toId);
    if (!fromNode || !toNode || !cable) return;

    // Resolve port indices (cable can be stored in either direction)
    int fromPort = (cable->fromId == fromId) ? cable->fromPort : cable->toPort;
    int toPort   = (cable->fromId == toId)   ? cable->fromPort : cable->toPort;

    Vector2 p0 = GetPortPosition(*fromNode, fromPort);
    Vector2 p3 = GetPortPosition(*toNode,   toPort);
    Vector2 c1 = BezierCtrl(p0, fromPort);
    Vector2 c2 = BezierCtrl(p3, toPort);

    Vector2 pos = EvaluateCubicBezier(p0, c1, c2, p3, anim.t);

    // MPLS label badge — orange rounded rect above the packet dot
    if (anim.currentLabel != 0) {
        char lbuf[12];
        std::snprintf(lbuf, sizeof(lbuf), "%u", anim.currentLabel);
        int   lw = MeasureText(lbuf, 10) + 10;
        float bx = pos.x - lw * 0.5f;
        float by = pos.y - 30.f;
        DrawRectangleRounded({bx, by, (float)lw, 16.f}, 0.5f, 4, Color{249, 115, 22, 220});
        DrawText(lbuf, (int)(bx + 5.f), (int)(by + 3.f), 10, WHITE);
    }

    // VLAN badge — blue pill above packet (stacks above MPLS badge if both present)
    if (anim.currentVlan != 0) {
        char vbuf[10];
        std::snprintf(vbuf, sizeof(vbuf), "V%d", anim.currentVlan);
        int   vw = MeasureText(vbuf, 10) + 10;
        float bx = pos.x - vw * 0.5f;
        float by = pos.y - 30.f;
        if (anim.currentLabel != 0) by -= 20.f;
        DrawRectangleRounded({bx, by, (float)vw, 16.f}, 0.5f, 4, Color{59, 130, 246, 220});
        DrawText(vbuf, (int)(bx + 5.f), (int)(by + 3.f), 10, WHITE);
    }

    // VNI badge — teal pill, stacks above existing badges
    if (anim.currentVni != 0) {
        char vnibuf[16];
        std::snprintf(vnibuf, sizeof(vnibuf), "VNI:%u", anim.currentVni);
        int   nw = MeasureText(vnibuf, 10) + 10;
        float bx = pos.x - nw * 0.5f;
        float by = pos.y - 30.f;
        if (anim.currentLabel != 0) by -= 20.f;
        if (anim.currentVlan  != 0) by -= 20.f;
        DrawRectangleRounded({bx, by, (float)nw, 16.f}, 0.5f, 4, Color{20, 184, 166, 220});
        DrawText(vnibuf, (int)(bx + 5.f), (int)(by + 3.f), 10, WHITE);
    }

    // Green glow (outer) + core dot — always green during travel
    DrawCircleV(pos, 14.f, Color{34, 197, 94, 55});
    DrawCircleV(pos, 7.f,  Color{34, 197, 94, 255});
}

// ── Log console (drawn outside BeginMode2D, full-width bottom strip) ─────
void DrawLogConsole(const std::vector<LogEntry>& entries) {
    DrawRectangle(0, CANVAS_H(), SCREEN_W(), LOG_H, Color{10, 15, 28, 255});
    DrawLineEx({0.f, (float)CANVAS_H()}, {(float)SCREEN_W(), (float)CANVAS_H()},
               1.f, Color{51, 65, 85, 255});
    DrawText("LOG", 12, CANVAS_H() + 8, 9, Color{71, 85, 105, 255});

    if (entries.empty()) {
        DrawText("No simulations run yet", 36, CANVAS_H() + 36, 10,
                 Color{51, 65, 85, 255});
        return;
    }

    int maxLines = 3;
    int startIdx = std::max(0, (int)entries.size() - maxLines);
    int shown    = std::min(maxLines, (int)entries.size());
    for (int i = 0; i < shown; ++i) {
        const auto& e = entries[startIdx + i];
        int lineY = CANVAS_H() + 8 + (shown - 1 - i) * 24;  // newest at top

        int   secs = (int)e.timestamp;
        int   mins = (secs / 60) % 60; secs %= 60;
        char  tsbuf[16];
        std::snprintf(tsbuf, sizeof(tsbuf), "[%02d:%02d]", mins, secs);
        DrawText(tsbuf, 36, lineY, 10, Color{71, 85, 105, 255});

        const char* icon;
        Color       icColor;
        switch (e.type) {
            case LOG_ARP_REQ:
                icon    = "?";
                icColor = Color{100, 160, 240, 255};
                break;
            case LOG_ARP_REPLY:
                icon    = "!";
                icColor = Color{80, 200, 180, 255};
                break;
            case LOG_ARP_HIT:
                icon    = "~";
                icColor = Color{140, 140, 140, 255};
                break;
            case LOG_OSPF:
                icon    = "O";
                icColor = Color{59, 130, 246, 255};
                break;
            case LOG_LINK_DOWN:
                icon    = "!";
                icColor = Color{239, 68, 68, 255};
                break;
            case LOG_DEVICE_CRASH:
                icon    = "!";
                icColor = Color{239, 68, 68, 255};
                break;
            case LOG_RESTORED:
                icon    = "+";
                icColor = Color{34, 197, 94, 255};
                break;
            default:
                icon    = e.success ? "\xe2\x9c\x93" : "\xe2\x9c\x97";
                icColor = e.success ? Color{34, 197, 94, 255}
                                    : Color{239, 68, 68, 255};
                break;
        }
        DrawText(icon, 90, lineY, 10, icColor);

        std::string msg = e.pathStr;
        if (!e.reason.empty()) msg += "  \xe2\x80\x94  " + e.reason;
        DrawText(msg.c_str(), 108, lineY, 10, icColor);
    }
}

void DrawBrokenPath(const std::vector<DeviceNode>& nodes,
                    const std::vector<Cable>& cables,
                    const ForwardResult& result)
{
    const auto& path = result.path;
    if (path.size() < 2) return;

    // Red glow on every cable segment the packet walked
    for (int i = 0; i < (int)path.size() - 1; ++i) {
        const DeviceNode* from  = FindNode(nodes, path[i]);
        const DeviceNode* to    = FindNode(nodes, path[i + 1]);
        const Cable*      cable = FindCable(cables, path[i], path[i + 1]);
        if (!from || !to || !cable) continue;

        int fromPort = (cable->fromId == path[i])     ? cable->fromPort : cable->toPort;
        int toPort   = (cable->fromId == path[i + 1]) ? cable->fromPort : cable->toPort;

        Vector2 p0 = GetPortPosition(*from, fromPort);
        Vector2 p3 = GetPortPosition(*to,   toPort);
        DrawSplineSegmentBezierCubic(p0, BezierCtrl(p0, fromPort),
                                     BezierCtrl(p3, toPort), p3,
                                     5.0f, Color{239, 68, 68, 140});
    }

    // Red badge above the last node in path (break point)
    const DeviceNode* breakNode = FindNode(nodes, path.back());
    if (!breakNode) return;

    float bx = breakNode->position.x;
    float by  = breakNode->position.y - NODE_H / 2.f - 18.f;
    DrawCircle((int)bx, (int)by, 12.f, Color{239, 68, 68, 255});
    const char* xmark = "\xe2\x9c\x97";
    int xw = MeasureText(xmark, 11);
    DrawText(xmark, (int)bx - xw / 2, (int)(by - 6.f), 11, WHITE);
}

void DrawTroubleshootOverlay(const std::vector<DeviceNode>& nodes,
                              const std::vector<Cable>& cables)
{
    for (const auto& c : cables) {
        if (!c.broken) continue;
        const DeviceNode* from = FindNode(nodes, c.fromId);
        const DeviceNode* to   = FindNode(nodes, c.toId);
        if (!from || !to) continue;
        Vector2 p0  = GetPortPosition(*from, c.fromPort);
        Vector2 p3  = GetPortPosition(*to,   c.toPort);
        Vector2 mid = {(p0.x + p3.x) / 2.0f, (p0.y + p3.y) / 2.0f};
        const char* txt = "LINK DOWN";
        int tw = MeasureText(txt, 9);
        DrawRectangle((int)mid.x - tw / 2 - 3, (int)(mid.y + 10), tw + 6, 14,
                      Color{239, 68, 68, 200});
        DrawText(txt, (int)mid.x - tw / 2, (int)(mid.y + 12), 9, WHITE);
    }

    for (const auto& n : nodes) {
        if (!n.crashed) continue;
        const char* txt = "CRASHED";
        int tw = MeasureText(txt, 9);
        int bx = (int)n.position.x;
        int by = (int)(n.position.y + NODE_H / 2.f + 4.f);
        DrawRectangle(bx - tw / 2 - 3, by, tw + 6, 14, Color{239, 68, 68, 200});
        DrawText(txt, bx - tw / 2, by + 2, 9, WHITE);
    }
}

void DrawConfigTab(const DeviceNode* n, const PanelState& ps) {
    DrawText("GENERAL", CANVAS_W() + 12, 124, 10, Color{100, 116, 139, 255});
    DrawTextField(PnlFieldRect(CFG_HOSTNAME_Y), "Hostname", nullptr,
                  n->label, ps.activeField == 0, !n->label.empty());
    DrawTextField(PnlFieldRect(CFG_MGMTIP_Y), "Mgmt IP", "x.x.x.x/xx",
                  n->mgmtIp, ps.activeField == 1, ValidateIP(n->mgmtIp));
    DrawLineEx({(float)CANVAS_W(),           (float)CFG_IFACE_SEP_Y},
               {(float)(CANVAS_W()+PANEL_W), (float)CFG_IFACE_SEP_Y},
               1.0f, PANEL_BORDER);
    DrawText("INTERFACES", CANVAS_W() + 12, CFG_IFACE_SEP_Y + 10, 10, Color{100, 116, 139, 255});
    for (int i = 0; i < PORTS_PER_NODE; ++i) {
        std::string pname = GetPortName(n->type, i);
        DrawTextField(PnlPortFieldRect(i), "", "x.x.x.x/xx",
                      n->portIp[i], ps.activeField == 2 + i, ValidateIP(n->portIp[i]));
        DrawText(pname.c_str(), CANVAS_W() + 16, CFG_PORT_Y0 + i * CFG_PORT_STRIDE + 7,
                 11, Color{148, 163, 184, 255});
        DrawText("A:", CANVAS_W() + 210, CFG_PORT_Y0 + i * CFG_PORT_STRIDE + 7,
                 10, Color{148, 163, 184, 255});
        std::string areaStr = (ps.activePortAreaField == i)
            ? ps.portAreaBuf
            : std::to_string(n->ospfPortArea[i]);
        DrawTextField(PnlPortAreaFieldRect(i), "", "0", areaStr,
                      ps.activePortAreaField == i, true);
    }
}

void DrawRoutesTab(const DeviceNode* n, const PanelState& ps) {
    // Column headers
    DrawText("T", CANVAS_W() + 12, 124, 10, Color{100, 116, 139, 255});
    DrawText("DESTINATION",  CANVAS_W() + 30, 124, 10, Color{100, 116, 139, 255});
    DrawText("NEXT-HOP",     CANVAS_W() + 130, 124, 10, Color{100, 116, 139, 255});
    DrawText("VIA",          CANVAS_W() + 210, 124, 10, Color{100, 116, 139, 255});
    DrawLineEx({(float)CANVAS_W(), (float)RTE_HEADER_SEP_Y}, {(float)(CANVAS_W()+PANEL_W), (float)RTE_HEADER_SEP_Y},
               1.0f, PANEL_BORDER);

    auto table = GetRoutingTable(*n);

    if (table.empty()) {
        DrawText("No routes configured", CANVAS_W() + 20, RTE_ROW_Y0, 11,
                 Color{51, 65, 85, 255});
    } else {
        int displayed = std::min((int)table.size(), 8);
        for (int i = 0; i < displayed; ++i) {
            const RouteEntry& r = table[i];
            int ry = RTE_ROW_Y0 + i * RTE_ROW_H;
            Color rowColor;
            if      (r.src == ROUTE_CONNECTED) rowColor = Color{34,  197,  94, 255};
            else if (r.src == ROUTE_OSPF)      rowColor = Color{234, 179,   8, 255};
            else if (r.src == ROUTE_OSPF_IA)   rowColor = Color{249, 115,  22, 255};
            else if (r.src == ROUTE_BGP)       rowColor = Color{ 20, 184, 166, 255};  // teal
            else                               rowColor = Color{ 59, 130, 246, 255};

            if (r.src == ROUTE_OSPF_IA) {
                DrawText("O",  CANVAS_W() + 12, ry + 3, 11, rowColor);
                DrawText("IA", CANVAS_W() + 21, ry + 5,  9, rowColor);
            } else {
                const char* typeLetter = (r.src == ROUTE_CONNECTED) ? "C"
                                       : (r.src == ROUTE_OSPF)      ? "O"
                                       : (r.src == ROUTE_BGP)       ? "B" : "S";
                DrawText(typeLetter, CANVAS_W() + 12, ry + 3, 11, rowColor);
            }

            // Destination
            DrawText(r.dest.c_str(), CANVAS_W() + 30, ry + 3, 10, rowColor);

            // Next-hop
            DrawText(r.nextHop.c_str(), CANVAS_W() + 130, ry + 3, 10, rowColor);

            // Via (port name or em-dash for mgmt/static)
            if (r.outPort >= 0) {
                std::string via = GetPortName(n->type, r.outPort);
                DrawText(via.c_str(), CANVAS_W() + 210, ry + 3, 10, rowColor);
            } else {
                DrawText("\xe2\x80\x94", CANVAS_W() + 210, ry + 3, 10, rowColor);
            }

            // [×] delete button for static routes only
            if (r.src == ROUTE_STATIC) {
                Rectangle delBtn = PnlRouteDeleteRect(i);
                DrawRectangleRounded(delBtn, 0.3f, 4, Color{51, 65, 85, 255});
                DrawText("x", (int)(delBtn.x + 4), (int)(delBtn.y + 1), 11,
                         Color{239, 68, 68, 255});
            }
        }
    }

    // Add-form separator and labels
    DrawLineEx({(float)CANVAS_W(),           (float)RTE_ADD_SEP_Y},
               {(float)(CANVAS_W()+PANEL_W), (float)RTE_ADD_SEP_Y},
               1.0f, PANEL_BORDER);
    DrawText("ADD STATIC ROUTE", CANVAS_W() + 12, RTE_ADD_SEP_Y + 8, 10,
             Color{100, 116, 139, 255});

    // Destination field
    DrawTextField(PnlRouteDestRect(), "Destination", "x.x.x.x/xx",
                  ps.newRouteDest, ps.activeRouteField == 0, ValidateIP(ps.newRouteDest));

    // Next-hop field
    DrawTextField(PnlRouteNextRect(), "Next-Hop", "x.x.x.x",
                  ps.newRouteNext, ps.activeRouteField == 1, ValidateIPOnly(ps.newRouteNext));

    // [Add Route] button — active only when both fields are valid
    bool canAdd = ValidateIP(ps.newRouteDest) && ValidateIPOnly(ps.newRouteNext);
    DrawRectangleRec(PnlRouteAddBtnRect(),
                     canAdd ? Color{30, 58, 138, 255} : Color{22, 33, 62, 255});
    DrawRectangleLinesEx(PnlRouteAddBtnRect(), 1.0f,
                         canAdd ? Color{59, 130, 246, 255} : PANEL_BORDER);
    {
        int tw = MeasureText("Add Route", 12);
        Rectangle btn = PnlRouteAddBtnRect();
        DrawText("Add Route",
                 (int)(btn.x + (btn.width - tw) / 2),
                 (int)(btn.y + 8), 12,
                 canAdd ? WHITE : Color{51, 65, 85, 255});
    }
}

void DrawArpTab(const DeviceNode* n) {
    if (!n) return;
    DrawText("ARP CACHE", CANVAS_W() + 12, 124, 10, Color{100, 116, 139, 255});

    if (n->arpTable.empty()) {
        DrawText("(empty)", CANVAS_W() + 12, 148, 11, Color{51, 65, 85, 255});
        DrawText("Run a simulation to populate the cache.",
                 CANVAS_W() + 12, 164, 10, Color{51, 65, 85, 255});
        return;
    }

    DrawText("IP ADDRESS",  CANVAS_W() + 12,  142, 9, Color{100, 116, 139, 255});
    DrawText("MAC ADDRESS", CANVAS_W() + 138, 142, 9, Color{100, 116, 139, 255});
    DrawLineEx({(float)CANVAS_W(),           156.0f},
               {(float)(CANVAS_W()+PANEL_W), 156.0f}, 0.5f, PANEL_BORDER);

    int y = 162;
    for (const auto& [ip, mac] : n->arpTable) {
        if (y > CANVAS_H() - LOG_H - 20) break;
        DrawText(ip.c_str(),  CANVAS_W() + 12,  y, 10, Color{34, 197, 94, 255});
        DrawText(mac.c_str(), CANVAS_W() + 138, y, 9,  Color{148, 163, 184, 255});
        y += 18;
    }
}

void DrawOspfTab(const DeviceNode* n) {
    if (!n) {
        DrawText("No device selected", CANVAS_W() + 20, 130, 12, Color{100,116,139,255});
        return;
    }
    if (n->type != ROUTER) {
        DrawText("OSPF: routers only", CANVAS_W() + 20, 130, 12, Color{100,116,139,255});
        return;
    }

    // Enable/disable button
    Rectangle btn = PnlOspfEnableRect();
    Color btnColor = n->ospfEnabled ? Color{34,197,94,255} : Color{51,65,85,255};
    DrawRectangleRec(btn, btnColor);
    DrawRectangleLinesEx(btn, 1.0f, Color{71,85,105,255});
    const char* btnLabel = n->ospfEnabled ? "OSPF: Enabled" : "OSPF: Disabled";
    int tw = MeasureText(btnLabel, 12);
    DrawText(btnLabel, (int)(btn.x + (btn.width - tw) / 2), (int)(btn.y + 7), 12,
             n->ospfEnabled ? Color{15,23,42,255} : Color{148,163,184,255});

    if (!n->ospfEnabled) return;

    // Router ID
    int y = 160;
    DrawText("Router ID", CANVAS_W() + 12, y, 11, Color{100,116,139,255});
    DrawText(n->routerId.empty() ? "(none)" : n->routerId.c_str(),
             CANVAS_W() + 90, y, 11, WHITE);
    y += 20;

    // ABR badge or single-area display
    if (IsAbr(*n)) {
        DrawRectangleRounded({(float)(CANVAS_W() + 90), (float)y, 34.0f, 16.0f},
                             0.5f, 4, Color{139, 92, 246, 255});
        DrawText("ABR", CANVAS_W() + 95, y + 3, 10, WHITE);
    } else {
        uint32_t displayArea = 0;
        for (int i = 0; i < PORTS_PER_NODE; ++i)
            if (ValidateIP(n->portIp[i])) { displayArea = n->ospfPortArea[i]; break; }
        DrawText("Area", CANVAS_W() + 12, y, 11, Color{100,116,139,255});
        DrawText(std::to_string(displayArea).c_str(), CANVAS_W() + 90, y, 11, WHITE);
    }
    y += 24;

    // Separator
    DrawLineEx({(float)CANVAS_W(), (float)y}, {(float)(CANVAS_W() + PANEL_W), (float)y},
               1.0f, PANEL_BORDER);
    y += 6;
    DrawText("Neighbors", CANVAS_W() + 12, y, 11, Color{100,116,139,255});
    y += 18;

    if (n->ospfNeighbors.empty()) {
        DrawText("(none)", CANVAS_W() + 20, y, 11, Color{71,85,105,255});
        return;
    }

    // Header row — Area column added between State and Dead
    DrawText("Router-ID",   CANVAS_W() + 12,  y, 10, Color{71,85,105,255});
    DrawText("State",       CANVAS_W() + 110, y, 10, Color{71,85,105,255});
    DrawText("Area",        CANVAS_W() + 175, y, 10, Color{71,85,105,255});
    DrawText("Dead",        CANVAS_W() + 218, y, 10, Color{71,85,105,255});
    y += 14;

    static const char* stateNames[] = { "DOWN", "INIT", "2WAY", "FULL" };
    static const Color stateColors[] = {
        {100,116,139,255}, {234,179,8,255}, {59,130,246,255}, {34,197,94,255}
    };

    for (const auto& nbr : n->ospfNeighbors) {
        if (y > CANVAS_H() - 20) break;
        std::string rid = nbr.neighborRouterId;
        if ((int)rid.size() > 11) rid = rid.substr(rid.size() - 11);
        DrawText(rid.c_str(), CANVAS_W() + 12, y, 10, WHITE);

        int si = std::clamp((int)nbr.state, 0, 3);
        DrawText(stateNames[si], CANVAS_W() + 110, y, 10, stateColors[si]);

        DrawText(std::to_string(nbr.area).c_str(), CANVAS_W() + 175, y, 10,
                 Color{148, 163, 184, 255});

        char deadBuf[8];
        std::snprintf(deadBuf, sizeof(deadBuf), "%.1fs", nbr.deadTimer);
        DrawText(deadBuf, CANVAS_W() + 218, y, 10, Color{148,163,184,255});
        y += 16;
    }
}

void DrawMplsTab(const DeviceNode* n) {
    if (!n) {
        DrawText("No device selected", CANVAS_W() + 20, 130, 12, Color{100,116,139,255});
        return;
    }
    if (n->type != ROUTER) {
        DrawText("MPLS: routers only", CANVAS_W() + 20, 130, 12, Color{100,116,139,255});
        return;
    }
    if (!n->ospfEnabled) {
        DrawText("Requires OSPF enabled", CANVAS_W() + 20, 130, 11, Color{100,116,139,255});
        return;
    }

    // Enable / disable toggle button
    Rectangle btn    = PnlMplsToggleRect();
    Color     btnCol = n->ldpEnabled ? Color{249,115,22,255} : Color{51,65,85,255};
    DrawRectangleRec(btn, btnCol);
    DrawRectangleLinesEx(btn, 1.0f, Color{71,85,105,255});
    const char* btnLabel = n->ldpEnabled ? "MPLS: Enabled" : "MPLS: Disabled";
    int tw = MeasureText(btnLabel, 12);
    DrawText(btnLabel, (int)(btn.x + (btn.width - tw) / 2), (int)(btn.y + 7), 12,
             n->ldpEnabled ? Color{15,23,42,255} : Color{148,163,184,255});

    if (!n->ldpEnabled) return;

    // LFIB table
    int y = 158;
    DrawText("LFIB", CANVAS_W() + 12, y, 11, Color{100,116,139,255});
    y += 18;

    if (n->lfib.empty()) {
        DrawText("(no bindings - wait for OSPF)", CANVAS_W() + 16, y, 10,
                 Color{71,85,105,255});
        return;
    }

    // Column headers
    DrawText("PREFIX",  CANVAS_W() + 12,  y, 10, Color{71,85,105,255});
    DrawText("LOCAL",   CANVAS_W() + 107, y, 10, Color{71,85,105,255});
    DrawText("OUT",     CANVAS_W() + 170, y, 10, Color{71,85,105,255});
    DrawLineEx({(float)CANVAS_W(), (float)(y + 13)},
               {(float)(CANVAS_W() + PANEL_W), (float)(y + 13)},
               0.5f, PANEL_BORDER);
    y += 16;

    for (const auto& [prefix, binding] : n->lfib) {
        if (y > CANVAS_H() - 20) break;
        DrawText(prefix.c_str(), CANVAS_W() + 12, y, 10, WHITE);

        char locBuf[16];
        std::snprintf(locBuf, sizeof(locBuf), "%u", binding.localLabel);
        DrawText(locBuf, CANVAS_W() + 107, y, 10, Color{253,186,116,255});

        if (binding.outLabel == MPLS_IMPLICIT_NULL) {
            DrawRectangleRounded({(float)(CANVAS_W() + 170), (float)y, 28.f, 13.f},
                                 0.4f, 4, Color{168,85,247,255});
            DrawText("PHP", CANVAS_W() + 174, y + 2, 9, WHITE);
        } else {
            char outBuf[16];
            std::snprintf(outBuf, sizeof(outBuf), "%u", binding.outLabel);
            DrawText(outBuf, CANVAS_W() + 170, y, 10, Color{253,186,116,255});
        }

        y += 16;
    }
}

void DrawBgpTab(const DeviceNode* n, const PanelState& ps) {
    if (!n) {
        DrawText("No device selected", CANVAS_W() + 20, 130, 12, Color{100,116,139,255});
        return;
    }
    if (n->type != ROUTER) {
        DrawText("BGP: routers only", CANVAS_W() + 20, 130, 12, Color{100,116,139,255});
        return;
    }

    // ── Enable / disable toggle ────────────────────────────────────────
    Rectangle btn    = PnlBgpToggleRect();
    Color     btnCol = n->bgpEnabled ? Color{34,197,94,255} : Color{51,65,85,255};
    DrawRectangleRec(btn, btnCol);
    DrawRectangleLinesEx(btn, 1.0f, Color{71,85,105,255});
    const char* btnLabel = n->bgpEnabled ? "BGP: Enabled" : "BGP: Disabled";
    int tw = MeasureText(btnLabel, 12);
    DrawText(btnLabel, (int)(btn.x + (btn.width - tw) / 2), (int)(btn.y + 7), 12,
             n->bgpEnabled ? Color{15,23,42,255} : Color{148,163,184,255});

    if (!n->bgpEnabled) return;

    // ── ASN input ─────────────────────────────────────────────────────
    int y = 152;
    DrawText("ASN:", CANVAS_W() + 12, y + 4, 11, Color{100,116,139,255});
    Rectangle asnRect  = PnlBgpAsnRect();
    bool      asnActive = (ps.bgpAsnField == n->id);
    DrawRectangleRec(asnRect, asnActive ? Color{30,41,59,255} : Color{15,23,42,255});
    DrawRectangleLinesEx(asnRect, 1.0f,
                         asnActive ? Color{59,130,246,255} : Color{71,85,105,255});
    std::string asnStr = asnActive ? ps.bgpAsnBuf
                                   : (n->localAsn > 0 ? std::to_string(n->localAsn) : "0");
    DrawText(asnStr.c_str(), (int)(asnRect.x + 4), (int)(asnRect.y + 5), 11, WHITE);

    if (n->localAsn == 0) {
        DrawText("Set ASN to form sessions", CANVAS_W() + 12, y + 30, 10,
                 Color{234,179,8,255});
    } else {
        // Route Reflector toggle (only when ASN is set)
        Rectangle rrRect = PnlBgpRrRect();
        Color rrCol = n->isRouteReflector ? Color{167,139,250,255} : Color{51,65,85,255};
        DrawRectangleRec(rrRect, rrCol);
        DrawRectangleLinesEx(rrRect, 1.0f, Color{71,85,105,255});
        const char* rrLabel = n->isRouteReflector ? "Route Reflector: ON"
                                                  : "Route Reflector: OFF";
        int rrTw = MeasureText(rrLabel, 11);
        DrawText(rrLabel, (int)(rrRect.x + (rrRect.width - rrTw) / 2),
                 (int)(rrRect.y + 5), 11,
                 n->isRouteReflector ? Color{15,23,42,255} : Color{148,163,184,255});
    }

    y = 210;

    // ── Neighbors ─────────────────────────────────────────────────────
    const char* neighborHeader = (n->isRouteReflector && n->localAsn > 0) ? "CLIENTS" : "NEIGHBORS";
    DrawText(neighborHeader, CANVAS_W() + 12, y, 10, Color{71,85,105,255});
    y += 14;
    if (n->bgpNeighbors.empty()) {
        DrawText("(none)", CANVAS_W() + 16, y, 10, Color{71,85,105,255});
        y += 14;
    } else {
        for (const auto& nb : n->bgpNeighbors) {
            if (y > CANVAS_H() - 80) break;
            std::string ip = nb.neighborIp.size() > 12
                             ? nb.neighborIp.substr(0, 11) + "\xe2\x80\xa6"
                             : nb.neighborIp;
            DrawText(ip.c_str(), CANVAS_W() + 12, y, 10, WHITE);
            const char* typeLabel = nb.ibgp ? "iBGP" : "eBGP";
            Color typeCol = nb.ibgp ? Color{167,139,250,255}   // purple
                                    : Color{253,186,116,255};  // orange
            DrawText(typeLabel, CANVAS_W() + 102, y, 10, typeCol);
            const char* state = nb.established ? "ESTAB" : "DOWN";
            Color stCol = nb.established ? Color{34,197,94,255} : Color{239,68,68,255};
            DrawText(state, CANVAS_W() + 142, y, 10, stCol);
            y += 14;
        }
    }

    // ── BGP RIB ───────────────────────────────────────────────────────
    y += 4;
    DrawText("BGP RIB", CANVAS_W() + 12, y, 10, Color{71,85,105,255});
    y += 14;
    if (n->bgpRoutes.empty()) {
        DrawText("(no routes)", CANVAS_W() + 16, y, 10, Color{71,85,105,255});
    } else {
        DrawText("PREFIX",   CANVAS_W() + 12,  y, 9, Color{71,85,105,255});
        DrawText("NEXT-HOP", CANVAS_W() + 90,  y, 9, Color{71,85,105,255});
        DrawText("AS-PATH",  CANVAS_W() + 158, y, 9, Color{71,85,105,255});
        y += 12;
        for (const auto& r : n->bgpRoutes) {
            if (y > CANVAS_H() - 20) break;
            {
                std::string pfx = r.prefix.size() > 15
                                  ? r.prefix.substr(0, 14) + "\xe2\x80\xa6" : r.prefix;
                DrawText(pfx.c_str(), CANVAS_W() + 12, y, 10, WHITE);
            }
            {
                std::string nh = r.nextHop.size() > 12
                                 ? r.nextHop.substr(0, 11) + "\xe2\x80\xa6" : r.nextHop;
                DrawText(nh.c_str(), CANVAS_W() + 90, y, 10, Color{94,234,212,255});
            }
            std::string path;
            for (size_t i = 0; i < r.asPath.size(); ++i) {
                if (i) path += ' ';
                path += std::to_string(r.asPath[i]);
            }
            DrawText(path.c_str(), CANVAS_W() + 158, y, 10, Color{253,186,116,255});
            y += 14;
        }
    }
}

void DrawVlanTab(const DeviceNode* n, const PanelState& ps) {
    if (!n) {
        DrawText("No device selected", CANVAS_W() + 20, 130, 12, Color{100,116,139,255});
        return;
    }
    if (n->type != SWITCH) {
        DrawText("VLAN: switches only", CANVAS_W() + 20, 130, 12, Color{100,116,139,255});
        return;
    }

    DrawText("PORT VLAN CONFIG", CANVAS_W() + 12, 124, 10, Color{71,85,105,255});

    for (int p = 0; p < PORTS_PER_NODE; ++p) {
        const VlanPortConfig& vc = n->vlanPorts[p];
        int rowY = 152 + p * 34;

        char plabel[12];
        std::snprintf(plabel, sizeof(plabel), "Port %d", p);
        DrawText(plabel, CANVAS_W() + 12, rowY + 5, 10, Color{100,116,139,255});

        // Mode toggle button
        Rectangle modeRect = PnlVlanPortModeRect(p);
        bool isTrunk = (vc.mode == VLAN_TRUNK);
        Color modeBg = isTrunk ? Color{120, 53, 15, 255} : Color{30, 58, 95, 255};
        Color modeFg = isTrunk ? Color{245,158,11,255}   : Color{96,165,250,255};
        Color modeBorder = isTrunk ? Color{180,83,9,255}  : Color{37,99,235,255};
        DrawRectangleRec(modeRect, modeBg);
        DrawRectangleLinesEx(modeRect, 1.0f, modeBorder);
        const char* modeLabel = isTrunk ? "Trunk" : "Access";
        int mlw = MeasureText(modeLabel, 10);
        DrawText(modeLabel, (int)(modeRect.x + (modeRect.width - mlw) / 2),
                 (int)(modeRect.y + 6), 10, modeFg);

        if (isTrunk) {
            DrawText("-- all --", CANVAS_W() + 93, rowY + 5, 10, Color{100,116,139,255});
        } else {
            // VLAN ID input field
            Rectangle idRect = PnlVlanPortIdRect(p);
            bool active = (ps.vlanPortField == p);
            DrawRectangleRec(idRect, active ? Color{30,41,59,255} : Color{15,23,42,255});
            DrawRectangleLinesEx(idRect, 1.0f,
                                 active ? Color{59,130,246,255} : Color{71,85,105,255});
            std::string idStr = active ? ps.vlanPortBuf : std::to_string(vc.accessVlan);
            DrawText(idStr.c_str(), (int)(idRect.x + 4), (int)(idRect.y + 5), 10, WHITE);
        }
    }
}

void DrawSubIfaceTab(const DeviceNode* n, const PanelState& ps) {
    if (!n || n->type != ROUTER) {
        int tw = MeasureText("Select a router to configure subinterfaces.", 10);
        DrawText("Select a router to configure subinterfaces.",
                 CANVAS_W() + (PANEL_W - tw) / 2, 200, 10, Color{100, 116, 139, 255});
        return;
    }

    // ── Existing subinterface list ─────────────────────────────────────────
    DrawLine(CANVAS_W() + 12, 128, CANVAS_W() + PANEL_W - 12, 128, Color{51, 65, 85, 255});
    DrawText("Subinterfaces", CANVAS_W() + 12, 132, 10, Color{148, 163, 184, 255});

    if (n->subIfaces.empty()) {
        DrawText("(none configured)", CANVAS_W() + 20, SUB_ROW_Y0, 10, Color{100, 116, 139, 255});
    } else {
        int maxRows = (SUB_FORM_Y0 - 12 - SUB_ROW_Y0) / SUB_ROW_H;
        int shown   = std::min((int)n->subIfaces.size(), maxRows);
        for (int i = 0; i < shown; ++i) {
            const SubInterface& si = n->subIfaces[i];
            int y = SUB_ROW_Y0 + i * SUB_ROW_H;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Gi0/%d.%d  V%d  %s",
                          si.parentPort, si.vlanId, si.vlanId, si.ip.c_str());
            DrawText(buf, CANVAS_W() + 12, y + 4, 10, Color{203, 213, 225, 255});

            Rectangle delR = PnlSubRowDeleteRect(i);
            DrawRectangleRec(delR, Color{127, 29, 29, 255});
            DrawText("x", (int)(delR.x + 3), (int)(delR.y + 1), 11, WHITE);
        }
        if ((int)n->subIfaces.size() > shown) {
            char more[32];
            std::snprintf(more, sizeof(more), "+%d more",
                          (int)n->subIfaces.size() - shown);
            DrawText(more, CANVAS_W() + 12,
                     SUB_ROW_Y0 + shown * SUB_ROW_H + 4, 9, Color{100, 116, 139, 255});
        }
    }

    // ── Add subinterface form ──────────────────────────────────────────────
    DrawLine(CANVAS_W() + 12, SUB_FORM_Y0 - 12,
             CANVAS_W() + PANEL_W - 12, SUB_FORM_Y0 - 12, Color{51, 65, 85, 255});
    DrawText("Add Subinterface", CANVAS_W() + 12, SUB_FORM_Y0 - 8, 10, Color{148, 163, 184, 255});

    DrawText("Port:", CANVAS_W() + 12, SUB_FORM_Y0 + 4, 10, Color{148, 163, 184, 255});
    for (int p = 0; p < PORTS_PER_NODE; ++p) {
        Rectangle btn = PnlSubPortBtnRect(p);
        bool sel = (ps.subFormPort == p);
        DrawRectangleRec(btn, sel ? Color{234, 88, 12, 255} : Color{30, 41, 59, 255});
        DrawRectangleLinesEx(btn, 1.0f, Color{51, 65, 85, 255});
        char lbl[4];
        std::snprintf(lbl, sizeof(lbl), "%d", p);
        int lw = MeasureText(lbl, 10);
        DrawText(lbl, (int)(btn.x + (btn.width - lw) * 0.5f), (int)(btn.y + 5), 10, WHITE);
    }

    DrawText("VLAN:", CANVAS_W() + 12, SUB_FORM_Y0 + 34, 10, Color{148, 163, 184, 255});
    DrawTextField(PnlSubVlanFieldRect(), nullptr, "10",
                  ps.subVlanBuf, ps.subActiveField == 0,
                  !ps.subVlanBuf.empty());

    DrawText("IP:", CANVAS_W() + 12, SUB_FORM_Y0 + 64, 10, Color{148, 163, 184, 255});
    DrawTextField(PnlSubIpFieldRect(), nullptr, "10.10.0.1/24",
                  ps.subIpBuf, ps.subActiveField == 1,
                  ValidateIP(ps.subIpBuf));

    Rectangle addBtn = PnlSubAddBtnRect();
    bool canAdd = !ps.subVlanBuf.empty() && ValidateIP(ps.subIpBuf);
    DrawRectangleRec(addBtn, canAdd ? Color{234, 88, 12, 200} : Color{30, 41, 59, 255});
    DrawRectangleLinesEx(addBtn, 1.0f, Color{51, 65, 85, 255});
    int tw2 = MeasureText("Add Subinterface", 10);
    DrawText("Add Subinterface",
             (int)(addBtn.x + (addBtn.width - tw2) * 0.5f),
             (int)(addBtn.y + 7), 10, WHITE);
}

void DrawVxlanTab(const DeviceNode* n, const PanelState& ps) {
    if (!n) {
        DrawText("No device selected", CANVAS_W() + 20, 130, 12, Color{100,116,139,255});
        return;
    }
    if (n->type != ROUTER) {
        DrawText("VXLAN: routers only", CANVAS_W() + 20, 130, 12, Color{100,116,139,255});
        return;
    }

    // VXLAN Enabled toggle
    Rectangle vxBtn = PnlVxlanToggleRect();
    Color vxCol = n->vxlanEnabled ? Color{20,184,166,255} : Color{51,65,85,255};
    DrawRectangleRec(vxBtn, vxCol);
    DrawRectangleLinesEx(vxBtn, 1.0f, Color{71,85,105,255});
    const char* vxLabel = n->vxlanEnabled ? "VXLAN: Enabled" : "VXLAN: Disabled";
    {
        int tw = MeasureText(vxLabel, 12);
        DrawText(vxLabel, (int)(vxBtn.x + (vxBtn.width - tw) / 2), (int)(vxBtn.y + 7), 12,
                 n->vxlanEnabled ? Color{15,23,42,255} : Color{148,163,184,255});
    }

    if (!n->vxlanEnabled) return;

    // VNI field
    DrawText("VNI:", CANVAS_W() + 12, 148, 10, Color{100,116,139,255});
    Rectangle vniRect = PnlVxlanVniRect();
    bool vniActive = (ps.vxlanField == 0);
    DrawRectangleRec(vniRect, vniActive ? Color{30,41,59,255} : Color{15,23,42,255});
    DrawRectangleLinesEx(vniRect, 1.0f, vniActive ? Color{59,130,246,255} : Color{71,85,105,255});
    std::string vniStr = vniActive ? ps.vxlanVniBuf
                                   : (n->vni > 0 ? std::to_string(n->vni) : "unset");
    DrawText(vniStr.c_str(), (int)(vniRect.x + 4), (int)(vniRect.y + 5), 11,
             n->vni > 0 || vniActive ? WHITE : Color{100,116,139,255});

    // VTEP IP field
    DrawText("VTEP IP:", CANVAS_W() + 12, 184, 10, Color{100,116,139,255});
    Rectangle vtepRect = PnlVxlanVtepRect();
    bool vtepActive = (ps.vxlanField == 1);
    DrawRectangleRec(vtepRect, vtepActive ? Color{30,41,59,255} : Color{15,23,42,255});
    DrawRectangleLinesEx(vtepRect, 1.0f, vtepActive ? Color{59,130,246,255} : Color{71,85,105,255});
    std::string vtepStr = vtepActive ? ps.vxlanVtepBuf
                                     : (n->vtepIp.empty() ? "unset" : n->vtepIp);
    DrawText(vtepStr.c_str(), (int)(vtepRect.x + 4), (int)(vtepRect.y + 5), 11,
             !n->vtepIp.empty() || vtepActive ? WHITE : Color{100,116,139,255});

    if (n->vtepIp.empty()) {
        DrawText("Must match a port IP", CANVAS_W() + 12, 218, 10, Color{234,179,8,255});
    }

    // EVPN Enabled toggle
    Rectangle evpnBtn = PnlVxlanEvpnRect();
    Color evpnCol = n->evpnEnabled ? Color{34,197,94,255} : Color{51,65,85,255};
    DrawRectangleRec(evpnBtn, evpnCol);
    DrawRectangleLinesEx(evpnBtn, 1.0f, Color{71,85,105,255});
    const char* evpnLabel = n->evpnEnabled ? "EVPN: Enabled" : "EVPN: Disabled";
    {
        int tw = MeasureText(evpnLabel, 11);
        DrawText(evpnLabel, (int)(evpnBtn.x + (evpnBtn.width - tw) / 2), (int)(evpnBtn.y + 6), 11,
                 n->evpnEnabled ? Color{15,23,42,255} : Color{148,163,184,255});
    }

    if (n->evpnEnabled) {
        char routeBuf[48];
        std::snprintf(routeBuf, sizeof(routeBuf), "EVPN routes: %d", (int)n->evpnRoutes.size());
        DrawText(routeBuf, CANVAS_W() + 12, 260, 10, Color{94,234,212,255});
    }
}

void DrawAclTab(const DeviceNode* n, const PanelState& ps) {
    if (!n) { DrawText("No device selected", CANVAS_W()+20, 130, 11, Color{100,116,139,255}); return; }
    if (n->type != ROUTER) { DrawText("ACL: routers only", CANVAS_W()+20, 130, 11, Color{100,116,139,255}); return; }

    // Enable toggle (grey = no rules, green = rules present)
    Rectangle togR = PnlAclToggleRect();
    bool hasRules  = !n->aclRules.empty();
    Color togCol   = hasRules ? Color{34,197,94,255} : Color{51,65,85,255};
    DrawRectangleRec(togR, togCol);
    DrawRectangleLinesEx(togR, 1.f, Color{71,85,105,255});
    const char* togLabel = hasRules ? "ACL: Active" : "ACL: No Rules";
    { int tw = MeasureText(togLabel, 12); DrawText(togLabel,
        (int)(togR.x + (togR.width-tw)/2), (int)(togR.y+7), 12, WHITE); }

    // IN port selector
    DrawText("IN port:", CANVAS_W()+12, 142, 10, Color{100,116,139,255});
    for (int p = 0; p <= 4; ++p) {
        Rectangle br = PnlAclInPortBtnRect(p);
        bool active  = (p < 4) ? (n->aclInPort == p) : (n->aclInPort == -1);
        DrawRectangleRec(br, active ? Color{59,130,246,255} : Color{30,41,59,255});
        DrawRectangleLinesEx(br, 1.f, Color{71,85,105,255});
        char pbuf[4] = {0};
        if (p < 4) pbuf[0] = '0' + p;
        else { pbuf[0]='\xe2'; pbuf[1]='\x80'; pbuf[2]='\x94'; }  // — (em dash UTF-8)
        int tw2 = MeasureText(pbuf, 10);
        DrawText(pbuf, (int)(br.x+(br.width-tw2)/2), (int)(br.y+6), 10, WHITE);
    }

    // OUT port selector
    DrawText("OUT port:", CANVAS_W()+12, 168, 10, Color{100,116,139,255});
    for (int p = 0; p <= 4; ++p) {
        Rectangle br = PnlAclOutPortBtnRect(p);
        bool active  = (p < 4) ? (n->aclOutPort == p) : (n->aclOutPort == -1);
        DrawRectangleRec(br, active ? Color{59,130,246,255} : Color{30,41,59,255});
        DrawRectangleLinesEx(br, 1.f, Color{71,85,105,255});
        char pbuf[4] = {0};
        if (p < 4) pbuf[0] = '0' + p;
        else { pbuf[0]='\xe2'; pbuf[1]='\x80'; pbuf[2]='\x94'; }
        int tw2 = MeasureText(pbuf, 10);
        DrawText(pbuf, (int)(br.x+(br.width-tw2)/2), (int)(br.y+6), 10, WHITE);
    }

    // Rules header
    DrawText("Rules:", CANVAS_W()+12, 208, 10, Color{100,116,139,255});
    DrawLineEx({(float)(CANVAS_W()+52), 214.f}, {(float)(CANVAS_W()+12+PANEL_W-24), 214.f},
               0.5f, Color{51,65,85,255});

    // Rule rows (max 5 visible)
    int maxVisible = 5;
    int ruleCount  = (int)n->aclRules.size();
    for (int i = 0; i < std::min(ruleCount, maxVisible); ++i) {
        const AclRule& r = n->aclRules[i];
        float ry = 226.f + i * 26.f;

        // Action badge
        bool permit = (r.action == ACL_PERMIT);
        Color badgeCol = permit ? Color{34,197,94,200} : Color{239,68,68,200};
        Rectangle badgeR = {(float)(CANVAS_W()+12), ry, 46.f, 20.f};
        DrawRectangleRounded(badgeR, 0.4f, 4, badgeCol);
        const char* aLabel = permit ? "PERMIT" : "DENY";
        int aw = MeasureText(aLabel, 9);
        DrawText(aLabel, (int)(badgeR.x+(46-aw)/2), (int)(ry+5), 9, WHITE);

        // Rule description
        char rdesc[96];
        if (r.dstPort > 0)
            std::snprintf(rdesc, sizeof(rdesc), "%s \xe2\x86\x92 %s :%d",
                          r.srcCidr.c_str(), r.dstCidr.c_str(), r.dstPort);
        else
            std::snprintf(rdesc, sizeof(rdesc), "%s \xe2\x86\x92 %s",
                          r.srcCidr.c_str(), r.dstCidr.c_str());
        DrawText(rdesc, CANVAS_W()+62, (int)(ry+5), 9, Color{203,213,225,255});

        // Delete button
        Rectangle delR = PnlAclRuleDeleteRect(i);
        DrawRectangleRec(delR, Color{127,29,29,200});
        DrawRectangleLinesEx(delR, 1.f, Color{153,27,27,255});
        { int xw = MeasureText("X", 9); DrawText("X",
            (int)(delR.x+(delR.width-xw)/2), (int)(delR.y+5), 9, WHITE); }
    }
    if (ruleCount > maxVisible) {
        char moreBuf[32];
        std::snprintf(moreBuf, sizeof(moreBuf), "+%d more", ruleCount - maxVisible);
        DrawText(moreBuf, CANVAS_W()+12, (int)(226.f + maxVisible*26.f + 2), 9,
                 Color{100,116,139,255});
    }

    // Add Rule form separator
    DrawText("Add Rule:", CANVAS_W()+12, 358, 10, Color{100,116,139,255});
    DrawLineEx({(float)(CANVAS_W()+68), 364.f}, {(float)(CANVAS_W()+12+PANEL_W-24), 364.f},
               0.5f, Color{51,65,85,255});

    // Action toggle for new rule
    Rectangle formAct = PnlAclFormActionRect();
    Color formActCol  = (ps.aclFormAction == 0) ? Color{34,197,94,255} : Color{239,68,68,255};
    DrawRectangleRec(formAct, formActCol);
    DrawRectangleLinesEx(formAct, 1.f, Color{71,85,105,255});
    const char* fActLabel = (ps.aclFormAction == 0) ? "PERMIT" : "DENY";
    { int tw3 = MeasureText(fActLabel, 12);
      DrawText(fActLabel, (int)(formAct.x+(formAct.width-tw3)/2), (int)(formAct.y+7), 12, WHITE); }

    // Src CIDR field
    DrawText("Src:", CANVAS_W()+12, 392, 10, Color{100,116,139,255});
    Rectangle srcR = PnlAclFormSrcRect();
    bool srcActive = (ps.aclActiveField == 0);
    DrawRectangleRec(srcR, srcActive ? Color{30,41,59,255} : Color{15,23,42,255});
    DrawRectangleLinesEx(srcR, 1.f, srcActive ? Color{59,130,246,255} : Color{71,85,105,255});
    const std::string& srcStr = srcActive ? ps.aclSrcBuf : (ps.aclSrcBuf.empty() ? std::string("any") : ps.aclSrcBuf);
    DrawText(srcStr.c_str(), (int)(srcR.x+4), (int)(srcR.y+5), 10,
             srcStr == "any" ? Color{71,85,105,255} : WHITE);

    // Dst CIDR field
    DrawText("Dst:", CANVAS_W()+12, 418, 10, Color{100,116,139,255});
    Rectangle dstR = PnlAclFormDstRect();
    bool dstActive = (ps.aclActiveField == 1);
    DrawRectangleRec(dstR, dstActive ? Color{30,41,59,255} : Color{15,23,42,255});
    DrawRectangleLinesEx(dstR, 1.f, dstActive ? Color{59,130,246,255} : Color{71,85,105,255});
    const std::string& dstStr = dstActive ? ps.aclDstBuf : (ps.aclDstBuf.empty() ? std::string("any") : ps.aclDstBuf);
    DrawText(dstStr.c_str(), (int)(dstR.x+4), (int)(dstR.y+5), 10,
             dstStr == "any" ? Color{71,85,105,255} : WHITE);

    // Dst Port field
    DrawText("Port:", CANVAS_W()+12, 444, 10, Color{100,116,139,255});
    Rectangle portR = PnlAclFormPortRect();
    bool portActive = (ps.aclActiveField == 2);
    DrawRectangleRec(portR, portActive ? Color{30,41,59,255} : Color{15,23,42,255});
    DrawRectangleLinesEx(portR, 1.f, portActive ? Color{59,130,246,255} : Color{71,85,105,255});
    const std::string& portStr = portActive ? ps.aclPortBuf
                                 : (ps.aclPortBuf.empty() ? std::string("any") : ps.aclPortBuf);
    DrawText(portStr.c_str(), (int)(portR.x+4), (int)(portR.y+5), 10,
             portStr == "any" ? Color{71,85,105,255} : WHITE);

    // Add button
    Rectangle addR = PnlAclFormAddBtnRect();
    DrawRectangleRec(addR, Color{30,64,175,255});
    DrawRectangleLinesEx(addR, 1.f, Color{59,130,246,255});
    { int tw4 = MeasureText("+ Add Rule", 11);
      DrawText("+ Add Rule", (int)(addR.x+(addR.width-tw4)/2), (int)(addR.y+8), 11, WHITE); }
}

void DrawNatTab(const DeviceNode* n, const PanelState& ps) {
    if (!n) { DrawText("No device selected", CANVAS_W()+20, 130, 11, Color{100,116,139,255}); return; }
    if (n->type != ROUTER) { DrawText("NAT: routers only", CANVAS_W()+20, 130, 11, Color{100,116,139,255}); return; }

    // Enable toggle
    Rectangle togR = PnlNatToggleRect();
    Color togCol   = n->natEnabled ? Color{234,179,8,255} : Color{51,65,85,255};
    DrawRectangleRec(togR, togCol);
    DrawRectangleLinesEx(togR, 1.f, Color{71,85,105,255});
    const char* natLabel = n->natEnabled ? "NAT: Enabled" : "NAT: Disabled";
    { int tw = MeasureText(natLabel, 12);
      DrawText(natLabel, (int)(togR.x+(togR.width-tw)/2), (int)(togR.y+7), 12, WHITE); }

    if (!n->natEnabled) return;

    // Inside port
    DrawText("Inside port:", CANVAS_W()+12, 142, 10, Color{100,116,139,255});
    for (int p = 0; p < 4; ++p) {
        Rectangle br = PnlNatInsidePortBtnRect(p);
        bool active  = (n->natInsidePort == p);
        DrawRectangleRec(br, active ? Color{34,197,94,255} : Color{30,41,59,255});
        DrawRectangleLinesEx(br, 1.f, Color{71,85,105,255});
        char pbuf[2] = {static_cast<char>('0'+p), 0};
        int tw2 = MeasureText(pbuf, 10);
        DrawText(pbuf, (int)(br.x+(br.width-tw2)/2), (int)(br.y+6), 10, WHITE);
    }

    // Outside port
    DrawText("Outside port:", CANVAS_W()+12, 168, 10, Color{100,116,139,255});
    for (int p = 0; p < 4; ++p) {
        Rectangle br = PnlNatOutsidePortBtnRect(p);
        bool active  = (n->natOutsidePort == p);
        DrawRectangleRec(br, active ? Color{59,130,246,255} : Color{30,41,59,255});
        DrawRectangleLinesEx(br, 1.f, Color{71,85,105,255});
        char pbuf[2] = {static_cast<char>('0'+p), 0};
        int tw2 = MeasureText(pbuf, 10);
        DrawText(pbuf, (int)(br.x+(br.width-tw2)/2), (int)(br.y+6), 10, WHITE);
    }

    // Inside prefix field
    DrawText("Inside prefix:", CANVAS_W()+12, 204, 10, Color{100,116,139,255});
    Rectangle prefR = PnlNatInsidePrefixRect();
    bool prefActive  = (ps.natField == 0);
    DrawRectangleRec(prefR, prefActive ? Color{30,41,59,255} : Color{15,23,42,255});
    DrawRectangleLinesEx(prefR, 1.f, prefActive ? Color{59,130,246,255} : Color{71,85,105,255});
    const std::string& prefStr = prefActive ? ps.natInsideBuf
                                  : (n->natInsidePrefix.empty() ? std::string("unset") : n->natInsidePrefix);
    DrawText(prefStr.c_str(), (int)(prefR.x+4), (int)(prefR.y+5), 10,
             (prefStr == "unset") ? Color{71,85,105,255} : WHITE);

    // Outside IP (auto, display only)
    DrawText("Outside IP (auto):", CANVAS_W()+12, 244, 10, Color{100,116,139,255});
    if (n->natOutsidePort < 0 || n->natOutsidePort >= PORTS_PER_NODE) {
        DrawText("Outside port not set", CANVAS_W()+12, 258, 10, Color{71,85,105,255});
    } else {
        const std::string& outsideCidr = n->portIp[n->natOutsidePort];
        auto sl = outsideCidr.find('/');
        std::string outsideIp = sl != std::string::npos ? outsideCidr.substr(0, sl) : outsideCidr;
        DrawText(outsideIp.empty() ? "unset" : outsideIp.c_str(),
                 CANVAS_W()+12, 258, 10,
                 outsideIp.empty() ? Color{71,85,105,255} : Color{94,234,212,255});

        // NAT summary
        if (!n->natInsidePrefix.empty() && !outsideIp.empty()) {
            char sumBuf[80];
            std::snprintf(sumBuf, sizeof(sumBuf), "%s \xe2\x86\x92 %s (overload)",
                          n->natInsidePrefix.c_str(), outsideIp.c_str());
            DrawText(sumBuf, CANVAS_W()+12, 278, 10, Color{234,179,8,200});
        }
    }
}

void DrawPanel(int selectedId, const std::vector<DeviceNode>& nodes,
               const PanelState& ps)
{
    DrawRectangle(CANVAS_W(), 0, PANEL_W, SCREEN_H(), PANEL_BG);
    DrawLineEx({(float)CANVAS_W(), 0.0f}, {(float)CANVAS_W(), (float)SCREEN_H()},
               1.0f, PANEL_BORDER);
    DrawText("CONFIGURATION", CANVAS_W() + 12, 14, 10, Color{100, 116, 139, 255});
    DrawLineEx({(float)CANVAS_W(), 38.0f}, {(float)(CANVAS_W() + PANEL_W), 38.0f},
               1.0f, PANEL_BORDER);

    if (selectedId == -1) {
        const char* msg = "<- Select a device";
        int tw = MeasureText(msg, 13);
        DrawText(msg, CANVAS_W() + (PANEL_W - tw) / 2, SCREEN_H() / 2 - 8, 13,
                 Color{100, 116, 139, 255});
        return;
    }

    const DeviceNode* n = FindNode(nodes, selectedId);
    if (!n) return;

    // Device type badge
    const char* typeNames[] = {"PC", "Router", "Switch"};
    int bw = MeasureText(typeNames[(int)n->type], 11) + 16;
    DrawRectangleRounded({(float)(CANVAS_W() + 12), 50.0f, (float)bw, 22.0f},
                         0.5f, 4, GetDeviceColor(n->type));
    DrawText(typeNames[(int)n->type], CANVAS_W() + 20, 56, 11, WHITE);
    DrawText(n->label.c_str(), CANVAS_W() + 16 + bw, 56, 13, WHITE);
    DrawLineEx({(float)CANVAS_W(), 84.0f}, {(float)(CANVAS_W() + PANEL_W), 84.0f},
               1.0f, PANEL_BORDER);

    // Tab header
    Rectangle cfgTab = PnlConfigTabRect();
    Rectangle rteTab = PnlRoutesTabRect();

    bool cfgActive = (ps.activeTab == TAB_CONFIG);
    DrawRectangleRec(cfgTab, cfgActive ? Color{30,41,59,255} : PANEL_BG);
    if (cfgActive)
        DrawLineEx({cfgTab.x, cfgTab.y + cfgTab.height},
                   {cfgTab.x + cfgTab.width, cfgTab.y + cfgTab.height}, 2.0f,
                   Color{59, 130, 246, 255});
    {
        int tw2 = MeasureText("Cfg", 10);
        DrawText("Cfg", (int)(cfgTab.x + (cfgTab.width - tw2) / 2),
                 (int)(cfgTab.y + 8), 10,
                 cfgActive ? WHITE : Color{100, 116, 139, 255});
    }

    bool rteActive = (ps.activeTab == TAB_ROUTES);
    DrawRectangleRec(rteTab, rteActive ? Color{30,41,59,255} : PANEL_BG);
    if (rteActive)
        DrawLineEx({rteTab.x, rteTab.y + rteTab.height},
                   {rteTab.x + rteTab.width, rteTab.y + rteTab.height}, 2.0f,
                   Color{59, 130, 246, 255});
    {
        int tw3 = MeasureText("Rte", 10);
        DrawText("Rte", (int)(rteTab.x + (rteTab.width - tw3) / 2),
                 (int)(rteTab.y + 8), 10,
                 rteActive ? WHITE : Color{100, 116, 139, 255});
    }

    Rectangle arpTab    = PnlArpTabRect();
    bool      arpActive = (ps.activeTab == TAB_ARP);
    DrawRectangleRec(arpTab, arpActive ? Color{30,41,59,255} : PANEL_BG);
    if (arpActive)
        DrawLineEx({arpTab.x, arpTab.y + arpTab.height},
                   {arpTab.x + arpTab.width, arpTab.y + arpTab.height}, 2.0f,
                   Color{59, 130, 246, 255});
    {
        int tw4 = MeasureText("ARP", 12);
        DrawText("ARP", (int)(arpTab.x + (arpTab.width - tw4) / 2),
                 (int)(arpTab.y + 7), 12,
                 arpActive ? WHITE : Color{100, 116, 139, 255});
    }

    Rectangle ospfTab    = PnlOspfTabRect();
    bool      ospfActive = (ps.activeTab == TAB_OSPF);
    DrawRectangleRec(ospfTab, ospfActive ? Color{30,41,59,255} : PANEL_BG);
    if (ospfActive)
        DrawLineEx({ospfTab.x, ospfTab.y + ospfTab.height},
                   {ospfTab.x + ospfTab.width, ospfTab.y + ospfTab.height}, 2.0f,
                   Color{59, 130, 246, 255});
    {
        int twO = MeasureText("OSPF", 12);
        DrawText("OSPF", (int)(ospfTab.x + (ospfTab.width - twO) / 2),
                 (int)(ospfTab.y + 7), 12,
                 ospfActive ? WHITE : Color{100, 116, 139, 255});
    }

    Rectangle mplsTab    = PnlMplsTabRect();
    bool      mplsActive = (ps.activeTab == TAB_MPLS);
    DrawRectangleRec(mplsTab, mplsActive ? Color{30,41,59,255} : PANEL_BG);
    if (mplsActive)
        DrawLineEx({mplsTab.x, mplsTab.y + mplsTab.height},
                   {mplsTab.x + mplsTab.width, mplsTab.y + mplsTab.height}, 2.0f,
                   Color{59, 130, 246, 255});
    {
        int twM = MeasureText("MPLS", 12);
        DrawText("MPLS", (int)(mplsTab.x + (mplsTab.width - twM) / 2),
                 (int)(mplsTab.y + 7), 12,
                 mplsActive ? WHITE : Color{100, 116, 139, 255});
    }

    Rectangle bgpTab    = PnlBgpTabRect();
    bool      bgpActive = (ps.activeTab == TAB_BGP);
    DrawRectangleRec(bgpTab, bgpActive ? Color{30,41,59,255} : PANEL_BG);
    if (bgpActive)
        DrawLineEx({bgpTab.x, bgpTab.y + bgpTab.height},
                   {bgpTab.x + bgpTab.width, bgpTab.y + bgpTab.height}, 2.0f,
                   Color{34, 197, 94, 255});
    {
        int twB = MeasureText("BGP", 11);
        DrawText("BGP", (int)(bgpTab.x + (bgpTab.width - twB) / 2),
                 (int)(bgpTab.y + 7), 11,
                 bgpActive ? Color{34,197,94,255} : Color{100, 116, 139, 255});
    }

    Rectangle vlanTab    = PnlVlanTabRect();
    bool      vlanActive = (ps.activeTab == TAB_VLAN);
    DrawRectangleRec(vlanTab, vlanActive ? Color{30,41,59,255} : PANEL_BG);
    if (vlanActive)
        DrawLineEx({vlanTab.x, vlanTab.y + vlanTab.height},
                   {vlanTab.x + vlanTab.width, vlanTab.y + vlanTab.height}, 2.0f,
                   Color{245, 158, 11, 255});
    {
        int twV = MeasureText("VLN", 10);
        DrawText("VLN", (int)(vlanTab.x + (vlanTab.width - twV) / 2),
                 (int)(vlanTab.y + 8), 10,
                 vlanActive ? Color{245,158,11,255} : Color{100, 116, 139, 255});
    }

    Rectangle subTab    = PnlSubTabRect();
    bool      subActive = (ps.activeTab == TAB_SUB);
    DrawRectangleRec(subTab, subActive ? Color{30,41,59,255} : PANEL_BG);
    if (subActive)
        DrawLineEx({subTab.x, subTab.y + subTab.height},
                   {subTab.x + subTab.width, subTab.y + subTab.height}, 2.0f,
                   Color{234, 88, 12, 255});
    {
        int twS = MeasureText("Sub", 10);
        DrawText("Sub", (int)(subTab.x + (subTab.width - twS) / 2),
                 (int)(subTab.y + 8), 10,
                 subActive ? Color{234, 88, 12, 255} : Color{100, 116, 139, 255});
    }

    Rectangle vxlTab    = PnlVxlanTabRect();
    bool      vxlActive = (ps.activeTab == TAB_VXLAN);
    DrawRectangleRec(vxlTab, vxlActive ? Color{30,41,59,255} : PANEL_BG);
    if (vxlActive)
        DrawLineEx({vxlTab.x, vxlTab.y + vxlTab.height},
                   {vxlTab.x + vxlTab.width, vxlTab.y + vxlTab.height}, 2.0f,
                   Color{20, 184, 166, 255});
    {
        int twX = MeasureText("VXL", 10);
        DrawText("VXL", (int)(vxlTab.x + (vxlTab.width - twX) / 2),
                 (int)(vxlTab.y + 8), 10,
                 vxlActive ? Color{20,184,166,255} : Color{100, 116, 139, 255});
    }

        // ACL tab
        Rectangle aclTab    = PnlAclTabRect();
        bool      aclActive = (ps.activeTab == TAB_ACL);
        DrawRectangleRec(aclTab, aclActive ? Color{30,41,59,255} : PANEL_BG);
        if (aclActive)
            DrawLineEx({aclTab.x, aclTab.y + aclTab.height},
                       {aclTab.x + aclTab.width, aclTab.y + aclTab.height}, 2.0f,
                       Color{59, 130, 246, 255});
        {
            int tw = MeasureText("ACL", 10);
            DrawText("ACL", (int)(aclTab.x + (aclTab.width - tw) / 2),
                     (int)(aclTab.y + 8), 10,
                     aclActive ? Color{239,68,68,255} : Color{100,116,139,255});
        }

        // NAT tab
        Rectangle natTab    = PnlNatTabRect();
        bool      natActive = (ps.activeTab == TAB_NAT);
        DrawRectangleRec(natTab, natActive ? Color{30,41,59,255} : PANEL_BG);
        if (natActive)
            DrawLineEx({natTab.x, natTab.y + natTab.height},
                       {natTab.x + natTab.width, natTab.y + natTab.height}, 2.0f,
                       Color{59, 130, 246, 255});
        {
            int tw = MeasureText("NAT", 10);
            DrawText("NAT", (int)(natTab.x + (natTab.width - tw) / 2),
                     (int)(natTab.y + 8), 10,
                     natActive ? Color{234,179,8,255} : Color{100,116,139,255});
        }

    DrawLineEx({(float)CANVAS_W(), 116.0f}, {(float)(CANVAS_W() + PANEL_W), 116.0f},
               1.0f, PANEL_BORDER);

    // Tab content
    if (ps.activeTab == TAB_CONFIG)
        DrawConfigTab(n, ps);
    else if (ps.activeTab == TAB_ROUTES)
        DrawRoutesTab(n, ps);
    else if (ps.activeTab == TAB_ARP)
        DrawArpTab(n);
    else if (ps.activeTab == TAB_OSPF)
        DrawOspfTab(n);
    else if (ps.activeTab == TAB_MPLS)
        DrawMplsTab(n);
    else if (ps.activeTab == TAB_BGP)
        DrawBgpTab(n, ps);
    else if (ps.activeTab == TAB_VLAN)
        DrawVlanTab(n, ps);
    else if (ps.activeTab == TAB_SUB) DrawSubIfaceTab(n, ps);
    else if (ps.activeTab == TAB_VXLAN) DrawVxlanTab(n, ps);
    else if (ps.activeTab == TAB_ACL)  DrawAclTab(n, ps);
    else if (ps.activeTab == TAB_NAT)  DrawNatTab(n, ps);
}

// ── Context menu draw ────────────────────────────────────────────────────
void DrawContextMenu(const ContextMenu& menu, Vector2 screenMouse) {
    (void)screenMouse;
    if (!menu.visible) return;

    static const char* nodeItemsNormal[]  = {"Rename", "Crash Device", "Delete",
                                              "Send Packet To\xe2\x80\xa6", nullptr};
    static const char* nodeItemsCrashed[] = {"Rename", "Restore Device", "Delete",
                                              "Send Packet To\xe2\x80\xa6", nullptr};
    static const char* cableItemsNormal[] = {"Cut Link", "Delete Cable", nullptr};
    static const char* cableItemsBroken[] = {"Restore Link", "Delete Cable", nullptr};
    static const char* canvasItems[]      = {"Add PC Here", "Add Router Here",
                                             "Add Switch Here", "Reset View", nullptr};

    const char** items = nullptr;
    if      (menu.ctx == CTX_NODE)   items = menu.targetBroken ? nodeItemsCrashed : nodeItemsNormal;
    else if (menu.ctx == CTX_CABLE)  items = menu.targetBroken ? cableItemsBroken : cableItemsNormal;
    else if (menu.ctx == CTX_CANVAS) items = canvasItems;
    else return;

    int count = 0;
    while (items[count]) ++count;

    float h = (float)(count * MENU_ITEM_H + 8);
    float x = std::min(menu.screenPos.x, (float)(CANVAS_W() - CONTEXT_MENU_W - 4));
    float y = std::min(menu.screenPos.y, (float)(CANVAS_H() - (int)h - 4));

    DrawRectangleRounded({x, y, (float)CONTEXT_MENU_W, h}, 0.08f, 4, Color{30, 41, 59, 255});
    DrawRectangleRoundedLinesEx({x, y, (float)CONTEXT_MENU_W, h}, 0.08f, 4, 1.0f, PANEL_BORDER);

    for (int i = 0; i < count; ++i) {
        Rectangle ir = {x + 4, y + 4 + (float)(i * MENU_ITEM_H),
                        (float)(CONTEXT_MENU_W - 8), (float)MENU_ITEM_H};
        if (menu.hoverItem == i)
            DrawRectangleRounded(ir, 0.08f, 4, Color{51, 65, 85, 255});
        DrawText(items[i], (int)ir.x + 8, (int)ir.y + 7, 13, WHITE);
    }
}
