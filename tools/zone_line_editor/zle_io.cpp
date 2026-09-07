// Zone Line Editor -- zone geometry, portal data and zone-line JSON I/O.
//
// T451: split out of the 1,303-line tools/zone_line_editor.cpp.

#include "zone_line_editor/zone_line_editor.h"

bool ZoneLineEditor::loadZoneGeometry() {
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

void ZoneLineEditor::loadPortalData() {
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

void ZoneLineEditor::loadZoneLineData() {
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

void ZoneLineEditor::saveZoneLineData() {
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

