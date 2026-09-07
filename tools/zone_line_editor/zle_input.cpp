// Zone Line Editor -- keyboard/mouse edit operations and view transforms.
//
// T451: split out of the 1,303-line tools/zone_line_editor.cpp.

#include "zone_line_editor/zone_line_editor.h"

void ZoneLineEditor::handleInput() {
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

void ZoneLineEditor::screenToWorld(int screen_x, int screen_y, float& world_x, float& world_y) {
    float screen_width = driver_->getScreenSize().Width;
    float screen_height = driver_->getScreenSize().Height;

    // Screen center is view position
    world_x = state_.view_x + (screen_x - screen_width / 2.0f) / state_.zoom;
    world_y = state_.view_y + (screen_y - screen_height / 2.0f) / state_.zoom;
}

void ZoneLineEditor::worldToScreen(float world_x, float world_y, int& screen_x, int& screen_y) {
    float screen_width = driver_->getScreenSize().Width;
    float screen_height = driver_->getScreenSize().Height;

    screen_x = (int)((world_x - state_.view_x) * state_.zoom + screen_width / 2.0f);
    screen_y = (int)((world_y - state_.view_y) * state_.zoom + screen_height / 2.0f);
}

