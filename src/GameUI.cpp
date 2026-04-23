#include "GameUI.h"
#include "NetworkCanvas.h"   // CANVAS_W, CANVAS_H
#include <cstdio>

Rectangle WinOverlayRect() {
    return {(float)(CANVAS_W - 320) / 2.0f,
            (float)(CANVAS_H - 240) / 2.0f,
            320.0f, 240.0f};
}

Rectangle WinRetryBtnRect() {
    Rectangle r = WinOverlayRect();
    return {r.x + 20, r.y + 186, 120.0f, 36.0f};
}

Rectangle WinNextBtnRect() {
    Rectangle r = WinOverlayRect();
    return {r.x + 180, r.y + 186, 120.0f, 36.0f};
}

void DrawLevelHUD(int levelId, const std::string& title,
                  int conditionsPassed, int conditionsTotal) {
    // Badge: top-left of canvas, 8px inset
    DrawRectangle(8, 8, 224, 22, Color{10, 15, 28, 210});
    DrawRectangleLinesEx({8, 8, 224, 22}, 1.0f, Color{51, 65, 85, 255});

    char buf[80];
    std::snprintf(buf, sizeof(buf), "LVL %d  %s", levelId, title.c_str());
    DrawText(buf, 14, 13, 10, Color{148, 163, 184, 255});

    // Condition counter (right-aligned inside badge)
    char prog[8];
    std::snprintf(prog, sizeof(prog), "%d/%d", conditionsPassed, conditionsTotal);
    Color progColor = (conditionsPassed == conditionsTotal && conditionsTotal > 0)
                    ? Color{34, 197, 94, 255}
                    : Color{234, 179, 8, 255};
    int pw = MeasureText(prog, 10);
    DrawText(prog, 8 + 224 - pw - 8, 13, 10, progColor);
}

void DrawWinOverlay(const LevelDef& def, bool hasNextLevel) {
    // Canvas dim
    DrawRectangle(0, 0, CANVAS_W, CANVAS_H, Color{0, 0, 0, 150});

    Rectangle r = WinOverlayRect();
    DrawRectangleRounded(r, 0.12f, 8, Color{22, 33, 62, 255});
    DrawRectangleRoundedLinesEx(r, 0.12f, 8, 2.0f, Color{59, 130, 246, 255});

    // "LEVEL COMPLETE!"
    const char* done = "LEVEL COMPLETE!";
    int tw = MeasureText(done, 20);
    DrawText(done, (int)(r.x + (r.width - tw) / 2.0f), (int)r.y + 20, 20, WHITE);

    // Level title
    int ttw = MeasureText(def.title.c_str(), 12);
    DrawText(def.title.c_str(),
             (int)(r.x + (r.width - ttw) / 2.0f), (int)r.y + 50, 12,
             Color{148, 163, 184, 255});

    // Three gold stars (UTF-8 filled star ★ = \xe2\x98\x85)
    const char* star   = "\xe2\x98\x85";
    Color       starC  = Color{234, 179, 8, 255};
    int         sx     = (int)(r.x + (r.width - 72) / 2.0f);
    DrawText(star, sx,      (int)r.y + 76, 24, starC);
    DrawText(star, sx + 24, (int)r.y + 76, 24, starC);
    DrawText(star, sx + 48, (int)r.y + 76, 24, starC);

    // Win conditions checklist
    int cy = (int)r.y + 116;
    for (const auto& wc : def.winConditions) {
        std::string line = "\xe2\x9c\x93 " + wc.description;  // UTF-8 ✓
        DrawText(line.c_str(), (int)(r.x + 20), cy, 11, Color{34, 197, 94, 255});
        cy += 18;
    }

    // Retry button
    Rectangle retry = WinRetryBtnRect();
    DrawRectangleRounded(retry, 0.3f, 4, Color{30, 41, 59, 255});
    DrawRectangleLinesEx(retry, 1.0f, Color{51, 65, 85, 255});
    {
        int rtw = MeasureText("Retry", 12);
        DrawText("Retry", (int)(retry.x + (retry.width - rtw) / 2.0f),
                 (int)(retry.y + 12), 12, Color{148, 163, 184, 255});
    }

    // Next Level button
    Rectangle next   = WinNextBtnRect();
    Color     nextBg = hasNextLevel ? Color{30, 58, 138, 255} : Color{22, 33, 62, 255};
    Color     nextBr = hasNextLevel ? Color{59, 130, 246, 255} : Color{51, 65, 85, 255};
    Color     nextTx = hasNextLevel ? WHITE : Color{51, 65, 85, 255};
    DrawRectangleRounded(next, 0.3f, 4, nextBg);
    DrawRectangleLinesEx(next, 1.0f, nextBr);
    {
        const char* nlabel = "Next Level";
        int ntw = MeasureText(nlabel, 12);
        DrawText(nlabel, (int)(next.x + (next.width - ntw) / 2.0f),
                 (int)(next.y + 12), 12, nextTx);
    }
}
