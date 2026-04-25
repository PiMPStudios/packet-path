#pragma once
#include "raylib.h"

// Call once after InitWindow().
void  InitGameFont();

// Call once before CloseWindow().
void  UnloadGameFont();

// Change UI scale without reloading the font.
// Valid values: 1.0f, 1.25f, 1.5f, 2.0f
void  SetUiScale(float scale);
float GetUiScale();

// Returns the loaded JetBrains Mono Regular font.
Font& GFont();

// Scaled font size: base * uiScale.
// Pass as fontSize to DrawTextEx / MeasureTextEx.
float FS(float base);

// Letter spacing for DrawTextEx (fixed at 1.0f for monospace).
float Sp(float fs);

// Convenience: measure text width in pixels at scaled size.
float TW(const char* text, float base);
