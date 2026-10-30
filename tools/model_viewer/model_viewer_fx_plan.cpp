// tools/model_viewer/model_viewer_fx_plan.cpp -- B1193 split (pure move from
// model_viewer_fx_harness.cpp, which had grown past the 1,500-line ceiling
// once the frame-output timers landed): the --plan parser and the --camera
// preset defaults. Behavior unchanged; the declarations are the header's.

#include "model_viewer_fx_harness.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { out.push_back(cur); cur.clear(); } else if (c != '\r') { cur += c; }
    }
    out.push_back(cur);
    return out;
}

}  // namespace

bool parseHarnessPlan(const std::string& path, std::vector<HarnessCast>& out, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open plan " + path; return false; }
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        if (line.empty() || line[0] == '#') continue;
        const auto c = splitTabs(line);
        if (c.size() < 4 || c.size() > 7) {
            err = path + ":" + std::to_string(n) +
                  ": want t_begin_s, spell_id, self|other, distance_units[, cast_ms[, caster_level[, begincast|action]]]";
            return false;
        }
        if (c[0] == "t_begin_s") continue;  // header row
        HarnessCast hc;
        try {
            hc.tBegin = std::stod(c[0]);
            hc.spellId = static_cast<uint32_t>(std::stoul(c[1]));
            if (c[2] != "self" && c[2] != "other") throw std::invalid_argument("target");
            hc.self = c[2] == "self";
            hc.distance = std::stof(c[3]);
            if (c.size() >= 5 && !c[4].empty() && c[4] != "-") hc.castMsOverride = std::stoi(c[4]);
            if (c.size() >= 6 && !c[5].empty() && c[5] != "-") hc.casterLevel = std::stoi(c[5]);
            if (c.size() >= 7) {
                if (c[6] != "begincast" && c[6] != "action") throw std::invalid_argument("packets");
                hc.beginCast = c[6] == "begincast";
            }
        } catch (const std::exception&) {
            err = path + ":" + std::to_string(n) + ": unparseable row '" + line + "'";
            return false;
        }
        out.push_back(hc);
    }
    if (out.empty()) { err = path + ": no casts"; return false; }
    return true;
}

void applyHarnessPresetDefaults() {
    if (harnessOpts.cameraPreset != "b1118" && harnessOpts.cameraPreset != "b1130" &&
        harnessOpts.cameraPreset != "b1130-self")
        return;
    const bool b1130 = harnessOpts.cameraPreset == "b1130" || harnessOpts.cameraPreset == "b1130-self";
    harnessOpts.width = 1280;
    harnessOpts.height = 800;
    // All three presets share the b1118 char-67 caster: char 67 Testcaster
    // (lane 5): race 6 dark elf, gender 1 (fable.log
    // "Updating appearance for entity 258 (Testcaster) to race 6 gender 1").
    // B1172: size 0 -- her S->C OP_ZoneEntry Spawn_Struct carries size 0.000
    // in the oracle captures (r51, B1118 oracle_druid) and in fable's lane
    // legs (B1118 fable_A, B1142 fable_N93), and no AT_Size names her; the
    // client then draws its race/gender default 5.0 x 0.2 = 1.0
    // (actor_scale.h). The 6.0 here before was fable's own zone_entry
    // default, not a wire value.
    if (!harnessOpts.casterGiven) {
        harnessOpts.casterRace = 6;
        harnessOpts.casterGender = 1;
        harnessOpts.casterSize = 0.0f;
        // B1139: her Spawn_Struct appearance in that leg's OP_ZoneEntry (S->C):
        // face 3, TextureProfile {0, 10, 1, 23, 1, 1, 1, 0, 72} -- the red robe
        // (chest 10) and IT72, the orb her left hand holds. equip_chest2 reads
        // 255 there; the client passes it through as appearance.texture.
        harnessOpts.casterAppearanceSet = true;
        harnessOpts.casterFace = 3;
        harnessOpts.casterTexture = 255;
        const uint32_t equip[9] = {0, 10, 1, 23, 1, 1, 1, 0, 72};
        std::copy(equip, equip + 9, harnessOpts.casterEquipment);
    }
    if (!harnessOpts.targetGiven) {
        if (b1130) {
            // b1130 / b1130-self: B1130's target is `#spawn TrollF 1 1 0 30000`.
            // eqemu's #spawn arg order is [name][race][level][texture][health]
            // [gender]... (command.cpp :229; npc.cpp SpawnNPC), so that command
            // is race=1 (HUMAN), level=1, texture=0, hp=30000 with the GENDER
            // ARG ABSENT -- filled by Mob::GetDefaultGender(1) = MALE (the
            // PC-race default, mob.cpp :4033). The "TrollF" name is only a
            // label: the oracle spawned the SAME level-1 human male as b1118's
            // target (the settle still r51/21_stun_settle shows a clothed,
            // standing human male), NOT a race-9 troll -- r51's own OP_NewSpawn
            // reads race 1, gender 0, size 6.000 (refactor/B1166/README.md
            // sec. 1). b1130 renders that human male and lets the solver find
            // its depth from the two feet pixels (288 px in the oracle; 238 px in
            // FABLE at 1.0 (B1166), 285 px at the client's actor scale 1.2, B1172).
            harnessOpts.targetRace = 1;
            harnessOpts.targetGender = 0;
            harnessOpts.targetSize = 6.0f;
        } else {
            // b1118: `#spawn TrollF`: eqemu's default #spawn is a level-1
            // human male (the leg's chat: "Race | Human (1)", "Gender | Male
            // (0)").
            harnessOpts.targetRace = 1;
            harnessOpts.targetGender = 0;
            harnessOpts.targetSize = 6.0f;
        }
    }
    // B1142 (#462): no UI occluder by default. B1139 drew the fable Tip of the
    // Day window (920,243..1280,543) here because every lane-5 leg up to B1127
    // left it open over the particles; the lane rig now starts the client
    // with [Tips] ShowTOTD=0 (review/B1142/scripts/fable_leg_remote.sh), so
    // the FX field is unoccluded on lane 5 as on the oracle (whose rig closes
    // its window with a click before the first cast). `--ui-occluder
    // 920,243,1280,543` reproduces a pre-B1142 capture.
    // B1139: the lane client simulates at its 60 fps and the grab keeps every
    // 2nd frame (B1138 measured +2.6 % landing coverage for it), and in leg B
    // Testcaster stands 228 px head top to feet (rows 377..605 of that leg's
    // tail frame, review/B1138/backdrop/), not B1118's 235.
    if (harnessOpts.simHz <= 0) harnessOpts.simHz = 60;
    if (!harnessOpts.casterPxGiven)
        harnessOpts.casterPx =
            (harnessOpts.cameraPreset == "b1130" || harnessOpts.cameraPreset == "b1130-self") ? 209.0f : 228.0f;
}
