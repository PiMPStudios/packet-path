#include "raylib.h"

static const int   SCREEN_W = 1280;
static const int   SCREEN_H = 720;
static const Color BG_COLOR = {15, 23, 42, 255};  // dark navy

int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Packet Path");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
            ClearBackground(BG_COLOR);
            DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
