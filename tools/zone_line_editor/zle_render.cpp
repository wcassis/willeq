// Zone Line Editor -- the 2D overlay draw passes and the HUD.
//
// T451: split out of the 1,303-line tools/zone_line_editor.cpp.

#include "zone_line_editor/zone_line_editor.h"

void ZoneLineEditor::render() {
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

void ZoneLineEditor::renderGrid() {
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

void ZoneLineEditor::renderZoneGeometry() {
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

void ZoneLineEditor::renderZoneLines() {
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

void ZoneLineEditor::renderDrawingRect() {
    int sx1, sy1, sx2, sy2;
    worldToScreen(state_.draw_start_x, state_.draw_start_y, sx1, sy1);
    worldToScreen(state_.draw_end_x, state_.draw_end_y, sx2, sy2);

    SColor draw_color(200, 255, 255, 0);
    driver_->draw2DRectangleOutline(recti(std::min(sx1, sx2), std::min(sy1, sy2),
                                          std::max(sx1, sx2), std::max(sy1, sy2)), draw_color);
}

void ZoneLineEditor::renderPortalOverlay() {
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

void ZoneLineEditor::renderUI() {
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

