#include "raylib.h"
#include <string>
#include <vector>
#include <algorithm>

// ── Constants ─────────────────────────────────────────────────────────────
static const int   SCREEN_W     = 1280;
static const int   SCREEN_H     = 720;
static const float NODE_W       = 120.0f;
static const float NODE_H       =  60.0f;
static const int   NODE_FONT_SZ =  14;
static const Color BG_COLOR     = {15, 23, 42, 255};

// ── Device types & node struct ─────────────────────────────────────────────
enum DeviceType { PC, ROUTER, SWITCH };

struct DeviceNode {
    int         id;
    DeviceType  type;
    Vector2     position;
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
}

// ── Spawn helper ──────────────────────────────────────────────────────────
static int nextId = 1;

DeviceNode SpawnNode(DeviceType type) {
    const char* names[] = {"PC", "RTR", "SW"};
    DeviceNode n;
    n.id       = nextId++;
    n.type     = type;
    n.position = {SCREEN_W / 2.0f + (float)GetRandomValue(-40, 40),
                  SCREEN_H / 2.0f + (float)GetRandomValue(-40, 40)};
    n.label    = std::string(names[(int)type]) + "-" + std::to_string(n.id);
    return n;
}

// ── Main ──────────────────────────────────────────────────────────────────
int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Packet Path");
    SetTargetFPS(60);

    std::vector<DeviceNode> nodes;
    nodes.push_back(SpawnNode(PC));

    int     selectedId = -1;
    bool    dragging   = false;
    Vector2 dragOffset = {0.0f, 0.0f};

    while (!WindowShouldClose()) {
        Vector2 mouse = GetMousePosition();

        // Spawn keys
        if (IsKeyPressed(KEY_P)) nodes.push_back(SpawnNode(PC));
        if (IsKeyPressed(KEY_R)) nodes.push_back(SpawnNode(ROUTER));
        if (IsKeyPressed(KEY_S)) nodes.push_back(SpawnNode(SWITCH));

        // Delete selected
        if (IsKeyPressed(KEY_DELETE) && selectedId != -1) {
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const DeviceNode& n){ return n.id == selectedId; }),
                nodes.end());
            selectedId = -1;
        }

        // LMB: select + start drag (back-to-front hit detection for correct z-order)
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int hitId = -1;
            for (int i = (int)nodes.size() - 1; i >= 0; --i) {
                if (CheckCollisionPointRec(mouse, GetNodeRect(nodes[i]))) {
                    hitId = nodes[i].id;
                    break;
                }
            }
            for (auto& n : nodes) n.selected = (n.id == hitId);
            selectedId = hitId;

            if (selectedId != -1) {
                dragging = true;
                for (auto& n : nodes) {
                    if (n.id == selectedId) {
                        dragOffset = {mouse.x - n.position.x,
                                      mouse.y - n.position.y};
                        break;
                    }
                }
            }
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragging) {
            for (auto& n : nodes) {
                if (n.id == selectedId) {
                    n.position = {mouse.x - dragOffset.x,
                                  mouse.y - dragOffset.y};
                    break;
                }
            }
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            dragging = false;

        // Draw
        BeginDrawing();
            ClearBackground(BG_COLOR);
            for (const auto& n : nodes) DrawDeviceNode(n);
            DrawFPS(10, 10);
            DrawText("P=PC  R=Router  S=Switch  Del=Delete selected",
                     10, SCREEN_H - 24, 12, Color{100, 116, 139, 255});
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
