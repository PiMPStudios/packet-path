#include "GameUI.h"
#include "Layout.h"   // CANVAS_W, CANVAS_H
#include <algorithm>
#include <cstdio>

Rectangle WinOverlayRect() {
    return {(float)(CANVAS_W() - 320) / 2.0f,
            (float)(CANVAS_H() - 260) / 2.0f,
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

Rectangle SandboxMenuBtnRect()     { return {120.f, 8.f,  52.f, 22.f}; }
Rectangle LevelHudMenuBtnRect()    { return {252.f, 8.f,  52.f, 22.f}; }
Rectangle LevelHudSandboxBtnRect() { return {308.f, 8.f,  72.f, 22.f}; }

Rectangle ReplaySpeedBtnRect(int idx) {
    return {88.f + idx * 42.f, 58.f, 38.f, 18.f};
}

void DrawReplayHUD(bool paused, float speedMult) {
    // PAUSED badge — amber, shown only when paused
    if (paused) {
        DrawRectangle(8, 58, 76, 18, Color{217, 119, 6, 210});
        DrawRectangleLinesEx({8.f, 58.f, 76.f, 18.f}, 1.f, Color{251, 191, 36, 255});
        DrawText("PAUSED", 14, 62, 10, Color{254, 243, 199, 255});
    }

    // Four speed buttons
    static const float  speeds[4] = {0.25f, 0.5f, 1.f, 2.f};
    static const char*  labels[4] = {"0.25x", "0.5x", "1x", "2x"};
    for (int i = 0; i < 4; ++i) {
        Rectangle r  = ReplaySpeedBtnRect(i);
        bool      active = (speedMult == speeds[i]);
        Color bg   = active ? Color{30, 58, 138, 255} : Color{30, 41, 59, 210};
        Color brd  = active ? Color{59, 130, 246, 255} : Color{51, 65, 85, 255};
        Color txtC = active ? WHITE : Color{148, 163, 184, 255};
        DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)r.height, bg);
        DrawRectangleLinesEx(r, 1.f, brd);
        int tw = MeasureText(labels[i], 9);
        DrawText(labels[i],
                 (int)(r.x + (r.width - tw) / 2.f),
                 (int)(r.y + 4),
                 9, txtC);
    }
}

Rectangle LevelSelectCardRect(int i) {
    // 4-column grid, 180x80 cards, 10px gaps, centered in canvas
    const float cardW = 180.f, cardH = 80.f, gapX = 10.f, gapY = 10.f;
    const float gridW = 4.f * cardW + 3.f * gapX;   // 750 px
    float xs = std::max(8.f, ((float)CANVAS_W() - gridW) / 2.f);
    int col = i % 4, row = i / 4;
    return {xs + col * (cardW + gapX), 90.f + row * (cardH + gapY), cardW, cardH};
}

Rectangle LevelSelectSandboxBtnRect() {
    const float cardW = 180.f, cardH = 80.f, gapX = 10.f, gapY = 10.f;
    const float gridW = 4.f * cardW + 3.f * gapX;
    float xs = std::max(8.f, ((float)CANVAS_W() - gridW) / 2.f);
    return {xs, 90.f + 4.f * (cardH + gapY), gridW, 50.f};
}

void DrawSandboxHUD() {
    // Teal "SANDBOX" badge
    DrawRectangle(8, 8, 108, 22, Color{15, 118, 110, 210});
    DrawRectangleLinesEx({8.f, 8.f, 108.f, 22.f}, 1.0f, Color{20, 184, 166, 255});
    DrawText("SANDBOX", 14, 13, 10, Color{204, 251, 241, 255});

    // MENU button — opens level select
    Rectangle mb = SandboxMenuBtnRect();
    DrawRectangle((int)mb.x, (int)mb.y, (int)mb.width, (int)mb.height,
                  Color{30, 41, 59, 210});
    DrawRectangleLinesEx(mb, 1.0f, Color{51, 65, 85, 255});
    int tw = MeasureText("MENU", 10);
    DrawText("MENU", (int)(mb.x + (mb.width - tw) / 2.f), (int)(mb.y + 6),
             10, Color{148, 163, 184, 255});
}

void DrawLevelSelectScreen(const std::string* levelTitles, const bool* levelExists) {
    if (!levelTitles || !levelExists) return;
    // Dim the entire screen (canvas + panel)
    DrawRectangle(0, 0, SCREEN_W(), SCREEN_H(), Color{0, 0, 0, 225});

    // Title
    const char* hdr = "SELECT A LEVEL";
    int hdw = MeasureText(hdr, 18);
    DrawText(hdr, (int)((CANVAS_W() - hdw) / 2.f), 50, 18, WHITE);

    Vector2 mouse = GetMousePosition();

    // 4x4 grid of level cards  (i = 0..15 → level i+1)
    for (int i = 0; i < 16; ++i) {
        Rectangle r       = LevelSelectCardRect(i);
        bool exists       = levelExists[i];
        bool hovered      = exists && CheckCollisionPointRec(mouse, r);

        Color bg  = exists ? (hovered ? Color{30,  58, 138, 255} : Color{22, 33, 62, 255})
                           : Color{15, 20, 35, 255};
        Color brd = exists ? (hovered ? Color{59, 130, 246, 255} : Color{51, 65, 85, 255})
                           : Color{30, 36, 48, 255};
        DrawRectangleRounded(r, 0.1f, 4, bg);
        DrawRectangleRoundedLinesEx(r, 0.1f, 4, 1.5f, brd);

        // "LVL N" number
        char lvlBuf[8];
        std::snprintf(lvlBuf, sizeof(lvlBuf), "LVL %d", i + 1);
        DrawText(lvlBuf, (int)(r.x + 10), (int)(r.y + 12), 11,
                 exists ? Color{148, 163, 184, 255} : Color{51, 65, 85, 255});

        if (exists) {
            // Level title — truncate at 22 chars to fit 180 px card
            std::string title = levelTitles[i];
            if ((int)title.size() > 22) title = title.substr(0, 19) + "...";
            DrawText(title.c_str(), (int)(r.x + 10), (int)(r.y + 32), 10,
                     Color{203, 213, 225, 255});
        } else {
            DrawText("Coming Soon", (int)(r.x + 10), (int)(r.y + 32), 10,
                     Color{51, 65, 85, 255});
        }
    }

    // Full-width sandbox card below the grid
    Rectangle sr   = LevelSelectSandboxBtnRect();
    bool sandHover = CheckCollisionPointRec(mouse, sr);
    Color sbg  = sandHover ? Color{15, 118, 110, 255} : Color{13,  94,  88, 255};
    Color sbrd = sandHover ? Color{20, 184, 166, 255} : Color{13, 148, 136, 255};
    DrawRectangleRounded(sr, 0.12f, 4, sbg);
    DrawRectangleRoundedLinesEx(sr, 0.12f, 4, 1.5f, sbrd);

    const char* sandTxt = "SANDBOX \xe2\x80\x94 Free Build Mode";
    int stw = MeasureText(sandTxt, 13);
    DrawText(sandTxt,
             (int)(sr.x + (sr.width  - stw) / 2.f),
             (int)(sr.y + (sr.height - 13)  / 2.f - 2.f),
             13, Color{204, 251, 241, 255});
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
    DrawRectangle(0, 0, CANVAS_W(), CANVAS_H(), Color{0, 0, 0, 150});

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

void DrawFileDialog(FileOpState        state,
                    const std::string& buf,
                    const std::string& msg,
                    float              msgTimer)
{
    // Toast only (dialog closed, show brief result)
    if (state == FILEOP_NONE) {
        if (msgTimer <= 0.f || msg.empty()) return;
        bool  ok  = (msg.rfind("Error", 0) != 0);
        Color tc  = ok ? Color{34, 197, 94, 255} : Color{239, 68, 68, 255};
        int   tw  = MeasureText(msg.c_str(), 11);
        DrawRectangle((CANVAS_W() - tw) / 2 - 8, 6, tw + 16, 20,
                      Color{15, 23, 42, 220});
        DrawText(msg.c_str(), (CANVAS_W() - tw) / 2, 10, 11, tc);
        return;
    }

    // Dim the screen behind the modal
    DrawRectangle(0, 0, SCREEN_W(), SCREEN_H(), Color{0, 0, 0, 160});

    // Dialog box
    const float BW = 480.f, BH = 148.f;
    const float BX = (SCREEN_W() - BW) / 2.f;
    const float BY = (SCREEN_H() - BH) / 2.f;

    DrawRectangleRounded({BX, BY, BW, BH}, 0.08f, 6, Color{22, 33, 62, 255});
    DrawRectangleRoundedLinesEx({BX, BY, BW, BH}, 0.08f, 6, 1.5f,
                                Color{51, 65, 85, 255});

    const char* title = (state == FILEOP_SAVING) ? "Save Scene" : "Open Scene";
    DrawText(title, (int)(BX + 20), (int)(BY + 16), 14, WHITE);

    // Filename input field
    DrawRectangleRounded({BX + 20, BY + 46, BW - 40, 32}, 0.12f, 4,
                         Color{15, 23, 42, 255});
    DrawRectangleRoundedLinesEx({BX + 20, BY + 46, BW - 40, 32}, 0.12f, 4,
                                1.0f, Color{94, 234, 212, 255});
    std::string display = buf + "|";
    DrawText(display.c_str(), (int)(BX + 28), (int)(BY + 54), 12,
             Color{203, 213, 225, 255});

    DrawText("Enter to confirm   \xe2\x80\xa2   Esc to cancel",
             (int)(BX + 20), (int)(BY + 96), 10, Color{100, 116, 139, 255});

    // Error/status message inside dialog
    if (!msg.empty()) {
        bool  ok = (msg.rfind("Error", 0) != 0);
        Color ec = ok ? Color{34, 197, 94, 255} : Color{239, 68, 68, 255};
        DrawText(msg.c_str(), (int)(BX + 20), (int)(BY + 120), 10, ec);
    }
}
