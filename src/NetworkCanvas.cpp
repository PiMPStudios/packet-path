#include "NetworkCanvas.h"

void DrawDeviceNode(const DeviceNode& n) {
    Rectangle r = GetNodeRect(n);
    Color     c = GetDeviceColor(n.type);
    DrawRectangleRounded({r.x + 3, r.y + 3, r.width, r.height}, 0.3f, 8,
                         Color{0, 0, 0, 80});
    DrawRectangleRounded(r, 0.3f, 8, c);
    if (n.selected)
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, 2.5f, WHITE);
    int tw = MeasureText(n.label.c_str(), NODE_FONT_SZ);
    DrawText(n.label.c_str(),
             (int)(n.position.x - tw / 2.0f),
             (int)(n.position.y - NODE_FONT_SZ / 2.0f),
             NODE_FONT_SZ, WHITE);
    for (int i = 0; i < PORTS_PER_NODE; ++i) {
        Vector2 pp = GetPortPosition(n, i);
        DrawCircleV(pp, PORT_RADIUS,        Color{51,  65,  85, 255});
        DrawCircleV(pp, PORT_RADIUS - 2.0f, Color{100, 116, 139, 255});
    }
}

// ── Dot-grid background (drawn inside BeginMode2D) ────────────────────────
void DrawDotGrid(const Camera2D& cam) {
    float spacing = 40.0f;
    Color dot     = {30, 41, 59, 255};

    Vector2 topLeft  = GetScreenToWorld2D({0.0f, 0.0f}, cam);
    Vector2 botRight = GetScreenToWorld2D({(float)CANVAS_W, (float)CANVAS_H}, cam);

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

    // Green glow (outer) + core dot — always green during travel
    DrawCircleV(pos, 14.f, Color{34, 197, 94, 55});
    DrawCircleV(pos, 7.f,  Color{34, 197, 94, 255});
}

// ── Log console (drawn outside BeginMode2D, full-width bottom strip) ─────
void DrawLogConsole(const std::vector<LogEntry>& entries) {
    DrawRectangle(0, CANVAS_H, SCREEN_W, LOG_H, Color{10, 15, 28, 255});
    DrawLineEx({0.f, (float)CANVAS_H}, {(float)SCREEN_W, (float)CANVAS_H},
               1.f, Color{51, 65, 85, 255});
    DrawText("LOG", 12, CANVAS_H + 8, 9, Color{71, 85, 105, 255});

    if (entries.empty()) {
        DrawText("No simulations run yet", 36, CANVAS_H + 36, 10,
                 Color{51, 65, 85, 255});
        return;
    }

    int maxLines = 3;
    int startIdx = std::max(0, (int)entries.size() - maxLines);
    int shown    = std::min(maxLines, (int)entries.size());
    for (int i = 0; i < shown; ++i) {
        const auto& e = entries[startIdx + i];
        int lineY = CANVAS_H + 8 + (shown - 1 - i) * 24;  // newest at top

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

void DrawConfigTab(const DeviceNode* n, const PanelState& ps) {
    DrawText("GENERAL", CANVAS_W + 12, 124, 10, Color{100, 116, 139, 255});
    DrawTextField(PnlFieldRect(CFG_HOSTNAME_Y), "Hostname", nullptr,
                  n->label, ps.activeField == 0, !n->label.empty());
    DrawTextField(PnlFieldRect(CFG_MGMTIP_Y), "Mgmt IP", "x.x.x.x/xx",
                  n->mgmtIp, ps.activeField == 1, ValidateIP(n->mgmtIp));
    DrawLineEx({(float)CANVAS_W,           (float)CFG_IFACE_SEP_Y},
               {(float)(CANVAS_W+PANEL_W), (float)CFG_IFACE_SEP_Y},
               1.0f, PANEL_BORDER);
    DrawText("INTERFACES", CANVAS_W + 12, CFG_IFACE_SEP_Y + 10, 10, Color{100, 116, 139, 255});
    for (int i = 0; i < PORTS_PER_NODE; ++i) {
        std::string pname = GetPortName(n->type, i);
        DrawTextField(PnlPortFieldRect(i), "", "x.x.x.x/xx",
                      n->portIp[i], ps.activeField == 2 + i, ValidateIP(n->portIp[i]));
        DrawText(pname.c_str(), CANVAS_W + 16, CFG_PORT_Y0 + i * CFG_PORT_STRIDE + 7,
                 11, Color{148, 163, 184, 255});
        DrawText("A:", CANVAS_W + 210, CFG_PORT_Y0 + i * CFG_PORT_STRIDE + 7,
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
    DrawText("T", CANVAS_W + 12, 124, 10, Color{100, 116, 139, 255});
    DrawText("DESTINATION",  CANVAS_W + 30, 124, 10, Color{100, 116, 139, 255});
    DrawText("NEXT-HOP",     CANVAS_W + 130, 124, 10, Color{100, 116, 139, 255});
    DrawText("VIA",          CANVAS_W + 210, 124, 10, Color{100, 116, 139, 255});
    DrawLineEx({(float)CANVAS_W, (float)RTE_HEADER_SEP_Y}, {(float)(CANVAS_W+PANEL_W), (float)RTE_HEADER_SEP_Y},
               1.0f, PANEL_BORDER);

    auto table = GetRoutingTable(*n);

    if (table.empty()) {
        DrawText("No routes configured", CANVAS_W + 20, RTE_ROW_Y0, 11,
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
            else                               rowColor = Color{ 59, 130, 246, 255};

            if (r.src == ROUTE_OSPF_IA) {
                DrawText("O",  CANVAS_W + 12, ry + 3, 11, rowColor);
                DrawText("IA", CANVAS_W + 21, ry + 5,  9, rowColor);
            } else {
                const char* typeLetter = (r.src == ROUTE_CONNECTED) ? "C"
                                       : (r.src == ROUTE_OSPF)      ? "O" : "S";
                DrawText(typeLetter, CANVAS_W + 12, ry + 3, 11, rowColor);
            }

            // Destination
            DrawText(r.dest.c_str(), CANVAS_W + 30, ry + 3, 10, rowColor);

            // Next-hop
            DrawText(r.nextHop.c_str(), CANVAS_W + 130, ry + 3, 10, rowColor);

            // Via (port name or em-dash for mgmt/static)
            if (r.outPort >= 0) {
                std::string via = GetPortName(n->type, r.outPort);
                DrawText(via.c_str(), CANVAS_W + 210, ry + 3, 10, rowColor);
            } else {
                DrawText("\xe2\x80\x94", CANVAS_W + 210, ry + 3, 10, rowColor);
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
    DrawLineEx({(float)CANVAS_W,           (float)RTE_ADD_SEP_Y},
               {(float)(CANVAS_W+PANEL_W), (float)RTE_ADD_SEP_Y},
               1.0f, PANEL_BORDER);
    DrawText("ADD STATIC ROUTE", CANVAS_W + 12, RTE_ADD_SEP_Y + 8, 10,
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
    DrawText("ARP CACHE", CANVAS_W + 12, 124, 10, Color{100, 116, 139, 255});

    if (n->arpTable.empty()) {
        DrawText("(empty)", CANVAS_W + 12, 148, 11, Color{51, 65, 85, 255});
        DrawText("Run a simulation to populate the cache.",
                 CANVAS_W + 12, 164, 10, Color{51, 65, 85, 255});
        return;
    }

    DrawText("IP ADDRESS",  CANVAS_W + 12,  142, 9, Color{100, 116, 139, 255});
    DrawText("MAC ADDRESS", CANVAS_W + 138, 142, 9, Color{100, 116, 139, 255});
    DrawLineEx({(float)CANVAS_W,           156.0f},
               {(float)(CANVAS_W+PANEL_W), 156.0f}, 0.5f, PANEL_BORDER);

    int y = 162;
    for (const auto& [ip, mac] : n->arpTable) {
        if (y > CANVAS_H - LOG_H - 20) break;
        DrawText(ip.c_str(),  CANVAS_W + 12,  y, 10, Color{34, 197, 94, 255});
        DrawText(mac.c_str(), CANVAS_W + 138, y, 9,  Color{148, 163, 184, 255});
        y += 18;
    }
}

void DrawOspfTab(const DeviceNode* n) {
    if (!n) {
        DrawText("No device selected", CANVAS_W + 20, 130, 12, Color{100,116,139,255});
        return;
    }
    if (n->type != ROUTER) {
        DrawText("OSPF: routers only", CANVAS_W + 20, 130, 12, Color{100,116,139,255});
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
    DrawText("Router ID", CANVAS_W + 12, y, 11, Color{100,116,139,255});
    DrawText(n->routerId.empty() ? "(none)" : n->routerId.c_str(),
             CANVAS_W + 90, y, 11, WHITE);
    y += 20;

    // ABR badge or single-area display
    if (IsAbr(*n)) {
        DrawRectangleRounded({(float)(CANVAS_W + 90), (float)y, 34.0f, 16.0f},
                             0.5f, 4, Color{139, 92, 246, 255});
        DrawText("ABR", CANVAS_W + 95, y + 3, 10, WHITE);
    } else {
        uint32_t displayArea = 0;
        for (int i = 0; i < PORTS_PER_NODE; ++i)
            if (ValidateIP(n->portIp[i])) { displayArea = n->ospfPortArea[i]; break; }
        DrawText("Area", CANVAS_W + 12, y, 11, Color{100,116,139,255});
        DrawText(std::to_string(displayArea).c_str(), CANVAS_W + 90, y, 11, WHITE);
    }
    y += 24;

    // Separator
    DrawLineEx({(float)CANVAS_W, (float)y}, {(float)(CANVAS_W + PANEL_W), (float)y},
               1.0f, PANEL_BORDER);
    y += 6;
    DrawText("Neighbors", CANVAS_W + 12, y, 11, Color{100,116,139,255});
    y += 18;

    if (n->ospfNeighbors.empty()) {
        DrawText("(none)", CANVAS_W + 20, y, 11, Color{71,85,105,255});
        return;
    }

    // Header row — Area column added between State and Dead
    DrawText("Router-ID",   CANVAS_W + 12,  y, 10, Color{71,85,105,255});
    DrawText("State",       CANVAS_W + 110, y, 10, Color{71,85,105,255});
    DrawText("Area",        CANVAS_W + 175, y, 10, Color{71,85,105,255});
    DrawText("Dead",        CANVAS_W + 218, y, 10, Color{71,85,105,255});
    y += 14;

    static const char* stateNames[] = { "DOWN", "INIT", "2WAY", "FULL" };
    static const Color stateColors[] = {
        {100,116,139,255}, {234,179,8,255}, {59,130,246,255}, {34,197,94,255}
    };

    for (const auto& nbr : n->ospfNeighbors) {
        if (y > CANVAS_H - 20) break;
        std::string rid = nbr.neighborRouterId;
        if ((int)rid.size() > 11) rid = rid.substr(rid.size() - 11);
        DrawText(rid.c_str(), CANVAS_W + 12, y, 10, WHITE);

        int si = std::clamp((int)nbr.state, 0, 3);
        DrawText(stateNames[si], CANVAS_W + 110, y, 10, stateColors[si]);

        DrawText(std::to_string(nbr.area).c_str(), CANVAS_W + 175, y, 10,
                 Color{148, 163, 184, 255});

        char deadBuf[8];
        std::snprintf(deadBuf, sizeof(deadBuf), "%.1fs", nbr.deadTimer);
        DrawText(deadBuf, CANVAS_W + 218, y, 10, Color{148,163,184,255});
        y += 16;
    }
}

void DrawMplsTab(const DeviceNode* n) {
    if (!n) {
        DrawText("No device selected", CANVAS_W + 20, 130, 12, Color{100,116,139,255});
        return;
    }
    if (n->type != ROUTER) {
        DrawText("MPLS: routers only", CANVAS_W + 20, 130, 12, Color{100,116,139,255});
        return;
    }
    DrawText("(MPLS - enable in Task 5)", CANVAS_W + 20, 130, 10, Color{71,85,105,255});
}

void DrawPanel(int selectedId, const std::vector<DeviceNode>& nodes,
               const PanelState& ps)
{
    DrawRectangle(CANVAS_W, 0, PANEL_W, SCREEN_H, PANEL_BG);
    DrawLineEx({(float)CANVAS_W, 0.0f}, {(float)CANVAS_W, (float)SCREEN_H},
               1.0f, PANEL_BORDER);
    DrawText("CONFIGURATION", CANVAS_W + 12, 14, 10, Color{100, 116, 139, 255});
    DrawLineEx({(float)CANVAS_W, 38.0f}, {(float)(CANVAS_W + PANEL_W), 38.0f},
               1.0f, PANEL_BORDER);

    if (selectedId == -1) {
        const char* msg = "<- Select a device";
        int tw = MeasureText(msg, 13);
        DrawText(msg, CANVAS_W + (PANEL_W - tw) / 2, SCREEN_H / 2 - 8, 13,
                 Color{100, 116, 139, 255});
        return;
    }

    const DeviceNode* n = FindNode(nodes, selectedId);
    if (!n) return;

    // Device type badge
    const char* typeNames[] = {"PC", "Router", "Switch"};
    int bw = MeasureText(typeNames[(int)n->type], 11) + 16;
    DrawRectangleRounded({(float)(CANVAS_W + 12), 50.0f, (float)bw, 22.0f},
                         0.5f, 4, GetDeviceColor(n->type));
    DrawText(typeNames[(int)n->type], CANVAS_W + 20, 56, 11, WHITE);
    DrawText(n->label.c_str(), CANVAS_W + 16 + bw, 56, 13, WHITE);
    DrawLineEx({(float)CANVAS_W, 84.0f}, {(float)(CANVAS_W + PANEL_W), 84.0f},
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
        int tw2 = MeasureText("Config", 12);
        DrawText("Config", (int)(cfgTab.x + (cfgTab.width - tw2) / 2),
                 (int)(cfgTab.y + 7), 12,
                 cfgActive ? WHITE : Color{100, 116, 139, 255});
    }

    bool rteActive = (ps.activeTab == TAB_ROUTES);
    DrawRectangleRec(rteTab, rteActive ? Color{30,41,59,255} : PANEL_BG);
    if (rteActive)
        DrawLineEx({rteTab.x, rteTab.y + rteTab.height},
                   {rteTab.x + rteTab.width, rteTab.y + rteTab.height}, 2.0f,
                   Color{59, 130, 246, 255});
    {
        int tw3 = MeasureText("Routes", 12);
        DrawText("Routes", (int)(rteTab.x + (rteTab.width - tw3) / 2),
                 (int)(rteTab.y + 7), 12,
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

    DrawLineEx({(float)CANVAS_W, 116.0f}, {(float)(CANVAS_W + PANEL_W), 116.0f},
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
}

// ── Context menu draw ────────────────────────────────────────────────────
void DrawContextMenu(const ContextMenu& menu, Vector2 screenMouse) {
    (void)screenMouse;
    if (!menu.visible) return;

    static const char* nodeItems[]   = {"Rename", "Delete", "Send Packet To\xe2\x80\xa6", nullptr};
    static const char* cableItems[]  = {"Delete Cable", nullptr};
    static const char* canvasItems[] = {"Add PC Here", "Add Router Here",
                                        "Add Switch Here", "Reset View", nullptr};

    const char** items = nullptr;
    if      (menu.ctx == CTX_NODE)   items = nodeItems;
    else if (menu.ctx == CTX_CABLE)  items = cableItems;
    else if (menu.ctx == CTX_CANVAS) items = canvasItems;
    else return;

    int count = 0;
    while (items[count]) ++count;

    float h = (float)(count * MENU_ITEM_H + 8);
    float x = std::min(menu.screenPos.x, (float)(CANVAS_W - CONTEXT_MENU_W - 4));
    float y = std::min(menu.screenPos.y, (float)(CANVAS_H - (int)h - 4));

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
