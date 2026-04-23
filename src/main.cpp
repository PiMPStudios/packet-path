#include "raylib.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>    // sscanf

// ── Constants ─────────────────────────────────────────────────────────────
static const int   SCREEN_W     = 1280;
static const int   SCREEN_H     = 720;
static const float NODE_W       = 120.0f;
static const float NODE_H       =  60.0f;
static const int   NODE_FONT_SZ =  14;
static const float PORT_RADIUS    =  6.0f;
static const int   PORTS_PER_NODE =  4;    // top=0, right=1, bottom=2, left=3
static const Color BG_COLOR     = {15, 23, 42, 255};

static const int   PANEL_W      = 280;
static const int   CANVAS_W     = SCREEN_W - PANEL_W;   // 1000
static const Color PANEL_BG     = {22, 33, 62, 255};
static const Color PANEL_BORDER = {51, 65, 85, 255};

// ── Device types & node struct ─────────────────────────────────────────────
enum DeviceType { PC, ROUTER, SWITCH };

struct DeviceNode {
    int         id       = 0;
    DeviceType  type     = PC;
    Vector2     position = {0.0f, 0.0f};
    std::string label;
    bool        selected = false;
    std::string mgmtIp;        // "x.x.x.x/xx", empty = unconfigured
    std::string portIp[4];     // per-port IPs, same format
};

// ── Helper functions ───────────────────────────────────────────────────────
Color GetDeviceColor(DeviceType t) {
    switch (t) {
        case PC:     return {59,  130, 246, 255};
        case ROUTER: return {249, 115,  22, 255};
        case SWITCH: return {34,  197,  94, 255};
        default:     return WHITE;
    }
}

Rectangle GetNodeRect(const DeviceNode& n) {
    return {n.position.x - NODE_W / 2.0f,
            n.position.y - NODE_H / 2.0f,
            NODE_W, NODE_H};
}

Vector2 GetPortPosition(const DeviceNode& n, int port) {
    float hw = NODE_W / 2.0f, hh = NODE_H / 2.0f;
    switch (port) {
        case 0: return {n.position.x,       n.position.y - hh};  // top
        case 1: return {n.position.x + hw,  n.position.y      };  // right
        case 2: return {n.position.x,       n.position.y + hh};  // bottom
        case 3: return {n.position.x - hw,  n.position.y      };  // left
        default: return n.position;
    }
}

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
    Vector2 botRight = GetScreenToWorld2D({(float)CANVAS_W, (float)SCREEN_H}, cam);

    float startX = floorf(topLeft.x / spacing) * spacing;
    float startY = floorf(topLeft.y / spacing) * spacing;

    for (float x = startX; x <= botRight.x; x += spacing)
        for (float y = startY; y <= botRight.y; y += spacing)
            DrawCircleV({x, y}, 1.5f / cam.zoom, dot);
}

// ── Cable struct and helpers ──────────────────────────────────────────────
struct Cable {
    int fromId, fromPort;
    int toId,   toPort;
};

const DeviceNode* FindNode(const std::vector<DeviceNode>& nodes, int id) {
    for (const auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
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

        auto ctrl = [](Vector2 p, int port) -> Vector2 {
            const float offset = 60.0f;
            switch (port) {
                case 0: return {p.x,           p.y - offset};
                case 1: return {p.x + offset,  p.y         };
                case 2: return {p.x,           p.y + offset};
                case 3: return {p.x - offset,  p.y         };
                default: return p;
            }
        };

        DrawSplineSegmentBezierCubic(p0, ctrl(p0, c.fromPort),
                                     ctrl(p3, c.toPort), p3,
                                     2.0f, Color{148, 163, 184, 255});
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

// ── Panel UI state ────────────────────────────────────────────────────────
struct PanelState {
    int activeField = -1;
    // -1=none  0=label(hostname)  1=mgmtIp  2-5=portIp[0-3]
};

Rectangle PnlFieldRect(int yOffset) {
    return {(float)(CANVAS_W + 12), (float)yOffset, (float)(PANEL_W - 24), 26.0f};
}

Rectangle PnlPortFieldRect(int port) {
    return {(float)(CANVAS_W + 80), 240.0f + port * 44, (float)(PANEL_W - 92), 24.0f};
}

bool ValidateIP(const std::string& ip) {
    if (ip.empty()) return false;
    int a, b, c, d, prefix, consumed = 0;
    std::sscanf(ip.c_str(), "%d.%d.%d.%d/%d%n", &a, &b, &c, &d, &prefix, &consumed);
    return (consumed == (int)ip.size() &&
            a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
            c >= 0 && c <= 255 && d >= 0 && d <= 255 &&
            prefix >= 0 && prefix <= 32);
}

std::string GetPortName(DeviceType type, int port) {
    if (type == PC) {
        static_assert(PORTS_PER_NODE == 4, "Update GetPortName PC branch to match PORTS_PER_NODE");
        const char* names[] = {"eth0", "eth1", "eth2", "eth3"};
        return names[port];
    }
    return "Gi0/" + std::to_string(port);
}

void UpdateTextField(std::string& text, int maxLen) {
    int ch;
    while ((ch = GetCharPressed()) > 0)
        if ((int)text.size() < maxLen && ch >= 32 && ch < 127)
            text += (char)ch;
    if (IsKeyPressed(KEY_BACKSPACE) && !text.empty())
        text.pop_back();
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

// ── Spawn helper ──────────────────────────────────────────────────────────
static int nextId = 1;

DeviceNode SpawnNode(DeviceType type, Vector2 worldPos) {
    const char* names[] = {"PC", "RTR", "SW"};
    DeviceNode n;
    n.id       = nextId++;
    n.type     = type;
    n.position = worldPos;
    n.label    = std::string(names[(int)type]) + "-" + std::to_string(n.id);
    return n;
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

    const char* typeNames[] = {"PC", "Router", "Switch"};
    int bw = MeasureText(typeNames[(int)n->type], 11) + 16;
    DrawRectangleRounded({(float)(CANVAS_W + 12), 50.0f, (float)bw, 22.0f},
                         0.5f, 4, GetDeviceColor(n->type));
    DrawText(typeNames[(int)n->type], CANVAS_W + 20, 56, 11, WHITE);
    DrawText(n->label.c_str(), CANVAS_W + 16 + bw, 56, 13, WHITE);

    DrawLineEx({(float)CANVAS_W, 84.0f}, {(float)(CANVAS_W + PANEL_W), 84.0f},
               1.0f, PANEL_BORDER);
    DrawText("GENERAL", CANVAS_W + 12, 94, 10, Color{100, 116, 139, 255});

    DrawTextField(PnlFieldRect(126), "Hostname", nullptr,
                  n->label, ps.activeField == 0, !n->label.empty());
    DrawTextField(PnlFieldRect(178), "Mgmt IP", "x.x.x.x/xx",
                  n->mgmtIp, ps.activeField == 1, ValidateIP(n->mgmtIp));

    DrawLineEx({(float)CANVAS_W, 214.0f}, {(float)(CANVAS_W + PANEL_W), 214.0f},
               1.0f, PANEL_BORDER);
    DrawText("INTERFACES", CANVAS_W + 12, 224, 10, Color{100, 116, 139, 255});

    for (int i = 0; i < PORTS_PER_NODE; ++i) {
        std::string pname = GetPortName(n->type, i);
        int ry = 240 + i * 44;
        DrawTextField(PnlPortFieldRect(i), "", "x.x.x.x/xx",
                      n->portIp[i], ps.activeField == 2 + i,
                      ValidateIP(n->portIp[i]));
        DrawText(pname.c_str(), CANVAS_W + 16, ry + 7, 11, Color{148, 163, 184, 255});
    }
}

// ── Main ──────────────────────────────────────────────────────────────────
int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Packet Path");
    SetTargetFPS(60);

    std::vector<DeviceNode> nodes;
    nodes.push_back(SpawnNode(PC, {0.0f, 0.0f}));

    Camera2D camera = {};
    camera.offset   = {CANVAS_W / 2.0f, SCREEN_H / 2.0f};
    camera.target   = {0.0f, 0.0f};
    camera.zoom     = 1.0f;

    int     selectedId = -1;
    bool    dragging   = false;
    Vector2 dragOffset = {0.0f, 0.0f};

    std::vector<Cable> cables;
    bool connecting      = false;
    int  connectFromId   = -1;
    int  connectFromPort = -1;
    int  hoverNodeId     = -1;
    int  hoverPort       = -1;

    PanelState ps;
    int        prevSelectedId = -2;  // -2 forces reset on first frame

    while (!WindowShouldClose()) {
        Vector2 screenMouse = GetMousePosition();
        Vector2 worldMouse  = GetScreenToWorld2D(screenMouse, camera);
        bool inCanvas = (screenMouse.x < (float)CANVAS_W);

        // ── Spawn / delete / cancel ────────────────────────────────────
        if (inCanvas && ps.activeField == -1) {
            if (IsKeyPressed(KEY_P)) nodes.push_back(SpawnNode(PC,     worldMouse));
            if (IsKeyPressed(KEY_R)) nodes.push_back(SpawnNode(ROUTER, worldMouse));
            if (IsKeyPressed(KEY_S)) nodes.push_back(SpawnNode(SWITCH, worldMouse));
        }

        if (IsKeyPressed(KEY_ESCAPE)) {
            if (ps.activeField != -1) {
                ps.activeField = -1;
            } else if (connecting) {
                connecting  = false;
                hoverNodeId = -1;
                hoverPort   = -1;
            }
        }

        if (ps.activeField == -1 && IsKeyPressed(KEY_DELETE) && selectedId != -1) {
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const DeviceNode& n){ return n.id == selectedId; }),
                nodes.end());
            cables.erase(std::remove_if(cables.begin(), cables.end(),
                [&](const Cable& c){ return c.fromId == selectedId || c.toId == selectedId; }),
                cables.end());
            selectedId = -1;
            dragging   = false;
        }

        // ── Camera pan (middle mouse) ──────────────────────────────────
        if (inCanvas && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            Vector2 delta    = GetMouseDelta();
            camera.target.x -= delta.x / camera.zoom;
            camera.target.y -= delta.y / camera.zoom;
        }

        // ── Camera zoom (scroll wheel, cursor-anchored) ────────────────
        float wheel = inCanvas ? std::clamp(GetMouseWheelMove(), -3.0f, 3.0f) : 0.0f;
        if (wheel != 0.0f) {
            Vector2 beforeZoom = GetScreenToWorld2D(screenMouse, camera);
            camera.zoom *= (1.0f + wheel * 0.1f);
            camera.zoom  = std::clamp(camera.zoom, 0.15f, 4.0f);
            Vector2 afterZoom  = GetScreenToWorld2D(screenMouse, camera);
            camera.target.x   += beforeZoom.x - afterZoom.x;
            camera.target.y   += beforeZoom.y - afterZoom.y;
        }

        // ── LMB pressed ───────────────────────────────────────────────
        if (inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int pNode = -1, pPort = -1;
            if (HitTestPort(nodes, worldMouse, -1, pNode, pPort)) {
                connecting      = true;
                connectFromId   = pNode;
                connectFromPort = pPort;
                dragging        = false;
            } else {
                int hitIdx = -1;
                for (int i = (int)nodes.size() - 1; i >= 0; --i) {
                    if (CheckCollisionPointRec(worldMouse, GetNodeRect(nodes[i]))) {
                        hitIdx = i;
                        break;
                    }
                }
                for (auto& n : nodes) n.selected = false;
                if (hitIdx != -1) {
                    nodes[hitIdx].selected = true;
                    selectedId = nodes[hitIdx].id;
                    dragging   = true;
                    dragOffset = {worldMouse.x - nodes[hitIdx].position.x,
                                  worldMouse.y - nodes[hitIdx].position.y};
                } else {
                    selectedId = -1;
                    dragging   = false;
                }
            }
        }

        // ── LMB held ──────────────────────────────────────────────────
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragging) {
            for (auto& n : nodes) {
                if (n.id == selectedId) {
                    n.position = {worldMouse.x - dragOffset.x,
                                  worldMouse.y - dragOffset.y};
                    break;
                }
            }
        }
        if (inCanvas && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && connecting) {
            hoverNodeId = -1;
            hoverPort   = -1;
            HitTestPort(nodes, worldMouse, connectFromId, hoverNodeId, hoverPort);
        }

        // ── LMB released ──────────────────────────────────────────────
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (connecting && hoverNodeId != -1) {
                bool portOccupied = false;
                for (const auto& c : cables) {
                    if ((c.fromId == connectFromId && c.fromPort == connectFromPort) ||
                        (c.toId   == connectFromId && c.toPort   == connectFromPort) ||
                        (c.fromId == hoverNodeId   && c.fromPort == hoverPort) ||
                        (c.toId   == hoverNodeId   && c.toPort   == hoverPort))
                    { portOccupied = true; break; }
                }
                if (!portOccupied)
                    cables.push_back({connectFromId, connectFromPort,
                                      hoverNodeId,   hoverPort});
            }
            connecting  = false;
            dragging    = false;
            hoverNodeId = -1;
            hoverPort   = -1;
        }

        // ── Panel click-to-focus ───────────────────────────────────────
        if (!inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            ps.activeField = -1;
            if (selectedId != -1) {
                if (CheckCollisionPointRec(screenMouse, PnlFieldRect(126))) ps.activeField = 0;
                if (CheckCollisionPointRec(screenMouse, PnlFieldRect(178))) ps.activeField = 1;
                for (int i = 0; i < PORTS_PER_NODE; ++i)
                    if (CheckCollisionPointRec(screenMouse, PnlPortFieldRect(i)))
                        ps.activeField = 2 + i;
            }
        }

        // ── Text field update ──────────────────────────────────────────
        if (ps.activeField != -1 && selectedId != -1) {
            DeviceNode* selNode = nullptr;
            for (auto& nd : nodes)
                if (nd.id == selectedId) { selNode = &nd; break; }
            if (selNode) {
                if (ps.activeField == 0) UpdateTextField(selNode->label,   32);
                if (ps.activeField == 1) UpdateTextField(selNode->mgmtIp, 18);
                for (int i = 0; i < PORTS_PER_NODE; ++i)
                    if (ps.activeField == 2 + i) UpdateTextField(selNode->portIp[i], 18);
            }
        } else {
            while (GetCharPressed() > 0) {}  // flush char queue when no field active
        }

        // Reset active field when selection changes
        if (selectedId != prevSelectedId) {
            ps.activeField  = -1;
            prevSelectedId  = selectedId;
        }

        // ── Draw ───────────────────────────────────────────────────────
        BeginDrawing();
            ClearBackground(BG_COLOR);

            BeginMode2D(camera);
                DrawDotGrid(camera);
                DrawAllCables(cables, nodes);

                if (connecting) {
                    const DeviceNode* fromNode = FindNode(nodes, connectFromId);
                    if (fromNode) {
                        Vector2 p0 = GetPortPosition(*fromNode, connectFromPort);
                        DrawLineEx(p0, worldMouse, 2.0f, Color{148, 163, 184, 180});
                        DrawCircleV(worldMouse, 4.0f, WHITE);
                    }
                }

                if (hoverNodeId != -1) {
                    const DeviceNode* hNode = FindNode(nodes, hoverNodeId);
                    if (hNode) {
                        Vector2 pp = GetPortPosition(*hNode, hoverPort);
                        DrawCircleV(pp, PORT_RADIUS + 3.0f, Color{34, 197, 94, 200});
                    }
                }

                for (const auto& n : nodes) DrawDeviceNode(n);
            EndMode2D();

            DrawPanel(selectedId, nodes, ps);

            // HUD — screen space, outside camera
            DrawFPS(CANVAS_W - 80, 10);
            DrawText("P=PC  R=Router  S=Switch  Del=Delete  MMB=Pan  Scroll=Zoom  Drag-port=Cable  Esc=Cancel",
                     10, SCREEN_H - 24, 12, Color{100, 116, 139, 255});
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
