#include "raylib.h"
#include <string>

// ── Constants ─────────────────────────────────────────────────────────────
static const int   SCREEN_W = 1280;
static const int   SCREEN_H = 720;
static const float NODE_W      = 120.0f;
static const float NODE_H      =  60.0f;
static const int   NODE_FONT_SZ =  14;
static const Color BG_COLOR    = {15, 23, 42, 255};

// ── Device types & node struct ─────────────────────────────────────────────
enum DeviceType { PC, ROUTER, SWITCH };

struct DeviceNode {
    int         id;
    DeviceType  type;
    Vector2     position;    // world-space centre
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

    // drop shadow
    DrawRectangleRounded({r.x + 3, r.y + 3, r.width, r.height}, 0.3f, 8,
                         Color{0, 0, 0, 80});
    // body
    DrawRectangleRounded(r, 0.3f, 8, c);
    // selection ring
    if (n.selected)
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, 2.5f, WHITE);

    // centred label
    int tw  = MeasureText(n.label.c_str(), NODE_FONT_SZ);
    DrawText(n.label.c_str(),
             (int)(n.position.x - tw / 2.0f),
             (int)(n.position.y - NODE_FONT_SZ / 2.0f),
             NODE_FONT_SZ, WHITE);
}

// ── Main ──────────────────────────────────────────────────────────────────
int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Packet Path");
    SetTargetFPS(60);

    DeviceNode node;
    node.id       = 1;
    node.type     = PC;
    node.position = {SCREEN_W / 2.0f, SCREEN_H / 2.0f};
    node.label    = "PC-1";

    bool    dragging   = false;
    Vector2 dragOffset = {0, 0};

    while (!WindowShouldClose()) {
        // ── Input ──────────────────────────────────────────────────────
        Vector2 mouse = GetMousePosition();

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mouse, GetNodeRect(node))) {
                dragging      = true;
                node.selected = true;
                dragOffset    = {mouse.x - node.position.x,
                                 mouse.y - node.position.y};
            } else {
                node.selected = false;
            }
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragging)
            node.position = {mouse.x - dragOffset.x, mouse.y - dragOffset.y};
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            dragging = false;

        // ── Draw ───────────────────────────────────────────────────────
        BeginDrawing();
            ClearBackground(BG_COLOR);
            DrawDeviceNode(node);
            DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
