#pragma once
#include "raylib.h"
#include "Device.h"
#include <vector>

// Returns index into logEntries of the LOG_FORWARD entry under screenMouse,
// or -1 if no LOG_FORWARD entry was clicked.
int  LogConsoleHitTest(Vector2 screenMouse, const std::vector<LogEntry>& entries, int scrollOffset = 0);

// Draws the centered 480x360 trace modal over a dimmed screen.
// Call only when the modal is open (outside BeginMode2D).
void DrawTraceModal(const ForwardResult& trace, int activeHop = -1);
