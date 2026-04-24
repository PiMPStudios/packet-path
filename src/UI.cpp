#include "UI.h"
#include "NetworkCanvas.h"

static int nextId = 1;

void SetNextId(int n) { nextId = n; }

DeviceNode SpawnNode(DeviceType type, Vector2 worldPos) {
    const char* names[] = {"PC", "RTR", "SW"};
    DeviceNode n;
    n.id       = nextId++;
    n.type     = type;
    n.position = worldPos;
    n.label    = std::string(names[(int)type]) + "-" + std::to_string(n.id);
    return n;
}

void UpdateContextMenuHover(ContextMenu& menu, Vector2 screenMouse) {
    if (!menu.visible) { menu.hoverItem = -1; return; }

    static const char* nodeItemsNormal[]  = {"Rename", "Crash Device", "Delete",
                                              "Send Packet To\xe2\x80\xa6", nullptr};
    static const char* nodeItemsCrashed[] = {"Rename", "Restore Device", "Delete",
                                              "Send Packet To\xe2\x80\xa6", nullptr};
    static const char* cableItemsNormal[] = {"Cut Link", "Delete Cable", nullptr};
    static const char* cableItemsBroken[] = {"Restore Link", "Delete Cable", nullptr};
    static const char* canvasItems[]      = {"Add PC Here", "Add Router Here",
                                             "Add Switch Here", "Reset View", nullptr};

    const char** items = nullptr;
    if      (menu.ctx == CTX_NODE)   items = menu.targetBroken ? nodeItemsCrashed : nodeItemsNormal;
    else if (menu.ctx == CTX_CABLE)  items = menu.targetBroken ? cableItemsBroken : cableItemsNormal;
    else if (menu.ctx == CTX_CANVAS) items = canvasItems;
    else { menu.hoverItem = -1; return; }

    int count = 0;
    while (items[count]) ++count;

    float h = (float)(count * MENU_ITEM_H + 8);
    float x = std::min(menu.screenPos.x, (float)(CANVAS_W - CONTEXT_MENU_W - 4));
    float y = std::min(menu.screenPos.y, (float)(CANVAS_H - (int)h - 4));

    menu.hoverItem = -1;
    for (int i = 0; i < count; ++i) {
        Rectangle ir = {x + 4, y + 4 + (float)(i * MENU_ITEM_H),
                        (float)(CONTEXT_MENU_W - 8), (float)MENU_ITEM_H};
        if (CheckCollisionPointRec(screenMouse, ir)) { menu.hoverItem = i; break; }
    }
}

void ExecuteMenuAction(ContextMenu& menu, std::vector<DeviceNode>& nodes,
                       std::vector<Cable>& cables, int& selectedId,
                       PanelState& ps, Camera2D& camera, SimState& simState,
                       std::vector<LogEntry>& logEntries)
{
    if (simState.mode == SIM_ANIMATING) return;
    int item = menu.hoverItem;

    auto pushFaultLog = [&](LogType type, bool success, const std::string& path) {
        LogEntry le;
        le.type      = type;
        le.success   = success;
        le.pathStr   = path;
        le.timestamp = GetTime();
        if (logEntries.size() >= 50) logEntries.erase(logEntries.begin());
        logEntries.push_back(le);
    };

    if (menu.ctx == CTX_NODE) {
        if (item == 0) {  // Rename
            selectedId = menu.targetId;
            for (auto& n : nodes) n.selected = (n.id == selectedId);
            ps.activeTab   = TAB_CONFIG;
            ps.activeField = 0;
        } else if (item == 1) {  // Crash Device / Restore Device
            for (auto& n : nodes) {
                if (n.id != menu.targetId) continue;
                n.crashed = !n.crashed;
                if (n.crashed)
                    pushFaultLog(LOG_DEVICE_CRASH, false, "DEVICE CRASHED: " + n.label);
                else
                    pushFaultLog(LOG_RESTORED,     true,  "RESTORED: "       + n.label);
                break;
            }
        } else if (item == 2) {  // Delete
            cables.erase(std::remove_if(cables.begin(), cables.end(),
                [&](const Cable& c){
                    return c.fromId == menu.targetId || c.toId == menu.targetId;
                }), cables.end());
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const DeviceNode& n){ return n.id == menu.targetId; }),
                nodes.end());
            if (selectedId == menu.targetId) { selectedId = -1; ps.activeField = -1; }
            if (simState.srcId == menu.targetId) { simState.mode = SIM_IDLE; simState.srcId = -1; }
        } else if (item == 3) {  // Send Packet To…
            if (simState.mode == SIM_IDLE || simState.mode == SIM_SELECTING_DST) {
                simState.mode  = SIM_SELECTING_DST;
                simState.srcId = menu.targetId;
            }
        }
    } else if (menu.ctx == CTX_CABLE) {
        if (item == 0) {  // Cut Link / Restore Link
            if (menu.targetId >= 0 && menu.targetId < (int)cables.size()) {
                Cable& cab = cables[menu.targetId];
                cab.broken = !cab.broken;
                const DeviceNode* from = FindNode(nodes, cab.fromId);
                const DeviceNode* to   = FindNode(nodes, cab.toId);
                std::string label = (from ? from->label : "?") +
                                    " \xe2\x80\x94 " +
                                    (to   ? to->label   : "?");
                if (cab.broken)
                    pushFaultLog(LOG_LINK_DOWN, false, "LINK DOWN: "  + label);
                else
                    pushFaultLog(LOG_RESTORED,  true,  "RESTORED: "   + label);
            }
        } else if (item == 1) {  // Delete Cable
            if (menu.targetId >= 0 && menu.targetId < (int)cables.size())
                cables.erase(cables.begin() + menu.targetId);
        }
    } else if (menu.ctx == CTX_CANVAS) {
        if      (item == 0) nodes.push_back(SpawnNode(PC,     menu.worldPos));
        else if (item == 1) nodes.push_back(SpawnNode(ROUTER, menu.worldPos));
        else if (item == 2) nodes.push_back(SpawnNode(SWITCH, menu.worldPos));
        else if (item == 3) { camera.target = {0.0f, 0.0f}; camera.zoom = 1.0f; }
    }
}
