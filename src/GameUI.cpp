#include "GameUI.h"
#include "NetworkCanvas.h"   // CANVAS_W, CANVAS_H
#include <cstdio>

Rectangle WinOverlayRect() {
    return {(float)(CANVAS_W - 320) / 2.0f,
            (float)(CANVAS_H - 260) / 2.0f,
            320.0f, 260.0f};
}

Rectangle WinRetryBtnRect() {
    Rectangle r = WinOverlayRect();
    return {r.x + 20, r.y + 206, 120.0f, 36.0f};
}

Rectangle WinNextBtnRect() {
    Rectangle r = WinOverlayRect();
    return {r.x + 180, r.y + 206, 120.0f, 36.0f};
}

void DrawLevelHUD(int levelId, const std::string& title,
                  int conditionsPassed, int conditionsTotal,
                  int starsEarned) {
    // Badge: top-left of canvas, 8px inset — widened to 240px to hold star dots
    DrawRectangle(8, 8, 240, 22, Color{10, 15, 28, 210});
    DrawRectangleLinesEx({8, 8, 240, 22}, 1.0f, Color{51, 65, 85, 255});

    char buf[80];
    std::snprintf(buf, sizeof(buf), "LVL %d  %s", levelId, title.c_str());
    DrawText(buf, 14, 13, 10, Color{148, 163, 184, 255});

    if (starsEarned > 0) {
        // Star dots: 3 circles at right side, spacing 12px, radius 4
        int dotCx[3] = {220, 232, 243};
        int dotCy    = 19;
        for (int i = 0; i < 3; ++i) {
            if (i < starsEarned)
                DrawCircle(dotCx[i], dotCy, 4.0f, Color{234, 179, 8, 255});
            else
                DrawCircleLines(dotCx[i], dotCy, 4.0f, Color{71, 85, 105, 255});
        }
    } else {
        // Condition counter (right-aligned inside badge)
        char prog[8];
        std::snprintf(prog, sizeof(prog), "%d/%d", conditionsPassed, conditionsTotal);
        Color progColor = (conditionsPassed == conditionsTotal && conditionsTotal > 0)
                        ? Color{34, 197, 94, 255}
                        : Color{234, 179, 8, 255};
        int pw = MeasureText(prog, 10);
        DrawText(prog, 8 + 240 - pw - 8, 13, 10, progColor);
    }
}

void DrawWinOverlay(const LevelDef& def, bool hasNextLevel, int starsEarned) {
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

    // Stars: filled gold = earned, grey ring = not yet earned
    float sy  = r.y + 90.0f;
    float scx = r.x + r.width / 2.0f;
    float offsets[3] = {-30.0f, 0.0f, 30.0f};
    for (int i = 0; i < 3; ++i) {
        int cx = (int)(scx + offsets[i]);
        int cy = (int)sy;
        if (i < starsEarned)
            DrawCircle(cx, cy, 11.0f, Color{234, 179, 8, 255});
        else
            DrawCircleLines(cx, cy, 11.0f, Color{71, 85, 105, 255});
    }

    // Score label beneath stars
    const char* scoreLabel = (starsEarned == 3) ? "PERFECT!"
                           : (starsEarned == 2) ? "GREAT!" : "CLEARED!";
    Color slColor = (starsEarned == 3) ? Color{234, 179, 8, 255}
                  : (starsEarned == 2) ? Color{148, 163, 184, 255}
                  :                      Color{100, 116, 139, 255};
    int slw = MeasureText(scoreLabel, 11);
    DrawText(scoreLabel, (int)(r.x + (r.width - slw) / 2.0f), (int)r.y + 110, 11, slColor);

    // Separator
    DrawLineEx({r.x + 16, r.y + 126}, {r.x + r.width - 16, r.y + 126},
               0.5f, Color{51, 65, 85, 255});

    // Win conditions checklist (capped at 3 rows to stay above buttons)
    int cy2 = (int)r.y + 134;
    int shownConditions = 0;
    for (const auto& wc : def.winConditions) {
        if (shownConditions >= 3) break;
        char lineBuf[128];
        std::snprintf(lineBuf, sizeof(lineBuf), "\xe2\x9c\x93 %s", wc.description.c_str());
        DrawText(lineBuf, (int)(r.x + 20), cy2, 11, Color{34, 197, 94, 255});
        cy2 += 18;
        ++shownConditions;
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
