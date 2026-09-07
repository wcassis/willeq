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

#include "zone_line_editor/zone_line_editor.h"

ZoneLineEditor::ZoneLineEditor(const std::string& zone_name, const std::string& eq_path,
                               const std::string& maps_path)
{
    state_.zone_name = zone_name;
    state_.eq_path = eq_path;
    state_.maps_path = maps_path;
}

void ZoneLineEditor::setDebugArea(float x1, float y1, float x2, float y2) {
    state_.debug_area_enabled = true;
    state_.debug_x1 = x1;
    state_.debug_y1 = y1;
    state_.debug_x2 = x2;
    state_.debug_y2 = y2;
}

bool ZoneLineEditor::init() {
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

void ZoneLineEditor::run() {
    while (device_->run()) {
        handleInput();
        render();
    }
}

ZoneLineEditor::~ZoneLineEditor() {
    if (device_) {
        device_->drop();
    }
}

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
