#include "Font.h"

static Font  gFont   = {};
static float gScale  = 1.0f;
static bool  gLoaded = false;

void InitGameFont() {
    // Codepoint set: printable ASCII + unicode symbols used in the codebase
    int codepoints[120];
    int count = 0;
    for (int i = 32; i <= 126; i++) codepoints[count++] = i;
    // ✓ ✗ → — • … ★ ☆
    int extras[] = {0x2713, 0x2717, 0x2192, 0x2014,
                    0x2022, 0x2026, 0x2605, 0x2606};
    for (int e : extras) codepoints[count++] = e;

    // Load at 64px — all draw sizes (max ~40px at 2x) are downscales → crisp
    gFont   = LoadFontEx("assets/fonts/JetBrainsMono-Regular.ttf",
                         64, codepoints, count);
    SetTextureFilter(gFont.texture, TEXTURE_FILTER_BILINEAR);
    gLoaded = true;
}

void UnloadGameFont() {
    if (gLoaded) { UnloadFont(gFont); gLoaded = false; }
}

void  SetUiScale(float scale) { gScale = scale; }
float GetUiScale()             { return gScale; }
Font& GFont()                  { return gFont; }
float FS(float base)           { return base * gScale; }
float Sp(float)                { return 1.0f; }
float TW(const char* text, float base) {
    return MeasureTextEx(gFont, text, FS(base), Sp(FS(base))).x;
}
