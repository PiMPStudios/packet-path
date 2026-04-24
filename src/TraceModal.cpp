#include "TraceModal.h"
#include "NetworkCanvas.h"   // CANVAS_H, CANVAS_W, SCREEN_W, SCREEN_H
#include <algorithm>
#include <cstdio>

// Geometry mirrors DrawLogConsole:
//   lineY = CANVAS_H + 8 + (shown - 1 - i) * 24   (newest at top, 24px stride)
int LogConsoleHitTest(Vector2 mouse, const std::vector<LogEntry>& entries) {
    if (entries.empty()) return -1;
    if (mouse.y < (float)CANVAS_H || mouse.y >= (float)SCREEN_H) return -1;
    if (mouse.x >= (float)CANVAS_W) return -1;   // ignore panel-side clicks

    int maxLines = 3;
    int startIdx = std::max(0, (int)entries.size() - maxLines);
    int shown    = std::min(maxLines, (int)entries.size());

    for (int i = 0; i < shown; ++i) {
        int       lineY = CANVAS_H + 8 + (shown - 1 - i) * 24;
        Rectangle r     = {0.f, (float)(lineY - 2), (float)CANVAS_W, 22.f};
        if (CheckCollisionPointRec(mouse, r)) {
            int idx = startIdx + i;
            return (entries[idx].type == LOG_FORWARD) ? idx : -1;
        }
    }
    return -1;
}

void DrawTraceModal(const ForwardResult& trace) {
    // Dim entire screen behind modal
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, 140});

    const float MW = 480.f, MH = 360.f;
    const float MX = (SCREEN_W - MW) / 2.f;
    const float MY = (SCREEN_H - MH) / 2.f;
    Rectangle modal = {MX, MY, MW, MH};

    DrawRectangleRounded(modal, 0.08f, 8, Color{22, 33, 62, 255});
    DrawRectangleRoundedLinesEx(modal, 0.08f, 8, 1.5f, Color{59, 130, 246, 255});

    // Header
    DrawText("Packet Trace", (int)(MX + 16), (int)(MY + 14), 13,
             Color{226, 232, 240, 255});
    const char* icon  = trace.success ? "\xe2\x9c\x93" : "\xe2\x9c\x97";
    Color       icCol = trace.success ? Color{34, 197, 94, 255}
                                      : Color{239, 68, 68, 255};
    DrawText(icon, (int)(MX + MW - 28), (int)(MY + 14), 13, icCol);
    DrawLineEx({MX, MY + 36.f}, {MX + MW, MY + 36.f}, 1.f,
               Color{51, 65, 85, 255});

    if (trace.hops.empty()) {
        DrawText("No hop detail available", (int)(MX + 16), (int)(MY + 52), 11,
                 Color{100, 116, 139, 255});
    } else {
        float rowY = MY + 44.f;
        for (int i = 0; i < (int)trace.hops.size() && rowY < MY + MH - 36.f; ++i) {
            const HopDecision& h = trace.hops[i];

            // Hop index circle
            DrawCircle((int)(MX + 22.f), (int)(rowY + 10.f), 10.f,
                       Color{30, 64, 175, 255});
            char num[4];
            std::snprintf(num, sizeof(num), "%d", i + 1);
            int nw = MeasureText(num, 10);
            DrawText(num, (int)(MX + 22.f) - nw / 2, (int)(rowY + 5.f), 10, WHITE);

            // Node label
            DrawText(h.nodeLabel.c_str(), (int)(MX + 40.f), (int)rowY, 12,
                     Color{226, 232, 240, 255});

            // Route type badge
            Color rtCol;
            if      (h.routeType == "C")    rtCol = Color{34, 197, 94, 255};
            else if (h.routeType == "S")    rtCol = Color{234, 179, 8, 255};
            else if (h.routeType == "O")    rtCol = Color{59, 130, 246, 255};
            else                            rtCol = Color{168, 85, 247, 255};
            DrawText(h.routeType.c_str(), (int)(MX + 40.f), (int)(rowY + 16.f), 10, rtCol);

            // Matched prefix → next hop
            char detail[256];
            std::snprintf(detail, sizeof(detail), "%s \xe2\x86\x92 %s",
                          h.destPrefix.c_str(), h.nextHopIp.c_str());
            DrawText(detail, (int)(MX + 72.f), (int)(rowY + 16.f), 10,
                     Color{100, 116, 139, 255});

            // MPLS label op annotation
            bool hasLabel = (h.labelOp != LABEL_NONE);
            if (hasLabel) {
                const char* opStr = "";
                Color        opCol = WHITE;
                char         lblBuf[32] = "";

                if (h.labelOp == LABEL_PUSH) {
                    opStr = "PUSH";
                    opCol = Color{249, 115, 22, 255};
                    std::snprintf(lblBuf, sizeof(lblBuf), "%u", h.outLabel);
                } else if (h.labelOp == LABEL_SWAP) {
                    opStr = "SWAP";
                    opCol = Color{234, 179, 8, 255};
                    std::snprintf(lblBuf, sizeof(lblBuf), "%u\xe2\x86\x92%u",
                                  h.inLabel, h.outLabel);
                } else if (h.labelOp == LABEL_POP) {
                    opStr = "POP";
                    opCol = Color{168, 85, 247, 255};
                    std::snprintf(lblBuf, sizeof(lblBuf), "%u", h.inLabel);
                }

                float bw = (float)(MeasureText(opStr, 9) + 10);
                DrawRectangleRounded({MX + 40.f, rowY + 30.f, bw, 13.f},
                                     0.4f, 4, opCol);
                int tw5 = MeasureText(opStr, 9);
                DrawText(opStr, (int)(MX + 40.f + (bw - tw5) / 2.f),
                         (int)(rowY + 32.f), 9, WHITE);
                DrawText(lblBuf, (int)(MX + 40.f + bw + 6.f),
                         (int)(rowY + 31.f), 10, Color{253, 186, 116, 255});
            }

            float rowStride = hasLabel ? 52.f : 44.f;
            rowY += rowStride;
            if (i + 1 < (int)trace.hops.size())
                DrawLineEx({MX + 8.f, rowY - 4.f}, {MX + MW - 8.f, rowY - 4.f},
                           0.5f, Color{30, 41, 59, 255});
        }
    }

    DrawText("ESC or click outside to close",
             (int)(MX + 16), (int)(MY + MH - 24), 10, Color{71, 85, 105, 255});
}
