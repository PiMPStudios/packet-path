#include "NetworkCanvas.h"
#include "Font.h"

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
        float xw = TW(xmark, 11);
        DrawTextEx(GFont(), xmark, {(float)(int)bx - xw * 0.5f, (float)(int)(by - 6.f)}, FS(11), Sp(FS(11)), WHITE);
    }
    int tw = (int)TW(n.label.c_str(), NODE_FONT_SZ);
    DrawTextEx(GFont(), n.label.c_str(),
               {(float)(int)(n.position.x - tw / 2.0f),
                (float)(int)(n.position.y - NODE_FONT_SZ / 2.0f)},
               FS(NODE_FONT_SZ), Sp(FS(NODE_FONT_SZ)), WHITE);
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
            int lw = (int)TW(lbl, 8);
            DrawTextEx(GFont(), lbl, {(float)(int)(pp.x - lw * 0.5f), (float)(int)(pp.y + PORT_RADIUS + 2)}, FS(8), Sp(FS(8)), lblCol);
        }
        if (n.type == ROUTER) {
            int subY = (int)(pp.y + PORT_RADIUS + 2);
            for (const auto& si : n.subIfaces) {
                if (si.parentPort != i) continue;
                char lbl[8];
                std::snprintf(lbl, sizeof(lbl), ".%d", si.vlanId);
                int lw = (int)TW(lbl, 8);
                DrawTextEx(GFont(), lbl, {(float)(int)(pp.x - lw * 0.5f), (float)subY},
                           FS(8), Sp(FS(8)), Color{249, 115, 22, 255});
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
            float xw = TW(xmark, 9);
            DrawTextEx(GFont(), xmark, {(float)(int)mid.x - xw * 0.5f, (float)(int)(mid.y - 5.f)}, FS(9), Sp(FS(9)), WHITE);
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

// ── TE tunnel overlays (drawn in world space after cables) ────────────────
void DrawTeTunnelOverlays(const std::vector<DeviceNode>& nodes,
                          const std::vector<Cable>&      cables)
{
    static const Color kTePalette[] = {
        {251, 191,  36, 200},   // amber
        { 34, 211, 238, 200},   // cyan
        {232, 121, 249, 200},   // magenta
        {163, 230,  53, 200},   // lime
        {251, 113, 133, 200},   // rose
        { 56, 189, 248, 200},   // sky
    };
    static const int kPaletteSize = 6;

    int tunnelColorIdx = 0;
    for (const auto& n : nodes) {
        if (!n.rsvpEnabled) continue;
        for (const auto& t : n.teTunnels) {
            if (!t.isUp || t.activePath.size() < 2) { ++tunnelColorIdx; continue; }
            Color col = kTePalette[tunnelColorIdx % kPaletteSize];
            ++tunnelColorIdx;

            for (size_t i = 0; i + 1 < t.activePath.size(); ++i) {
                int aId = t.activePath[i];
                int bId = t.activePath[i + 1];

                const DeviceNode* nodeA = FindNode(nodes, aId);
                const DeviceNode* nodeB = FindNode(nodes, bId);
                if (!nodeA || !nodeB) continue;

                // Find the cable between aId and bId and resolve port indices
                const Cable* cab = FindCable(cables, aId, bId);
                if (!cab) continue;

                int aPort = (cab->fromId == aId) ? cab->fromPort : cab->toPort;
                int bPort = (cab->fromId == bId) ? cab->fromPort : cab->toPort;

                Vector2 p0 = GetPortPosition(*nodeA, aPort);
                Vector2 p3 = GetPortPosition(*nodeB, bPort);
                Vector2 c1 = BezierCtrl(p0, aPort);
                Vector2 c2 = BezierCtrl(p3, bPort);

                // Perpendicular offset to separate stacked tunnels
                float   off  = (float)(((tunnelColorIdx - 1) % 3) - 1) * 3.0f;
                Vector2 dir  = {p3.x - p0.x, p3.y - p0.y};
                float   len  = sqrtf(dir.x * dir.x + dir.y * dir.y);
                Vector2 perp = (len > 0.001f) ? Vector2{-dir.y / len, dir.x / len}
                                              : Vector2{0.f, 0.f};
                Vector2 o    = {perp.x * off, perp.y * off};

                Vector2 op0 = {p0.x + o.x, p0.y + o.y};
                Vector2 op3 = {p3.x + o.x, p3.y + o.y};
                Vector2 oc1 = {c1.x + o.x, c1.y + o.y};
                Vector2 oc2 = {c2.x + o.x, c2.y + o.y};

                DrawSplineSegmentBezierCubic(op0, oc1, oc2, op3, 3.0f, col);

                // Midpoint label badge — cubic Bezier at t=0.5:
                // B(0.5) = (1/8)(p0 + 3*c1 + 3*c2 + p3)
                Vector2 mid = {
                    (op0.x + 3.0f * oc1.x + 3.0f * oc2.x + op3.x) * 0.125f,
                    (op0.y + 3.0f * oc1.y + 3.0f * oc2.y + op3.y) * 0.125f
                };
                char badge[32];
                std::snprintf(badge, sizeof(badge), "T%d.%uM", t.id, t.bandwidth);
                float bw = TW(badge, 9) + 8.0f;
                DrawRectangleRounded({mid.x - bw * 0.5f, mid.y - 9.0f, bw, 14.0f},
                                     0.5f, 4, Color{15, 23, 42, 210});
                DrawTextEx(GFont(), badge,
                           {mid.x - bw * 0.5f + 4.0f, mid.y - 7.0f},
                           FS(9), Sp(FS(9)), col);
            }
        }
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
        DrawTextEx(GFont(), topLabel, {(float)(int)r.x, (float)((int)r.y - 16)}, FS(11), Sp(FS(11)), Color{100, 116, 139, 255});
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
            DrawTextEx(GFont(), placeholder, {(float)tx, (float)ty}, FS(12), Sp(FS(12)), Color{51, 65, 85, 255});
        if (active && std::fmod(GetTime(), 1.0) < 0.5)
            DrawRectangle(tx, (int)r.y + 4, 2, (int)r.height - 8, WHITE);
    } else {
        int start = 0;
        while (start < (int)value.size() &&
               (int)TW(value.c_str() + start, 12) > maxW)
            ++start;
        DrawTextEx(GFont(), value.c_str() + start, {(float)tx, (float)ty}, FS(12), Sp(FS(12)), WHITE);
        if (active && std::fmod(GetTime(), 1.0) < 0.5) {
            int curX = tx + (int)TW(value.c_str() + start, 12);
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
        int   lw = (int)TW(lbuf, 10) + 10;
        float bx = pos.x - lw * 0.5f;
        float by = pos.y - 30.f;
        DrawRectangleRounded({bx, by, (float)lw, 16.f}, 0.5f, 4, Color{249, 115, 22, 220});
        DrawTextEx(GFont(), lbuf, {(float)(int)(bx + 5.f), (float)(int)(by + 3.f)}, FS(10), Sp(FS(10)), WHITE);
    }

    // VLAN badge — blue pill above packet (stacks above MPLS badge if both present)
    if (anim.currentVlan != 0) {
        char vbuf[10];
        std::snprintf(vbuf, sizeof(vbuf), "V%d", anim.currentVlan);
        int   vw = (int)TW(vbuf, 10) + 10;
        float bx = pos.x - vw * 0.5f;
        float by = pos.y - 30.f;
        if (anim.currentLabel != 0) by -= 20.f;
        DrawRectangleRounded({bx, by, (float)vw, 16.f}, 0.5f, 4, Color{59, 130, 246, 220});
        DrawTextEx(GFont(), vbuf, {(float)(int)(bx + 5.f), (float)(int)(by + 3.f)}, FS(10), Sp(FS(10)), WHITE);
    }

    // VNI badge — teal pill, stacks above existing badges
    if (anim.currentVni != 0) {
        char vnibuf[16];
        std::snprintf(vnibuf, sizeof(vnibuf), "VNI:%u", anim.currentVni);
        int   nw = (int)TW(vnibuf, 10) + 10;
        float bx = pos.x - nw * 0.5f;
        float by = pos.y - 30.f;
        if (anim.currentLabel != 0) by -= 20.f;
        if (anim.currentVlan  != 0) by -= 20.f;
        DrawRectangleRounded({bx, by, (float)nw, 16.f}, 0.5f, 4, Color{20, 184, 166, 220});
        DrawTextEx(GFont(), vnibuf, {(float)(int)(bx + 5.f), (float)(int)(by + 3.f)}, FS(10), Sp(FS(10)), WHITE);
    }

    // Core dot color: use overrideColor if set (alpha != 0), else default green
    Color core  = (anim.overrideColor.a != 0) ? anim.overrideColor
                                               : Color{34, 197, 94, 255};
    Color glow  = {core.r, core.g, core.b, 55};
    DrawCircleV(pos, 14.f, glow);
    DrawCircleV(pos, 7.f,  core);
}

// ── Log console (drawn outside BeginMode2D, full-width bottom strip) ─────
void DrawLogConsole(const std::vector<LogEntry>& entries, int scrollOffset) {
    DrawRectangle(0, CANVAS_H(), SCREEN_W(), LOG_H, Color{10, 15, 28, 255});
    DrawLineEx({0.f, (float)CANVAS_H()}, {(float)SCREEN_W(), (float)CANVAS_H()},
               1.f, Color{51, 65, 85, 255});
    DrawTextEx(GFont(), "LOG", {(float)12, (float)(CANVAS_H() + 8)}, FS(9), Sp(FS(9)), Color{71, 85, 105, 255});

    // Scroll-back indicator: shown when not at the newest entries
    if (scrollOffset > 0) {
        char ibuf[32];
        std::snprintf(ibuf, sizeof(ibuf), "^ %d newer", scrollOffset);
        float iw = TW(ibuf, 9);
        DrawTextEx(GFont(), ibuf,
                   {(float)CANVAS_W() - iw - 12.f, (float)(CANVAS_H() + 6)},
                   FS(9), Sp(FS(9)), Color{148, 163, 184, 200});
    }

    if (entries.empty()) {
        DrawTextEx(GFont(), "No simulations run yet", {(float)36, (float)(CANVAS_H() + 36)}, FS(10), Sp(FS(10)),
                   Color{51, 65, 85, 255});
        return;
    }

    int startIdx = std::max(0, (int)entries.size() - LOG_MAX_LINES - scrollOffset);
    int shown    = std::min(LOG_MAX_LINES, std::max(0, (int)entries.size() - scrollOffset));
    if (shown <= 0) return;

    for (int i = 0; i < shown; ++i) {
        const auto& e = entries[startIdx + i];
        int lineY = CANVAS_H() + 8 + (shown - 1 - i) * 24;  // newest at top

        int   secs = (int)e.timestamp;
        int   mins = (secs / 60) % 60; secs %= 60;
        char  tsbuf[16];
        std::snprintf(tsbuf, sizeof(tsbuf), "[%02d:%02d]", mins, secs);
        DrawTextEx(GFont(), tsbuf, {(float)36, (float)lineY}, FS(10), Sp(FS(10)), Color{71, 85, 105, 255});

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
        DrawTextEx(GFont(), icon, {(float)90, (float)lineY}, FS(10), Sp(FS(10)), icColor);

        std::string msg = e.pathStr;
        if (!e.reason.empty()) msg += "  \xe2\x80\x94  " + e.reason;
        DrawTextEx(GFont(), msg.c_str(), {(float)108, (float)lineY}, FS(10), Sp(FS(10)), icColor);
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
    float xw = TW(xmark, 11);
    DrawTextEx(GFont(), xmark, {(float)(int)bx - xw * 0.5f, (float)(int)(by - 6.f)}, FS(11), Sp(FS(11)), WHITE);
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
        float tw = TW(txt, 9);
        DrawRectangle((int)((float)mid.x - tw * 0.5f - 3.f), (int)(mid.y + 10), (int)(tw + 6.f), 14,
                      Color{239, 68, 68, 200});
        DrawTextEx(GFont(), txt, {(float)mid.x - tw * 0.5f, (float)(int)(mid.y + 12)}, FS(9), Sp(FS(9)), WHITE);
    }

    for (const auto& n : nodes) {
        if (!n.crashed) continue;
        const char* txt = "CRASHED";
        float tw = TW(txt, 9);
        float bx = n.position.x;
        int by = (int)(n.position.y + NODE_H / 2.f + 4.f);
        DrawRectangle((int)(bx - tw * 0.5f - 3.f), by, (int)(tw + 6.f), 14, Color{239, 68, 68, 200});
        DrawTextEx(GFont(), txt, {bx - tw * 0.5f, (float)(by + 2)}, FS(9), Sp(FS(9)), WHITE);
    }
}

void DrawConfigTab(const DeviceNode* n, const PanelState& ps) {
    DrawTextEx(GFont(), "GENERAL", {(float)(CANVAS_W() + 12), (float)124}, FS(10), Sp(FS(10)), Color{100, 116, 139, 255});
    DrawTextField(PnlFieldRect(CFG_HOSTNAME_Y), "Hostname", nullptr,
                  n->label, ps.activeField == 0, !n->label.empty());
    DrawTextField(PnlFieldRect(CFG_MGMTIP_Y), "Mgmt IP", "x.x.x.x/xx",
                  n->mgmtIp, ps.activeField == 1, ValidateIP(n->mgmtIp));
    DrawLineEx({(float)CANVAS_W(),           (float)CFG_IFACE_SEP_Y},
               {(float)(CANVAS_W()+PANEL_W), (float)CFG_IFACE_SEP_Y},
               1.0f, PANEL_BORDER);
    DrawTextEx(GFont(), "INTERFACES", {(float)(CANVAS_W() + 12), (float)(CFG_IFACE_SEP_Y + 10)}, FS(10), Sp(FS(10)), Color{100, 116, 139, 255});
    for (int i = 0; i < PORTS_PER_NODE; ++i) {
        std::string pname = GetPortName(n->type, i);
        DrawTextField(PnlPortFieldRect(i), "", "x.x.x.x/xx",
                      n->portIp[i], ps.activeField == 2 + i, ValidateIP(n->portIp[i]));
        DrawTextEx(GFont(), pname.c_str(), {(float)(CANVAS_W() + 16), (float)(CFG_PORT_Y0 + i * CFG_PORT_STRIDE + 7)},
                   FS(11), Sp(FS(11)), Color{148, 163, 184, 255});
        DrawTextEx(GFont(), "A:", {(float)(CANVAS_W() + 210), (float)(CFG_PORT_Y0 + i * CFG_PORT_STRIDE + 7)},
                   FS(10), Sp(FS(10)), Color{148, 163, 184, 255});
        std::string areaStr = (ps.activePortAreaField == i)
            ? ps.portAreaBuf
            : std::to_string(n->ospfPortArea[i]);
        DrawTextField(PnlPortAreaFieldRect(i), "", "0", areaStr,
                      ps.activePortAreaField == i, true);
    }
}

void DrawRoutesTab(const DeviceNode* n, const PanelState& ps) {
    // Column headers
    DrawTextEx(GFont(), "T", {(float)(CANVAS_W() + 12), (float)124}, FS(10), Sp(FS(10)), Color{100, 116, 139, 255});
    DrawTextEx(GFont(), "DESTINATION",  {(float)(CANVAS_W() + 30),  (float)124}, FS(10), Sp(FS(10)), Color{100, 116, 139, 255});
    DrawTextEx(GFont(), "NEXT-HOP",     {(float)(CANVAS_W() + 130), (float)124}, FS(10), Sp(FS(10)), Color{100, 116, 139, 255});
    DrawTextEx(GFont(), "VIA",          {(float)(CANVAS_W() + 210), (float)124}, FS(10), Sp(FS(10)), Color{100, 116, 139, 255});
    DrawLineEx({(float)CANVAS_W(), (float)RTE_HEADER_SEP_Y}, {(float)(CANVAS_W()+PANEL_W), (float)RTE_HEADER_SEP_Y},
               1.0f, PANEL_BORDER);

    auto table = GetRoutingTable(*n);

    if (table.empty()) {
        DrawTextEx(GFont(), "No routes configured", {(float)(CANVAS_W() + 20), (float)RTE_ROW_Y0}, FS(11), Sp(FS(11)),
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
                DrawTextEx(GFont(), "O",  {(float)(CANVAS_W() + 12), (float)(ry + 3)}, FS(11), Sp(FS(11)), rowColor);
                DrawTextEx(GFont(), "IA", {(float)(CANVAS_W() + 21), (float)(ry + 5)}, FS(9),  Sp(FS(9)),  rowColor);
            } else {
                const char* typeLetter = (r.src == ROUTE_CONNECTED) ? "C"
                                       : (r.src == ROUTE_OSPF)      ? "O"
                                       : (r.src == ROUTE_BGP)       ? "B" : "S";
                DrawTextEx(GFont(), typeLetter, {(float)(CANVAS_W() + 12), (float)(ry + 3)}, FS(11), Sp(FS(11)), rowColor);
            }

            // Destination
            DrawTextEx(GFont(), r.dest.c_str(), {(float)(CANVAS_W() + 30), (float)(ry + 3)}, FS(10), Sp(FS(10)), rowColor);

            // Next-hop
            DrawTextEx(GFont(), r.nextHop.c_str(), {(float)(CANVAS_W() + 130), (float)(ry + 3)}, FS(10), Sp(FS(10)), rowColor);

            // Via (port name or em-dash for mgmt/static)
            if (r.outPort >= 0) {
                std::string via = GetPortName(n->type, r.outPort);
                DrawTextEx(GFont(), via.c_str(), {(float)(CANVAS_W() + 210), (float)(ry + 3)}, FS(10), Sp(FS(10)), rowColor);
            } else {
                DrawTextEx(GFont(), "\xe2\x80\x94", {(float)(CANVAS_W() + 210), (float)(ry + 3)}, FS(10), Sp(FS(10)), rowColor);
            }

            // [×] delete button for static routes only
            if (r.src == ROUTE_STATIC) {
                Rectangle delBtn = PnlRouteDeleteRect(i);
                DrawRectangleRounded(delBtn, 0.3f, 4, Color{51, 65, 85, 255});
                DrawTextEx(GFont(), "x", {(float)(int)(delBtn.x + 4), (float)(int)(delBtn.y + 1)}, FS(11), Sp(FS(11)),
                           Color{239, 68, 68, 255});
            }
        }
    }

    // Add-form separator and labels
    DrawLineEx({(float)CANVAS_W(),           (float)RTE_ADD_SEP_Y},
               {(float)(CANVAS_W()+PANEL_W), (float)RTE_ADD_SEP_Y},
               1.0f, PANEL_BORDER);
    DrawTextEx(GFont(), "ADD STATIC ROUTE", {(float)(CANVAS_W() + 12), (float)(RTE_ADD_SEP_Y + 8)}, FS(10), Sp(FS(10)),
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
        int tw = (int)TW("Add Route", 12);
        Rectangle btn = PnlRouteAddBtnRect();
        DrawTextEx(GFont(), "Add Route",
                   {(float)(int)(btn.x + (btn.width - tw) / 2),
                    (float)(int)(btn.y + 8)},
                   FS(12), Sp(FS(12)),
                   canAdd ? WHITE : Color{51, 65, 85, 255});
    }
}

void DrawArpTab(const DeviceNode* n) {
    if (!n) return;
    DrawTextEx(GFont(), "ARP CACHE", {(float)(CANVAS_W() + 12), (float)124}, FS(10), Sp(FS(10)), Color{100, 116, 139, 255});

    if (n->arpTable.empty()) {
        DrawTextEx(GFont(), "(empty)", {(float)(CANVAS_W() + 12), (float)148}, FS(11), Sp(FS(11)), Color{51, 65, 85, 255});
        DrawTextEx(GFont(), "Run a simulation to populate the cache.",
                   {(float)(CANVAS_W() + 12), (float)164}, FS(10), Sp(FS(10)), Color{51, 65, 85, 255});
        return;
    }

    DrawTextEx(GFont(), "IP ADDRESS",  {(float)(CANVAS_W() + 12),  (float)142}, FS(9), Sp(FS(9)), Color{100, 116, 139, 255});
    DrawTextEx(GFont(), "MAC ADDRESS", {(float)(CANVAS_W() + 138), (float)142}, FS(9), Sp(FS(9)), Color{100, 116, 139, 255});
    DrawLineEx({(float)CANVAS_W(),           156.0f},
               {(float)(CANVAS_W()+PANEL_W), 156.0f}, 0.5f, PANEL_BORDER);

    int y = 162;
    for (const auto& [ip, mac] : n->arpTable) {
        if (y > CANVAS_H() - LOG_H - 20) break;
        DrawTextEx(GFont(), ip.c_str(),  {(float)(CANVAS_W() + 12),  (float)y}, FS(10), Sp(FS(10)), Color{34, 197, 94, 255});
        DrawTextEx(GFont(), mac.c_str(), {(float)(CANVAS_W() + 138), (float)y}, FS(9),  Sp(FS(9)),  Color{148, 163, 184, 255});
        y += 18;
    }
}

void DrawOspfTab(const DeviceNode* n) {
    if (!n) {
        DrawTextEx(GFont(), "No device selected", {(float)(CANVAS_W() + 20), (float)130}, FS(12), Sp(FS(12)), Color{100,116,139,255});
        return;
    }
    if (n->type != ROUTER) {
        DrawTextEx(GFont(), "OSPF: routers only", {(float)(CANVAS_W() + 20), (float)130}, FS(12), Sp(FS(12)), Color{100,116,139,255});
        return;
    }

    // Enable/disable button
    Rectangle btn = PnlOspfEnableRect();
    Color btnColor = n->ospfEnabled ? Color{34,197,94,255} : Color{51,65,85,255};
    DrawRectangleRec(btn, btnColor);
    DrawRectangleLinesEx(btn, 1.0f, Color{71,85,105,255});
    const char* btnLabel = n->ospfEnabled ? "OSPF: Enabled" : "OSPF: Disabled";
    int tw = (int)TW(btnLabel, 12);
    DrawTextEx(GFont(), btnLabel, {(float)(int)(btn.x + (btn.width - tw) / 2), (float)(int)(btn.y + 7)}, FS(12), Sp(FS(12)),
               n->ospfEnabled ? Color{15,23,42,255} : Color{148,163,184,255});

    if (!n->ospfEnabled) return;

    // Router ID
    int y = 160;
    DrawTextEx(GFont(), "Router ID", {(float)(CANVAS_W() + 12), (float)y}, FS(11), Sp(FS(11)), Color{100,116,139,255});
    DrawTextEx(GFont(), n->routerId.empty() ? "(none)" : n->routerId.c_str(),
               {(float)(CANVAS_W() + 90), (float)y}, FS(11), Sp(FS(11)), WHITE);
    y += 20;

    // ABR badge or single-area display
    if (IsAbr(*n)) {
        DrawRectangleRounded({(float)(CANVAS_W() + 90), (float)y, 34.0f, 16.0f},
                             0.5f, 4, Color{139, 92, 246, 255});
        DrawTextEx(GFont(), "ABR", {(float)(CANVAS_W() + 95), (float)(y + 3)}, FS(10), Sp(FS(10)), WHITE);
    } else {
        uint32_t displayArea = 0;
        for (int i = 0; i < PORTS_PER_NODE; ++i)
            if (ValidateIP(n->portIp[i])) { displayArea = n->ospfPortArea[i]; break; }
        DrawTextEx(GFont(), "Area", {(float)(CANVAS_W() + 12), (float)y}, FS(11), Sp(FS(11)), Color{100,116,139,255});
        DrawTextEx(GFont(), std::to_string(displayArea).c_str(), {(float)(CANVAS_W() + 90), (float)y}, FS(11), Sp(FS(11)), WHITE);
    }
    y += 24;

    // Separator
    DrawLineEx({(float)CANVAS_W(), (float)y}, {(float)(CANVAS_W() + PANEL_W), (float)y},
               1.0f, PANEL_BORDER);
    y += 6;
    DrawTextEx(GFont(), "Neighbors", {(float)(CANVAS_W() + 12), (float)y}, FS(11), Sp(FS(11)), Color{100,116,139,255});
    y += 18;

    if (n->ospfNeighbors.empty()) {
        DrawTextEx(GFont(), "(none)", {(float)(CANVAS_W() + 20), (float)y}, FS(11), Sp(FS(11)), Color{71,85,105,255});
        return;
    }

    // Header row — Area column added between State and Dead
    DrawTextEx(GFont(), "Router-ID",   {(float)(CANVAS_W() + 12),  (float)y}, FS(10), Sp(FS(10)), Color{71,85,105,255});
    DrawTextEx(GFont(), "State",       {(float)(CANVAS_W() + 110), (float)y}, FS(10), Sp(FS(10)), Color{71,85,105,255});
    DrawTextEx(GFont(), "Area",        {(float)(CANVAS_W() + 175), (float)y}, FS(10), Sp(FS(10)), Color{71,85,105,255});
    DrawTextEx(GFont(), "Dead",        {(float)(CANVAS_W() + 218), (float)y}, FS(10), Sp(FS(10)), Color{71,85,105,255});
    y += 14;

    static const char* stateNames[] = { "DOWN", "INIT", "2WAY", "FULL" };
    static const Color stateColors[] = {
        {100,116,139,255}, {234,179,8,255}, {59,130,246,255}, {34,197,94,255}
    };

    for (const auto& nbr : n->ospfNeighbors) {
        if (y > CANVAS_H() - 20) break;
        std::string rid = nbr.neighborRouterId;
        if ((int)rid.size() > 11) rid = rid.substr(rid.size() - 11);
        DrawTextEx(GFont(), rid.c_str(), {(float)(CANVAS_W() + 12), (float)y}, FS(10), Sp(FS(10)), WHITE);

        int si = std::clamp((int)nbr.state, 0, 3);
        DrawTextEx(GFont(), stateNames[si], {(float)(CANVAS_W() + 110), (float)y}, FS(10), Sp(FS(10)), stateColors[si]);

        DrawTextEx(GFont(), std::to_string(nbr.area).c_str(), {(float)(CANVAS_W() + 175), (float)y}, FS(10), Sp(FS(10)),
                   Color{148, 163, 184, 255});

        char deadBuf[8];
        std::snprintf(deadBuf, sizeof(deadBuf), "%.1fs", nbr.deadTimer);
        DrawTextEx(GFont(), deadBuf, {(float)(CANVAS_W() + 218), (float)y}, FS(10), Sp(FS(10)), Color{148,163,184,255});
        y += 16;
    }
}

void DrawMplsTab(const DeviceNode* n) {
    if (!n) {
        DrawTextEx(GFont(), "No device selected", {(float)(CANVAS_W() + 20), (float)130}, FS(12), Sp(FS(12)), Color{100,116,139,255});
        return;
    }
    if (n->type != ROUTER) {
        DrawTextEx(GFont(), "MPLS: routers only", {(float)(CANVAS_W() + 20), (float)130}, FS(12), Sp(FS(12)), Color{100,116,139,255});
        return;
    }
    if (!n->ospfEnabled) {
        DrawTextEx(GFont(), "Requires OSPF enabled", {(float)(CANVAS_W() + 20), (float)130}, FS(11), Sp(FS(11)), Color{100,116,139,255});
        return;
    }

    // Enable / disable toggle button
    Rectangle btn    = PnlMplsToggleRect();
    Color     btnCol = n->ldpEnabled ? Color{249,115,22,255} : Color{51,65,85,255};
    DrawRectangleRec(btn, btnCol);
    DrawRectangleLinesEx(btn, 1.0f, Color{71,85,105,255});
    const char* btnLabel = n->ldpEnabled ? "MPLS: Enabled" : "MPLS: Disabled";
    int tw = (int)TW(btnLabel, 12);
    DrawTextEx(GFont(), btnLabel, {(float)(int)(btn.x + (btn.width - tw) / 2), (float)(int)(btn.y + 7)}, FS(12), Sp(FS(12)),
               n->ldpEnabled ? Color{15,23,42,255} : Color{148,163,184,255});

    if (!n->ldpEnabled) return;

    // LFIB table
    int y = 158;
    DrawTextEx(GFont(), "LFIB", {(float)(CANVAS_W() + 12), (float)y}, FS(11), Sp(FS(11)), Color{100,116,139,255});
    y += 18;

    if (n->lfib.empty()) {
        DrawTextEx(GFont(), "(no bindings - wait for OSPF)", {(float)(CANVAS_W() + 16), (float)y}, FS(10), Sp(FS(10)),
                   Color{71,85,105,255});
        return;
    }

    // Column headers
    DrawTextEx(GFont(), "PREFIX",  {(float)(CANVAS_W() + 12),  (float)y}, FS(10), Sp(FS(10)), Color{71,85,105,255});
    DrawTextEx(GFont(), "LOCAL",   {(float)(CANVAS_W() + 107), (float)y}, FS(10), Sp(FS(10)), Color{71,85,105,255});
    DrawTextEx(GFont(), "OUT",     {(float)(CANVAS_W() + 170), (float)y}, FS(10), Sp(FS(10)), Color{71,85,105,255});
    DrawLineEx({(float)CANVAS_W(), (float)(y + 13)},
               {(float)(CANVAS_W() + PANEL_W), (float)(y + 13)},
               0.5f, PANEL_BORDER);
    y += 16;

    for (const auto& [prefix, binding] : n->lfib) {
        if (y > CANVAS_H() - 20) break;
        DrawTextEx(GFont(), prefix.c_str(), {(float)(CANVAS_W() + 12), (float)y}, FS(10), Sp(FS(10)), WHITE);

        char locBuf[16];
        std::snprintf(locBuf, sizeof(locBuf), "%u", binding.localLabel);
        DrawTextEx(GFont(), locBuf, {(float)(CANVAS_W() + 107), (float)y}, FS(10), Sp(FS(10)), Color{253,186,116,255});

        if (binding.outLabel == MPLS_IMPLICIT_NULL) {
            DrawRectangleRounded({(float)(CANVAS_W() + 170), (float)y, 28.f, 13.f},
                                 0.4f, 4, Color{168,85,247,255});
            DrawTextEx(GFont(), "PHP", {(float)(CANVAS_W() + 174), (float)(y + 2)}, FS(9), Sp(FS(9)), WHITE);
        } else {
            char outBuf[16];
            std::snprintf(outBuf, sizeof(outBuf), "%u", binding.outLabel);
            DrawTextEx(GFont(), outBuf, {(float)(CANVAS_W() + 170), (float)y}, FS(10), Sp(FS(10)), Color{253,186,116,255});
        }

        y += 16;
    }
}

void DrawBgpTab(const DeviceNode* n, const PanelState& ps) {
    if (!n) {
        DrawTextEx(GFont(), "No device selected", {(float)(CANVAS_W() + 20), (float)130}, FS(12), Sp(FS(12)), Color{100,116,139,255});
        return;
    }
    if (n->type != ROUTER) {
        DrawTextEx(GFont(), "BGP: routers only", {(float)(CANVAS_W() + 20), (float)130}, FS(12), Sp(FS(12)), Color{100,116,139,255});
        return;
    }

    // ── Enable / disable toggle ────────────────────────────────────────
    Rectangle btn    = PnlBgpToggleRect();
    Color     btnCol = n->bgpEnabled ? Color{34,197,94,255} : Color{51,65,85,255};
    DrawRectangleRec(btn, btnCol);
    DrawRectangleLinesEx(btn, 1.0f, Color{71,85,105,255});
    const char* btnLabel = n->bgpEnabled ? "BGP: Enabled" : "BGP: Disabled";
    int tw = (int)TW(btnLabel, 12);
    DrawTextEx(GFont(), btnLabel, {(float)(int)(btn.x + (btn.width - tw) / 2), (float)(int)(btn.y + 7)}, FS(12), Sp(FS(12)),
               n->bgpEnabled ? Color{15,23,42,255} : Color{148,163,184,255});

    if (!n->bgpEnabled) return;

    // ── ASN input ─────────────────────────────────────────────────────
    int y = 152;
    DrawTextEx(GFont(), "ASN:", {(float)(CANVAS_W() + 12), (float)(y + 4)}, FS(11), Sp(FS(11)), Color{100,116,139,255});
    Rectangle asnRect  = PnlBgpAsnRect();
    bool      asnActive = (ps.bgpAsnField == n->id);
    DrawRectangleRec(asnRect, asnActive ? Color{30,41,59,255} : Color{15,23,42,255});
    DrawRectangleLinesEx(asnRect, 1.0f,
                         asnActive ? Color{59,130,246,255} : Color{71,85,105,255});
    std::string asnStr = asnActive ? ps.bgpAsnBuf
                                   : (n->localAsn > 0 ? std::to_string(n->localAsn) : "0");
    DrawTextEx(GFont(), asnStr.c_str(), {(float)(int)(asnRect.x + 4), (float)(int)(asnRect.y + 5)}, FS(11), Sp(FS(11)), WHITE);

    if (n->localAsn == 0) {
        DrawTextEx(GFont(), "Set ASN to form sessions", {(float)(CANVAS_W() + 12), (float)(y + 30)}, FS(10), Sp(FS(10)),
                   Color{234,179,8,255});
    } else {
        // Route Reflector toggle (only when ASN is set)
        Rectangle rrRect = PnlBgpRrRect();
        Color rrCol = n->isRouteReflector ? Color{167,139,250,255} : Color{51,65,85,255};
        DrawRectangleRec(rrRect, rrCol);
        DrawRectangleLinesEx(rrRect, 1.0f, Color{71,85,105,255});
        const char* rrLabel = n->isRouteReflector ? "Route Reflector: ON"
                                                  : "Route Reflector: OFF";
        int rrTw = (int)TW(rrLabel, 11);
        DrawTextEx(GFont(), rrLabel, {(float)(int)(rrRect.x + (rrRect.width - rrTw) / 2),
                   (float)(int)(rrRect.y + 5)}, FS(11), Sp(FS(11)),
                   n->isRouteReflector ? Color{15,23,42,255} : Color{148,163,184,255});
    }

    y = 210;

    // ── Neighbors ─────────────────────────────────────────────────────
    const char* neighborHeader = (n->isRouteReflector && n->localAsn > 0) ? "CLIENTS" : "NEIGHBORS";
    DrawTextEx(GFont(), neighborHeader, {(float)(CANVAS_W() + 12), (float)y}, FS(10), Sp(FS(10)), Color{71,85,105,255});
    y += 14;
    if (n->bgpNeighbors.empty()) {
        DrawTextEx(GFont(), "(none)", {(float)(CANVAS_W() + 16), (float)y}, FS(10), Sp(FS(10)), Color{71,85,105,255});
        y += 14;
    } else {
        for (const auto& nb : n->bgpNeighbors) {
            if (y > CANVAS_H() - 80) break;
            std::string ip = nb.neighborIp.size() > 12
                             ? nb.neighborIp.substr(0, 11) + "\xe2\x80\xa6"
                             : nb.neighborIp;
            DrawTextEx(GFont(), ip.c_str(), {(float)(CANVAS_W() + 12), (float)y}, FS(10), Sp(FS(10)), WHITE);
            const char* typeLabel = nb.ibgp ? "iBGP" : "eBGP";
            Color typeCol = nb.ibgp ? Color{167,139,250,255}   // purple
                                    : Color{253,186,116,255};  // orange
            DrawTextEx(GFont(), typeLabel, {(float)(CANVAS_W() + 102), (float)y}, FS(10), Sp(FS(10)), typeCol);
            const char* state = nb.established ? "ESTAB" : "DOWN";
            Color stCol = nb.established ? Color{34,197,94,255} : Color{239,68,68,255};
            DrawTextEx(GFont(), state, {(float)(CANVAS_W() + 142), (float)y}, FS(10), Sp(FS(10)), stCol);
            y += 14;
        }
    }

    // ── BGP RIB ───────────────────────────────────────────────────────
    y += 4;
    DrawTextEx(GFont(), "BGP RIB", {(float)(CANVAS_W() + 12), (float)y}, FS(10), Sp(FS(10)), Color{71,85,105,255});
    y += 14;
    if (n->bgpRoutes.empty()) {
        DrawTextEx(GFont(), "(no routes)", {(float)(CANVAS_W() + 16), (float)y}, FS(10), Sp(FS(10)), Color{71,85,105,255});
    } else {
        DrawTextEx(GFont(), "PREFIX",   {(float)(CANVAS_W() + 12),  (float)y}, FS(9), Sp(FS(9)), Color{71,85,105,255});
        DrawTextEx(GFont(), "NEXT-HOP", {(float)(CANVAS_W() + 90),  (float)y}, FS(9), Sp(FS(9)), Color{71,85,105,255});
        DrawTextEx(GFont(), "AS-PATH",  {(float)(CANVAS_W() + 158), (float)y}, FS(9), Sp(FS(9)), Color{71,85,105,255});
        y += 12;
        for (const auto& r : n->bgpRoutes) {
            if (y > CANVAS_H() - 20) break;
            {
                std::string pfx = r.prefix.size() > 15
                                  ? r.prefix.substr(0, 14) + "\xe2\x80\xa6" : r.prefix;
                DrawTextEx(GFont(), pfx.c_str(), {(float)(CANVAS_W() + 12), (float)y}, FS(10), Sp(FS(10)), WHITE);
            }
            {
                std::string nh = r.nextHop.size() > 12
                                 ? r.nextHop.substr(0, 11) + "\xe2\x80\xa6" : r.nextHop;
                DrawTextEx(GFont(), nh.c_str(), {(float)(CANVAS_W() + 90), (float)y}, FS(10), Sp(FS(10)), Color{94,234,212,255});
            }
            std::string path;
            for (size_t i = 0; i < r.asPath.size(); ++i) {
                if (i) path += ' ';
                path += std::to_string(r.asPath[i]);
            }
            DrawTextEx(GFont(), path.c_str(), {(float)(CANVAS_W() + 158), (float)y}, FS(10), Sp(FS(10)), Color{253,186,116,255});
            y += 14;
        }
    }
}

void DrawVlanTab(const DeviceNode* n, const PanelState& ps) {
    if (!n) {
        DrawTextEx(GFont(), "No device selected", {(float)(CANVAS_W() + 20), (float)130}, FS(12), Sp(FS(12)), Color{100,116,139,255});
        return;
    }
    if (n->type != SWITCH) {
        DrawTextEx(GFont(), "VLAN: switches only", {(float)(CANVAS_W() + 20), (float)130}, FS(12), Sp(FS(12)), Color{100,116,139,255});
        return;
    }

    DrawTextEx(GFont(), "PORT VLAN CONFIG", {(float)(CANVAS_W() + 12), (float)124}, FS(10), Sp(FS(10)), Color{71,85,105,255});

    for (int p = 0; p < PORTS_PER_NODE; ++p) {
        const VlanPortConfig& vc = n->vlanPorts[p];
        int rowY = 152 + p * 34;

        char plabel[12];
        std::snprintf(plabel, sizeof(plabel), "Port %d", p);
        DrawTextEx(GFont(), plabel, {(float)(CANVAS_W() + 12), (float)(rowY + 5)}, FS(10), Sp(FS(10)), Color{100,116,139,255});

        // Mode toggle button
        Rectangle modeRect = PnlVlanPortModeRect(p);
        bool isTrunk = (vc.mode == VLAN_TRUNK);
        Color modeBg = isTrunk ? Color{120, 53, 15, 255} : Color{30, 58, 95, 255};
        Color modeFg = isTrunk ? Color{245,158,11,255}   : Color{96,165,250,255};
        Color modeBorder = isTrunk ? Color{180,83,9,255}  : Color{37,99,235,255};
        DrawRectangleRec(modeRect, modeBg);
        DrawRectangleLinesEx(modeRect, 1.0f, modeBorder);
        const char* modeLabel = isTrunk ? "Trunk" : "Access";
        int mlw = (int)TW(modeLabel, 10);
        DrawTextEx(GFont(), modeLabel, {(float)(int)(modeRect.x + (modeRect.width - mlw) / 2),
                   (float)(int)(modeRect.y + 6)}, FS(10), Sp(FS(10)), modeFg);

        if (isTrunk) {
            DrawTextEx(GFont(), "-- all --", {(float)(CANVAS_W() + 93), (float)(rowY + 5)}, FS(10), Sp(FS(10)), Color{100,116,139,255});
        } else {
            // VLAN ID input field
            Rectangle idRect = PnlVlanPortIdRect(p);
            bool active = (ps.vlanPortField == p);
            DrawRectangleRec(idRect, active ? Color{30,41,59,255} : Color{15,23,42,255});
            DrawRectangleLinesEx(idRect, 1.0f,
                                 active ? Color{59,130,246,255} : Color{71,85,105,255});
            std::string idStr = active ? ps.vlanPortBuf : std::to_string(vc.accessVlan);
            DrawTextEx(GFont(), idStr.c_str(), {(float)(int)(idRect.x + 4), (float)(int)(idRect.y + 5)}, FS(10), Sp(FS(10)), WHITE);
        }
    }
}

void DrawSubIfaceTab(const DeviceNode* n, const PanelState& ps) {
    if (!n || n->type != ROUTER) {
        float tw = TW("Select a router to configure subinterfaces.", 10);
        DrawTextEx(GFont(), "Select a router to configure subinterfaces.",
                   {(float)CANVAS_W() + (PANEL_W - tw) * 0.5f, 200.f}, FS(10), Sp(FS(10)), Color{100, 116, 139, 255});
        return;
    }

    // ── Existing subinterface list ─────────────────────────────────────────
    DrawLine(CANVAS_W() + 12, 128, CANVAS_W() + PANEL_W - 12, 128, Color{51, 65, 85, 255});
    DrawTextEx(GFont(), "Subinterfaces", {(float)(CANVAS_W() + 12), (float)132}, FS(10), Sp(FS(10)), Color{148, 163, 184, 255});

    if (n->subIfaces.empty()) {
        DrawTextEx(GFont(), "(none configured)", {(float)(CANVAS_W() + 20), (float)SUB_ROW_Y0}, FS(10), Sp(FS(10)), Color{100, 116, 139, 255});
    } else {
        int maxRows = (SUB_FORM_Y0 - 12 - SUB_ROW_Y0) / SUB_ROW_H;
        int shown   = std::min((int)n->subIfaces.size(), maxRows);
        for (int i = 0; i < shown; ++i) {
            const SubInterface& si = n->subIfaces[i];
            int y = SUB_ROW_Y0 + i * SUB_ROW_H;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Gi0/%d.%d  V%d  %s",
                          si.parentPort, si.vlanId, si.vlanId, si.ip.c_str());
            DrawTextEx(GFont(), buf, {(float)(CANVAS_W() + 12), (float)(y + 4)}, FS(10), Sp(FS(10)), Color{203, 213, 225, 255});

            Rectangle delR = PnlSubRowDeleteRect(i);
            DrawRectangleRec(delR, Color{127, 29, 29, 255});
            DrawTextEx(GFont(), "x", {(float)(int)(delR.x + 3), (float)(int)(delR.y + 1)}, FS(11), Sp(FS(11)), WHITE);
        }
        if ((int)n->subIfaces.size() > shown) {
            char more[32];
            std::snprintf(more, sizeof(more), "+%d more",
                          (int)n->subIfaces.size() - shown);
            DrawTextEx(GFont(), more, {(float)(CANVAS_W() + 12), (float)(SUB_ROW_Y0 + shown * SUB_ROW_H + 4)}, FS(9), Sp(FS(9)), Color{100, 116, 139, 255});
        }
    }

    // ── Add subinterface form ──────────────────────────────────────────────
    DrawLine(CANVAS_W() + 12, SUB_FORM_Y0 - 12,
             CANVAS_W() + PANEL_W - 12, SUB_FORM_Y0 - 12, Color{51, 65, 85, 255});
    DrawTextEx(GFont(), "Add Subinterface", {(float)(CANVAS_W() + 12), (float)(SUB_FORM_Y0 - 8)}, FS(10), Sp(FS(10)), Color{148, 163, 184, 255});

    DrawTextEx(GFont(), "Port:", {(float)(CANVAS_W() + 12), (float)(SUB_FORM_Y0 + 4)}, FS(10), Sp(FS(10)), Color{148, 163, 184, 255});
    for (int p = 0; p < PORTS_PER_NODE; ++p) {
        Rectangle btn = PnlSubPortBtnRect(p);
        bool sel = (ps.subFormPort == p);
        DrawRectangleRec(btn, sel ? Color{234, 88, 12, 255} : Color{30, 41, 59, 255});
        DrawRectangleLinesEx(btn, 1.0f, Color{51, 65, 85, 255});
        char lbl[4];
        std::snprintf(lbl, sizeof(lbl), "%d", p);
        int lw = (int)TW(lbl, 10);
        DrawTextEx(GFont(), lbl, {(float)(int)(btn.x + (btn.width - lw) * 0.5f), (float)(int)(btn.y + 5)}, FS(10), Sp(FS(10)), WHITE);
    }

    DrawTextEx(GFont(), "VLAN:", {(float)(CANVAS_W() + 12), (float)(SUB_FORM_Y0 + 34)}, FS(10), Sp(FS(10)), Color{148, 163, 184, 255});
    DrawTextField(PnlSubVlanFieldRect(), nullptr, "10",
                  ps.subVlanBuf, ps.subActiveField == 0,
                  !ps.subVlanBuf.empty());

    DrawTextEx(GFont(), "IP:", {(float)(CANVAS_W() + 12), (float)(SUB_FORM_Y0 + 64)}, FS(10), Sp(FS(10)), Color{148, 163, 184, 255});
    DrawTextField(PnlSubIpFieldRect(), nullptr, "10.10.0.1/24",
                  ps.subIpBuf, ps.subActiveField == 1,
                  ValidateIP(ps.subIpBuf));

    Rectangle addBtn = PnlSubAddBtnRect();
    bool canAdd = !ps.subVlanBuf.empty() && ValidateIP(ps.subIpBuf);
    DrawRectangleRec(addBtn, canAdd ? Color{234, 88, 12, 200} : Color{30, 41, 59, 255});
    DrawRectangleLinesEx(addBtn, 1.0f, Color{51, 65, 85, 255});
    int tw2 = (int)TW("Add Subinterface", 10);
    DrawTextEx(GFont(), "Add Subinterface",
               {(float)(int)(addBtn.x + (addBtn.width - tw2) * 0.5f),
                (float)(int)(addBtn.y + 7)},
               FS(10), Sp(FS(10)), WHITE);
}

void DrawVxlanTab(const DeviceNode* n, const PanelState& ps) {
    if (!n) {
        DrawTextEx(GFont(), "No device selected", {(float)(CANVAS_W() + 20), (float)130}, FS(12), Sp(FS(12)), Color{100,116,139,255});
        return;
    }
    if (n->type != ROUTER) {
        DrawTextEx(GFont(), "VXLAN: routers only", {(float)(CANVAS_W() + 20), (float)130}, FS(12), Sp(FS(12)), Color{100,116,139,255});
        return;
    }

    // VXLAN Enabled toggle
    Rectangle vxBtn = PnlVxlanToggleRect();
    Color vxCol = n->vxlanEnabled ? Color{20,184,166,255} : Color{51,65,85,255};
    DrawRectangleRec(vxBtn, vxCol);
    DrawRectangleLinesEx(vxBtn, 1.0f, Color{71,85,105,255});
    const char* vxLabel = n->vxlanEnabled ? "VXLAN: Enabled" : "VXLAN: Disabled";
    {
        int tw = (int)TW(vxLabel, 12);
        DrawTextEx(GFont(), vxLabel, {(float)(int)(vxBtn.x + (vxBtn.width - tw) / 2), (float)(int)(vxBtn.y + 7)}, FS(12), Sp(FS(12)),
                   n->vxlanEnabled ? Color{15,23,42,255} : Color{148,163,184,255});
    }

    if (!n->vxlanEnabled) return;

    // VNI field
    DrawTextEx(GFont(), "VNI:", {(float)(CANVAS_W() + 12), (float)148}, FS(10), Sp(FS(10)), Color{100,116,139,255});
    Rectangle vniRect = PnlVxlanVniRect();
    bool vniActive = (ps.vxlanField == 0);
    DrawRectangleRec(vniRect, vniActive ? Color{30,41,59,255} : Color{15,23,42,255});
    DrawRectangleLinesEx(vniRect, 1.0f, vniActive ? Color{59,130,246,255} : Color{71,85,105,255});
    std::string vniStr = vniActive ? ps.vxlanVniBuf
                                   : (n->vni > 0 ? std::to_string(n->vni) : "unset");
    DrawTextEx(GFont(), vniStr.c_str(), {(float)(int)(vniRect.x + 4), (float)(int)(vniRect.y + 5)}, FS(11), Sp(FS(11)),
               n->vni > 0 || vniActive ? WHITE : Color{100,116,139,255});

    // VTEP IP field
    DrawTextEx(GFont(), "VTEP IP:", {(float)(CANVAS_W() + 12), (float)184}, FS(10), Sp(FS(10)), Color{100,116,139,255});
    Rectangle vtepRect = PnlVxlanVtepRect();
    bool vtepActive = (ps.vxlanField == 1);
    DrawRectangleRec(vtepRect, vtepActive ? Color{30,41,59,255} : Color{15,23,42,255});
    DrawRectangleLinesEx(vtepRect, 1.0f, vtepActive ? Color{59,130,246,255} : Color{71,85,105,255});
    std::string vtepStr = vtepActive ? ps.vxlanVtepBuf
                                     : (n->vtepIp.empty() ? "unset" : n->vtepIp);
    DrawTextEx(GFont(), vtepStr.c_str(), {(float)(int)(vtepRect.x + 4), (float)(int)(vtepRect.y + 5)}, FS(11), Sp(FS(11)),
               !n->vtepIp.empty() || vtepActive ? WHITE : Color{100,116,139,255});

    if (n->vtepIp.empty()) {
        DrawTextEx(GFont(), "Must match a port IP", {(float)(CANVAS_W() + 12), (float)218}, FS(10), Sp(FS(10)), Color{234,179,8,255});
    }

    // EVPN Enabled toggle
    Rectangle evpnBtn = PnlVxlanEvpnRect();
    Color evpnCol = n->evpnEnabled ? Color{34,197,94,255} : Color{51,65,85,255};
    DrawRectangleRec(evpnBtn, evpnCol);
    DrawRectangleLinesEx(evpnBtn, 1.0f, Color{71,85,105,255});
    const char* evpnLabel = n->evpnEnabled ? "EVPN: Enabled" : "EVPN: Disabled";
    {
        int tw = (int)TW(evpnLabel, 11);
        DrawTextEx(GFont(), evpnLabel, {(float)(int)(evpnBtn.x + (evpnBtn.width - tw) / 2), (float)(int)(evpnBtn.y + 6)}, FS(11), Sp(FS(11)),
                   n->evpnEnabled ? Color{15,23,42,255} : Color{148,163,184,255});
    }

    if (n->evpnEnabled) {
        char routeBuf[48];
        std::snprintf(routeBuf, sizeof(routeBuf), "EVPN routes: %d", (int)n->evpnRoutes.size());
        DrawTextEx(GFont(), routeBuf, {(float)(CANVAS_W() + 12), (float)260}, FS(10), Sp(FS(10)), Color{94,234,212,255});
    }
}

void DrawAclTab(const DeviceNode* n, const PanelState& ps) {
    if (!n) { DrawTextEx(GFont(), "No device selected", {(float)(CANVAS_W()+20), (float)130}, FS(11), Sp(FS(11)), Color{100,116,139,255}); return; }
    if (n->type != ROUTER) { DrawTextEx(GFont(), "ACL: routers only", {(float)(CANVAS_W()+20), (float)130}, FS(11), Sp(FS(11)), Color{100,116,139,255}); return; }

    // Enable toggle (grey = no rules, green = rules present)
    Rectangle togR = PnlAclToggleRect();
    bool hasRules  = !n->aclRules.empty();
    Color togCol   = hasRules ? Color{34,197,94,255} : Color{51,65,85,255};
    DrawRectangleRec(togR, togCol);
    DrawRectangleLinesEx(togR, 1.f, Color{71,85,105,255});
    const char* togLabel = hasRules ? "ACL: Active" : "ACL: No Rules";
    { int tw = (int)TW(togLabel, 12); DrawTextEx(GFont(), togLabel,
        {(float)(int)(togR.x + (togR.width-tw)/2), (float)(int)(togR.y+7)}, FS(12), Sp(FS(12)), WHITE); }

    // IN port selector
    DrawTextEx(GFont(), "IN port:", {(float)(CANVAS_W()+12), (float)142}, FS(10), Sp(FS(10)), Color{100,116,139,255});
    for (int p = 0; p <= 4; ++p) {
        Rectangle br = PnlAclInPortBtnRect(p);
        bool active  = (p < 4) ? (n->aclInPort == p) : (n->aclInPort == -1);
        DrawRectangleRec(br, active ? Color{59,130,246,255} : Color{30,41,59,255});
        DrawRectangleLinesEx(br, 1.f, Color{71,85,105,255});
        char pbuf[4] = {0};
        if (p < 4) pbuf[0] = '0' + p;
        else { pbuf[0]='\xe2'; pbuf[1]='\x80'; pbuf[2]='\x94'; }  // — (em dash UTF-8)
        int tw2 = (int)TW(pbuf, 10);
        DrawTextEx(GFont(), pbuf, {(float)(int)(br.x+(br.width-tw2)/2), (float)(int)(br.y+6)}, FS(10), Sp(FS(10)), WHITE);
    }

    // OUT port selector
    DrawTextEx(GFont(), "OUT port:", {(float)(CANVAS_W()+12), (float)168}, FS(10), Sp(FS(10)), Color{100,116,139,255});
    for (int p = 0; p <= 4; ++p) {
        Rectangle br = PnlAclOutPortBtnRect(p);
        bool active  = (p < 4) ? (n->aclOutPort == p) : (n->aclOutPort == -1);
        DrawRectangleRec(br, active ? Color{59,130,246,255} : Color{30,41,59,255});
        DrawRectangleLinesEx(br, 1.f, Color{71,85,105,255});
        char pbuf[4] = {0};
        if (p < 4) pbuf[0] = '0' + p;
        else { pbuf[0]='\xe2'; pbuf[1]='\x80'; pbuf[2]='\x94'; }
        int tw2 = (int)TW(pbuf, 10);
        DrawTextEx(GFont(), pbuf, {(float)(int)(br.x+(br.width-tw2)/2), (float)(int)(br.y+6)}, FS(10), Sp(FS(10)), WHITE);
    }

    // Rules header
    DrawTextEx(GFont(), "Rules:", {(float)(CANVAS_W()+12), (float)208}, FS(10), Sp(FS(10)), Color{100,116,139,255});
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
        float aw = TW(aLabel, 9);
        DrawTextEx(GFont(), aLabel, {badgeR.x + (46.f - aw) * 0.5f, (float)(int)(ry+5)}, FS(9), Sp(FS(9)), WHITE);

        // Rule description
        char rdesc[96];
        if (r.dstPort > 0)
            std::snprintf(rdesc, sizeof(rdesc), "%s \xe2\x86\x92 %s :%d",
                          r.srcCidr.c_str(), r.dstCidr.c_str(), r.dstPort);
        else
            std::snprintf(rdesc, sizeof(rdesc), "%s \xe2\x86\x92 %s",
                          r.srcCidr.c_str(), r.dstCidr.c_str());
        DrawTextEx(GFont(), rdesc, {(float)(CANVAS_W()+62), (float)(int)(ry+5)}, FS(9), Sp(FS(9)), Color{203,213,225,255});

        // Delete button
        Rectangle delR = PnlAclRuleDeleteRect(i);
        DrawRectangleRec(delR, Color{127,29,29,200});
        DrawRectangleLinesEx(delR, 1.f, Color{153,27,27,255});
        { int xw = (int)TW("X", 9); DrawTextEx(GFont(), "X",
            {(float)(int)(delR.x+(delR.width-xw)/2), (float)(int)(delR.y+5)}, FS(9), Sp(FS(9)), WHITE); }
    }
    if (ruleCount > maxVisible) {
        char moreBuf[32];
        std::snprintf(moreBuf, sizeof(moreBuf), "+%d more", ruleCount - maxVisible);
        DrawTextEx(GFont(), moreBuf, {(float)(CANVAS_W()+12), (float)(int)(226.f + maxVisible*26.f + 2)}, FS(9), Sp(FS(9)),
                   Color{100,116,139,255});
    }

    // Add Rule form separator
    DrawTextEx(GFont(), "Add Rule:", {(float)(CANVAS_W()+12), (float)358}, FS(10), Sp(FS(10)), Color{100,116,139,255});
    DrawLineEx({(float)(CANVAS_W()+68), 364.f}, {(float)(CANVAS_W()+12+PANEL_W-24), 364.f},
               0.5f, Color{51,65,85,255});

    // Action toggle for new rule
    Rectangle formAct = PnlAclFormActionRect();
    Color formActCol  = (ps.aclFormAction == 0) ? Color{34,197,94,255} : Color{239,68,68,255};
    DrawRectangleRec(formAct, formActCol);
    DrawRectangleLinesEx(formAct, 1.f, Color{71,85,105,255});
    const char* fActLabel = (ps.aclFormAction == 0) ? "PERMIT" : "DENY";
    { int tw3 = (int)TW(fActLabel, 12);
      DrawTextEx(GFont(), fActLabel, {(float)(int)(formAct.x+(formAct.width-tw3)/2), (float)(int)(formAct.y+7)}, FS(12), Sp(FS(12)), WHITE); }

    // Src CIDR field
    DrawTextEx(GFont(), "Src:", {(float)(CANVAS_W()+12), (float)392}, FS(10), Sp(FS(10)), Color{100,116,139,255});
    Rectangle srcR = PnlAclFormSrcRect();
    bool srcActive = (ps.aclActiveField == 0);
    DrawRectangleRec(srcR, srcActive ? Color{30,41,59,255} : Color{15,23,42,255});
    DrawRectangleLinesEx(srcR, 1.f, srcActive ? Color{59,130,246,255} : Color{71,85,105,255});
    const std::string& srcStr = srcActive ? ps.aclSrcBuf : (ps.aclSrcBuf.empty() ? std::string("any") : ps.aclSrcBuf);
    DrawTextEx(GFont(), srcStr.c_str(), {(float)(int)(srcR.x+4), (float)(int)(srcR.y+5)}, FS(10), Sp(FS(10)),
               srcStr == "any" ? Color{71,85,105,255} : WHITE);

    // Dst CIDR field
    DrawTextEx(GFont(), "Dst:", {(float)(CANVAS_W()+12), (float)418}, FS(10), Sp(FS(10)), Color{100,116,139,255});
    Rectangle dstR = PnlAclFormDstRect();
    bool dstActive = (ps.aclActiveField == 1);
    DrawRectangleRec(dstR, dstActive ? Color{30,41,59,255} : Color{15,23,42,255});
    DrawRectangleLinesEx(dstR, 1.f, dstActive ? Color{59,130,246,255} : Color{71,85,105,255});
    const std::string& dstStr = dstActive ? ps.aclDstBuf : (ps.aclDstBuf.empty() ? std::string("any") : ps.aclDstBuf);
    DrawTextEx(GFont(), dstStr.c_str(), {(float)(int)(dstR.x+4), (float)(int)(dstR.y+5)}, FS(10), Sp(FS(10)),
               dstStr == "any" ? Color{71,85,105,255} : WHITE);

    // Dst Port field
    DrawTextEx(GFont(), "Port:", {(float)(CANVAS_W()+12), (float)444}, FS(10), Sp(FS(10)), Color{100,116,139,255});
    Rectangle portR = PnlAclFormPortRect();
    bool portActive = (ps.aclActiveField == 2);
    DrawRectangleRec(portR, portActive ? Color{30,41,59,255} : Color{15,23,42,255});
    DrawRectangleLinesEx(portR, 1.f, portActive ? Color{59,130,246,255} : Color{71,85,105,255});
    const std::string& portStr = portActive ? ps.aclPortBuf
                                 : (ps.aclPortBuf.empty() ? std::string("any") : ps.aclPortBuf);
    DrawTextEx(GFont(), portStr.c_str(), {(float)(int)(portR.x+4), (float)(int)(portR.y+5)}, FS(10), Sp(FS(10)),
               portStr == "any" ? Color{71,85,105,255} : WHITE);

    // Add button
    Rectangle addR = PnlAclFormAddBtnRect();
    DrawRectangleRec(addR, Color{30,64,175,255});
    DrawRectangleLinesEx(addR, 1.f, Color{59,130,246,255});
    { int tw4 = (int)TW("+ Add Rule", 11);
      DrawTextEx(GFont(), "+ Add Rule", {(float)(int)(addR.x+(addR.width-tw4)/2), (float)(int)(addR.y+8)}, FS(11), Sp(FS(11)), WHITE); }
}

void DrawNatTab(const DeviceNode* n, const PanelState& ps) {
    if (!n) { DrawTextEx(GFont(), "No device selected", {(float)(CANVAS_W()+20), (float)130}, FS(11), Sp(FS(11)), Color{100,116,139,255}); return; }
    if (n->type != ROUTER) { DrawTextEx(GFont(), "NAT: routers only", {(float)(CANVAS_W()+20), (float)130}, FS(11), Sp(FS(11)), Color{100,116,139,255}); return; }

    // Enable toggle
    Rectangle togR = PnlNatToggleRect();
    Color togCol   = n->natEnabled ? Color{234,179,8,255} : Color{51,65,85,255};
    DrawRectangleRec(togR, togCol);
    DrawRectangleLinesEx(togR, 1.f, Color{71,85,105,255});
    const char* natLabel = n->natEnabled ? "NAT: Enabled" : "NAT: Disabled";
    { int tw = (int)TW(natLabel, 12);
      DrawTextEx(GFont(), natLabel, {(float)(int)(togR.x+(togR.width-tw)/2), (float)(int)(togR.y+7)}, FS(12), Sp(FS(12)), WHITE); }

    if (!n->natEnabled) return;

    // Inside port
    DrawTextEx(GFont(), "Inside port:", {(float)(CANVAS_W()+12), (float)142}, FS(10), Sp(FS(10)), Color{100,116,139,255});
    for (int p = 0; p < 4; ++p) {
        Rectangle br = PnlNatInsidePortBtnRect(p);
        bool active  = (n->natInsidePort == p);
        DrawRectangleRec(br, active ? Color{34,197,94,255} : Color{30,41,59,255});
        DrawRectangleLinesEx(br, 1.f, Color{71,85,105,255});
        char pbuf[2] = {static_cast<char>('0'+p), 0};
        int tw2 = (int)TW(pbuf, 10);
        DrawTextEx(GFont(), pbuf, {(float)(int)(br.x+(br.width-tw2)/2), (float)(int)(br.y+6)}, FS(10), Sp(FS(10)), WHITE);
    }

    // Outside port
    DrawTextEx(GFont(), "Outside port:", {(float)(CANVAS_W()+12), (float)168}, FS(10), Sp(FS(10)), Color{100,116,139,255});
    for (int p = 0; p < 4; ++p) {
        Rectangle br = PnlNatOutsidePortBtnRect(p);
        bool active  = (n->natOutsidePort == p);
        DrawRectangleRec(br, active ? Color{59,130,246,255} : Color{30,41,59,255});
        DrawRectangleLinesEx(br, 1.f, Color{71,85,105,255});
        char pbuf[2] = {static_cast<char>('0'+p), 0};
        int tw2 = (int)TW(pbuf, 10);
        DrawTextEx(GFont(), pbuf, {(float)(int)(br.x+(br.width-tw2)/2), (float)(int)(br.y+6)}, FS(10), Sp(FS(10)), WHITE);
    }

    // Inside prefix field
    DrawTextEx(GFont(), "Inside prefix:", {(float)(CANVAS_W()+12), (float)204}, FS(10), Sp(FS(10)), Color{100,116,139,255});
    Rectangle prefR = PnlNatInsidePrefixRect();
    bool prefActive  = (ps.natField == 0);
    DrawRectangleRec(prefR, prefActive ? Color{30,41,59,255} : Color{15,23,42,255});
    DrawRectangleLinesEx(prefR, 1.f, prefActive ? Color{59,130,246,255} : Color{71,85,105,255});
    const std::string& prefStr = prefActive ? ps.natInsideBuf
                                  : (n->natInsidePrefix.empty() ? std::string("unset") : n->natInsidePrefix);
    DrawTextEx(GFont(), prefStr.c_str(), {(float)(int)(prefR.x+4), (float)(int)(prefR.y+5)}, FS(10), Sp(FS(10)),
               (prefStr == "unset") ? Color{71,85,105,255} : WHITE);

    // Outside IP (auto, display only)
    DrawTextEx(GFont(), "Outside IP (auto):", {(float)(CANVAS_W()+12), (float)244}, FS(10), Sp(FS(10)), Color{100,116,139,255});
    if (n->natOutsidePort < 0 || n->natOutsidePort >= PORTS_PER_NODE) {
        DrawTextEx(GFont(), "Outside port not set", {(float)(CANVAS_W()+12), (float)258}, FS(10), Sp(FS(10)), Color{71,85,105,255});
    } else {
        const std::string& outsideCidr = n->portIp[n->natOutsidePort];
        auto sl = outsideCidr.find('/');
        std::string outsideIp = sl != std::string::npos ? outsideCidr.substr(0, sl) : outsideCidr;
        DrawTextEx(GFont(), outsideIp.empty() ? "unset" : outsideIp.c_str(),
                   {(float)(CANVAS_W()+12), (float)258}, FS(10), Sp(FS(10)),
                   outsideIp.empty() ? Color{71,85,105,255} : Color{94,234,212,255});

        // NAT summary
        if (!n->natInsidePrefix.empty() && !outsideIp.empty()) {
            char sumBuf[80];
            std::snprintf(sumBuf, sizeof(sumBuf), "%s \xe2\x86\x92 %s (overload)",
                          n->natInsidePrefix.c_str(), outsideIp.c_str());
            DrawTextEx(GFont(), sumBuf, {(float)(CANVAS_W()+12), (float)278}, FS(10), Sp(FS(10)), Color{234,179,8,200});
        }
    }
}

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

void DrawPanel(int selectedId, const std::vector<DeviceNode>& nodes,
               const PanelState& ps)
{
    DrawRectangle(CANVAS_W(), 0, PANEL_W, SCREEN_H(), PANEL_BG);
    DrawLineEx({(float)CANVAS_W(), 0.0f}, {(float)CANVAS_W(), (float)SCREEN_H()},
               1.0f, PANEL_BORDER);
    DrawTextEx(GFont(), "CONFIGURATION", {(float)(CANVAS_W() + 12), (float)14}, FS(10), Sp(FS(10)), Color{100, 116, 139, 255});
    DrawLineEx({(float)CANVAS_W(), 38.0f}, {(float)(CANVAS_W() + PANEL_W), 38.0f},
               1.0f, PANEL_BORDER);

    if (selectedId == -1) {
        const char* msg = "<- Select a device";
        float tw = TW(msg, 13);
        DrawTextEx(GFont(), msg, {(float)CANVAS_W() + (PANEL_W - tw) * 0.5f, (float)SCREEN_H() * 0.5f - 8.f}, FS(13), Sp(FS(13)),
                   Color{100, 116, 139, 255});
        return;
    }

    const DeviceNode* n = FindNode(nodes, selectedId);
    if (!n) return;

    // Device type badge
    const char* typeNames[] = {"PC", "Router", "Switch"};
    int bw = (int)TW(typeNames[(int)n->type], 11) + 16;
    DrawRectangleRounded({(float)(CANVAS_W() + 12), 50.0f, (float)bw, 22.0f},
                         0.5f, 4, GetDeviceColor(n->type));
    DrawTextEx(GFont(), typeNames[(int)n->type], {(float)(CANVAS_W() + 20), (float)56}, FS(11), Sp(FS(11)), WHITE);
    DrawTextEx(GFont(), n->label.c_str(), {(float)(CANVAS_W() + 16 + bw), (float)56}, FS(13), Sp(FS(13)), WHITE);
    DrawLineEx({(float)CANVAS_W(), 84.0f}, {(float)(CANVAS_W() + PANEL_W), 84.0f},
               1.0f, PANEL_BORDER);

    // Tab header — device-type-aware
    {
        const TabInfo* tabs     = PnlTabList(n->type);
        int            tabCount = PnlTabCount(n->type);
        for (int ti = 0; ti < tabCount; ++ti) {
            Rectangle tr  = PnlTabRect(n->type, ti);
            bool      act = (ps.activeTab == tabs[ti].tab);
            DrawRectangleRec(tr, act ? Color{30,41,59,255} : PANEL_BG);
            if (act)
                DrawLineEx({tr.x, tr.y + tr.height},
                           {tr.x + tr.width, tr.y + tr.height},
                           2.0f, tabs[ti].activeUnder);
            int tw = (int)TW(tabs[ti].label, 9);
            DrawTextEx(GFont(), tabs[ti].label,
                       {(float)(int)(tr.x + (tr.width - tw) / 2.f),
                        (float)(int)(tr.y + 9)},
                       FS(9), Sp(FS(9)),
                       act ? tabs[ti].activeTxt : Color{100,116,139,255});
        }
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
    else if (ps.activeTab == TAB_TE)   DrawTeTab(n, ps);
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
        DrawTextEx(GFont(), items[i], {(float)((int)ir.x + 8), (float)((int)ir.y + 7)}, FS(13), Sp(FS(13)), WHITE);
    }
}
