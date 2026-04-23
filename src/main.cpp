#include "raylib.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

// ── Constants ─────────────────────────────────────────────────────────────
static const int   SCREEN_W     = 1280;
static const int   SCREEN_H     = 720;
static const float NODE_W       = 120.0f;
static const float NODE_H       =  60.0f;
static const int   NODE_FONT_SZ =  14;
static const float PORT_RADIUS    =  6.0f;
static const int   PORTS_PER_NODE =  4;    // top=0, right=1, bottom=2, left=3
static const Color BG_COLOR     = {15, 23, 42, 255};

// ── Device types & node struct ─────────────────────────────────────────────
enum DeviceType { PC, ROUTER, SWITCH };

struct DeviceNode {
    int         id       = 0;
    DeviceType  type     = PC;
    Vector2     position = {0.0f, 0.0f};
    std::string label;
    bool        selected = false;
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
    Vector2 botRight = GetScreenToWorld2D({(float)SCREEN_W, (float)SCREEN_H}, cam);

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

DeviceNode* FindNode(std::vector<DeviceNode>& nodes, int id) {
    for (auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

void DrawAllCables(const std::vector<Cable>& cables,
                   std::vector<DeviceNode>& nodes)
{
    for (const auto& c : cables) {
        DeviceNode* from = FindNode(nodes, c.fromId);
        DeviceNode* to   = FindNode(nodes, c.toId);
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

// ── Spawn helper ──────────────────────────────────────────────────────────
static int nextId = 1;

// Requires InitWindow() — uses raylib RNG (GetRandomValue).
DeviceNode SpawnNode(DeviceType type) {
    const char* names[] = {"PC", "RTR", "SW"};
    DeviceNode n;
    n.id       = nextId++;
    n.type     = type;
    n.position = {(float)GetRandomValue(-200, 200),
                  (float)GetRandomValue(-200, 200)};
    n.label    = std::string(names[(int)type]) + "-" + std::to_string(n.id);
    return n;
}

// ── Main ──────────────────────────────────────────────────────────────────
int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Packet Path");
    SetTargetFPS(60);

    std::vector<DeviceNode> nodes;
    nodes.push_back(SpawnNode(PC));

    Camera2D camera = {};
    camera.offset   = {SCREEN_W / 2.0f, SCREEN_H / 2.0f};
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

    while (!WindowShouldClose()) {
        Vector2 screenMouse = GetMousePosition();
        Vector2 worldMouse  = GetScreenToWorld2D(screenMouse, camera);

        // ── Spawn / delete ─────────────────────────────────────────────
        if (IsKeyPressed(KEY_P)) nodes.push_back(SpawnNode(PC));
        if (IsKeyPressed(KEY_R)) nodes.push_back(SpawnNode(ROUTER));
        if (IsKeyPressed(KEY_S)) nodes.push_back(SpawnNode(SWITCH));

        if (IsKeyPressed(KEY_DELETE) && selectedId != -1) {
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const DeviceNode& n){ return n.id == selectedId; }),
                nodes.end());
            selectedId = -1;
            dragging   = false;
        }

        // ── Camera pan (middle mouse) ──────────────────────────────────
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            Vector2 delta    = GetMouseDelta();
            camera.target.x -= delta.x / camera.zoom;
            camera.target.y -= delta.y / camera.zoom;
        }

        // ── Camera zoom (scroll wheel, cursor-anchored) ────────────────
        float wheel = std::clamp(GetMouseWheelMove(), -3.0f, 3.0f);
        if (wheel != 0.0f) {
            Vector2 beforeZoom = GetScreenToWorld2D(screenMouse, camera);
            camera.zoom *= (1.0f + wheel * 0.1f);
            camera.zoom  = std::clamp(camera.zoom, 0.15f, 4.0f);
            Vector2 afterZoom  = GetScreenToWorld2D(screenMouse, camera);
            camera.target.x   += beforeZoom.x - afterZoom.x;
            camera.target.y   += beforeZoom.y - afterZoom.y;
        }

        // ── LMB pressed ───────────────────────────────────────────────
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
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
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            if (dragging) {
                for (auto& n : nodes) {
                    if (n.id == selectedId) {
                        n.position = {worldMouse.x - dragOffset.x,
                                      worldMouse.y - dragOffset.y};
                        break;
                    }
                }
            }
            if (connecting) {
                hoverNodeId = -1;
                hoverPort   = -1;
                HitTestPort(nodes, worldMouse, connectFromId, hoverNodeId, hoverPort);
            }
        }

        // ── LMB released ──────────────────────────────────────────────
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (connecting && hoverNodeId != -1) {
                bool exists = false;
                for (const auto& c : cables) {
                    if ((c.fromId == connectFromId && c.fromPort == connectFromPort &&
                         c.toId   == hoverNodeId   && c.toPort   == hoverPort) ||
                        (c.fromId == hoverNodeId   && c.fromPort == hoverPort &&
                         c.toId   == connectFromId && c.toPort   == connectFromPort))
                    { exists = true; break; }
                }
                if (!exists)
                    cables.push_back({connectFromId, connectFromPort,
                                      hoverNodeId,   hoverPort});
            }
            connecting  = false;
            dragging    = false;
            hoverNodeId = -1;
        }

        // ── Draw ───────────────────────────────────────────────────────
        BeginDrawing();
            ClearBackground(BG_COLOR);

            BeginMode2D(camera);
                DrawDotGrid(camera);
                DrawAllCables(cables, nodes);

                if (connecting) {
                    DeviceNode* fromNode = FindNode(nodes, connectFromId);
                    if (fromNode) {
                        Vector2 p0 = GetPortPosition(*fromNode, connectFromPort);
                        DrawLineEx(p0, worldMouse, 2.0f, Color{148, 163, 184, 180});
                        DrawCircleV(worldMouse, 4.0f, WHITE);
                    }
                }

                if (hoverNodeId != -1) {
                    DeviceNode* hNode = FindNode(nodes, hoverNodeId);
                    if (hNode) {
                        Vector2 pp = GetPortPosition(*hNode, hoverPort);
                        DrawCircleV(pp, PORT_RADIUS + 3.0f, Color{34, 197, 94, 200});
                    }
                }

                for (const auto& n : nodes) DrawDeviceNode(n);
            EndMode2D();

            // HUD — screen space, outside camera
            DrawFPS(SCREEN_W - 80, 10);
            DrawText("P=PC  R=Router  S=Switch  Del=Delete  MMB=Pan  Scroll=Zoom  Drag-port=Cable",
                     10, SCREEN_H - 24, 12, Color{100, 116, 139, 255});
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
