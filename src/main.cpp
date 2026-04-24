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
    InitWindow(SCREEN_W, SCREEN_H, "Packet Path");
    SetTargetFPS(60);
    InitAudioDevice();
    InitSounds();

    std::vector<DeviceNode> nodes;
    nodes.push_back(SpawnNode(PC, {0.0f, 0.0f}));

    Camera2D camera = {};
    camera.offset   = {CANVAS_W / 2.0f, CANVAS_H / 2.0f};
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
    bool          traceModalOpen = false;
    ForwardResult activeTrace;
    float         failAnnotationTimer = 0.f;
    ForwardResult lastFailedTrace;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        Vector2 screenMouse = GetMousePosition();
        Vector2 worldMouse  = GetScreenToWorld2D(screenMouse, camera);
        bool inCanvas = (screenMouse.x < (float)CANVAS_W &&
                         screenMouse.y < (float)CANVAS_H);

        // ── Spawn / delete / cancel ────────────────────────────────────
        if (inCanvas && gameMode != GAME_WIN &&
            ps.activeField == -1 && ps.activeRouteField == -1 &&
            simState.mode == SIM_IDLE) {
            if (IsKeyPressed(KEY_P)) nodes.push_back(SpawnNode(PC,     worldMouse));
            if (IsKeyPressed(KEY_R)) nodes.push_back(SpawnNode(ROUTER, worldMouse));
            if (IsKeyPressed(KEY_S)) nodes.push_back(SpawnNode(SWITCH, worldMouse));
            // Level shortcuts: 1–8 load JSON levels, 0 returns to sandbox
            if (ps.activePortAreaField == -1) {
                for (int k = 1; k <= 8; ++k) {
                    if (IsKeyPressed(KEY_ONE + (k - 1))) {
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
                            gameMode             = GAME_PLAYING;
                            dragging             = false;
                            connecting           = false;
                            hoverNodeId          = -1;
                            hoverPort            = -1;
                            contextMenu.visible  = false;
                            traceModalOpen       = false;
                            failAnnotationTimer  = 0.f;
                            lastFailedTrace      = {};
                        }
                    }
                }
                if (IsKeyPressed(KEY_ZERO)) {
                    gameMode            = GAME_SANDBOX;
                    currentLevel        = 0;
                    traceModalOpen      = false;
                    failAnnotationTimer = 0.f;
                    lastFailedTrace     = {};
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
                    gameMode             = GAME_PLAYING;
                    dragging             = false;
                    connecting           = false;
                    hoverNodeId          = -1;
                    hoverPort            = -1;
                    contextMenu.visible  = false;
                    traceModalOpen       = false;
                    failAnnotationTimer  = 0.f;
                    lastFailedTrace      = {};
                } else if (CheckCollisionPointRec(screenMouse, WinNextBtnRect()) &&
                           currentLevel < 8) {
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
                        gameMode             = GAME_PLAYING;
                        dragging             = false;
                        connecting           = false;
                        hoverNodeId          = -1;
                        hoverPort            = -1;
                        contextMenu.visible  = false;
                        traceModalOpen       = false;
                        failAnnotationTimer  = 0.f;
                        lastFailedTrace      = {};
                    }
                }
                // any other click on the WIN screen is silently consumed
            } else if (traceModalOpen) {
                const float MW = 480.f, MH = 360.f;
                Rectangle modal = {(SCREEN_W - MW) / 2.f, (SCREEN_H - MH) / 2.f, MW, MH};
                if (!CheckCollisionPointRec(screenMouse, modal))
                    traceModalOpen = false;
                // all clicks consumed while modal is open
            } else {
            if (contextMenu.visible) {
                if (contextMenu.hoverItem != -1)
                    ExecuteMenuAction(contextMenu, nodes, cables, selectedId, ps, camera, simState);
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
                            uint32_t seed = (!fr.hops.empty()) ? fr.hops[0].outLabel : 0u;
                            if (seed == MPLS_IMPLICIT_NULL) seed = 0u;
                            simState.anim = PacketAnim{.result = fr, .currentLabel = seed};
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
                        if (passed == (int)activeLevelDef.winConditions.size())
                            gameMode = GAME_WIN;
                    }
                    if (simState.mode != SIM_ANIMATING) {
                        simState.mode  = SIM_IDLE;
                        simState.srcId = -1;
                    }
                }
                // else: no-op, stay in SIM_SELECTING_DST
            } else if (screenMouse.y >= (float)CANVAS_H &&
                       screenMouse.y <  (float)SCREEN_H  &&
                       screenMouse.x <  (float)CANVAS_W) {
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
                    contextMenu.visible  = true;
                    contextMenu.ctx      = CTX_NODE;
                    contextMenu.targetId = nodes[hitIdx].id;
                } else {
                    int ci = HitTestCable(cables, nodes, worldMouse, 6.0f);
                    if (ci != -1) {
                        contextMenu.visible  = true;
                        contextMenu.ctx      = CTX_CABLE;
                        contextMenu.targetId = ci;
                    } else {
                        contextMenu.visible  = true;
                        contextMenu.ctx      = CTX_CANVAS;
                        contextMenu.targetId = -1;
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
                    ps.bgpAsnField = -1;
                    ps.bgpAsnBuf.clear();
                }
                if (CheckCollisionPointRec(screenMouse, PnlRoutesTabRect())) {
                    ps.activeTab           = TAB_ROUTES;
                    ps.activeField         = -1;
                    ps.activeRouteField    = -1;
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                    ps.bgpAsnField = -1;
                    ps.bgpAsnBuf.clear();
                }
                if (CheckCollisionPointRec(screenMouse, PnlArpTabRect())) {
                    ps.activeTab           = TAB_ARP;
                    ps.activeField         = -1;
                    ps.activeRouteField    = -1;
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                    ps.bgpAsnField = -1;
                    ps.bgpAsnBuf.clear();
                }
                if (CheckCollisionPointRec(screenMouse, PnlOspfTabRect())) {
                    ps.activeTab           = TAB_OSPF;
                    ps.activeField         = -1;
                    ps.activeRouteField    = -1;
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                    ps.bgpAsnField = -1;
                    ps.bgpAsnBuf.clear();
                }
                if (CheckCollisionPointRec(screenMouse, PnlMplsTabRect())) {
                    ps.activeTab           = TAB_MPLS;
                    ps.activeField         = -1;
                    ps.activeRouteField    = -1;
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                    ps.bgpAsnField = -1;
                    ps.bgpAsnBuf.clear();
                }
                if (CheckCollisionPointRec(screenMouse, PnlBgpTabRect())) {
                    ps.activeTab           = TAB_BGP;
                    ps.activeField         = -1;
                    ps.activeRouteField    = -1;
                    ps.activePortAreaField = -1;
                    ps.portAreaBuf.clear();
                    ps.bgpAsnField         = -1;
                    ps.bgpAsnBuf.clear();
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
                DrawText(hint, (CANVAS_W - tw) / 2, 12, 12,
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
                             (int)activeLevelDef.winConditions.size());
            }
            if (gameMode == GAME_WIN) {
                DrawWinOverlay(activeLevelDef, currentLevel < 8);
            }

            // HUD — screen space, outside camera
            DrawFPS(CANVAS_W - 80, 10);
            DrawText("P=PC  R=Router  S=Switch  Del=Delete  MMB=Pan  Scroll=Zoom  "
                     "Drag-port=Cable  Esc=Cancel  1-8=Level  0=Sandbox",
                     10, CANVAS_H - 24, 10, Color{100, 116, 139, 255});
        EndDrawing();
    }

    UnloadSounds();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
