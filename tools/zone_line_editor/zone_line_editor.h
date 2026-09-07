#pragma once

// Zone Line Editor -- shared declarations.
//
// T451: split out of the 1,303-line tools/zone_line_editor.cpp. The editor
// was one class with every body inline; the bodies now live in
// tools/zone_line_editor/zle_{io,input,render}.cpp and the lifecycle ones in
// tools/zone_line_editor.cpp beside main(). Each body is a contiguous line
// range of the original, de-indented one level; nothing else changed.

#include <irrlicht.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>
#include <cmath>
#include <limits>
#include <json/json.h>

#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/portal_geometry_clipper.h"
#include "client/hc_map.h"

using namespace irr;
using namespace core;
using namespace video;
using namespace scene;
using namespace gui;
using namespace EQT::Graphics;


// Zone line data structure
struct ZoneLineBounds {
    std::string target_zone;
    std::string target_long_name;
    std::string type;  // SHORT, LONG_X, LONG_Y, CLICK
    float trigger_x = 0;
    float trigger_y = 0;
    float trigger_z = 0;
    // Bounds (the part we're editing)
    float min_x = 0;
    float max_x = 0;
    float min_y = 0;
    float max_y = 0;
    float min_z = 0;
    float max_z = 0;
    bool has_bounds = false;
    std::string source;
    std::string landing;
};

// Editor state
struct EditorState {
    std::string zone_name;
    std::string zone_long_name;
    std::string eq_path;
    std::string maps_path;
    float debug_x1 = 0, debug_y1 = 0, debug_x2 = 0, debug_y2 = 0;
    bool debug_area_enabled = false;
    std::vector<ZoneLineBounds> zone_lines;
    int selected_index = -1;

    // View state
    float view_x = 0.0f;
    float view_y = 0.0f;
    float zoom = 1.0f;
    float min_zoom = 0.01f;
    float max_zoom = 10.0f;

    // Drawing state
    bool is_drawing = false;
    float draw_start_x = 0.0f;
    float draw_start_y = 0.0f;
    float draw_end_x = 0.0f;
    float draw_end_y = 0.0f;

    // Dragging state
    bool is_dragging = false;
    int dragging_index = -1;
    float drag_offset_x = 0.0f;
    float drag_offset_y = 0.0f;

    // Draw mode for zone line bounds
    // 0 = XY (horizontal area, Z spans full range) - walk-through walls
    // 1 = X-line (fixed X range, Y and Z span full) - north-south walls
    // 2 = Y-line (fixed Y range, X and Z span full) - east-west walls
    // 3 = Z-flat (horizontal area at fixed Z) - shafts, cliffs, swimming
    int draw_mode = 0;
    float fixed_z_level = 0.0f;  // For Z-flat mode
    float fixed_z_thickness = 20.0f;  // Thickness of Z-flat trigger

    // Display options
    bool show_grid = true;
    bool show_geometry = true;
    bool show_points = true;
    bool show_portals = false;  // R key toggle

    // Z value for zone line placement (adjusted by zoom keys in increments of 10)
    float placement_z = 0.0f;

    // Z-depth filtering (uses placement_z - hides geometry above it)
    bool z_filter_enabled = false;

    // Zone geometry bounds
    float zone_min_x = 0, zone_max_x = 0;
    float zone_min_y = 0, zone_max_y = 0;
    float zone_min_z = 0, zone_max_z = 0;
};

class ZoneLineEditorEventReceiver : public IEventReceiver {
public:
    ZoneLineEditorEventReceiver() {
        for (u32 i = 0; i < KEY_KEY_CODES_COUNT; ++i)
            keys_down_[i] = false;
    }

    virtual bool OnEvent(const SEvent& event) override {
        if (event.EventType == EET_KEY_INPUT_EVENT) {
            keys_down_[event.KeyInput.Key] = event.KeyInput.PressedDown;
            if (event.KeyInput.PressedDown) {
                last_key_pressed_ = event.KeyInput.Key;
                key_pressed_ = true;
            }
        }
        else if (event.EventType == EET_MOUSE_INPUT_EVENT) {
            mouse_x_ = event.MouseInput.X;
            mouse_y_ = event.MouseInput.Y;

            switch (event.MouseInput.Event) {
                case EMIE_LMOUSE_PRESSED_DOWN:
                    left_button_down_ = true;
                    left_clicked_ = true;
                    click_x_ = mouse_x_;
                    click_y_ = mouse_y_;
                    break;
                case EMIE_LMOUSE_LEFT_UP:
                    left_button_down_ = false;
                    left_released_ = true;
                    break;
                case EMIE_RMOUSE_PRESSED_DOWN:
                    right_button_down_ = true;
                    right_clicked_ = true;
                    break;
                case EMIE_RMOUSE_LEFT_UP:
                    right_button_down_ = false;
                    break;
                case EMIE_MOUSE_WHEEL:
                    wheel_delta_ += event.MouseInput.Wheel;
                    break;
                default:
                    break;
            }
        }
        return false;
    }

    bool isKeyDown(EKEY_CODE key) const { return keys_down_[key]; }
    bool wasKeyPressed() { bool p = key_pressed_; key_pressed_ = false; return p; }
    EKEY_CODE getLastKeyPressed() const { return last_key_pressed_; }

    int getMouseX() const { return mouse_x_; }
    int getMouseY() const { return mouse_y_; }
    bool isLeftButtonDown() const { return left_button_down_; }
    bool isRightButtonDown() const { return right_button_down_; }
    bool wasLeftClicked() { bool c = left_clicked_; left_clicked_ = false; return c; }
    bool wasLeftReleased() { bool r = left_released_; left_released_ = false; return r; }
    bool wasRightClicked() { bool c = right_clicked_; right_clicked_ = false; return c; }
    int getClickX() const { return click_x_; }
    int getClickY() const { return click_y_; }
    float getWheelDelta() { float d = wheel_delta_; wheel_delta_ = 0; return d; }

private:
    bool keys_down_[KEY_KEY_CODES_COUNT];
    EKEY_CODE last_key_pressed_ = KEY_KEY_CODES_COUNT;
    bool key_pressed_ = false;
    int mouse_x_ = 0, mouse_y_ = 0;
    int click_x_ = 0, click_y_ = 0;
    bool left_button_down_ = false;
    bool right_button_down_ = false;
    bool left_clicked_ = false;
    bool left_released_ = false;
    bool right_clicked_ = false;
    float wheel_delta_ = 0;
};

class ZoneLineEditor {
public:
    ZoneLineEditor(const std::string& zone_name, const std::string& eq_path,
                   const std::string& maps_path = "");

    void setDebugArea(float x1, float y1, float x2, float y2);

    bool init();

    void run();

    ~ZoneLineEditor();

private:
    bool loadZoneGeometry();
    void loadPortalData();
    void loadZoneLineData();
    void saveZoneLineData();
    void handleInput();
    void screenToWorld(int screen_x, int screen_y, float& world_x, float& world_y);
    void worldToScreen(float world_x, float world_y, int& screen_x, int& screen_y);
    void render();
    void renderGrid();
    void renderZoneGeometry();
    void renderZoneLines();
    void renderDrawingRect();
    void renderPortalOverlay();
    void renderUI();

private:
    IrrlichtDevice* device_ = nullptr;
    IVideoDriver* driver_ = nullptr;
    ISceneManager* smgr_ = nullptr;
    IGUIEnvironment* guienv_ = nullptr;
    ZoneLineEditorEventReceiver event_receiver_;

    EditorState state_;
    std::shared_ptr<ZoneGeometry> zone_geometry_;

    // Portal overlay data
    struct PortalOverlayEntry {
        size_t regionA, regionB;
        float centerX, centerY, centerZ;
        float area;
        std::string classification;  // "open-air", "wall", "doorway"
        // BSP boundary polygon vertices (EQ coords: x,y,z packed)
        std::vector<float> boundaryVerts;
        // Opening polygon vertices (EQ coords)
        std::vector<std::vector<float>> openingVerts;
    };
    struct RegionOverlayEntry {
        size_t regionIndex;
        bool indoor;
        float minX, minY, minZ, maxX, maxY, maxZ;  // AABB in EQ coords
    };
    std::vector<PortalOverlayEntry> portal_overlays_;
    std::vector<RegionOverlayEntry> region_overlays_;
    std::unique_ptr<WldLoader> wld_loader_;  // Keep alive for per-region geometry access
};

void printUsage(const char* program);
