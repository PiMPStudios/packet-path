#pragma once
#include "Device.h"
#include "Cable.h"
#include "ConfigPanel.h"
#include "Packet.h"
#include "raylib.h"
#include <vector>
#include <string>

enum ContextType { CTX_NONE, CTX_NODE, CTX_CABLE, CTX_CANVAS };

struct ContextMenu {
    bool        visible      = false;
    Vector2     screenPos    = {0.0f, 0.0f};
    Vector2     worldPos     = {0.0f, 0.0f};
    ContextType ctx          = CTX_NONE;
    int         targetId     = -1;
    int         hoverItem    = -1;
    bool        targetBroken = false;   // cable.broken or node.crashed at menu-open time
};

DeviceNode SpawnNode(DeviceType type, Vector2 worldPos);
void SetNextId(int n);

void UpdateContextMenuHover(ContextMenu& menu, Vector2 screenMouse);

void ExecuteMenuAction(ContextMenu& menu,
                       std::vector<DeviceNode>& nodes,
                       std::vector<Cable>& cables,
                       int& selectedId,
                       PanelState& ps,
                       Camera2D& camera,
                       SimState& simState,
                       std::vector<LogEntry>& logEntries);
