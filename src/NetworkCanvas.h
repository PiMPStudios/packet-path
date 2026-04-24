#pragma once
#include "raylib.h"
#include "Device.h"
#include "Cable.h"
#include "Packet.h"
#include "SimulationEngine.h"
#include "ConfigPanel.h"
#include "UI.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ── Screen & layout constants ─────────────────────────────────────────────
static const int   SCREEN_W       = 1280;
static const int   SCREEN_H       = 720;
static const Color BG_COLOR       = {15, 23, 42, 255};
static const int   PANEL_W        = 280;
static const int   CANVAS_W       = SCREEN_W - PANEL_W;
static const int   LOG_H          = 90;
static const int   CANVAS_H       = SCREEN_H - LOG_H;
static const Color PANEL_BG       = {22, 33, 62, 255};
static const Color PANEL_BORDER   = {51, 65, 85, 255};
static const int   MENU_ITEM_H    = 28;
static const int   CONTEXT_MENU_W = 160;

// ── Config tab layout ─────────────────────────────────────────────────────
static const int CFG_HOSTNAME_Y   = 158;
static const int CFG_MGMTIP_Y     = 210;
static const int CFG_IFACE_SEP_Y  = 246;
static const int CFG_PORT_Y0      = 272;
static const int CFG_PORT_STRIDE  = 44;

// ── Routes tab layout ─────────────────────────────────────────────────────
static const int RTE_ROW_Y0       = 142;
static const int RTE_HEADER_SEP_Y = 136;
static const int RTE_ROW_H        = 22;
static const int RTE_ADD_SEP_Y    = 420;
static const int RTE_DEST_Y       = 464;
static const int RTE_NEXT_Y       = 516;
static const int RTE_BTN_Y        = 554;

// ── Canvas hit testing ────────────────────────────────────────────────────
bool HitTestPort(const std::vector<DeviceNode>& nodes, Vector2 worldMouse,
                 int excludeId, int& outNode, int& outPort);

int  HitTestCable(const std::vector<Cable>& cables,
                  const std::vector<DeviceNode>& nodes,
                  Vector2 worldMouse, float threshold);

// ── Draw functions ────────────────────────────────────────────────────────
void DrawDotGrid(const Camera2D& cam);
void DrawDeviceNode(const DeviceNode& n);
void DrawAllCables(const std::vector<Cable>& cables,
                   const std::vector<DeviceNode>& nodes);
void DrawPacketAnim(const PacketAnim& anim,
                    const std::vector<DeviceNode>& nodes,
                    const std::vector<Cable>& cables);
void DrawTextField(Rectangle r, const char* topLabel, const char* placeholder,
                   const std::string& value, bool active, bool valid);
void DrawConfigTab(const DeviceNode* n, const PanelState& ps);
void DrawRoutesTab(const DeviceNode* n, const PanelState& ps);
void DrawArpTab(const DeviceNode* n);
void DrawOspfTab(const DeviceNode* n);
void DrawMplsTab(const DeviceNode* n);
void DrawBgpTab(const DeviceNode* n, const PanelState& ps);
void DrawVlanTab(const DeviceNode* n, const PanelState& ps);
void DrawPanel(int selectedId, const std::vector<DeviceNode>& nodes,
               const PanelState& ps);
void DrawContextMenu(const ContextMenu& menu, Vector2 screenMouse);
void DrawLogConsole(const std::vector<LogEntry>& entries);
void DrawBrokenPath(const std::vector<DeviceNode>& nodes,
                    const std::vector<Cable>& cables,
                    const ForwardResult& result);
