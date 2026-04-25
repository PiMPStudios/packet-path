#include "GameUI.h"
#include "Font.h"
#include "Layout.h"   // CANVAS_W, CANVAS_H
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ── Word-wrap helper ──────────────────────────────────────────────────────
static std::vector<std::string> WordWrap(const std::string& text, int maxW, int fontSize) {
    std::vector<std::string> lines;
    std::string word, line;
    for (size_t i = 0; i <= text.size(); ++i) {
        char c = (i < text.size()) ? text[i] : ' ';
        if (c == ' ' || c == '\n') {
            if (!word.empty()) {
                std::string cand = line.empty() ? word : line + " " + word;
                if ((int)TW(cand.c_str(), fontSize) <= maxW) {
                    line = cand;
                } else {
                    if (!line.empty()) lines.push_back(line);
                    line = word;
                }
                word.clear();
            }
            if (c == '\n') { lines.push_back(line); line.clear(); }
        } else {
            word += c;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

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
        DrawTextEx(GFont(), "PAUSED", {14.f, 62.f}, FS(10), Sp(FS(10)), Color{254, 243, 199, 255});
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
        int tw = (int)TW(labels[i], 9);
        DrawTextEx(GFont(), labels[i],
                   {r.x + (r.width - tw) / 2.f, r.y + 4.f},
                   FS(9), Sp(FS(9)), txtC);
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
    DrawTextEx(GFont(), "SANDBOX", {14.f, 13.f}, FS(10), Sp(FS(10)), Color{204, 251, 241, 255});

    // MENU button — opens level select
    Rectangle mb = SandboxMenuBtnRect();
    DrawRectangle((int)mb.x, (int)mb.y, (int)mb.width, (int)mb.height,
                  Color{30, 41, 59, 210});
    DrawRectangleLinesEx(mb, 1.0f, Color{51, 65, 85, 255});
    int tw = (int)TW("MENU", 10);
    DrawTextEx(GFont(), "MENU",
               {mb.x + (mb.width - tw) / 2.f, mb.y + 6.f},
               FS(10), Sp(FS(10)), Color{148, 163, 184, 255});
}

void DrawLevelSelectScreen(const std::string* levelTitles, const bool* levelExists) {
    if (!levelTitles || !levelExists) return;
    // Dim the entire screen (canvas + panel)
    DrawRectangle(0, 0, SCREEN_W(), SCREEN_H(), Color{0, 0, 0, 225});

    // Title
    const char* hdr = "SELECT A LEVEL";
    int hdw = (int)TW(hdr, 18);
    DrawTextEx(GFont(), hdr,
               {(float)((CANVAS_W() - hdw) / 2.f), 50.f},
               FS(18), Sp(FS(18)), WHITE);

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
        DrawTextEx(GFont(), lvlBuf,
                   {r.x + 10.f, r.y + 12.f},
                   FS(11), Sp(FS(11)),
                   exists ? Color{148, 163, 184, 255} : Color{51, 65, 85, 255});

        if (exists) {
            // Level title — truncate at 22 chars to fit 180 px card
            std::string title = levelTitles[i];
            if ((int)title.size() > 22) title = title.substr(0, 19) + "...";
            DrawTextEx(GFont(), title.c_str(),
                       {r.x + 10.f, r.y + 32.f},
                       FS(10), Sp(FS(10)), Color{203, 213, 225, 255});
        } else {
            DrawTextEx(GFont(), "Coming Soon",
                       {r.x + 10.f, r.y + 32.f},
                       FS(10), Sp(FS(10)), Color{51, 65, 85, 255});
        }
    }

    // Full-width sandbox card below the grid
    Rectangle sr   = LevelSelectSandboxBtnRect();
    bool sandHover = CheckCollisionPointRec(mouse, sr);
    Color sbg  = sandHover ? Color{15, 118, 110, 255} : Color{13,  94,  88, 255};
    Color sbrd = sandHover ? Color{20, 184, 166, 255} : Color{13, 148, 136, 255};
    DrawRectangleRounded(sr, 0.12f, 4, sbg);
    DrawRectangleRoundedLinesEx(sr, 0.12f, 4, 1.5f, sbrd);

    const char* sandTxt = "SANDBOX: Free Build Mode";
    int stw = (int)TW(sandTxt, 13);
    DrawTextEx(GFont(), sandTxt,
               {sr.x + (sr.width - stw) / 2.f,
                sr.y + (sr.height - 13.f) / 2.f - 2.f},
               FS(13), Sp(FS(13)), Color{204, 251, 241, 255});
}

void DrawLevelHUD(int levelId, const std::string& title,
                  int conditionsPassed, int conditionsTotal,
                  int starsEarned) {
    // Badge: top-left of canvas, 8px inset — widened to 240px to hold star dots
    DrawRectangle(8, 8, 240, 22, Color{10, 15, 28, 210});
    DrawRectangleLinesEx({8, 8, 240, 22}, 1.0f, Color{51, 65, 85, 255});

    char buf[80];
    std::snprintf(buf, sizeof(buf), "LVL %d  %s", levelId, title.c_str());
    DrawTextEx(GFont(), buf, {14.f, 13.f}, FS(10), Sp(FS(10)), Color{148, 163, 184, 255});

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
        int pw = (int)TW(prog, 10);
        DrawTextEx(GFont(), prog,
                   {(float)(8 + 240 - pw - 8), 13.f},
                   FS(10), Sp(FS(10)), progColor);
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
    int tw = (int)TW(done, 20);
    DrawTextEx(GFont(), done,
               {r.x + (r.width - tw) / 2.0f, r.y + 20.f},
               FS(20), Sp(FS(20)), WHITE);

    // Level title
    int ttw = (int)TW(def.title.c_str(), 12);
    DrawTextEx(GFont(), def.title.c_str(),
               {r.x + (r.width - ttw) / 2.0f, r.y + 50.f},
               FS(12), Sp(FS(12)), Color{148, 163, 184, 255});

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
    int slw = (int)TW(scoreLabel, 11);
    DrawTextEx(GFont(), scoreLabel,
               {r.x + (r.width - slw) / 2.0f, r.y + 110.f},
               FS(11), Sp(FS(11)), slColor);

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
        DrawTextEx(GFont(), lineBuf,
                   {r.x + 20.f, (float)cy2},
                   FS(11), Sp(FS(11)), Color{34, 197, 94, 255});
        cy2 += 18;
        ++shownConditions;
    }

    // Retry button
    Rectangle retry = WinRetryBtnRect();
    DrawRectangleRounded(retry, 0.3f, 4, Color{30, 41, 59, 255});
    DrawRectangleLinesEx(retry, 1.0f, Color{51, 65, 85, 255});
    {
        int rtw = (int)TW("Retry", 12);
        DrawTextEx(GFont(), "Retry",
                   {retry.x + (retry.width - rtw) / 2.0f, retry.y + 12.f},
                   FS(12), Sp(FS(12)), Color{148, 163, 184, 255});
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
        int ntw = (int)TW(nlabel, 12);
        DrawTextEx(GFont(), nlabel,
                   {next.x + (next.width - ntw) / 2.0f, next.y + 12.f},
                   FS(12), Sp(FS(12)), nextTx);
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
        float tw  = TW(msg.c_str(), 11);
        DrawRectangle((int)((float)CANVAS_W() * 0.5f - tw * 0.5f - 8.f), 6, (int)(tw + 16.f), 20,
                      Color{15, 23, 42, 220});
        DrawTextEx(GFont(), msg.c_str(),
                   {(float)CANVAS_W() * 0.5f - tw * 0.5f, 10.f},
                   FS(11), Sp(FS(11)), tc);
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
    DrawTextEx(GFont(), title, {BX + 20.f, BY + 16.f}, FS(14), Sp(FS(14)), WHITE);

    // Filename input field
    DrawRectangleRounded({BX + 20, BY + 46, BW - 40, 32}, 0.12f, 4,
                         Color{15, 23, 42, 255});
    DrawRectangleRoundedLinesEx({BX + 20, BY + 46, BW - 40, 32}, 0.12f, 4,
                                1.0f, Color{94, 234, 212, 255});
    std::string display = buf + "|";
    DrawTextEx(GFont(), display.c_str(),
               {BX + 28.f, BY + 54.f},
               FS(12), Sp(FS(12)), Color{203, 213, 225, 255});

    DrawTextEx(GFont(), "Enter to confirm   \xe2\x80\xa2   Esc to cancel",
               {BX + 20.f, BY + 96.f},
               FS(10), Sp(FS(10)), Color{100, 116, 139, 255});

    // Error/status message inside dialog
    if (!msg.empty()) {
        bool  ok = (msg.rfind("Error", 0) != 0);
        Color ec = ok ? Color{34, 197, 94, 255} : Color{239, 68, 68, 255};
        DrawTextEx(GFont(), msg.c_str(),
                   {BX + 20.f, BY + 120.f},
                   FS(10), Sp(FS(10)), ec);
    }
}

// ── Mission briefing card ─────────────────────────────────────────────────

Rectangle BriefingCardRect() {
    const float W = std::min(520.f, (float)CANVAS_W() - 40.f);
    return {((float)CANVAS_W() - W) / 2.f, 82.f, W, 210.f};
}
Rectangle BriefingCloseBtnRect() {
    Rectangle r = BriefingCardRect();
    return {r.x + r.width - 26.f, r.y + 6.f, 20.f, 20.f};
}
Rectangle BriefingGotItBtnRect() {
    Rectangle r = BriefingCardRect();
    return {r.x + r.width - 92.f, r.y + r.height - 34.f, 82.f, 26.f};
}

void DrawBriefingCard(const LevelDef& def) {
    Rectangle r     = BriefingCardRect();
    Rectangle close = BriefingCloseBtnRect();
    Rectangle gotit = BriefingGotItBtnRect();
    Vector2   mouse = GetMousePosition();

    // Card body
    DrawRectangleRounded(r, 0.07f, 6, Color{10, 17, 35, 245});
    DrawRectangleRoundedLinesEx(r, 0.07f, 6, 1.5f, Color{59, 130, 246, 180});

    // Title bar
    DrawRectangleRounded({r.x, r.y, r.width, 30.f}, 0.07f, 6, Color{23, 47, 110, 240});
    const char* hdr = "MISSION BRIEFING";
    int hw = (int)TW(hdr, 11);
    DrawTextEx(GFont(), hdr,
               {r.x + (r.width - hw) / 2.f, r.y + 9.f},
               FS(11), Sp(FS(11)), Color{147, 197, 253, 255});

    // [×] close button
    bool closeHov = CheckCollisionPointRec(mouse, close);
    DrawRectangleRounded(close, 0.35f, 4,
                         closeHov ? Color{239,68,68,220} : Color{51,65,85,180});
    DrawTextEx(GFont(), "x",
               {close.x + (close.width - (int)TW("x", 10)) / 2.f, close.y + 5.f},
               FS(10), Sp(FS(10)), WHITE);

    // Briefing text (word-wrapped)
    int px   = (int)(r.x + 14);
    int py   = (int)(r.y + 38);
    int maxW = (int)(r.width - 28);

    auto lines = WordWrap(def.briefing, maxW, 11);
    for (const auto& ln : lines) {
        if (py > (int)(r.y + r.height - 60)) break;   // stop before button row
        DrawTextEx(GFont(), ln.c_str(),
                   {(float)px, (float)py},
                   FS(11), Sp(FS(11)), Color{203, 213, 225, 255});
        py += 16;
    }

    // Win conditions
    if (!def.winConditions.empty()) {
        py += 6;
        DrawTextEx(GFont(), "OBJECTIVE",
                   {(float)px, (float)py},
                   FS(9), Sp(FS(9)), Color{147, 197, 253, 255});
        py += 14;
        for (const auto& wc : def.winConditions) {
            if (py > (int)(r.y + r.height - 44)) break;
            std::string bullet = "\xE2\x96\xB8 " + wc.description;
            DrawTextEx(GFont(), bullet.c_str(),
                       {(float)(px + 4), (float)py},
                       FS(10), Sp(FS(10)), Color{167, 243, 208, 255});
            py += 14;
        }
    }

    // [Got it] button
    bool gotHov = CheckCollisionPointRec(mouse, gotit);
    DrawRectangleRounded(gotit, 0.35f, 4,
                         gotHov ? Color{37,99,235,255} : Color{30,58,138,220});
    DrawRectangleRoundedLinesEx(gotit, 0.35f, 4, 1.f, Color{59,130,246,160});
    const char* gtxt = "Got it";
    DrawTextEx(GFont(), gtxt,
               {gotit.x + (gotit.width - (int)TW(gtxt, 10)) / 2.f, gotit.y + 8.f},
               FS(10), Sp(FS(10)), WHITE);
}

// ── Help overlay ──────────────────────────────────────────────────────────

Rectangle HelpOverlayRect() {
    const float W = std::min(640.f, (float)SCREEN_W() - 40.f);
    const float H = std::min(400.f, (float)SCREEN_H() - 40.f);
    return {((float)SCREEN_W() - W) / 2.f, ((float)SCREEN_H() - H) / 2.f, W, H};
}
Rectangle HelpCloseBtnRect() {
    Rectangle r = HelpOverlayRect();
    return {r.x + r.width - 26.f, r.y + 6.f, 20.f, 20.f};
}

void DrawHelpOverlay() {
    // Dim
    DrawRectangle(0, 0, SCREEN_W(), SCREEN_H(), Color{0, 0, 0, 150});

    Rectangle r     = HelpOverlayRect();
    Rectangle close = HelpCloseBtnRect();
    Vector2   mouse = GetMousePosition();

    DrawRectangleRounded(r, 0.05f, 8, Color{10, 17, 35, 252});
    DrawRectangleRoundedLinesEx(r, 0.05f, 8, 1.5f, Color{59, 130, 246, 180});

    // Title bar
    DrawRectangleRounded({r.x, r.y, r.width, 30.f}, 0.05f, 8, Color{23, 47, 110, 240});
    const char* hdr = "KEYBOARD REFERENCE";
    DrawTextEx(GFont(), hdr,
               {r.x + (r.width - (int)TW(hdr, 12)) / 2.f, r.y + 9.f},
               FS(12), Sp(FS(12)), WHITE);

    // [×] close
    bool closeHov = CheckCollisionPointRec(mouse, close);
    DrawRectangleRounded(close, 0.35f, 4,
                         closeHov ? Color{239,68,68,220} : Color{51,65,85,180});
    DrawTextEx(GFont(), "x",
               {close.x + (close.width - (int)TW("x", 10)) / 2.f, close.y + 5.f},
               FS(10), Sp(FS(10)), WHITE);

    float kx  = r.x + 18.f;     // key column x
    float dx  = kx + 130.f;     // desc column x (left pane)
    float kx2 = r.x + r.width / 2.f + 8.f;
    float dx2 = kx2 + 130.f;    // desc column x (right pane)

    const Color secClr  = Color{147, 197, 253, 255};
    const Color keyClr  = Color{234, 179,   8, 255};
    const Color descClr = Color{203, 213, 225, 255};

    auto sec = [&](float x, int& y, const char* label) {
        DrawTextEx(GFont(), label, {x, (float)y}, FS(9), Sp(FS(9)), secClr);
        y += 15;
    };
    auto row = [&](float kCol, float dCol, int& y, const char* key, const char* desc) {
        DrawTextEx(GFont(), key,  {kCol, (float)y}, FS(10), Sp(FS(10)), keyClr);
        DrawTextEx(GFont(), desc, {dCol, (float)y}, FS(10), Sp(FS(10)), descClr);
        y += 15;
    };

    // Left column
    int ly = (int)(r.y + 40);
    sec(kx, ly, "CANVAS / EDIT");
    row(kx, dx, ly, "P",          "Add PC at cursor");
    row(kx, dx, ly, "R",          "Add Router");
    row(kx, dx, ly, "S",          "Add Switch");
    row(kx, dx, ly, "Del",        "Delete selected");
    row(kx, dx, ly, "T",          "Troubleshoot mode");
    row(kx, dx, ly, "Drag port",  "Draw cable");
    row(kx, dx, ly, "MMB drag",   "Pan camera");
    row(kx, dx, ly, "Scroll",     "Zoom in / out");
    row(kx, dx, ly, "Right-click","Context menu");
    ly += 4;
    sec(kx, ly, "MENU / FILE");
    row(kx, dx, ly, "M",      "Open menu");
    row(kx, dx, ly, "Esc",    "Open menu (when idle)");
    row(kx, dx, ly, "Ctrl+S", "Save scene");
    row(kx, dx, ly, "Ctrl+O", "Open scene");

    // Right column
    int ry = (int)(r.y + 40);
    sec(kx2, ry, "SIMULATION");
    row(kx2, dx2, ry, "Right-click", "Open context menu");
    row(kx2, dx2, ry, "  \xe2\x86\x92 Send Packet", "Start packet trace");
    row(kx2, dx2, ry, "Esc",         "Cancel selection");
    ry += 4;
    sec(kx2, ry, "REPLAY");
    row(kx2, dx2, ry, "Space",        "Pause / resume");
    row(kx2, dx2, ry, "\xe2\x86\x92 arrow",      "Step one hop (paused)");
    row(kx2, dx2, ry, "Speed buttons","0.25\xc3\xb7 / 0.5\xc3\xb7 / 1\xc3\xb7 / 2\xc3\xb7");
    ry += 4;
    sec(kx2, ry, "GENERAL");
    row(kx2, dx2, ry, "H",  "Toggle this help");
    row(kx2, dx2, ry, "Esc","Cancel / close dialogs");

    // Footer hint
    DrawTextEx(GFont(), "Press H or click \xc3\x97 to close",
               {r.x + 14.f, r.y + r.height - 20.f},
               FS(9), Sp(FS(9)), Color{71, 85, 105, 255});
}

// ── Game menu ─────────────────────────────────────────────────────────────

GameMenuLayout ComputeGameMenuLayout(const GameMenuState& s, bool hasRestart) {
    GameMenuLayout L = {};
    const float CW  = 440.f;
    const float PAD = 12.f;
    const float IW  = CW - 2.f * PAD;  // 416
    const float BH  = 28.f;

    int numRes = (int)s.resolutions.size();
    if (numRes > 7) numRes = 7;
    L.numRes = numRes;
    int numResRows = (numRes > 0) ? (numRes + 2) / 3 : 0;
    const float RBW = (IW - 2.f * 4.f) / 3.f;
    const float RBH = 26.f;

    // Compute card height
    float h = PAD;
    h += 18.f + 8.f;                                        // title + gap
    h += 1.f  + 10.f;                                       // top divider + gap
    h += BH   + 8.f;                                        // Resume
    h += BH   + 14.f;                                       // Level Select
    h += 12.f + 7.f;                                        // Display label + gap
    if (numRes > 0)
        h += numResRows * (RBH + 4.f) - 4.f + 8.f;         // resolution grid
    h += 12.f + 7.f;                                        // UI Scale label + gap
    h += RBH  + 8.f;                                        // 4 scale buttons row + gap
    h += BH   + 8.f;                                        // Fullscreen
    h += BH   + 14.f;                                       // Show FPS
    h += 12.f + 7.f;                                        // Audio label + gap
    h += BH   + 14.f;                                       // Audio row
    h += 12.f + 7.f;                                        // File label + gap
    h += BH   + 8.f;                                        // Save/Load
    if (hasRestart)  h += BH + 8.f;                         // Restart
    h += 1.f  + 8.f;                                        // bottom divider + gap
    h += BH   + PAD;                                        // Quit + bottom padding

    float cx = ((float)SCREEN_W() - CW) / 2.f;
    float cy = ((float)SCREEN_H() - h)  / 2.f;
    L.card = {cx, cy, CW, h};

    float x0 = cx + PAD;
    float y  = cy + PAD;

    y += 26.f;   // title + gap
    y += 11.f;   // divider + gap

    L.resume      = {x0, y, IW, BH};  y += BH + 8.f;
    L.levelSelect = {x0, y, IW, BH};  y += BH + 14.f;

    L.displayLabelY = y;
    y += 19.f;

    for (int i = 0; i < numRes; ++i) {
        int col = i % 3, row = i / 3;
        L.resBtns[i] = {x0 + col * (RBW + 4.f), y + row * (RBH + 4.f), RBW, RBH};
    }
    if (numRes > 0)
        y += numResRows * (RBH + 4.f) - 4.f + 8.f;

    L.uiScaleLabelY = y;
    y += 19.f;

    float sbw = (IW - 3.f * 4.f) / 4.f;
    for (int i = 0; i < 4; ++i)
        L.uiScaleBtns[i] = {x0 + i * (sbw + 4.f), y, sbw, RBH};
    y += RBH + 8.f;

    L.fullscreen = {x0, y, IW, BH};  y += BH + 8.f;
    L.showFps    = {x0, y, IW, BH};  y += BH + 14.f;

    L.audioLabelY = y;
    y += 19.f;

    const float muteW = 150.f;
    const float vBtnW = 36.f;
    L.mute    = {x0,           y, muteW, BH};
    L.volDown = {x0 + muteW + 12.f, y, vBtnW, BH};
    L.volUp   = {x0 + IW - vBtnW,  y, vBtnW, BH};
    y += BH + 14.f;

    L.fileLabelY = y;
    y += 19.f;

    float halfW = (IW - 8.f) / 2.f;
    L.save = {x0,              y, halfW, BH};
    L.load = {x0 + halfW + 8.f, y, halfW, BH};
    y += BH + 8.f;

    if (hasRestart) {
        L.restart = {x0, y, IW, BH};
        y += BH + 8.f;
    }

    L.quitDividerY = y;
    y += 9.f;
    L.quit = {x0, y, IW, BH};

    return L;
}

void DrawGameMenu(const GameMenuState& s, bool hasRestart) {
    DrawRectangle(0, 0, SCREEN_W(), SCREEN_H(), Color{0, 0, 0, 185});

    GameMenuLayout L = ComputeGameMenuLayout(s, hasRestart);
    const Rectangle& C = L.card;
    Vector2 mouse = GetMousePosition();

    DrawRectangleRounded(C, 0.04f, 6, Color{10, 17, 35, 252});
    DrawRectangleLinesEx(C, 1.5f, Color{51, 65, 85, 255});

    // Button helpers
    auto drawBtn = [&](Rectangle r, const char* label, Color bg, Color brd, Color tc) {
        bool hov = CheckCollisionPointRec(mouse, r);
        Color abg = hov ? Color{(uint8_t)std::min(255,(int)bg.r+25),
                                (uint8_t)std::min(255,(int)bg.g+25),
                                (uint8_t)std::min(255,(int)bg.b+25), bg.a} : bg;
        DrawRectangleRounded(r, 0.2f, 4, abg);
        DrawRectangleLinesEx(r, 1.f, brd);
        int tw = (int)TW(label, 10);
        DrawTextEx(GFont(), label,
                   {r.x + (r.width - tw) / 2.f, r.y + (r.height - 10.f) / 2.f},
                   FS(10), Sp(FS(10)), tc);
    };

    auto drawToggle = [&](Rectangle r, const char* label, bool on,
                          Color onBg, Color onBrd) {
        bool hov = CheckCollisionPointRec(mouse, r);
        Color bg  = on ? onBg : (hov ? Color{30,41,59,255} : Color{20,28,48,255});
        Color brd = on ? onBrd : Color{51,65,85,255};
        DrawRectangleRounded(r, 0.2f, 4, bg);
        DrawRectangleLinesEx(r, 1.f, brd);
        int tw = (int)TW(label, 10);
        DrawTextEx(GFont(), label,
                   {r.x + (r.width - tw) / 2.f, r.y + (r.height - 10.f) / 2.f},
                   FS(10), Sp(FS(10)),
                   on ? WHITE : Color{148,163,184,255});
    };

    auto drawSecLabel = [&](float lx, float ly, const char* text) {
        DrawTextEx(GFont(), text, {lx, ly}, FS(9), Sp(FS(9)), Color{71,85,105,255});
        DrawLineEx({C.x + 12.f, ly + 11.f}, {C.x + C.width - 12.f, ly + 11.f},
                   0.5f, Color{51,65,85,80});
    };

    // Title
    const char* title = "MENU";
    int ttw = (int)TW(title, 16);
    DrawTextEx(GFont(), title,
               {C.x + C.width / 2.f - ttw / 2.f, C.y + 12.f},
               FS(16), Sp(FS(16)), Color{226,232,240,255});

    DrawLineEx({C.x + 12.f, C.y + 38.f}, {C.x + C.width - 12.f, C.y + 38.f},
               1.f, Color{51,65,85,255});

    // Navigation
    drawBtn(L.resume,     "Resume",      Color{30,58,138,255}, Color{59,130,246,255}, WHITE);
    drawBtn(L.levelSelect,"Level Select",Color{22,33,62, 255}, Color{51,65,85, 255},
            Color{148,163,184,255});

    // Display section
    drawSecLabel(C.x + 12.f, L.displayLabelY, "DISPLAY");

    for (int i = 0; i < L.numRes; ++i) {
        bool active = (i == s.resIdx);
        char label[16];
        std::snprintf(label, sizeof(label), "%dx%d",
                      s.resolutions[i].first, s.resolutions[i].second);
        drawToggle(L.resBtns[i], label, active,
                   Color{30,58,138,255}, Color{59,130,246,255});
    }

    // UI Scale
    drawSecLabel(C.x + 12.f, L.uiScaleLabelY, "UI SCALE");
    const float scaleVals[4]   = {1.0f, 1.25f, 1.5f, 2.0f};
    const char* scaleLabels[4] = {"1x", "1.25x", "1.5x", "2x"};
    for (int i = 0; i < 4; ++i) {
        bool active = (std::fabs(s.uiScale - scaleVals[i]) < 0.01f);
        drawToggle(L.uiScaleBtns[i], scaleLabels[i], active,
                   Color{30,58,138,255}, Color{59,130,246,255});
    }

    const char* fsLabel = s.fullscreen ? "Fullscreen: ON" : "Fullscreen: OFF";
    drawToggle(L.fullscreen, fsLabel, s.fullscreen,
               Color{20,83,45,255}, Color{34,197,94,255});

    const char* fpsLabel = s.showFps ? "Show FPS: ON" : "Show FPS: OFF";
    drawToggle(L.showFps, fpsLabel, s.showFps,
               Color{76,29,149,255}, Color{147,51,234,255});

    // Audio section
    drawSecLabel(C.x + 12.f, L.audioLabelY, "AUDIO");

    const char* sndLabel = s.soundOn ? "Sound: ON" : "Sound: MUTED";
    drawToggle(L.mute, sndLabel, s.soundOn,
               Color{20,83,45,255}, Color{34,197,94,255});

    drawBtn(L.volDown, "-", Color{30,41,59,255}, Color{51,65,85,255},
            Color{148,163,184,255});

    char volBuf[8];
    std::snprintf(volBuf, sizeof(volBuf), "%d%%", (int)(s.volume * 100.f + 0.5f));
    float vdx = L.volDown.x + L.volDown.width + 4.f;
    float vdw = L.volUp.x - vdx - 4.f;
    int   vtw = (int)TW(volBuf, 10);
    DrawTextEx(GFont(), volBuf,
               {vdx + (vdw - vtw) / 2.f,
                L.volDown.y + (L.volDown.height - 10.f) / 2.f},
               FS(10), Sp(FS(10)),
               s.soundOn ? Color{203,213,225,255} : Color{71,85,105,255});

    drawBtn(L.volUp, "+", Color{30,41,59,255}, Color{51,65,85,255},
            Color{148,163,184,255});

    // File section
    drawSecLabel(C.x + 12.f, L.fileLabelY, "FILE");

    drawBtn(L.save, "Save Scene", Color{30,41,59,255}, Color{51,65,85,255},
            Color{148,163,184,255});
    drawBtn(L.load, "Open Scene", Color{30,41,59,255}, Color{51,65,85,255},
            Color{148,163,184,255});

    if (hasRestart)
        drawBtn(L.restart, "Restart Level", Color{30,41,59,255},
                Color{127,29,29,255}, Color{239,68,68,255});

    DrawLineEx({C.x + 12.f, L.quitDividerY},
               {C.x + C.width - 12.f, L.quitDividerY},
               1.f, Color{51,65,85,255});

    drawBtn(L.quit, "Quit", Color{30,41,59,255},
            Color{127,29,29,255}, Color{239,68,68,255});
}
