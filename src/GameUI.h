#pragma once
#include "Level.h"
#include "raylib.h"
#include <string>

enum GameMode    { GAME_SANDBOX, GAME_PLAYING, GAME_WIN };
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
