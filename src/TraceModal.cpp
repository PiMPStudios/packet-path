#include "TraceModal.h"
#include "Layout.h"           // CANVAS_H, CANVAS_W, SCREEN_W, SCREEN_H
#include "Font.h"
#include <algorithm>
#include <cstdio>

// Geometry mirrors DrawLogConsole:
//   lineY = CANVAS_H + 8 + (shown - 1 - i) * 24   (newest at top, 24px stride)
int LogConsoleHitTest(Vector2 mouse, const std::vector<LogEntry>& entries, int scrollOffset) {
    if (entries.empty()) return -1;
    if (mouse.y < (float)CANVAS_H() || mouse.y >= (float)SCREEN_H()) return -1;
    if (mouse.x >= (float)CANVAS_W()) return -1;   // ignore panel-side clicks

    int startIdx = std::max(0, (int)entries.size() - LOG_MAX_LINES - scrollOffset);
    int shown    = std::min(LOG_MAX_LINES, std::max(0, (int)entries.size() - scrollOffset));
    if (shown <= 0) return -1;

    for (int i = 0; i < shown; ++i) {
        int       lineY = CANVAS_H() + 8 + (shown - 1 - i) * 24;
        Rectangle r     = {0.f, (float)(lineY - 2), (float)CANVAS_W(), 22.f};
        if (CheckCollisionPointRec(mouse, r)) {
            int idx = startIdx + i;
            return (entries[idx].type == LOG_FORWARD) ? idx : -1;
        }
    }
    return -1;
}

void DrawTraceModal(const ForwardResult& trace, int activeHop) {
    // Dim entire screen behind modal
    DrawRectangle(0, 0, SCREEN_W(), SCREEN_H(), Color{0, 0, 0, 140});

    const float MW = 480.f, MH = 360.f;
    const float MX = (SCREEN_W() - MW) / 2.f;
    const float MY = (SCREEN_H() - MH) / 2.f;
    Rectangle modal = {MX, MY, MW, MH};

    DrawRectangleRounded(modal, 0.08f, 8, Color{22, 33, 62, 255});
    DrawRectangleRoundedLinesEx(modal, 0.08f, 8, 1.5f, Color{59, 130, 246, 255});

    // Header
    DrawTextEx(GFont(), "Packet Trace", {MX + 16, MY + 14}, FS(13), Sp(FS(13)),
               Color{226, 232, 240, 255});
    const char* icon  = trace.success ? "\xe2\x9c\x93" : "\xe2\x9c\x97";
    Color       icCol = trace.success ? Color{34, 197, 94, 255}
                                      : Color{239, 68, 68, 255};
    DrawTextEx(GFont(), icon, {MX + MW - 28, MY + 14}, FS(13), Sp(FS(13)), icCol);
    DrawLineEx({MX, MY + 36.f}, {MX + MW, MY + 36.f}, 1.f,
               Color{51, 65, 85, 255});

    if (trace.hops.empty()) {
        DrawTextEx(GFont(), "No hop detail available", {MX + 16, MY + 52}, FS(11), Sp(FS(11)),
                   Color{100, 116, 139, 255});
    } else {
        float rowY = MY + 44.f;
        for (int i = 0; i < (int)trace.hops.size() && rowY < MY + MH - 36.f; ++i) {
            const HopDecision& h = trace.hops[i];

            // Pre-compute so highlight rect uses the correct row height
            bool hasLabel = (h.labelOp != LABEL_NONE);
            bool hasAcl   = !h.aclResult.empty();
            bool hasNat   = !h.natResult.empty();
            bool hasSrv6  = !h.srv6ActiveSid.empty();
            bool hasSdwan = h.sdwanPolicyId != 0;
            int  extras   = (hasLabel ? 1 : 0) + (hasAcl ? 1 : 0) +
                            (hasNat ? 1 : 0) + (hasSrv6 ? 1 : 0) + (hasSdwan ? 1 : 0);
            float rowStride = 44.f + extras * 16.f;

            // Active-hop highlight — subtle blue background behind the entire row
            if (i == activeHop)
                DrawRectangleRounded({MX + 4.f, rowY - 2.f, MW - 8.f, rowStride - 4.f},
                                     0.06f, 4, Color{30, 58, 138, 80});

            // Hop index circle
            DrawCircle((int)(MX + 22.f), (int)(rowY + 10.f), 10.f,
                       Color{30, 64, 175, 255});
            char num[4];
            std::snprintf(num, sizeof(num), "%d", i + 1);
            int nw = (int)TW(num, 10);
            DrawTextEx(GFont(), num, {MX + 22.f - nw / 2.f, rowY + 5.f}, FS(10), Sp(FS(10)), WHITE);

            // Node label
            DrawTextEx(GFont(), h.nodeLabel.c_str(), {MX + 40.f, rowY}, FS(12), Sp(FS(12)),
                       Color{226, 232, 240, 255});

            // Route type badge
            Color rtCol;
            if      (h.routeType == "C")    rtCol = Color{34, 197, 94, 255};
            else if (h.routeType == "S")    rtCol = Color{234, 179, 8, 255};
            else if (h.routeType == "O")    rtCol = Color{59, 130, 246, 255};
            else if (h.routeType == "B")    rtCol = Color{20, 184, 166, 255};
            else                            rtCol = Color{168, 85, 247, 255};
            DrawTextEx(GFont(), h.routeType.c_str(), {MX + 40.f, rowY + 16.f}, FS(10), Sp(FS(10)), rtCol);

            // Matched prefix → next hop
            char detail[256];
            std::snprintf(detail, sizeof(detail), "%s \xe2\x86\x92 %s",
                          h.destPrefix.c_str(), h.nextHopIp.c_str());
            DrawTextEx(GFont(), detail, {MX + 72.f, rowY + 16.f}, FS(10), Sp(FS(10)),
                       Color{100, 116, 139, 255});

            // MPLS label op annotation
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

                float bw = TW(opStr, 9) + 10.f;
                DrawRectangleRounded({MX + 40.f, rowY + 30.f, bw, 13.f},
                                      0.4f, 4, opCol);
                float tw5 = TW(opStr, 9);
                DrawTextEx(GFont(), opStr, {MX + 40.f + (bw - tw5) / 2.f, rowY + 32.f},
                           FS(9), Sp(FS(9)), WHITE);
                DrawTextEx(GFont(), lblBuf, {MX + 40.f + bw + 6.f, rowY + 31.f},
                           FS(10), Sp(FS(10)), Color{253, 186, 116, 255});
            }

            if (hasSrv6) {
                const float annotY = rowY + 30.f + (hasLabel ? 16.f : 0.f);
                const std::string text = "SRH " + h.srv6ActiveSid +
                    "  Segments Left: " + std::to_string(h.srv6SegmentsLeft);
                const float width = TW(text.c_str(), 9) + 10.f;
                DrawRectangleRounded({MX + 40.f, annotY, width, 13.f},
                                     0.4f, 4, Color{217,70,239,255});
                DrawTextEx(GFont(), text.c_str(), {MX + 45.f, annotY + 2.f},
                           FS(9), Sp(FS(9)), WHITE);
            }
            if (hasSdwan) {
                const float annotY = rowY + 30.f + (hasLabel ? 16.f : 0.f) +
                                     (hasSrv6 ? 16.f : 0.f);
                const std::string text = "SD-WAN Policy-" + std::to_string(h.sdwanPolicyId) +
                    " -> Gi0/" + std::to_string(h.sdwanSelectedPort) +
                    (h.sdwanUsingBackup ? " (backup)" : " (primary)");
                const float width = TW(text.c_str(),9) + 10.f;
                DrawRectangleRounded({MX+40.f,annotY,width,13.f},.4f,4,Color{14,165,233,255});
                DrawTextEx(GFont(), text.c_str(), {MX+45.f,annotY+2.f},FS(9),Sp(FS(9)),WHITE);
            }

            // ACL annotation badge
            if (hasAcl) {
                float annotY = rowY + 30.f + (hasLabel ? 16.f : 0.f) +
                               (hasSrv6 ? 16.f : 0.f) + (hasSdwan ? 16.f : 0.f);
                bool permit  = (h.aclResult.rfind("PERMIT", 0) == 0);
                Color ac     = permit ? Color{34,197,94,255} : Color{239,68,68,255};
                float bw = TW(h.aclResult.c_str(), 9) + 10.f;
                DrawRectangleRounded({MX+40.f, annotY, bw, 13.f}, 0.4f, 4, ac);
                DrawTextEx(GFont(), h.aclResult.c_str(), {MX+45.f, annotY+2.f}, FS(9), Sp(FS(9)), WHITE);
            }
            // NAT annotation badge
            if (hasNat) {
                float annotY = rowY + 30.f + (hasLabel ? 16.f : 0.f) +
                               (hasSrv6 ? 16.f : 0.f) + (hasSdwan ? 16.f : 0.f) +
                               (hasAcl ? 16.f : 0.f);
                char natBuf[64];
                std::snprintf(natBuf, sizeof(natBuf), "NAT %s", h.natResult.c_str());
                float bw = TW(natBuf, 9) + 10.f;
                DrawRectangleRounded({MX+40.f, annotY, bw, 13.f}, 0.4f, 4, Color{234,179,8,255});
                DrawTextEx(GFont(), natBuf, {MX+45.f, annotY+2.f}, FS(9), Sp(FS(9)), WHITE);
            }

            rowY += rowStride;
            if (i + 1 < (int)trace.hops.size())
                DrawLineEx({MX + 8.f, rowY - 4.f}, {MX + MW - 8.f, rowY - 4.f},
                           0.5f, Color{30, 41, 59, 255});
        }
    }

    DrawTextEx(GFont(), "ESC or click outside to close",
               {MX + 16, MY + MH - 24}, FS(10), Sp(FS(10)), Color{71, 85, 105, 255});
}
