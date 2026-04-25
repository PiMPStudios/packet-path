#pragma once
#include "Level.h"
#include "raylib.h"
#include <string>

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
// Full-screen overlay; levelTitles must be a 16-element array (indices 0–15).
void DrawLevelSelectScreen(const std::string* levelTitles);
Rectangle LevelSelectCardRect(int i);   // i=0..15 → level card for level i+1
Rectangle LevelSelectSandboxBtnRect();  // Full-width sandbox card below the grid

// ── Replay controls HUD ───────────────────────────────────────────────────
// Shown at y=58 when SIM_ANIMATING. PAUSED badge + four speed buttons.
void DrawReplayHUD(bool paused, float speedMult);
Rectangle ReplaySpeedBtnRect(int idx);  // idx=0..3 → 0.25x/0.5x/1x/2x
