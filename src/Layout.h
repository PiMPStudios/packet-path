#pragma once
#include "raylib.h"

// Fixed layout constants — these never change with window size
inline constexpr int PANEL_W        = 280;
inline constexpr int LOG_H          = 90;
inline constexpr int MENU_ITEM_H    = 28;
inline constexpr int CONTEXT_MENU_W = 160;
inline constexpr int MIN_W          = 1024;
inline constexpr int MIN_H          = 600;

// Dynamic layout — queried fresh each use
inline int SCREEN_W() { return GetScreenWidth();              }
inline int SCREEN_H() { return GetScreenHeight();             }
inline int CANVAS_W() { return GetScreenWidth()  - PANEL_W;  }
inline int CANVAS_H() { return GetScreenHeight() - LOG_H;    }
