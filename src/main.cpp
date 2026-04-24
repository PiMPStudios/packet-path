#include "NetworkCanvas.h"
#include "OspfEngine.h"
#include "LdpEngine.h"
#include "BgpEngine.h"
#include "Level.h"
#include "GameUI.h"
#include "TraceModal.h"
#include "SoundEngine.h"

// ── Main ──────────────────────────────────────────────────────────────────
int main() {
    InitWindow(1280, 720, "Packet Path");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetWindowMinSize(MIN_W, MIN_H);
    SetTargetFPS(60);
    InitAudioDevice();
    InitSounds();

    std::vector<DeviceNode> nodes;
    nodes.push_back(SpawnNode(PC, {0.0f, 0.0f}));

    Camera2D camera = {};
    camera.offset   = {CANVAS_W() / 2.0f, CANVAS_H() / 2.0f};
    camera.target   = {0.0f, 0.0f};
    camera.zoom     = 1.0f;

    int     selectedId = -1;
    bool    dragging   = false;
    Vector2 dragOffset = {0.0f, 0.0f};

    std::vector<Cable> cables;
    bool connecting      = false;
    int  connectFromId   = -1;
    int  connectFromPort = -1;
    int  hoverNodeId     = -1;
    int  hoverPort       = -1;

    PanelState  ps;
    int         prevSelectedId = -2;  // -2 forces reset on first frame
    ContextMenu contextMenu;
    std::vector<LogEntry> logEntries;
    SimState simState;
    GameMode    gameMode             = GAME_SANDBOX;
    int         currentLevel         = 0;
    LevelDef    activeLevelDef;
    int         lastConditionsPassed = 0;
    int         failedAttempts       = 0;
    int         starsEarned          = 0;
    bool          troubleshootMode = false;
    bool          traceModalOpen = false;
    ForwardResult activeTrace;
    float         failAnnotationTimer = 0.f;
    ForwardResult lastFailedTrace;

    // ── Save / Load dialog state ───────────────────────────────
    FileOpState fileOp      = FILEOP_NONE;
    std::string fileNameBuf = "scene.json";
    std::string fileOpMsg;
    float       fileOpTimer = 0.f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        if (IsWindowResized())
            camera.offset = {CANVAS_W() / 2.0f, CANVAS_H() / 2.0f};

        // ── File op timer ──────────────────────────────────────
        if (fileOpTimer > 0.f) fileOpTimer -= dt;

        // ── Ctrl+S / Ctrl+O ───────────────────────────────────
        bool ctrlHeld = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (fileOp == FILEOP_NONE) {
            if (ctrlHeld && IsKeyPressed(KEY_S)) {
                fileOp      = FILEOP_SAVING;
                fileNameBuf = "scene.json";
                fileOpMsg.clear();
            } else if (ctrlHeld && IsKeyPressed(KEY_O)) {
                fileOp      = FILEOP_LOADING;
                fileNameBuf = "scene.json";
                fileOpMsg.clear();
            }
        }

        // ── File dialog text input ─────────────────────────────
        if (fileOp != FILEOP_NONE) {
            int ch = GetCharPressed();
            while (ch > 0) {
                if (ch >= 32 && ch < 127) fileNameBuf += (char)ch;
                ch = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE) && !fileNameBuf.empty())
                fileNameBuf.pop_back();
            if (IsKeyPressed(KEY_ESCAPE)) {
                fileOp = FILEOP_NONE;
                fileOpMsg.clear();
            }
            if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) &&
                !fileNameBuf.empty()) {
                if (fileOp == FILEOP_SAVING) {
                    if (SaveScene(fileNameBuf, nodes, cables)) {
                        fileOpMsg   = "Saved: " + fileNameBuf;
                        fileOp      = FILEOP_NONE;
                        fileOpTimer = 3.0f;
                    } else {
                        fileOpMsg = "Error: could not write " + fileNameBuf;
                    }
                } else {
                    LevelDef loaded;
                    if (LoadLevel(fileNameBuf, loaded)) {
                        ApplyLevel(loaded, nodes, cables, selectedId);
                        ps                   = PanelState{};
                        simState             = SimState{};
                        logEntries.clear();
                        lastConditionsPassed = 0;
                        failedAttempts       = 0;
                        starsEarned          = 0;
                        gameMode             = GAME_SANDBOX;
                        dragging             = false;
                        connecting           = false;
                        hoverNodeId          = -1;
                        hoverPort            = -1;
                        contextMenu.visible  = false;
                        troubleshootMode     = false;
                        traceModalOpen       = false;
                        failAnnotationTimer  = 0.f;
                        lastFailedTrace      = {};
                        fileOpMsg   = "Loaded: " + fileNameBuf;
                        fileOp      = FILEOP_NONE;
                        fileOpTimer = 3.0f;
                    } else {
                        fileOpMsg = "Error: could not open " + fileNameBuf;
                    }
                }
            }
        }

        Vector2 screenMouse = GetMousePosition();
        Vector2 worldMouse  = GetScreenToWorld2D(screenMouse, camera);
        bool inCanvas = (screenMouse.x < (float)CANVAS_W() &&
                         screenMouse.y < (float)CANVAS_H());

        // ── Spawn / delete / cancel ────────────────────────────────────
        if (fileOp == FILEOP_NONE && inCanvas && gameMode != GAME_WIN &&
            ps.activeField == -1 && ps.activeRouteField == -1 &&
            simState.mode == SIM_IDLE) {
            if (IsKeyPressed(KEY_P)) nodes.push_back(SpawnNode(PC,     worldMouse));
            if (IsKeyPressed(KEY_R)) nodes.push_back(SpawnNode(ROUTER, worldMouse));
            if (IsKeyPressed(KEY_S) && !ctrlHeld) nodes.push_back(SpawnNode(SWITCH, worldMouse));
            if (IsKeyPressed(KEY_T))
                troubleshootMode = !troubleshootMode;
            // Level shortcuts: 1–8 load JSON levels, 0 returns to sandbox
            if (ps.activePortAreaField == -1) {
                for (int k = 1; k <= 10; ++k) {
                    int key = (k <= 9) ? (KEY_ONE + (k - 1)) : KEY_ZERO;
                    if (IsKeyPressed(key)) {
                        char path[64];
                        std::snprintf(path, sizeof(path), "levels/level_%02d.json", k);
                        LevelDef def;
                        if (LoadLevel(path, def)) {
                            currentLevel         = k;
                            activeLevelDef       = def;
                            ApplyLevel(def, nodes, cables, selectedId);
                            ps                   = PanelState{};
                            simState             = SimState{};
                            logEntries.clear();
                            lastConditionsPassed = 0;
                            failedAttempts       = 0;
                            starsEarned          = 0;
                            gameMode             = GAME_PLAYING;
                            dragging             = false;
                            connecting           = false;
                            hoverNodeId          = -1;
                            hoverPort            = -1;
                            contextMenu.visible  = false;
                            troubleshootMode     = false;
                            traceModalOpen       = false;
                            failAnnotationTimer  = 0.f;
                            lastFailedTrace      = {};
                        }
                    }
                }
            }
        }

        if (IsKeyPressed(KEY_ESCAPE)) {
            if (traceModalOpen) {
                traceModalOpen = false;
            } else {
                contextMenu.visible = false;
                if (ps.activeField != -1) {
                    ps.activeField = -1;
                } else if (ps.activeRouteField != -1) {
                    ps.activeRouteField = -1;
                } else if (ps.activePortAreaField != -1) {
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                } else if (ps.bgpAsnField != -1) {
                    ps.bgpAsnField = -1;
                    ps.bgpAsnBuf.clear();
                } else if (ps.vlanPortField != -1) {
                    ps.vlanPortField = -1;
                    ps.vlanPortBuf.clear();
                } else if (ps.subActiveField != -1) {
                    ps.subActiveField = -1;
                    ps.subVlanBuf.clear();
                    ps.subIpBuf.clear();
                } else if (connecting) {
                    connecting  = false;
                    hoverNodeId = -1;
                    hoverPort   = -1;
                } else if (simState.mode == SIM_SELECTING_DST) {
                    simState.mode  = SIM_IDLE;
                    simState.srcId = -1;
                }
            }
        }

        if (ps.activeField == -1 && ps.activeRouteField == -1 &&
            simState.mode == SIM_IDLE &&
            IsKeyPressed(KEY_DELETE) && selectedId != -1) {
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const DeviceNode& n){ return n.id == selectedId; }),
                nodes.end());
            cables.erase(std::remove_if(cables.begin(), cables.end(),
                [&](const Cable& c){ return c.fromId == selectedId || c.toId == selectedId; }),
                cables.end());
            if (simState.srcId == selectedId) { simState.mode = SIM_IDLE; simState.srcId = -1; }
            selectedId = -1;
            dragging   = false;
            ps.bgpAsnField = -1;
            ps.bgpAsnBuf.clear();
            ps.activePortAreaField = -1;
            ps.portAreaBuf.clear();
        }

        // ── Camera pan (middle mouse) ──────────────────────────────────
        if (inCanvas && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            Vector2 delta    = GetMouseDelta();
            camera.target.x -= delta.x / camera.zoom;
            camera.target.y -= delta.y / camera.zoom;
        }

        // ── Camera zoom (scroll wheel, cursor-anchored) ────────────────
        float wheel = inCanvas ? std::clamp(GetMouseWheelMove(), -3.0f, 3.0f) : 0.0f;
        if (wheel != 0.0f) {
            Vector2 beforeZoom = GetScreenToWorld2D(screenMouse, camera);
            camera.zoom *= (1.0f + wheel * 0.1f);
            camera.zoom  = std::clamp(camera.zoom, 0.15f, 4.0f);
            Vector2 afterZoom  = GetScreenToWorld2D(screenMouse, camera);
            camera.target.x   += beforeZoom.x - afterZoom.x;
            camera.target.y   += beforeZoom.y - afterZoom.y;
        }

        UpdateContextMenuHover(contextMenu, screenMouse);

        // ── LMB pressed ───────────────────────────────────────────────
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (gameMode == GAME_WIN) {
                // Win overlay clicks — consume event; don't fall through to canvas
                if (CheckCollisionPointRec(screenMouse, WinRetryBtnRect())) {
                    ApplyLevel(activeLevelDef, nodes, cables, selectedId);
                    ps                   = PanelState{};
                    simState             = SimState{};
                    logEntries.clear();
                    lastConditionsPassed = 0;
                    failedAttempts       = 0;
                    starsEarned          = 0;
                    gameMode             = GAME_PLAYING;
                    dragging             = false;
                    connecting           = false;
                    hoverNodeId          = -1;
                    hoverPort            = -1;
                    contextMenu.visible  = false;
                    troubleshootMode     = false;
                    traceModalOpen       = false;
                    failAnnotationTimer  = 0.f;
                    lastFailedTrace      = {};
                } else if (CheckCollisionPointRec(screenMouse, WinNextBtnRect()) &&
                           currentLevel < 13) {
                    int nextLevel = currentLevel + 1;
                    char path[64];
                    std::snprintf(path, sizeof(path), "levels/level_%02d.json", nextLevel);
                    LevelDef def;
                    if (LoadLevel(path, def)) {
                        currentLevel         = nextLevel;
                        activeLevelDef       = def;
                        ApplyLevel(def, nodes, cables, selectedId);
                        ps                   = PanelState{};
                        simState             = SimState{};
                        logEntries.clear();
                        lastConditionsPassed = 0;
                        failedAttempts       = 0;
                        starsEarned          = 0;
                        gameMode             = GAME_PLAYING;
                        dragging             = false;
                        connecting           = false;
                        hoverNodeId          = -1;
                        hoverPort            = -1;
                        contextMenu.visible  = false;
                        troubleshootMode     = false;
                        traceModalOpen       = false;
                        failAnnotationTimer  = 0.f;
                        lastFailedTrace      = {};
                    }
                }
                // any other click on the WIN screen is silently consumed
            } else if (traceModalOpen) {
                const float MW = 480.f, MH = 360.f;
                Rectangle modal = {(SCREEN_W() - MW) / 2.f, (SCREEN_H() - MH) / 2.f, MW, MH};
                if (!CheckCollisionPointRec(screenMouse, modal))
                    traceModalOpen = false;
                // all clicks consumed while modal is open
            } else {
            if (contextMenu.visible) {
                if (contextMenu.hoverItem != -1)
                    ExecuteMenuAction(contextMenu, nodes, cables, selectedId, ps, camera, simState, logEntries);
                contextMenu.visible = false;
            } else if (simState.mode == SIM_SELECTING_DST && inCanvas) {
                // Destination selection — find clicked node
                int dstId = -1;
                for (int i = (int)nodes.size() - 1; i >= 0; --i) {
                    if (CheckCollisionPointRec(worldMouse, GetNodeRect(nodes[i]))) {
                        dstId = nodes[i].id;
                        break;
                    }
                }
                // Clicking empty canvas or src node is a no-op
                if (dstId != -1 && dstId != simState.srcId) {
                    const DeviceNode* dst = FindNode(nodes, dstId);
                    std::string destIp = dst ? GetFirstValidIp(*dst) : "";
                    LogEntry le;
                    auto pushLog = [&](LogEntry entry) {
                        if (logEntries.size() >= 50) logEntries.erase(logEntries.begin());
                        logEntries.push_back(entry);
                    };
                    if (destIp.empty()) {
                        le.success   = false;
                        le.type      = LOG_FORWARD;
                        const DeviceNode* src = FindNode(nodes, simState.srcId);
                        le.pathStr   = (src ? src->label : "?") + " \xe2\x86\x92 " +
                                       (dst ? dst->label : "?");
                        le.reason    = "destination has no configured IP";
                        le.timestamp = GetTime();
                    } else {
                        PlayPacketSend();
                        ForwardResult fr = SimulateForward(simState.srcId, destIp,
                                                           nodes, cables);

                        // Apply ARP cache updates to nodes
                        for (const auto& ev : fr.arpEvents) {
                            if (!ev.cacheHit && !ev.mac.empty()) {
                                for (auto& n : nodes)
                                    if (n.id == ev.nodeId) { n.arpTable[ev.ip] = ev.mac; break; }
                            }
                        }

                        // Push ARP log entries before the routing summary
                        for (const auto& ev : fr.arpEvents) {
                            if (ev.cacheHit) {
                                LogEntry e;
                                e.type = LOG_ARP_HIT; e.success = true;
                                e.pathStr   = "ARP cache hit: " + ev.ip + " \xe2\x86\x92 " + ev.mac;
                                e.timestamp = GetTime();
                                pushLog(e);
                            } else if (!ev.mac.empty()) {
                                LogEntry req;
                                req.type = LOG_ARP_REQ; req.success = true;
                                req.pathStr   = "ARP who has " + ev.ip + "?";
                                req.timestamp = GetTime();
                                pushLog(req);
                                LogEntry rep;
                                rep.type = LOG_ARP_REPLY; rep.success = true;
                                rep.pathStr   = ev.ip + " is at " + ev.mac;
                                rep.timestamp = GetTime();
                                pushLog(rep);
                            } else {
                                LogEntry e;
                                e.type = LOG_ARP_REQ; e.success = false;
                                e.pathStr   = "ARP who has " + ev.ip + "? \xe2\x80\x94 no reply";
                                e.timestamp = GetTime();
                                pushLog(e);
                            }
                        }

                        {
                            uint32_t seed    = (!fr.hops.empty()) ? fr.hops[0].outLabel : 0u;
                            if (seed == MPLS_IMPLICIT_NULL) seed = 0u;
                            int vlanSeed = fr.hops.empty() ? 0 : fr.hops[0].vlanTag;
                            simState.anim = PacketAnim{.result = fr, .currentLabel = seed,
                                                       .currentVlan = vlanSeed};
                        }
                        le.success     = fr.success;
                        le.pathStr     = BuildPathStr(fr.path, nodes);
                        le.reason      = fr.reason;
                        le.type        = LOG_FORWARD;
                        le.traceResult = fr;
                        le.timestamp   = GetTime();
                        simState.mode  = SIM_ANIMATING;
                        if (fr.success) {
                            PlayPacketArrive();
                            failAnnotationTimer = 0.f;
                        } else {
                            ++failedAttempts;
                            PlayPacketFail();
                            failAnnotationTimer = 5.0f;
                            lastFailedTrace     = fr;
                        }
                    }
                    pushLog(le);
                    // Check all win conditions after every simulation attempt
                    if (gameMode == GAME_PLAYING &&
                        !activeLevelDef.winConditions.empty()) {
                        int passed = CheckWinConditions(
                                         activeLevelDef, nodes, cables);
                        lastConditionsPassed = passed;
                        if (passed == (int)activeLevelDef.winConditions.size()) {
                            gameMode    = GAME_WIN;
                            starsEarned = ComputeStars(failedAttempts);
                        }
                    }
                    if (simState.mode != SIM_ANIMATING) {
                        simState.mode  = SIM_IDLE;
                        simState.srcId = -1;
                    }
                }
                // else: no-op, stay in SIM_SELECTING_DST
            } else if (screenMouse.y >= (float)CANVAS_H() &&
                       screenMouse.y <  (float)SCREEN_H()  &&
                       screenMouse.x <  (float)CANVAS_W()) {
                // Log console click — open trace modal for LOG_FORWARD entries
                int hitIdx = LogConsoleHitTest(screenMouse, logEntries);
                if (hitIdx >= 0) {
                    activeTrace    = logEntries[hitIdx].traceResult;
                    traceModalOpen = true;
                }
            } else if (inCanvas) {
            int pNode = -1, pPort = -1;
            if (HitTestPort(nodes, worldMouse, -1, pNode, pPort)) {
                connecting      = true;
                connectFromId   = pNode;
                connectFromPort = pPort;
                dragging        = false;
            } else {
                int hitIdx = -1;
                for (int i = (int)nodes.size() - 1; i >= 0; --i) {
                    if (CheckCollisionPointRec(worldMouse, GetNodeRect(nodes[i]))) {
                        hitIdx = i;
                        break;
                    }
                }
                for (auto& n : nodes) n.selected = false;
                if (hitIdx != -1) {
                    nodes[hitIdx].selected = true;
                    selectedId = nodes[hitIdx].id;
                    dragging   = true;
                    dragOffset = {worldMouse.x - nodes[hitIdx].position.x,
                                  worldMouse.y - nodes[hitIdx].position.y};
                } else {
                    selectedId = -1;
                    dragging   = false;
                }
            }
            }  // closes else if (inCanvas)
            }  // closes else (gameMode != GAME_WIN)
        }

        // ── LMB held ──────────────────────────────────────────────────
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragging) {
            for (auto& n : nodes) {
                if (n.id == selectedId) {
                    n.position = {worldMouse.x - dragOffset.x,
                                  worldMouse.y - dragOffset.y};
                    break;
                }
            }
        }
        if (inCanvas && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && connecting) {
            hoverNodeId = -1;
            hoverPort   = -1;
            HitTestPort(nodes, worldMouse, connectFromId, hoverNodeId, hoverPort);
        }

        // ── LMB released ──────────────────────────────────────────────
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (connecting && hoverNodeId != -1) {
                bool portOccupied = false;
                for (const auto& c : cables) {
                    if ((c.fromId == connectFromId && c.fromPort == connectFromPort) ||
                        (c.toId   == connectFromId && c.toPort   == connectFromPort) ||
                        (c.fromId == hoverNodeId   && c.fromPort == hoverPort) ||
                        (c.toId   == hoverNodeId   && c.toPort   == hoverPort))
                    { portOccupied = true; break; }
                }
                if (!portOccupied)
                    cables.push_back({connectFromId, connectFromPort,
                                      hoverNodeId,   hoverPort});
            }
            connecting  = false;
            dragging    = false;
            hoverNodeId = -1;
            hoverPort   = -1;
        }

        // ── RMB pressed — open context menu ───────────────────────────
        if (inCanvas && !traceModalOpen && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            if (simState.mode == SIM_SELECTING_DST || simState.mode == SIM_ANIMATING) {
                if (simState.mode == SIM_SELECTING_DST) {
                    simState.mode  = SIM_IDLE;
                    simState.srcId = -1;
                }
                // Swallow the RMB — don't open context menu during sim
            } else {
                connecting  = false;
                hoverNodeId = -1;
                hoverPort   = -1;
                contextMenu.screenPos = screenMouse;
                contextMenu.worldPos  = worldMouse;
                contextMenu.hoverItem = -1;
                ps.activeField        = -1;
                ps.activeRouteField   = -1;

                // Priority: node body > cable > canvas
                int hitIdx = -1;
                for (int i = (int)nodes.size() - 1; i >= 0; --i) {
                    if (CheckCollisionPointRec(worldMouse, GetNodeRect(nodes[i]))) {
                        hitIdx = i;
                        break;
                    }
                }
                if (hitIdx != -1) {
                    contextMenu.visible      = true;
                    contextMenu.ctx          = CTX_NODE;
                    contextMenu.targetId     = nodes[hitIdx].id;
                    contextMenu.targetBroken = nodes[hitIdx].crashed;
                } else {
                    int ci = HitTestCable(cables, nodes, worldMouse, 6.0f);
                    if (ci != -1) {
                        contextMenu.visible      = true;
                        contextMenu.ctx          = CTX_CABLE;
                        contextMenu.targetId     = ci;
                        contextMenu.targetBroken = cables[ci].broken;
                    } else {
                        contextMenu.visible      = true;
                        contextMenu.ctx          = CTX_CANVAS;
                        contextMenu.targetId     = -1;
                        contextMenu.targetBroken = false;
                    }
                }
            }
        }

        // ── Panel click-to-focus ───────────────────────────────────────
        if (gameMode != GAME_WIN && !traceModalOpen && !inCanvas && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (selectedId != -1) {
                // Tab clicks
                if (CheckCollisionPointRec(screenMouse, PnlConfigTabRect())) {
                    ps.activeTab           = TAB_CONFIG;
                    ps.activeField         = -1;
                    ps.activeRouteField    = -1;
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                    ps.bgpAsnField         = -1;
                    ps.bgpAsnBuf.clear();
                    ps.subActiveField      = -1;
                }
                if (CheckCollisionPointRec(screenMouse, PnlRoutesTabRect())) {
                    ps.activeTab           = TAB_ROUTES;
                    ps.activeField         = -1;
                    ps.activeRouteField    = -1;
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                    ps.bgpAsnField         = -1;
                    ps.bgpAsnBuf.clear();
                    ps.subActiveField      = -1;
                }
                if (CheckCollisionPointRec(screenMouse, PnlArpTabRect())) {
                    ps.activeTab           = TAB_ARP;
                    ps.activeField         = -1;
                    ps.activeRouteField    = -1;
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                    ps.bgpAsnField         = -1;
                    ps.bgpAsnBuf.clear();
                    ps.subActiveField      = -1;
                }
                if (CheckCollisionPointRec(screenMouse, PnlOspfTabRect())) {
                    ps.activeTab           = TAB_OSPF;
                    ps.activeField         = -1;
                    ps.activeRouteField    = -1;
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                    ps.bgpAsnField         = -1;
                    ps.bgpAsnBuf.clear();
                    ps.subActiveField      = -1;
                }
                if (CheckCollisionPointRec(screenMouse, PnlMplsTabRect())) {
                    ps.activeTab           = TAB_MPLS;
                    ps.activeField         = -1;
                    ps.activeRouteField    = -1;
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                    ps.bgpAsnField         = -1;
                    ps.bgpAsnBuf.clear();
                    ps.subActiveField      = -1;
                }
                if (CheckCollisionPointRec(screenMouse, PnlBgpTabRect())) {
                    ps.activeTab           = TAB_BGP;
                    ps.activeField         = -1;
                    ps.activeRouteField    = -1;
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                    ps.bgpAsnField         = -1;
                    ps.bgpAsnBuf.clear();
                    ps.subActiveField      = -1;
                }
                if (CheckCollisionPointRec(screenMouse, PnlVlanTabRect())) {
                    ps.activeTab           = TAB_VLAN;
                    ps.activeField         = -1;
                    ps.activeRouteField    = -1;
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                    ps.bgpAsnField         = -1;
                    ps.bgpAsnBuf.clear();
                    ps.vlanPortField       = -1;
                    ps.vlanPortBuf.clear();
                    ps.subActiveField      = -1;
                }
                else if (CheckCollisionPointRec(screenMouse, PnlSubTabRect())) {
                    ps.activeTab           = TAB_SUB;
                    ps.activeField         = -1;
                    ps.activeRouteField    = -1;
                    ps.activePortAreaField = -1;
                    ps.bgpAsnField         = -1;
                    ps.vlanPortField       = -1;
                    ps.subActiveField      = -1;
                }
                if (CheckCollisionPointRec(screenMouse, PnlVxlanTabRect())) {
                    ps.activeTab           = TAB_VXLAN;
                    ps.activeField         = -1;
                    ps.activeRouteField    = -1;
                    ps.activePortAreaField = -1;
                    ps.bgpAsnField         = -1;
                    ps.vlanPortField       = -1;
                    ps.subActiveField      = -1;
                    ps.vxlanField          = -1;
                }
                // Config tab field focus
                if (ps.activeTab == TAB_CONFIG) {
                    ps.activeField         = -1;
                    ps.activePortAreaField = -1;
                    if (CheckCollisionPointRec(screenMouse, PnlFieldRect(CFG_HOSTNAME_Y))) ps.activeField = 0;
                    if (CheckCollisionPointRec(screenMouse, PnlFieldRect(CFG_MGMTIP_Y)))  ps.activeField = 1;
                    DeviceNode* selNode = nullptr;
                    for (auto& nd : nodes)
                        if (nd.id == selectedId) { selNode = &nd; break; }
                    for (int i = 0; i < PORTS_PER_NODE; ++i) {
                        if (CheckCollisionPointRec(screenMouse, PnlPortFieldRect(i)))
                            ps.activeField = 2 + i;
                        if (CheckCollisionPointRec(screenMouse, PnlPortAreaFieldRect(i))) {
                            ps.activePortAreaField = i;
                            if (selNode)
                                ps.portAreaBuf = std::to_string(selNode->ospfPortArea[i]);
                        }
                    }
                }
                // Routes tab: field focus, [×] delete, [Add] button
                if (ps.activeTab == TAB_ROUTES) {
                    ps.activeRouteField = -1;

                    // Add-form field focus
                    if (CheckCollisionPointRec(screenMouse, PnlRouteDestRect()))
                        ps.activeRouteField = 0;
                    if (CheckCollisionPointRec(screenMouse, PnlRouteNextRect()))
                        ps.activeRouteField = 1;

                    DeviceNode* selNode = nullptr;
                    for (auto& nd : nodes)
                        if (nd.id == selectedId) { selNode = &nd; break; }

                    if (selNode) {
                        // [Add] button
                        if (CheckCollisionPointRec(screenMouse, PnlRouteAddBtnRect())) {
                            if (ValidateIP(ps.newRouteDest) && ValidateIPOnly(ps.newRouteNext)) {
                                selNode->staticRoutes.push_back(
                                    {ps.newRouteDest, ps.newRouteNext, -1, ROUTE_STATIC});
                                ps.newRouteDest.clear();
                                ps.newRouteNext.clear();
                                ps.activeRouteField = -1;
                            }
                        }

                        // [×] delete buttons — check each displayed route row
                        auto table = GetRoutingTable(*selNode);
                        int displayed = std::min((int)table.size(), 8);
                        int staticCounter = 0;
                        for (int i = 0; i < displayed; ++i) {
                            if (table[i].src == ROUTE_STATIC) {
                                if (CheckCollisionPointRec(screenMouse, PnlRouteDeleteRect(i))) {
                                    if (staticCounter < (int)selNode->staticRoutes.size())
                                        selNode->staticRoutes.erase(
                                            selNode->staticRoutes.begin() + staticCounter);
                                    break;
                                }
                                ++staticCounter;
                            }
                        }
                    }
                }

                // OSPF tab: enable/disable toggle
                if (ps.activeTab == TAB_OSPF) {
                    DeviceNode* selNode = nullptr;
                    for (auto& nd : nodes)
                        if (nd.id == selectedId) { selNode = &nd; break; }
                    if (selNode && selNode->type == ROUTER) {
                        if (CheckCollisionPointRec(screenMouse, PnlOspfEnableRect())) {
                            selNode->ospfEnabled = !selNode->ospfEnabled;
                            if (!selNode->ospfEnabled) {
                                selNode->ospfNeighbors.clear();
                                selNode->areaLsdbs.clear();
                                selNode->ospfRoutes.clear();
                                selNode->helloTimer = 0.f;
                                selNode->routerId.clear();
                            }
                        }
                    }
                }

                // MPLS tab: ldpEnabled toggle
                if (ps.activeTab == TAB_MPLS) {
                    DeviceNode* selNode = nullptr;
                    for (auto& nd : nodes)
                        if (nd.id == selectedId) { selNode = &nd; break; }
                    if (selNode && selNode->type == ROUTER && selNode->ospfEnabled) {
                        if (CheckCollisionPointRec(screenMouse, PnlMplsToggleRect())) {
                            selNode->ldpEnabled = !selNode->ldpEnabled;
                            if (!selNode->ldpEnabled) selNode->lfib.clear();
                        }
                    }
                }

                // BGP tab: toggle + ASN field click
                if (ps.activeTab == TAB_BGP) {
                    DeviceNode* selNode = nullptr;
                    for (auto& nd : nodes)
                        if (nd.id == selectedId) { selNode = &nd; break; }
                    if (selNode && selNode->type == ROUTER) {
                        if (CheckCollisionPointRec(screenMouse, PnlBgpToggleRect())) {
                            selNode->bgpEnabled = !selNode->bgpEnabled;
                            if (!selNode->bgpEnabled) {
                                selNode->bgpNeighbors.clear();
                                selNode->bgpRoutes.clear();
                                selNode->isRouteReflector = false;
                            }
                            ps.bgpAsnField = -1;
                            ps.bgpAsnBuf.clear();
                        }
                        if (selNode->bgpEnabled &&
                            CheckCollisionPointRec(screenMouse, PnlBgpAsnRect())) {
                            ps.bgpAsnField = selNode->id;
                            ps.bgpAsnBuf   = selNode->localAsn > 0
                                             ? std::to_string(selNode->localAsn) : "";
                        }
                        if (selNode->bgpEnabled && selNode->localAsn > 0 &&
                            CheckCollisionPointRec(screenMouse, PnlBgpRrRect())) {
                            selNode->isRouteReflector = !selNode->isRouteReflector;
                        }
                    }
                }

                // VLAN tab: mode toggle + VLAN ID field click
                if (ps.activeTab == TAB_VLAN) {
                    DeviceNode* selNode = nullptr;
                    for (auto& nd : nodes)
                        if (nd.id == selectedId) { selNode = &nd; break; }
                    if (selNode && selNode->type == SWITCH) {
                        for (int p = 0; p < PORTS_PER_NODE; ++p) {
                            if (CheckCollisionPointRec(screenMouse, PnlVlanPortModeRect(p))) {
                                selNode->vlanPorts[p].mode =
                                    (selNode->vlanPorts[p].mode == VLAN_ACCESS)
                                    ? VLAN_TRUNK : VLAN_ACCESS;
                                ps.vlanPortField = -1;
                                ps.vlanPortBuf.clear();
                            }
                            if (selNode->vlanPorts[p].mode == VLAN_ACCESS &&
                                CheckCollisionPointRec(screenMouse, PnlVlanPortIdRect(p))) {
                                ps.vlanPortField = p;
                                ps.vlanPortBuf   = std::to_string(selNode->vlanPorts[p].accessVlan);
                            }
                        }
                    }
                }

                // Sub-interface tab: port selector, VLAN/IP fields, add/delete
                else if (ps.activeTab == TAB_SUB && selectedId != -1) {
                    DeviceNode* n = nullptr;
                    for (auto& node : nodes) if (node.id == selectedId) { n = &node; break; }
                    if (n && n->type == ROUTER) {
                        // Port selector buttons
                        for (int p = 0; p < PORTS_PER_NODE; ++p)
                            if (CheckCollisionPointRec(screenMouse, PnlSubPortBtnRect(p)))
                                ps.subFormPort = p;
                        // VLAN text field
                        if (CheckCollisionPointRec(screenMouse, PnlSubVlanFieldRect()))
                            ps.subActiveField = 0;
                        // IP text field
                        if (CheckCollisionPointRec(screenMouse, PnlSubIpFieldRect()))
                            ps.subActiveField = 1;
                        // Add button
                        if (CheckCollisionPointRec(screenMouse, PnlSubAddBtnRect())) {
                            int vlan = std::atoi(ps.subVlanBuf.c_str());
                            if (vlan >= 1 && vlan <= 4094 && ValidateIP(ps.subIpBuf)) {
                                SubInterface sif;
                                sif.parentPort = ps.subFormPort;
                                sif.vlanId     = vlan;
                                sif.ip         = ps.subIpBuf;
                                n->subIfaces.push_back(sif);
                                ps.subVlanBuf.clear();
                                ps.subIpBuf.clear();
                                ps.subActiveField = -1;
                            }
                        }
                        // Delete buttons
                        for (int i = 0; i < (int)n->subIfaces.size(); ++i)
                            if (CheckCollisionPointRec(screenMouse, PnlSubRowDeleteRect(i))) {
                                n->subIfaces.erase(n->subIfaces.begin() + i);
                                break;
                            }
                    }
                }
                // VXLAN tab: toggle + VNI/VTEP field clicks
                if (ps.activeTab == TAB_VXLAN) {
                    DeviceNode* selNode = nullptr;
                    for (auto& nd : nodes)
                        if (nd.id == selectedId) { selNode = &nd; break; }
                    if (selNode && selNode->type == ROUTER) {
                        if (CheckCollisionPointRec(screenMouse, PnlVxlanToggleRect())) {
                            selNode->vxlanEnabled = !selNode->vxlanEnabled;
                            if (!selNode->vxlanEnabled) {
                                selNode->evpnEnabled = false;
                                selNode->vni = 0;
                                selNode->vtepIp.clear();
                                ps.vxlanField = -1;
                                ps.vxlanVniBuf.clear();
                                ps.vxlanVtepBuf.clear();
                            }
                        }
                        if (selNode->vxlanEnabled) {
                            if (CheckCollisionPointRec(screenMouse, PnlVxlanVniRect())) {
                                ps.vxlanField = 0;
                                ps.vxlanVniBuf = selNode->vni > 0 ? std::to_string(selNode->vni) : "";
                            }
                            if (CheckCollisionPointRec(screenMouse, PnlVxlanVtepRect())) {
                                ps.vxlanField = 1;
                                ps.vxlanVtepBuf = selNode->vtepIp;
                            }
                            if (CheckCollisionPointRec(screenMouse, PnlVxlanEvpnRect())) {
                                selNode->evpnEnabled = !selNode->evpnEnabled;
                            }
                        }
                    }
                }
            }
        }

        // ── Text field update ──────────────────────────────────────────
        if (ps.activeTab == TAB_CONFIG && ps.activeField != -1 && selectedId != -1) {
            DeviceNode* selNode = nullptr;
            for (auto& nd : nodes)
                if (nd.id == selectedId) { selNode = &nd; break; }
            if (selNode) {
                if (ps.activeField == 0) UpdateTextField(selNode->label,   32);
                if (ps.activeField == 1) UpdateTextField(selNode->mgmtIp, 18);
                for (int i = 0; i < PORTS_PER_NODE; ++i)
                    if (ps.activeField == 2 + i) UpdateTextField(selNode->portIp[i], 18);
            }
        } else if (ps.activeTab == TAB_CONFIG && ps.activePortAreaField != -1 && selectedId != -1) {
            DeviceNode* selNode = nullptr;
            for (auto& nd : nodes)
                if (nd.id == selectedId) { selNode = &nd; break; }
            if (selNode) {
                UpdateTextField(ps.portAreaBuf, 5);  // area IDs 0-65535 (5 digits max)
                if (IsKeyPressed(KEY_ENTER)) {
                    bool allDigits = !ps.portAreaBuf.empty() &&
                        std::all_of(ps.portAreaBuf.begin(), ps.portAreaBuf.end(), ::isdigit);
                    if (allDigits) {
                        uint32_t newArea = (uint32_t)std::stoul(ps.portAreaBuf);
                        selNode->ospfPortArea[ps.activePortAreaField] = newArea;
                        if (selNode->ospfEnabled) {
                            selNode->ospfNeighbors.clear();
                            selNode->areaLsdbs.clear();
                            selNode->ospfRoutes.clear();
                        }
                    }
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                } else if (IsKeyPressed(KEY_ESCAPE)) {
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                }
            }
        } else if (ps.activeTab == TAB_ROUTES && ps.activeRouteField != -1 && selectedId != -1) {
            DeviceNode* selNode = nullptr;
            for (auto& nd : nodes)
                if (nd.id == selectedId) { selNode = &nd; break; }
            if (selNode) UpdateRoutesTab(selNode, ps);
        } else if (ps.bgpAsnField != -1 && selectedId != -1) {
            int key = GetCharPressed();
            while (key > 0) {
                if (key >= '0' && key <= '9' && ps.bgpAsnBuf.size() < 9)
                    ps.bgpAsnBuf += static_cast<char>(key);
                key = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE) && !ps.bgpAsnBuf.empty())
                ps.bgpAsnBuf.pop_back();
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                for (auto& nd : nodes) {
                    if (nd.id == ps.bgpAsnField) {
                        try { nd.localAsn = static_cast<uint32_t>(std::stoul(ps.bgpAsnBuf)); }
                        catch (...) { nd.localAsn = 0; }
                        break;
                    }
                }
                ps.bgpAsnField = -1;
                ps.bgpAsnBuf.clear();
            }
        } else if (ps.vlanPortField != -1 && selectedId != -1) {
            int key = GetCharPressed();
            while (key > 0) {
                if (key >= '0' && key <= '9' && ps.vlanPortBuf.size() < 4)
                    ps.vlanPortBuf += static_cast<char>(key);
                key = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE) && !ps.vlanPortBuf.empty())
                ps.vlanPortBuf.pop_back();
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                for (auto& nd : nodes) {
                    if (nd.id == selectedId) {
                        try {
                            int v = std::stoi(ps.vlanPortBuf);
                            if (v >= 1 && v <= 4094)
                                nd.vlanPorts[ps.vlanPortField].accessVlan = v;
                        } catch (...) {}
                        break;
                    }
                }
                ps.vlanPortField = -1;
                ps.vlanPortBuf.clear();
            } else if (IsKeyPressed(KEY_ESCAPE)) {
                ps.vlanPortField = -1;
                ps.vlanPortBuf.clear();
            }
        } else if (ps.subActiveField != -1 && selectedId != -1) {
            if (ps.subActiveField == 0) {
                // VLAN field: digits only, max 4 chars
                int key = GetCharPressed();
                while (key > 0) {
                    if (key >= '0' && key <= '9' && (int)ps.subVlanBuf.size() < 4)
                        ps.subVlanBuf += static_cast<char>(key);
                    key = GetCharPressed();
                }
                if (IsKeyPressed(KEY_BACKSPACE) && !ps.subVlanBuf.empty())
                    ps.subVlanBuf.pop_back();
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB))
                    ps.subActiveField = 1;
                if (IsKeyPressed(KEY_ESCAPE))
                    ps.subActiveField = -1;
            } else if (ps.subActiveField == 1) {
                // IP/CIDR field: allow digits, dots, slash
                int key = GetCharPressed();
                while (key > 0) {
                    if ((std::isdigit(key) || key == '.' || key == '/') && (int)ps.subIpBuf.size() < 18)
                        ps.subIpBuf += static_cast<char>(key);
                    key = GetCharPressed();
                }
                if (IsKeyPressed(KEY_BACKSPACE) && !ps.subIpBuf.empty())
                    ps.subIpBuf.pop_back();
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE))
                    ps.subActiveField = -1;
            }
        } else if (ps.vxlanField != -1 && selectedId != -1) {
            DeviceNode* selNode = nullptr;
            for (auto& nd : nodes)
                if (nd.id == selectedId) { selNode = &nd; break; }
            if (selNode) {
                if (ps.vxlanField == 0) {
                    // VNI field: digits only, up to 8 digits (16777215 = 8 chars)
                    int key = GetCharPressed();
                    while (key > 0) {
                        if (key >= '0' && key <= '9' && (int)ps.vxlanVniBuf.size() < 8)
                            ps.vxlanVniBuf += static_cast<char>(key);
                        key = GetCharPressed();
                    }
                    if (IsKeyPressed(KEY_BACKSPACE) && !ps.vxlanVniBuf.empty())
                        ps.vxlanVniBuf.pop_back();
                    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_TAB)) {
                        try {
                            uint32_t v = (uint32_t)std::stoul(ps.vxlanVniBuf);
                            if (v >= 1 && v <= 16777215) selNode->vni = v;
                        } catch (...) {}
                        ps.vxlanField = -1;
                        ps.vxlanVniBuf.clear();
                        while (GetCharPressed() > 0) {}
                    } else if (IsKeyPressed(KEY_ESCAPE)) {
                        ps.vxlanField = -1;
                        ps.vxlanVniBuf.clear();
                    }
                } else if (ps.vxlanField == 1) {
                    // VTEP IP field: digits, dots only (no slash — bare IP)
                    int key = GetCharPressed();
                    while (key > 0) {
                        if ((std::isdigit(key) || key == '.') && (int)ps.vxlanVtepBuf.size() < 15)
                            ps.vxlanVtepBuf += static_cast<char>(key);
                        key = GetCharPressed();
                    }
                    if (IsKeyPressed(KEY_BACKSPACE) && !ps.vxlanVtepBuf.empty())
                        ps.vxlanVtepBuf.pop_back();
                    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                        selNode->vtepIp = ps.vxlanVtepBuf;
                        ps.vxlanField = -1;
                        ps.vxlanVtepBuf.clear();
                    } else if (IsKeyPressed(KEY_ESCAPE)) {
                        ps.vxlanField = -1;
                        ps.vxlanVtepBuf.clear();
                    }
                }
            }
        } else {
            while (GetCharPressed() > 0) {}  // flush char queue when no field active
        }

        // Reset active field when selection changes
        if (selectedId != prevSelectedId) {
            ps.activeField         = -1;
            ps.activeTab           = TAB_CONFIG;
            ps.activeRouteField    = -1;
            ps.activePortAreaField = -1;
            ps.portAreaBuf.clear();
            ps.newRouteDest.clear();
            ps.newRouteNext.clear();
            ps.bgpAsnField         = -1;
            ps.bgpAsnBuf.clear();
            ps.vlanPortField       = -1;
            ps.vlanPortBuf.clear();
            ps.subActiveField      = -1;
            ps.vxlanField          = -1;
            ps.vxlanVniBuf.clear();
            ps.vxlanVtepBuf.clear();
            prevSelectedId         = selectedId;
        }

        if (failAnnotationTimer > 0.f)
            failAnnotationTimer -= dt;

        // ── OSPF engine tick ─────────────────────────────────────────────
        {
            auto ospfEvents = UpdateOspf(dt, nodes, cables);
            UpdateLdp(nodes, cables);   // recompute LFIB after each OSPF tick
            UpdateBgp(nodes, cables);   // recompute BGP RIB every frame
            auto pushLog = [&](LogEntry entry) {
                if (logEntries.size() >= 50) logEntries.erase(logEntries.begin());
                logEntries.push_back(entry);
            };
            for (const auto& msg : ospfEvents) {
                LogEntry e;
                e.success   = true;
                e.pathStr   = msg;
                e.type      = LOG_OSPF;
                e.timestamp = GetTime();
                pushLog(e);
            }
        }

        // ── Packet animation update ───────────────────────────────────────
        if (simState.mode == SIM_ANIMATING) {
            UpdatePacketAnim(simState.anim, dt, nodes, cables);
            if (simState.anim.done && simState.anim.failPulse    <= 0.f
                                   && simState.anim.successPulse <= 0.f) {
                simState.mode  = SIM_IDLE;
                simState.srcId = -1;
            }
        }

        // ── Draw ───────────────────────────────────────────────────────
        BeginDrawing();
            ClearBackground(BG_COLOR);

            BeginMode2D(camera);
                DrawDotGrid(camera);
                DrawAllCables(cables, nodes);
                // Annotation first (background layer) — packet anim renders on top
                if (failAnnotationTimer > 0.f)
                    DrawBrokenPath(nodes, cables, lastFailedTrace);
                if (troubleshootMode)
                    DrawTroubleshootOverlay(nodes, cables);
                DrawPacketAnim(simState.anim, nodes, cables);

                if (connecting) {
                    const DeviceNode* fromNode = FindNode(nodes, connectFromId);
                    if (fromNode) {
                        Vector2 p0 = GetPortPosition(*fromNode, connectFromPort);
                        DrawLineEx(p0, worldMouse, 2.0f, Color{148, 163, 184, 180});
                        DrawCircleV(worldMouse, 4.0f, WHITE);
                    }
                }

                if (hoverNodeId != -1) {
                    const DeviceNode* hNode = FindNode(nodes, hoverNodeId);
                    if (hNode) {
                        Vector2 pp = GetPortPosition(*hNode, hoverPort);
                        DrawCircleV(pp, PORT_RADIUS + 3.0f, Color{34, 197, 94, 200});
                    }
                }

                for (const auto& n : nodes) DrawDeviceNode(n);

                // SIM_SELECTING_DST — ring on all eligible destination nodes
                if (simState.mode == SIM_SELECTING_DST) {
                    for (const auto& n : nodes) {
                        if (n.id == simState.srcId) {
                            // Bright ring on source
                            DrawCircleLinesV(n.position, NODE_W * 0.6f,
                                             Color{34, 197, 94, 200});
                        } else {
                            // Faint blue ring on valid destinations
                            DrawCircleLinesV(n.position, NODE_W * 0.6f,
                                             Color{96, 165, 250, 100});
                        }
                    }
                }
            EndMode2D();

            if (simState.mode == SIM_SELECTING_DST) {
                const char* hint = "Click destination node  \xe2\x80\x94  ESC to cancel";
                int tw = MeasureText(hint, 12);
                DrawText(hint, (CANVAS_W() - tw) / 2, 12, 12,
                         Color{148, 163, 184, 255});
            }

            DrawPanel(selectedId, nodes, ps);
            DrawContextMenu(contextMenu, screenMouse);
            DrawLogConsole(logEntries);
            if (traceModalOpen)
                DrawTraceModal(activeTrace);

            // Level HUD badge (top-left) and win overlay
            if (gameMode == GAME_PLAYING || gameMode == GAME_WIN) {
                DrawLevelHUD(currentLevel, activeLevelDef.title,
                             lastConditionsPassed,
                             (int)activeLevelDef.winConditions.size(),
                             starsEarned);
                if (troubleshootMode) {
                    DrawRectangle(8, 34, 148, 18, Color{239, 68, 68, 200});
                    DrawRectangleLinesEx({8, 34, 148, 18}, 1.0f, Color{239, 68, 68, 255});
                    DrawText("TROUBLESHOOT [T]", 14, 38, 9, WHITE);
                }
            }
            if (gameMode == GAME_WIN) {
                DrawWinOverlay(activeLevelDef, currentLevel < 13, starsEarned);
            }

            // HUD — screen space, outside camera
            DrawFPS(CANVAS_W() - 80, 10);
            DrawText("P=PC  R=Router  S=Switch  Del=Delete  MMB=Pan  Scroll=Zoom  "
                     "Drag-port=Cable  Esc=Cancel  1-9,0=Level",
                     10, CANVAS_H() - 24, 10, Color{100, 116, 139, 255});
            DrawFileDialog(fileOp, fileNameBuf, fileOpMsg, fileOpTimer);
        EndDrawing();
    }

    UnloadSounds();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
