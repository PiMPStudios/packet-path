#pragma once
#include "Level.h"
#include "raylib.h"
#include <string>
#include <utility>
#include <vector>

enum GameMode    { GAME_SANDBOX, GAME_PLAYING, GAME_WIN, GAME_LEVEL_SELECT };
enum FileOpState { FILEOP_NONE, FILEOP_SAVING, FILEOP_LOADING };

Rectangle WinOverlayRect();
Rectangle WinRetryBtnRect();
Rectangle WinNextBtnRect();

// Top-left badge showing current level, title, and win-condition progress.
void DrawLevelHUD(int levelId, const std::string& title,
                  int conditionsPassed, int conditionsTotal,
                  int starsEarned = 0);

// Semi-transparent win overlay with "LEVEL COMPLETE!", stars, checklist, and buttons.
// hasNextLevel: if false, the Next Level button is greyed out.
void DrawWinOverlay(const LevelDef& def, bool hasNextLevel, int starsEarned = 0);

// Centered filename-input dialog for Ctrl+S / Ctrl+O.
// Shows modal when state != FILEOP_NONE.
// Shows toast when state == FILEOP_NONE and msgTimer > 0.
void DrawFileDialog(FileOpState        state,
                    const std::string& buf,
                    const std::string& msg,
                    float              msgTimer);

// ── Sandbox mode HUD ──────────────────────────────────────────────────────
// Teal "SANDBOX" badge {8,8,108,22} + MENU button at {120,8,52,22}.
void DrawSandboxHUD();
Rectangle SandboxMenuBtnRect();      // {120,8,52,22}

// ── Level-play HUD additions ──────────────────────────────────────────────
// Drawn to the right of the existing 240 px level badge.
Rectangle LevelHudMenuBtnRect();     // {252,8,52,22}
Rectangle LevelHudSandboxBtnRect();  // {308,8,72,22}

// ── Level-select overlay ──────────────────────────────────────────────────
// Full-screen overlay; levelTitles and levelExists must be 16-element arrays (indices 0–15).
void DrawLevelSelectScreen(const std::string* levelTitles, const bool* levelExists);
Rectangle LevelSelectCardRect(int i);   // i=0..15 → level card for level i+1
Rectangle LevelSelectSandboxBtnRect();  // Full-width sandbox card below the grid

// ── Replay controls HUD ───────────────────────────────────────────────────
// Shown at y=58 when SIM_ANIMATING. PAUSED badge + four speed buttons.
void DrawReplayHUD(bool paused, float speedMult);
Rectangle ReplaySpeedBtnRect(int idx);  // idx=0..3 → 0.25x/0.5x/1x/2x

// ── Mission briefing card ─────────────────────────────────────────────────
// Floating, draggable, collapsible. Only shown during GAME_PLAYING.
struct BriefingCardState {
    Vector2 pos        = {0.f, 82.f};  // top-left corner; init via BriefingDefaultPos()
    bool    collapsed  = false;         // true = only title bar visible
    bool    dragging   = false;         // true while LMB held on title bar
    Vector2 dragOff    = {};            // mouse offset from pos when drag began
    float   cardHeight = 210.f;        // computed by BriefingComputeHeight; fallback default
};

float     BriefingComputeHeight(const LevelDef& def);                        // dry-run content height (max 500px)
Vector2   BriefingDefaultPos();                                               // canvas-centered starting pos
Rectangle BriefingCardBounds(Vector2 pos, bool collapsed, float cardHeight); // full card rect
Rectangle BriefingTitleBarRect(Vector2 pos);                                 // drag zone (full-width × 30 px)
Rectangle BriefingCollapseBtnRect(Vector2 pos);                              // [−]/[+] toggle button
Rectangle BriefingCloseBtnRect(Vector2 pos);                                 // [×] hide-entirely button
Rectangle BriefingGotItBtnRect(Vector2 pos, float cardHeight);               // "Got it" → collapses card (only valid when !collapsed)
void      DrawBriefingCard(const LevelDef& def, const BriefingCardState& state);

// ── Help overlay ──────────────────────────────────────────────────────────
// Full-screen keyboard reference. Toggle with H key.
void      DrawHelpOverlay();
Rectangle HelpOverlayRect();
Rectangle HelpCloseBtnRect();

// ── Game menu ─────────────────────────────────────────────────────────────
// Pause-style menu: resolution, fullscreen, FPS, audio, save/load, quit.
struct GameMenuState {
    std::vector<std::pair<int,int>> resolutions;  // supported resolutions (filtered preset list)
    int   resIdx     = 0;
    bool  fullscreen = false;
    bool  showFps    = true;
    bool  soundOn    = true;
    float volume     = 1.0f;
    float uiScale    = 1.0f;
};

struct GameMenuLayout {
    Rectangle card;
    Rectangle resume;
    Rectangle levelSelect;
    Rectangle resBtns[7];
    int       numRes = 0;
    Rectangle fullscreen;
    Rectangle showFps;
    Rectangle uiScaleBtns[4];
    float     uiScaleLabelY;
    Rectangle mute;
    Rectangle volDown;
    Rectangle volUp;
    Rectangle save;
    Rectangle load;
    Rectangle restart;       // zero-rect when hasRestart==false
    Rectangle quit;
    float     displayLabelY;
    float     audioLabelY;
    float     fileLabelY;
    float     quitDividerY;
};

// hasRestart: show "Restart Level" option (GAME_PLAYING / GAME_WIN only).
GameMenuLayout ComputeGameMenuLayout(const GameMenuState& s, bool hasRestart);
void           DrawGameMenu(const GameMenuState& s, bool hasRestart);
