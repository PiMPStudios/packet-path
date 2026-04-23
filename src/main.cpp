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

        // ── Node select + drag (world-space mouse) ─────────────────────
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
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

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragging) {
            for (auto& n : nodes) {
                if (n.id == selectedId) {
                    n.position = {worldMouse.x - dragOffset.x,
                                  worldMouse.y - dragOffset.y};
                    break;
                }
            }
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            dragging = false;

        // ── Draw ───────────────────────────────────────────────────────
        BeginDrawing();
            ClearBackground(BG_COLOR);

            BeginMode2D(camera);
                DrawDotGrid(camera);
                for (const auto& n : nodes) DrawDeviceNode(n);
            EndMode2D();

            // HUD — screen space, outside camera
            DrawFPS(SCREEN_W - 80, 10);
            DrawText("P=PC  R=Router  S=Switch  Del=Delete  MMB=Pan  Scroll=Zoom",
                     10, SCREEN_H - 24, 12, Color{100, 116, 139, 255});
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
