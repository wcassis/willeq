/**
 * Zone Line Editor - 2D tool for marking zone line boundaries
 *
 * Usage: zone_line_editor <zone_name> [--eq-path /path/to/EQ]
 *
 * Controls:
 *   WASD / Arrow Keys - Pan view
 *   Mouse Wheel / +/- - Zoom in/out (camera only)
 *   [ / ] - Adjust placement Z (+/- 10)
 *   { / } - Adjust Z thickness for Z-flat mode
 *   Left Click - Start drawing line/rect, or click existing zone line to drag
 *   Left Drag - Draw bounds or move zone line
 *   Left Release - Finish drawing/dragging
 *   Right Click - Cancel current drawing
 *   Tab - Cycle through zone line targets
 *   Delete - Clear selected zone line bounds
 *   Shift+Delete - Delete zone line entry entirely
 *   Ctrl+S - Save all to JSON
 *   G - Toggle grid
 *   Z - Toggle zone geometry
 *   P - Toggle zone points display
 *   M - Cycle draw mode (XY, X-line, Y-line, Z-flat)
 *   F - Toggle Z-depth filtering (hides geometry above placement Z)
 *   Escape - Exit
 */

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
                   const std::string& maps_path = "")
    {
        state_.zone_name = zone_name;
        state_.eq_path = eq_path;
        state_.maps_path = maps_path;
    }

    void setDebugArea(float x1, float y1, float x2, float y2) {
        state_.debug_area_enabled = true;
        state_.debug_x1 = x1;
        state_.debug_y1 = y1;
        state_.debug_x2 = x2;
        state_.debug_y2 = y2;
    }

    bool init() {
        // Create Irrlicht device
        device_ = createDevice(EDT_SOFTWARE, dimension2d<u32>(1280, 800), 32,
                               false, false, false, &event_receiver_);
        if (!device_) {
            std::cerr << "Failed to create Irrlicht device" << std::endl;
            return false;
        }

        device_->setWindowCaption(L"Zone Line Editor");
        driver_ = device_->getVideoDriver();
        smgr_ = device_->getSceneManager();
        guienv_ = device_->getGUIEnvironment();

        // Load zone geometry
        if (!loadZoneGeometry()) {
            std::cerr << "Failed to load zone geometry" << std::endl;
            return false;
        }

        // Load existing zone line data
        loadZoneLineData();

        // Center view on zone
        state_.view_x = (state_.zone_min_x + state_.zone_max_x) / 2.0f;
        state_.view_y = (state_.zone_min_y + state_.zone_max_y) / 2.0f;
        // Initialize placement_z to zone center
        state_.placement_z = (state_.zone_min_z + state_.zone_max_z) / 2.0f;

        // Set initial zoom to fit zone tightly (with small margin for UI)
        float zone_width = state_.zone_max_x - state_.zone_min_x;
        float zone_height = state_.zone_max_y - state_.zone_min_y;
        float screen_width = driver_->getScreenSize().Width;
        float screen_height = driver_->getScreenSize().Height - 150.0f;  // Leave room for UI at top
        state_.zoom = std::min(screen_width / zone_width, screen_height / zone_height) * 0.95f;

        return true;
    }

    void run() {
        while (device_->run()) {
            handleInput();
            render();
        }
    }

    ~ZoneLineEditor() {
        if (device_) {
            device_->drop();
        }
    }

private:
    bool loadZoneGeometry() {
        std::string s3d_path = state_.eq_path + "/" + state_.zone_name + ".s3d";
        std::string wld_name = state_.zone_name + ".wld";

        // Check if file exists
        std::ifstream test(s3d_path);
        if (!test.good()) {
            std::cerr << "Cannot find zone file: " << s3d_path << std::endl;
            return false;
        }
        test.close();

        // Load zone using WldLoader (keep alive for per-region geometry access)
        wld_loader_ = std::make_unique<WldLoader>();
        if (!wld_loader_->parseFromArchive(s3d_path, wld_name)) {
            std::cerr << "Failed to parse zone archive" << std::endl;
            return false;
        }

        // Get combined geometry
        zone_geometry_ = wld_loader_->getCombinedGeometry();
        if (!zone_geometry_) {
            std::cerr << "No geometry found in zone" << std::endl;
            return false;
        }

        // Use bounds from geometry
        state_.zone_min_x = zone_geometry_->minX;
        state_.zone_max_x = zone_geometry_->maxX;
        state_.zone_min_y = zone_geometry_->minY;
        state_.zone_max_y = zone_geometry_->maxY;
        state_.zone_min_z = zone_geometry_->minZ;
        state_.zone_max_z = zone_geometry_->maxZ;

        std::cout << "Loaded zone " << state_.zone_name << " with "
                  << zone_geometry_->vertices.size() << " vertices, "
                  << zone_geometry_->triangles.size() << " triangles" << std::endl;
        std::cout << "Bounds: X[" << state_.zone_min_x << ", " << state_.zone_max_x << "] "
                  << "Y[" << state_.zone_min_y << ", " << state_.zone_max_y << "] "
                  << "Z[" << state_.zone_min_z << ", " << state_.zone_max_z << "]" << std::endl;

        // Load portal data if BSP + PVS available
        loadPortalData();

        return true;
    }

    void loadPortalData() {
        auto bspTree = wld_loader_->getBspTree();
        if (!bspTree || !wld_loader_->hasPvsData()) {
            std::cout << "No BSP/PVS data — portal overlay unavailable" << std::endl;
            return;
        }

        // Load HCMap collision mesh
        std::unique_ptr<HCMap> hcmap;
        if (!state_.maps_path.empty()) {
            hcmap.reset(HCMap::LoadMapFile(state_.zone_name, state_.maps_path));
            if (hcmap && hcmap->IsLoaded()) {
                auto stats = hcmap->GetMemoryStats();
                std::cout << "HCMap loaded: " << stats.vertexCount << " vertices, "
                          << stats.faceCount << " faces" << std::endl;
            } else {
                std::cout << "Warning: Failed to load HCMap from " << state_.maps_path << std::endl;
                hcmap.reset();
            }
        } else {
            std::cout << "No --maps-path specified — portal clipping disabled" << std::endl;
            return;
        }

        size_t numRegions = bspTree->regions.size();
        float bMinX = state_.zone_min_x, bMinY = state_.zone_min_y, bMinZ = state_.zone_min_z;
        float bMaxX = state_.zone_max_x, bMaxY = state_.zone_max_y, bMaxZ = state_.zone_max_z;

        // Extract BSP boundary portals
        PortalSystem portalSystem;
        portalSystem.buildFromBsp(*bspTree, bMinX, bMinY, bMinZ, bMaxX, bMaxY, bMaxZ);
        const auto& portalData = portalSystem.getData();

        std::cout << "BSP portals: " << portalData.portals.size() << std::endl;

        // Compute region AABBs and classify
        struct AABB { float minX, minY, minZ, maxX, maxY, maxZ; };
        std::map<size_t, AABB> regionAABBs;

        for (size_t i = 0; i < numRegions; ++i) {
            auto geom = wld_loader_->getGeometryForRegion(i);
            if (!geom || geom->vertices.empty()) continue;
            AABB aabb;
            aabb.minX = aabb.minY = aabb.minZ = std::numeric_limits<float>::max();
            aabb.maxX = aabb.maxY = aabb.maxZ = std::numeric_limits<float>::lowest();
            for (const auto& v : geom->vertices) {
                float wx = geom->centerX + v.x, wy = geom->centerY + v.y, wz = geom->centerZ + v.z;
                aabb.minX = std::min(aabb.minX, wx); aabb.minY = std::min(aabb.minY, wy); aabb.minZ = std::min(aabb.minZ, wz);
                aabb.maxX = std::max(aabb.maxX, wx); aabb.maxY = std::max(aabb.maxY, wy); aabb.maxZ = std::max(aabb.maxZ, wz);
            }
            regionAABBs[i] = aabb;
        }

        // Clip portals against HCMap collision mesh
        std::unordered_map<size_t, float> regionOpeningArea;
        std::unordered_map<size_t, int> regionDoorwayCount;
        float rayLength = 15.0f;
        float gridSpacing = 0.5f;

        int processed = 0;
        int progressStep = std::max(1, (int)portalData.portals.size() / 10);

        int skippedOutside = 0;
        for (size_t pi = 0; pi < portalData.portals.size(); ++pi) {
            const auto& portal = portalData.portals[pi];

            // If debug area is set, skip portals outside it
            if (state_.debug_area_enabled) {
                float daMinX = std::min(state_.debug_x1, state_.debug_x2);
                float daMaxX = std::max(state_.debug_x1, state_.debug_x2);
                float daMinY = std::min(state_.debug_y1, state_.debug_y2);
                float daMaxY = std::max(state_.debug_y1, state_.debug_y2);
                if (portal.centerX < daMinX || portal.centerX > daMaxX ||
                    portal.centerY < daMinY || portal.centerY > daMaxY) {
                    skippedOutside++;
                    continue;
                }
            }

            ClippedPortalResult clipped = clipPortalAgainstGeometry(
                portal, hcmap.get(), rayLength, gridSpacing);

            PortalOverlayEntry entry;
            entry.regionA = portal.regionA;
            entry.regionB = portal.regionB;
            entry.centerX = portal.centerX;
            entry.centerY = portal.centerY;
            entry.centerZ = portal.centerZ;
            entry.area = portal.area;
            entry.boundaryVerts = portal.vertices;

            if (clipped.isFullyOpen()) {
                entry.classification = "open-air";
            } else if (clipped.isFullyCovered()) {
                entry.classification = "wall";
            } else {
                entry.classification = "doorway";
                regionOpeningArea[portal.regionA] += clipped.totalOpeningArea;
                regionOpeningArea[portal.regionB] += clipped.totalOpeningArea;
                regionDoorwayCount[portal.regionA]++;
                regionDoorwayCount[portal.regionB]++;

                for (auto& opening : clipped.openings) {
                    entry.openingVerts.push_back(std::move(opening.vertices));
                }
            }

            portal_overlays_.push_back(std::move(entry));

            if (++processed % progressStep == 0) {
                std::cout << "  Portal clipping: " << processed << " / " << portalData.portals.size() << std::endl;
            }
        }

        if (state_.debug_area_enabled) {
            std::cout << "  Debug area: processed " << processed << " portals, skipped " << skippedOutside << " outside area" << std::endl;
        }

        // Classify regions
        float threshold = 0.3f;
        for (const auto& [idx, aabb] : regionAABBs) {
            float surfArea = 2.0f * ((aabb.maxX-aabb.minX)*(aabb.maxY-aabb.minY) +
                                      (aabb.maxX-aabb.minX)*(aabb.maxZ-aabb.minZ) +
                                      (aabb.maxY-aabb.minY)*(aabb.maxZ-aabb.minZ));
            float ratio = surfArea > 0 ? regionOpeningArea[idx] / surfArea : 999.0f;
            bool indoor = (ratio <= threshold && regionDoorwayCount[idx] > 0);

            RegionOverlayEntry re;
            re.regionIndex = idx;
            re.indoor = indoor;
            re.minX = aabb.minX; re.minY = aabb.minY; re.minZ = aabb.minZ;
            re.maxX = aabb.maxX; re.maxY = aabb.maxY; re.maxZ = aabb.maxZ;
            region_overlays_.push_back(re);
        }

        int indoorCount = 0;
        for (const auto& r : region_overlays_) if (r.indoor) indoorCount++;

        int wallCount = 0, doorwayCount = 0, openAirCount = 0;
        for (const auto& p : portal_overlays_) {
            if (p.classification == "wall") wallCount++;
            else if (p.classification == "doorway") doorwayCount++;
            else openAirCount++;
        }

        std::cout << "Portal overlay ready: " << portal_overlays_.size() << " portals ("
                  << openAirCount << " open-air, " << wallCount << " wall, " << doorwayCount << " doorway), "
                  << indoorCount << "/" << region_overlays_.size() << " indoor regions" << std::endl;
    }

    void loadZoneLineData() {
        // Load from zone-specific file in data/zone_lines/
        std::string json_path = "data/zone_lines/" + state_.zone_name + ".json";
        std::ifstream file(json_path);
        if (!file.good()) {
            std::cout << "No zone line data at " << json_path << std::endl;
            return;
        }

        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(file, root)) {
            std::cerr << "Failed to parse " << json_path << std::endl;
            return;
        }

        state_.zone_long_name = root.get("long_name", state_.zone_name).asString();

        const Json::Value& zone_lines = root["zone_lines"];
        for (const auto& zl : zone_lines) {
            ZoneLineBounds bounds;
            bounds.target_zone = zl.get("target_zone", "").asString();
            bounds.target_long_name = zl.get("target_long_name", "").asString();
            bounds.type = zl.get("type", "SHORT").asString();
            bounds.source = zl.get("source", "").asString();
            bounds.landing = zl.get("landing", "fixed").asString();

            // JSON stores coordinates directly (no transformation needed)
            const Json::Value& coords = zl["trigger_coords"];
            if (!coords["x"].isNull() && !coords["y"].isNull()) {
                bounds.trigger_x = coords["x"].asFloat();
                bounds.trigger_y = coords["y"].asFloat();
                bounds.trigger_z = coords.get("z", 0.0f).asFloat();

                // Load bounds if present
                if (zl.isMember("bounds")) {
                    const Json::Value& b = zl["bounds"];
                    bounds.min_x = b["min_x"].asFloat();
                    bounds.max_x = b["max_x"].asFloat();
                    bounds.min_y = b["min_y"].asFloat();
                    bounds.max_y = b["max_y"].asFloat();
                    bounds.min_z = b["min_z"].asFloat();
                    bounds.max_z = b["max_z"].asFloat();
                    bounds.has_bounds = true;
                } else {
                    bounds.has_bounds = false;
                }

                state_.zone_lines.push_back(bounds);
            } else {
                // Skip entries with null coordinates (UNKNOWN type)
                std::cout << "Skipping " << bounds.target_zone << " (no coordinates)" << std::endl;
            }
        }

        std::cout << "Loaded " << state_.zone_lines.size() << " zone lines from " << json_path << std::endl;
    }

    void saveZoneLineData() {
        // Load existing zone-specific file to preserve other fields
        std::string json_path = "data/zone_lines/" + state_.zone_name + ".json";
        Json::Value root;

        std::ifstream infile(json_path);
        if (infile.good()) {
            Json::Reader reader;
            if (!reader.parse(infile, root)) {
                std::cerr << "Failed to parse existing " << json_path << std::endl;
                return;
            }
        }
        infile.close();

        // Update zone_lines array
        Json::Value zone_lines(Json::arrayValue);
        for (const auto& zl : state_.zone_lines) {
            Json::Value entry;
            entry["target_zone"] = zl.target_zone;
            entry["target_long_name"] = zl.target_long_name;
            entry["type"] = zl.type;
            entry["source"] = zl.source;
            entry["landing"] = zl.landing;

            // Save coordinates directly (no transformation)
            Json::Value coords;
            coords["x"] = zl.trigger_x;
            coords["y"] = zl.trigger_y;
            coords["z"] = zl.trigger_z;
            entry["trigger_coords"] = coords;

            if (zl.has_bounds) {
                // Save bounds directly (no transformation)
                Json::Value bounds;
                bounds["min_x"] = zl.min_x;
                bounds["max_x"] = zl.max_x;
                bounds["min_y"] = zl.min_y;
                bounds["max_y"] = zl.max_y;
                bounds["min_z"] = zl.min_z;
                bounds["max_z"] = zl.max_z;
                entry["bounds"] = bounds;
            }

            zone_lines.append(entry);
        }

        // Update root
        root["zone"] = state_.zone_name;
        root["long_name"] = state_.zone_long_name;
        root["zone_lines"] = zone_lines;

        // Write back to zone-specific file
        std::ofstream outfile(json_path);
        Json::StyledWriter writer;
        outfile << writer.write(root);
        outfile.close();

        std::cout << "Saved zone line data to " << json_path << std::endl;
    }

    void handleInput() {
        float pan_speed = 500.0f / state_.zoom;
        float zoom_speed = 0.05f;  // Reduced from 0.1f for finer control

        // Pan with WASD or arrow keys (but not when Ctrl is held for shortcuts)
        bool ctrl_held = event_receiver_.isKeyDown(KEY_LCONTROL) || event_receiver_.isKeyDown(KEY_RCONTROL);
        if (!ctrl_held) {
            if (event_receiver_.isKeyDown(KEY_KEY_W) || event_receiver_.isKeyDown(KEY_UP)) {
                state_.view_y -= pan_speed * 0.016f;  // Assuming ~60fps
            }
            if (event_receiver_.isKeyDown(KEY_KEY_S) || event_receiver_.isKeyDown(KEY_DOWN)) {
                state_.view_y += pan_speed * 0.016f;
            }
            if (event_receiver_.isKeyDown(KEY_KEY_A) || event_receiver_.isKeyDown(KEY_LEFT)) {
                state_.view_x -= pan_speed * 0.016f;
            }
            if (event_receiver_.isKeyDown(KEY_KEY_D) || event_receiver_.isKeyDown(KEY_RIGHT)) {
                state_.view_x += pan_speed * 0.016f;
            }
        }

        // Zoom with mouse wheel (camera only, no Z adjustment)
        float wheel = event_receiver_.getWheelDelta();
        if (wheel != 0) {
            state_.zoom *= (1.0f + wheel * zoom_speed);
            state_.zoom = std::max(state_.min_zoom, std::min(state_.max_zoom, state_.zoom));
        }
        // Note: +/- and [/] key zoom is handled in wasKeyPressed() section below

        // Handle key presses
        if (event_receiver_.wasKeyPressed()) {
            EKEY_CODE key = event_receiver_.getLastKeyPressed();

            switch (key) {
                case KEY_ESCAPE:
                    device_->closeDevice();
                    break;
                case KEY_KEY_G:
                    state_.show_grid = !state_.show_grid;
                    break;
                case KEY_KEY_Z:
                    state_.show_geometry = !state_.show_geometry;
                    break;
                case KEY_KEY_P:
                    state_.show_points = !state_.show_points;
                    break;
                case KEY_KEY_R:
                    state_.show_portals = !state_.show_portals;
                    std::cout << "Portal overlay: " << (state_.show_portals ? "ON" : "OFF") << std::endl;
                    break;
                case KEY_TAB:
                    // Cycle through zone lines
                    if (!state_.zone_lines.empty()) {
                        state_.selected_index = (state_.selected_index + 1) % state_.zone_lines.size();
                        // Center on selected
                        auto& zl = state_.zone_lines[state_.selected_index];
                        if (zl.trigger_x != 0 || zl.trigger_y != 0) {
                            state_.view_x = zl.trigger_x;
                            state_.view_y = zl.trigger_y;
                        }
                    }
                    break;
                case KEY_KEY_S:
                    if (event_receiver_.isKeyDown(KEY_LCONTROL) || event_receiver_.isKeyDown(KEY_RCONTROL)) {
                        saveZoneLineData();
                    }
                    break;
                case KEY_DELETE:
                    if (state_.selected_index >= 0 && state_.selected_index < (int)state_.zone_lines.size()) {
                        if (event_receiver_.isKeyDown(KEY_LSHIFT) || event_receiver_.isKeyDown(KEY_RSHIFT)) {
                            // Shift+Delete: Remove zone line entirely
                            std::string removed = state_.zone_lines[state_.selected_index].target_zone;
                            state_.zone_lines.erase(state_.zone_lines.begin() + state_.selected_index);
                            std::cout << "Deleted zone line: " << removed << std::endl;
                            if (state_.selected_index >= (int)state_.zone_lines.size()) {
                                state_.selected_index = state_.zone_lines.empty() ? -1 : (int)state_.zone_lines.size() - 1;
                            }
                        } else {
                            // Delete: Just clear bounds
                            state_.zone_lines[state_.selected_index].has_bounds = false;
                            std::cout << "Cleared bounds for: " << state_.zone_lines[state_.selected_index].target_zone << std::endl;
                        }
                    }
                    break;
                case KEY_KEY_M:
                    // Cycle draw mode
                    state_.draw_mode = (state_.draw_mode + 1) % 4;
                    {
                        const char* mode_names[] = {"XY (walk-through)", "X-line (N-S wall)", "Y-line (E-W wall)", "Z-flat (shaft/cliff)"};
                        std::cout << "Draw mode: " << mode_names[state_.draw_mode] << std::endl;
                        // For Z-flat mode, use selected zone line's Z as the level
                        if (state_.draw_mode == 3 && state_.selected_index >= 0) {
                            state_.fixed_z_level = state_.zone_lines[state_.selected_index].trigger_z;
                            std::cout << "  Z level: " << state_.fixed_z_level << " (thickness: " << state_.fixed_z_thickness << ")" << std::endl;
                        }
                    }
                    break;
                case KEY_OEM_4:  // [ or { key
                    if (event_receiver_.isKeyDown(KEY_LSHIFT) || event_receiver_.isKeyDown(KEY_RSHIFT)) {
                        // { - decrease Z thickness
                        state_.fixed_z_thickness = std::max(5.0f, state_.fixed_z_thickness - 5.0f);
                        std::cout << "Z thickness: " << state_.fixed_z_thickness << std::endl;
                    } else {
                        // [ - increase placement Z by 10
                        state_.placement_z += 10.0f;
                        std::cout << "Placement Z: " << state_.placement_z << std::endl;
                    }
                    break;
                case KEY_OEM_6:  // ] or } key
                    if (event_receiver_.isKeyDown(KEY_LSHIFT) || event_receiver_.isKeyDown(KEY_RSHIFT)) {
                        // } - increase Z thickness
                        state_.fixed_z_thickness = std::min(200.0f, state_.fixed_z_thickness + 5.0f);
                        std::cout << "Z thickness: " << state_.fixed_z_thickness << std::endl;
                    } else {
                        // ] - decrease placement Z by 10
                        state_.placement_z -= 10.0f;
                        std::cout << "Placement Z: " << state_.placement_z << std::endl;
                    }
                    break;
                case KEY_PLUS:
                case KEY_ADD:
                    // Zoom in only
                    state_.zoom *= 1.1f;
                    state_.zoom = std::min(state_.max_zoom, state_.zoom);
                    break;
                case KEY_MINUS:
                case KEY_SUBTRACT:
                    // Zoom out only
                    state_.zoom *= 0.9f;
                    state_.zoom = std::max(state_.min_zoom, state_.zoom);
                    break;
                case KEY_KEY_F:
                    // Toggle Z filtering (hides geometry above placement_z)
                    state_.z_filter_enabled = !state_.z_filter_enabled;
                    std::cout << "Z filter: " << (state_.z_filter_enabled ? "enabled" : "disabled");
                    if (state_.z_filter_enabled) {
                        std::cout << " (hiding geometry above Z=" << state_.placement_z << ")";
                    }
                    std::cout << std::endl;
                    break;
                default:
                    break;
            }
        }

        // Mouse interaction - check for click on existing zone line first
        if (event_receiver_.wasLeftClicked()) {
            float click_world_x, click_world_y;
            screenToWorld(event_receiver_.getClickX(), event_receiver_.getClickY(),
                          click_world_x, click_world_y);

            // Check if clicking on an existing zone line box
            int clicked_zone_line = -1;
            for (size_t i = 0; i < state_.zone_lines.size(); ++i) {
                const auto& zl = state_.zone_lines[i];
                if (zl.has_bounds) {
                    if (click_world_x >= zl.min_x && click_world_x <= zl.max_x &&
                        click_world_y >= zl.min_y && click_world_y <= zl.max_y) {
                        clicked_zone_line = (int)i;
                        break;
                    }
                }
            }

            if (clicked_zone_line >= 0) {
                // Start dragging the clicked zone line
                state_.is_dragging = true;
                state_.dragging_index = clicked_zone_line;
                state_.selected_index = clicked_zone_line;
                const auto& zl = state_.zone_lines[clicked_zone_line];
                float center_x = (zl.min_x + zl.max_x) / 2.0f;
                float center_y = (zl.min_y + zl.max_y) / 2.0f;
                state_.drag_offset_x = click_world_x - center_x;
                state_.drag_offset_y = click_world_y - center_y;
                std::cout << "Dragging zone line: " << zl.target_zone << std::endl;
            } else {
                // Start drawing new bounds for selected zone line
                state_.is_drawing = true;
                state_.draw_start_x = click_world_x;
                state_.draw_start_y = click_world_y;
                state_.draw_end_x = click_world_x;
                state_.draw_end_y = click_world_y;
            }
        }

        // Handle dragging
        if (state_.is_dragging && event_receiver_.isLeftButtonDown()) {
            float mouse_world_x, mouse_world_y;
            screenToWorld(event_receiver_.getMouseX(), event_receiver_.getMouseY(),
                          mouse_world_x, mouse_world_y);

            if (state_.dragging_index >= 0 && state_.dragging_index < (int)state_.zone_lines.size()) {
                auto& zl = state_.zone_lines[state_.dragging_index];
                float width = zl.max_x - zl.min_x;
                float height = zl.max_y - zl.min_y;
                float new_center_x = mouse_world_x - state_.drag_offset_x;
                float new_center_y = mouse_world_y - state_.drag_offset_y;
                zl.min_x = new_center_x - width / 2.0f;
                zl.max_x = new_center_x + width / 2.0f;
                zl.min_y = new_center_y - height / 2.0f;
                zl.max_y = new_center_y + height / 2.0f;
                zl.trigger_x = new_center_x;
                zl.trigger_y = new_center_y;
            }
        }

        // Handle drawing
        if (state_.is_drawing && event_receiver_.isLeftButtonDown()) {
            screenToWorld(event_receiver_.getMouseX(), event_receiver_.getMouseY(),
                          state_.draw_end_x, state_.draw_end_y);
        }

        // Handle mouse release
        if (event_receiver_.wasLeftReleased()) {
            if (state_.is_dragging) {
                state_.is_dragging = false;
                if (state_.dragging_index >= 0 && state_.dragging_index < (int)state_.zone_lines.size()) {
                    const auto& zl = state_.zone_lines[state_.dragging_index];
                    std::cout << "Moved " << zl.target_zone << " to: ["
                              << zl.min_x << ", " << zl.max_x << "] x ["
                              << zl.min_y << ", " << zl.max_y << "]" << std::endl;
                }
                state_.dragging_index = -1;
            } else if (state_.is_drawing) {
                state_.is_drawing = false;
                // Apply bounds to selected zone line based on draw mode
                if (state_.selected_index >= 0 && state_.selected_index < (int)state_.zone_lines.size()) {
                    auto& zl = state_.zone_lines[state_.selected_index];

                    float draw_min_x = std::min(state_.draw_start_x, state_.draw_end_x);
                    float draw_max_x = std::max(state_.draw_start_x, state_.draw_end_x);
                    float draw_min_y = std::min(state_.draw_start_y, state_.draw_end_y);
                    float draw_max_y = std::max(state_.draw_start_y, state_.draw_end_y);

                    switch (state_.draw_mode) {
                        case 0:  // XY mode - walk-through walls (default)
                            zl.min_x = draw_min_x;
                            zl.max_x = draw_max_x;
                            zl.min_y = draw_min_y;
                            zl.max_y = draw_max_y;
                            // Use placement_z with reasonable height bounds (-10 to +30)
                            zl.min_z = state_.placement_z - 10.0f;
                            zl.max_z = state_.placement_z + 30.0f;
                            break;
                        case 1:  // X-line mode - N-S wall (fixed X range, Y spans full)
                            zl.min_x = draw_min_x;
                            zl.max_x = draw_max_x;
                            zl.min_y = state_.zone_min_y;
                            zl.max_y = state_.zone_max_y;
                            // Use placement_z with reasonable height bounds (-10 to +30)
                            zl.min_z = state_.placement_z - 10.0f;
                            zl.max_z = state_.placement_z + 30.0f;
                            break;
                        case 2:  // Y-line mode - E-W wall (fixed Y range, X spans full)
                            zl.min_x = state_.zone_min_x;
                            zl.max_x = state_.zone_max_x;
                            zl.min_y = draw_min_y;
                            zl.max_y = draw_max_y;
                            // Use placement_z with reasonable height bounds (-10 to +30)
                            zl.min_z = state_.placement_z - 10.0f;
                            zl.max_z = state_.placement_z + 30.0f;
                            break;
                        case 3:  // Z-flat mode - shaft/cliff (horizontal area at fixed Z)
                            zl.min_x = draw_min_x;
                            zl.max_x = draw_max_x;
                            zl.min_y = draw_min_y;
                            zl.max_y = draw_max_y;
                            // Use placement_z for Z-flat mode too
                            zl.min_z = state_.placement_z - state_.fixed_z_thickness / 2.0f;
                            zl.max_z = state_.placement_z + state_.fixed_z_thickness / 2.0f;
                            break;
                    }

                    zl.has_bounds = true;
                    zl.trigger_x = (zl.min_x + zl.max_x) / 2.0f;
                    zl.trigger_y = (zl.min_y + zl.max_y) / 2.0f;
                    // Update trigger_z to match placement_z
                    zl.trigger_z = state_.placement_z;

                    const char* mode_names[] = {"XY", "X-line", "Y-line", "Z-flat"};
                    std::cout << "Set bounds for " << zl.target_zone << " [" << mode_names[state_.draw_mode] << "]: "
                              << "X[" << zl.min_x << ", " << zl.max_x << "] "
                              << "Y[" << zl.min_y << ", " << zl.max_y << "] "
                              << "Z[" << zl.min_z << ", " << zl.max_z << "]" << std::endl;
                }
            }
        }

        if (event_receiver_.wasRightClicked()) {
            state_.is_drawing = false;
            state_.is_dragging = false;
        }
    }

    void screenToWorld(int screen_x, int screen_y, float& world_x, float& world_y) {
        float screen_width = driver_->getScreenSize().Width;
        float screen_height = driver_->getScreenSize().Height;

        // Screen center is view position
        world_x = state_.view_x + (screen_x - screen_width / 2.0f) / state_.zoom;
        world_y = state_.view_y + (screen_y - screen_height / 2.0f) / state_.zoom;
    }

    void worldToScreen(float world_x, float world_y, int& screen_x, int& screen_y) {
        float screen_width = driver_->getScreenSize().Width;
        float screen_height = driver_->getScreenSize().Height;

        screen_x = (int)((world_x - state_.view_x) * state_.zoom + screen_width / 2.0f);
        screen_y = (int)((world_y - state_.view_y) * state_.zoom + screen_height / 2.0f);
    }

    void render() {
        driver_->beginScene(true, true, SColor(255, 40, 40, 60));

        if (state_.show_grid) {
            renderGrid();
        }

        if (state_.show_geometry && zone_geometry_) {
            renderZoneGeometry();
        }

        if (state_.show_portals) {
            renderPortalOverlay();
        }

        if (state_.show_points) {
            renderZoneLines();
        }

        if (state_.is_drawing) {
            renderDrawingRect();
        }

        renderUI();

        driver_->endScene();
    }

    void renderGrid() {
        // Calculate grid spacing based on zoom
        float base_spacing = 100.0f;
        float spacing = base_spacing;
        while (spacing * state_.zoom < 50) spacing *= 2;
        while (spacing * state_.zoom > 200) spacing /= 2;

        SColor grid_color(100, 80, 80, 80);
        SColor axis_color(200, 150, 150, 150);

        float screen_width = driver_->getScreenSize().Width;
        float screen_height = driver_->getScreenSize().Height;

        // Calculate visible world bounds
        float left, top, right, bottom;
        screenToWorld(0, 0, left, top);
        screenToWorld(screen_width, screen_height, right, bottom);

        // Draw vertical lines
        float start_x = std::floor(left / spacing) * spacing;
        for (float x = start_x; x <= right; x += spacing) {
            int sx1, sy1, sx2, sy2;
            worldToScreen(x, top, sx1, sy1);
            worldToScreen(x, bottom, sx2, sy2);
            SColor color = (std::abs(x) < 1.0f) ? axis_color : grid_color;
            driver_->draw2DLine(position2d<s32>(sx1, sy1), position2d<s32>(sx2, sy2), color);
        }

        // Draw horizontal lines
        float start_y = std::floor(top / spacing) * spacing;
        for (float y = start_y; y <= bottom; y += spacing) {
            int sx1, sy1, sx2, sy2;
            worldToScreen(left, y, sx1, sy1);
            worldToScreen(right, y, sx2, sy2);
            SColor color = (std::abs(y) < 1.0f) ? axis_color : grid_color;
            driver_->draw2DLine(position2d<s32>(sx1, sy1), position2d<s32>(sx2, sy2), color);
        }
    }

    void renderZoneGeometry() {
        SColor geometry_color(150, 100, 150, 100);

        // Draw triangles as lines (wireframe)
        for (const auto& tri : zone_geometry_->triangles) {
            if (tri.v1 >= zone_geometry_->vertices.size() ||
                tri.v2 >= zone_geometry_->vertices.size() ||
                tri.v3 >= zone_geometry_->vertices.size()) {
                continue;
            }

            const auto& v0 = zone_geometry_->vertices[tri.v1];
            const auto& v1 = zone_geometry_->vertices[tri.v2];
            const auto& v2 = zone_geometry_->vertices[tri.v3];

            // Z-depth filtering: hide geometry above placement_z
            if (state_.z_filter_enabled) {
                // Skip triangle if all vertices are above placement_z
                if (v0.z > state_.placement_z && v1.z > state_.placement_z && v2.z > state_.placement_z) {
                    continue;
                }
            }

            int sx0, sy0, sx1, sy1, sx2, sy2;
            worldToScreen(v0.x, v0.y, sx0, sy0);
            worldToScreen(v1.x, v1.y, sx1, sy1);
            worldToScreen(v2.x, v2.y, sx2, sy2);

            driver_->draw2DLine(position2d<s32>(sx0, sy0), position2d<s32>(sx1, sy1), geometry_color);
            driver_->draw2DLine(position2d<s32>(sx1, sy1), position2d<s32>(sx2, sy2), geometry_color);
            driver_->draw2DLine(position2d<s32>(sx2, sy2), position2d<s32>(sx0, sy0), geometry_color);
        }
    }

    void renderZoneLines() {
        IGUIFont* font = guienv_->getBuiltInFont();

        for (size_t i = 0; i < state_.zone_lines.size(); ++i) {
            const auto& zl = state_.zone_lines[i];
            bool selected = ((int)i == state_.selected_index);

            // Skip if no valid trigger coords (UNKNOWN type with null coords)
            if (zl.trigger_x == 0 && zl.trigger_y == 0 && zl.trigger_z == 0 && zl.type == "UNKNOWN") {
                continue;
            }

            int sx, sy;
            worldToScreen(zl.trigger_x, zl.trigger_y, sx, sy);

            // Draw trigger point
            SColor point_color = selected ? SColor(255, 255, 255, 0) : SColor(255, 255, 100, 100);
            int point_size = selected ? 8 : 5;
            driver_->draw2DRectangle(point_color,
                recti(sx - point_size, sy - point_size, sx + point_size, sy + point_size));

            // Draw bounds if present
            if (zl.has_bounds) {
                int bx1, by1, bx2, by2;
                worldToScreen(zl.min_x, zl.min_y, bx1, by1);
                worldToScreen(zl.max_x, zl.max_y, bx2, by2);

                // Yellow for selected, cyan for unselected
                SColor bounds_color = selected ? SColor(255, 255, 255, 0) : SColor(200, 0, 200, 255);
                driver_->draw2DRectangleOutline(recti(bx1, by1, bx2, by2), bounds_color);

                // Draw filled rectangle with transparency for better visibility
                SColor fill_color = selected ? SColor(50, 255, 255, 0) : SColor(30, 0, 200, 255);
                driver_->draw2DRectangle(fill_color, recti(std::min(bx1, bx2), std::min(by1, by2),
                                                           std::max(bx1, bx2), std::max(by1, by2)));
            }

            // Draw label
            if (font) {
                std::wstring label(zl.target_zone.begin(), zl.target_zone.end());
                font->draw(label.c_str(), recti(sx + 10, sy - 10, sx + 200, sy + 10),
                           selected ? SColor(255, 255, 255, 0) : SColor(255, 200, 200, 200));
            }
        }
    }

    void renderDrawingRect() {
        int sx1, sy1, sx2, sy2;
        worldToScreen(state_.draw_start_x, state_.draw_start_y, sx1, sy1);
        worldToScreen(state_.draw_end_x, state_.draw_end_y, sx2, sy2);

        SColor draw_color(200, 255, 255, 0);
        driver_->draw2DRectangleOutline(recti(std::min(sx1, sx2), std::min(sy1, sy2),
                                              std::max(sx1, sx2), std::max(sy1, sy2)), draw_color);
    }

    void renderPortalOverlay() {
        // Draw region AABBs as filled rectangles (top-down XY projection)
        for (const auto& reg : region_overlays_) {
            // Z-depth filtering
            if (state_.z_filter_enabled) {
                if (reg.minZ > state_.placement_z) continue;
            }

            int sx1, sy1, sx2, sy2;
            worldToScreen(reg.minX, reg.minY, sx1, sy1);
            worldToScreen(reg.maxX, reg.maxY, sx2, sy2);

            // Indoor = blue tint, outdoor = no fill
            if (reg.indoor) {
                SColor fill(30, 60, 60, 200);
                driver_->draw2DRectangle(fill, recti(std::min(sx1, sx2), std::min(sy1, sy2),
                                                      std::max(sx1, sx2), std::max(sy1, sy2)));
            }
        }

        // Draw portal boundaries and openings
        for (const auto& p : portal_overlays_) {
            // Z-depth filtering: skip portals above placement Z
            if (state_.z_filter_enabled && p.centerZ > state_.placement_z) continue;

            // Choose color by classification
            SColor color;
            if (p.classification == "open-air") {
                color = SColor(60, 255, 165, 0);   // Orange, very transparent
            } else if (p.classification == "wall") {
                color = SColor(120, 255, 0, 0);     // Red
            } else {
                color = SColor(180, 255, 255, 0);   // Yellow for doorway boundary
            }

            // Draw BSP boundary polygon edges (top-down: use X,Y, ignore Z)
            size_t nv = p.boundaryVerts.size() / 3;
            if (nv >= 3 && p.classification != "open-air") {
                for (size_t i = 0; i < nv; ++i) {
                    size_t j = (i + 1) % nv;
                    float x0 = p.boundaryVerts[i * 3 + 0], y0 = p.boundaryVerts[i * 3 + 1];
                    float x1 = p.boundaryVerts[j * 3 + 0], y1 = p.boundaryVerts[j * 3 + 1];
                    int sx0, sy0, sx1, sy1;
                    worldToScreen(x0, y0, sx0, sy0);
                    worldToScreen(x1, y1, sx1, sy1);
                    driver_->draw2DLine(position2d<s32>(sx0, sy0), position2d<s32>(sx1, sy1), color);
                }
            }

            // Draw opening polygons in green
            if (!p.openingVerts.empty()) {
                SColor openColor(220, 0, 255, 0);  // Bright green
                for (const auto& opening : p.openingVerts) {
                    size_t onv = opening.size() / 3;
                    if (onv < 3) continue;
                    for (size_t i = 0; i < onv; ++i) {
                        size_t j = (i + 1) % onv;
                        float x0 = opening[i * 3 + 0], y0 = opening[i * 3 + 1];
                        float x1 = opening[j * 3 + 0], y1 = opening[j * 3 + 1];
                        int sx0, sy0, sx1, sy1;
                        worldToScreen(x0, y0, sx0, sy0);
                        worldToScreen(x1, y1, sx1, sy1);
                        driver_->draw2DLine(position2d<s32>(sx0, sy0), position2d<s32>(sx1, sy1), openColor);
                    }
                }
            }
        }
    }

    void renderUI() {
        IGUIFont* font = guienv_->getBuiltInFont();
        if (!font) return;

        int y = 10;
        SColor text_color(255, 255, 255, 255);
        SColor help_color(255, 180, 180, 180);

        // Zone info
        std::wstring zone_info = L"Zone: " + std::wstring(state_.zone_name.begin(), state_.zone_name.end());
        font->draw(zone_info.c_str(), recti(10, y, 400, y + 20), text_color);
        y += 20;

        // Selected zone line
        if (state_.selected_index >= 0 && state_.selected_index < (int)state_.zone_lines.size()) {
            const auto& zl = state_.zone_lines[state_.selected_index];
            std::wstring sel_info = L"Selected: " + std::wstring(zl.target_zone.begin(), zl.target_zone.end());
            sel_info += L" (" + std::wstring(zl.type.begin(), zl.type.end()) + L")";
            font->draw(sel_info.c_str(), recti(10, y, 400, y + 20), SColor(255, 255, 255, 0));
            y += 20;

            if (zl.has_bounds) {
                wchar_t bounds_str[100];
                swprintf(bounds_str, 100, L"Bounds: [%.0f, %.0f] x [%.0f, %.0f]",
                         zl.min_x, zl.max_x, zl.min_y, zl.max_y);
                font->draw(bounds_str, recti(10, y, 400, y + 20), SColor(255, 200, 255, 200));
            } else {
                font->draw(L"Bounds: (not set - draw with mouse)", recti(10, y, 400, y + 20), SColor(255, 255, 200, 200));
            }
            y += 20;
        } else {
            font->draw(L"Press TAB to select a zone line", recti(10, y, 400, y + 20), help_color);
            y += 20;
        }

        y += 10;

        // View info
        wchar_t view_str[150];
        swprintf(view_str, 150, L"View: (%.0f, %.0f) Zoom: %.2fx  Placement Z: %.0f",
                 state_.view_x, state_.view_y, state_.zoom, state_.placement_z);
        font->draw(view_str, recti(10, y, 500, y + 20), help_color);
        y += 20;

        // Mouse world coords
        float mx, my;
        screenToWorld(event_receiver_.getMouseX(), event_receiver_.getMouseY(), mx, my);
        swprintf(view_str, 100, L"Mouse: (%.0f, %.0f)", mx, my);
        font->draw(view_str, recti(10, y, 400, y + 20), help_color);
        y += 30;

        // Draw mode
        const wchar_t* mode_names[] = {L"XY (walk-through)", L"X-line (N-S wall)", L"Y-line (E-W wall)", L"Z-flat (shaft/cliff)"};
        std::wstring mode_str = L"Draw Mode: ";
        mode_str += mode_names[state_.draw_mode];
        if (state_.draw_mode == 3) {
            wchar_t z_info[50];
            swprintf(z_info, 50, L" [Z=%.0f, thickness=%.0f]", state_.fixed_z_level, state_.fixed_z_thickness);
            mode_str += z_info;
        }
        font->draw(mode_str.c_str(), recti(10, y, 500, y + 20), SColor(255, 100, 255, 255));
        y += 20;

        // Z filter status
        if (state_.z_filter_enabled) {
            wchar_t z_filter_str[100];
            swprintf(z_filter_str, 100, L"Z Filter: ON (hiding above Z=%.0f)", state_.placement_z);
            font->draw(z_filter_str, recti(10, y, 500, y + 20), SColor(255, 255, 200, 100));
        } else {
            font->draw(L"Z Filter: OFF (F to enable)", recti(10, y, 400, y + 20), SColor(255, 120, 120, 120));
        }
        y += 25;

        // Help
        font->draw(L"Controls:", recti(10, y, 400, y + 20), text_color);
        y += 15;
        font->draw(L"  WASD/Arrows - Pan    Wheel/+/- - Zoom", recti(10, y, 500, y + 20), help_color);
        y += 15;
        font->draw(L"  Tab - Cycle zone lines    Ctrl+S - Save", recti(10, y, 500, y + 20), help_color);
        y += 15;
        font->draw(L"  G - Grid    Z - Geometry    P - Points    R - Portals", recti(10, y, 600, y + 20), help_color);
        y += 15;
        font->draw(L"  M - Cycle draw mode    [/] - Z thickness", recti(10, y, 500, y + 20), help_color);
        y += 15;
        font->draw(L"  F - Toggle Z filter    PgUp/Dn - Adjust Z level", recti(10, y, 500, y + 20), help_color);
        y += 15;
        font->draw(L"  Shift+[/] - Z filter range    Shift+Del - Delete zone line", recti(10, y, 500, y + 20), help_color);
        y += 15;
        font->draw(L"  Left drag - Draw bounds    Del - Clear bounds", recti(10, y, 500, y + 20), help_color);
    }

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

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <zone_name> [--eq-path /path/to/EQ] [--maps-path /path/to/maps]" << std::endl;
    std::cout << std::endl;
    std::cout << "Example: " << program << " qeynos2 --eq-path /home/user/EverQuest --maps-path /path/to/maps/base" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string zone_name = argv[1];
    std::string eq_path = "/home/user/projects/claude/EverQuestP1999";  // Default path
    std::string maps_path;
    float debug_x1 = 0, debug_y1 = 0, debug_x2 = 0, debug_y2 = 0;
    bool debug_area = false;

    // Parse arguments
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--eq-path" && i + 1 < argc) {
            eq_path = argv[++i];
        } else if (arg == "--maps-path" && i + 1 < argc) {
            maps_path = argv[++i];
        } else if (arg == "--debug-area" && i + 4 < argc) {
            debug_x1 = std::stof(argv[++i]);
            debug_y1 = std::stof(argv[++i]);
            debug_x2 = std::stof(argv[++i]);
            debug_y2 = std::stof(argv[++i]);
            debug_area = true;
        }
    }

    std::cout << "Zone Line Editor" << std::endl;
    std::cout << "Zone: " << zone_name << std::endl;
    std::cout << "EQ Path: " << eq_path << std::endl;
    if (!maps_path.empty())
        std::cout << "Maps Path: " << maps_path << std::endl;
    std::cout << std::endl;

    ZoneLineEditor editor(zone_name, eq_path, maps_path);
    if (debug_area) {
        editor.setDebugArea(debug_x1, debug_y1, debug_x2, debug_y2);
    }
    if (!editor.init()) {
        return 1;
    }

    editor.run();
    return 0;
}
