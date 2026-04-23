#pragma once
#include "Level.h"
#include "raylib.h"
#include <string>

enum GameMode { GAME_SANDBOX, GAME_PLAYING, GAME_WIN };

// Overlay layout (centered on the canvas — requires CANVAS_W/CANVAS_H from NetworkCanvas.h,
// so implementations live in GameUI.cpp which includes NetworkCanvas.h).
Rectangle WinOverlayRect();
Rectangle WinRetryBtnRect();
Rectangle WinNextBtnRect();

// Top-left badge showing current level, title, and win-condition progress.
void DrawLevelHUD(int levelId, const std::string& title,
                  int conditionsPassed, int conditionsTotal);

// Semi-transparent win overlay with "LEVEL COMPLETE!", stars, checklist, and buttons.
// hasNextLevel: if false, the Next Level button is greyed out.
void DrawWinOverlay(const LevelDef& def, bool hasNextLevel);
