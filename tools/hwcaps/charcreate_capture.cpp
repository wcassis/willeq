// B888-CC4 (GitLab #204): the character select + creation visual gate's WILLEQ
// side. One headless frame per gate state, rendered through the PRODUCTION
// render path (renderCharSelectScreen / renderCharCreateScreen) against a
// real UIAtlas -- so the glyphs are the client's own, not a test stand-in.
//
// The gate's seam is the device-free state machines (char_select_screen.h,
// char_create_screen.h) plus those two render functions: this tool builds the
// CharSelectScreenState / CharCreateScreenState the way the live host does
// (openCharCreateWith, selectRace/Class/Gender, statUp, pressNext/Back,
// applyRandomName, applySubmitRefused, openFacePick) and reads each back as a
// PNG. It needs no server, no network and no EQ client data beyond the
// shipped string/era tables, so a CI job can re-run it on any tip and the
// gate (scripts/ci/charcreate_gate.py) scores the result against the blessed
// official-client reference set.
//
// Resolution is 1280x800 (the default) because that is exactly the coordinate
// space the oracle reference set is captured in: the lane-5 rig's screen crop
// is `import -crop 1280x800+0+30`, and the layout headers (char_select_layout.h
// and char_create_layout.h) evaluate the controls in that same space at that
// size -- e.g. CharCreateLayout::nextButtonRect(1280,800) is x 1211..1275, y
// 8..40, and the B1058 next_button template was cut at 1226,15 inside it.
// Rendering anywhere else would make the gate's per-rect IoU compare two
// different frames, so a non-1280x800 default is a defect, not a choice.
//
// Usage: charcreate_capture <output_dir> [--eq-path <dir>] [--w N] [--h M]
//   Writes <output_dir>/<state>.png for every state in kStates (one line per
//   state on stdout: "<state>\t<path>"). --eq-path points at a directory
//   holding a willeq.eqe1 archive (the UIAtlas); without it the tool falls
//   back to the solid-rectangle + built-in-font path and prints a note.
//   Exit 0 on success, 1 on a render failure.
//
// The states are the gate's canonical list (CC-4 / #204). They are the single
// source of truth: scripts/ci/charcreate_gate.py scores exactly these, and the
// README's table of states must name exactly these (the hygiene test checks
// the last). A state's name is its file stem -- the oracle's reference still
// for the same state uses the same name, so a missing reference is reported
// by name, not by index.

#include "client/graphics/ui/char_select_screen.h"
#include "client/graphics/ui/char_select_panel.h"
#include "client/graphics/ui/char_select_layout.h"
#include "client/graphics/ui/char_create_screen.h"
#include "client/graphics/ui/char_create_panel.h"
#include "client/graphics/ui/face_pick_panel.h"
#include "client/login_world/char_create_data.h"
#include "client/login_world/char_create_rules.h"
#include "client/login_world/character_select.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/gles2_shader_cache.h"
#include "client/graphics/text_batch.h"
#include "client/string_database.h"
#include "client/willeq_database.h"

#include <irrlicht.h>

#include <cstdio>
#include <filesystem>
#include <functional>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace EQT::Graphics;
using namespace eqt::charcreate;
namespace cc = eqt::charcreate;

namespace {

// The gate's canonical state list -- the single source of truth (see the
// file header). Order is capture order; name is the file stem.
struct GateState {
    const char* name;
};
const GateState kStates[] = {
    {"select_empty"},          // character select, no characters
    {"select_one"},            // character select, one character (selected)
    {"select_delete_prompt"},  // character select, delete confirm modal up
    {"create_initial"},        // creation page 0, fresh (no pair selected yet)
    {"create_after_gender"},   // page 0, a gender selected
    {"create_after_race"},     // page 0, a race selected
    {"create_after_class"},    // page 0, a class selected
    {"create_stats_part"},     // page 0, some points spent
    {"create_stats_all"},      // page 0, every point spent
    {"page1_initial"},         // page 1, deity/city not chosen
    {"page1_deity_city"},      // page 1, a deity and a city chosen
    {"page1_getname"},         // page 1, Get Name pressed (a name in the field)
    {"page1_name_rejected"},   // page 1, the rejected-name dialog up
    {"facepick_open"},         // the FacePick modal over creation page 0
};
constexpr size_t kStateCount = sizeof(kStates) / sizeof(kStates[0]);

const char* kWilleqEqe1Default = "/mnt/projects/EverQuest_ETC1/willeq.eqe1";

struct CaptureCtx {
    irr::video::IVideoDriver* driver = nullptr;
    UIRenderer* ui = nullptr;
    TextBatch* text = nullptr;
    UILayout layout;
    const char* outDir = nullptr;
    bool primeDone = false;
};

bool hasFile(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

// Render one frame of `draw` and, after the second of two content frames,
// read the framebuffer and write it. The two-frame idiom (prime + repeated
// content) and the single font draw per frame are load-bearing on the
// surfaceless-EGL/llvmpipe backend -- the first content frame after device
// creation loses its immediate-mode 2D draws, and a 2D-only frame never
// reaches the framebuffer at all (test_ui_atlas_pixels.cpp documents both).
// The live client always draws UI text, so keeping a text draw in the frame
// reproduces its frame shape.
bool capture(CaptureCtx& c, const std::function<void(UIRenderer&)>& draw, const std::string& name) {
    if (!c.primeDone) {
        c.driver->beginScene(true, true, irr::video::SColor(255, 20, 20, 30));
        c.driver->endScene();
        c.primeDone = true;
    }
    irr::video::IImage* img = nullptr;
    for (int frame = 0; frame < 2; ++frame) {
        c.driver->beginScene(true, true, irr::video::SColor(255, 20, 20, 30));
        c.ui->beginFrame();
        draw(*c.ui);
        c.text->addText(".", 2, 2, irr::video::SColor(255, 255, 255, 255));
        c.ui->endFrame();
        c.driver->endScene();
        if (frame == 1) img = c.driver->createScreenShot();
    }
    if (!img) {
        std::fprintf(stderr, "ERROR: createScreenShot() returned null for %s\n", name.c_str());
        return false;
    }
    const std::string path = std::string(c.outDir) + "/" + name + ".png";
    bool ok = c.driver->writeImageToFile(img, path.c_str());
    img->drop();
    if (!ok) {
        std::fprintf(stderr, "ERROR: writeImageToFile failed for %s\n", path.c_str());
        return false;
    }
    std::printf("%s\t%s\n", name.c_str(), path.c_str());
    return ok;
}

// eqstr id -> text, from the shipped string tables (the client's own labels).
// An empty result makes the render draw "eqstr <id>", so a miss is loud, not
// silent: the gate's OCR would read the placeholder and fail the state.
EqstrResolver makeEqstr(EQT::StringDatabase& strings) {
    return [&strings](uint32_t id) { return strings.getString(id); };
}

// --- the creation states ----------------------------------------------------

CharCreateScreenState makeCreate(const cc::CharCreateData& data) {
    CharCreateScreenState s;
    // Human/Warrior: the shipped oracle plan's pair (charcreate_untouched.tsv
    // creates Human,Warrior,Male), so it is a valid classic pair.
    // openCharCreateWith leaves every point unspent and pre-selects the first
    // offered deity/city.
    if (!openCharCreateWith(s, data, Race::Human, Gender::Male, cc::Class::Warrior)) {
        std::fprintf(stderr, "ERROR: openCharCreateWith(Human,Warrior) refused\n");
        std::abort();
    }
    return s;
}

// --- the character select states -------------------------------------------
// (The select screen's string table is built in main from
// CharacterSelectEqstrIds(data); the rows below are plain fixtures.)

}  // namespace

int main(int argc, char** argv) {
    std::string outDir;
    std::string eqPath = "/mnt/projects/EverQuestP1999";
    int w = 1280, h = 800;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--eq-path" && i + 1 < argc) eqPath = argv[++i];
        else if (a == "--w" && i + 1 < argc) w = std::atoi(argv[++i]);
        else if (a == "--h" && i + 1 < argc) h = std::atoi(argv[++i]);
        else if (a.rfind("--", 0) != 0) outDir = a;
    }
    if (outDir.empty()) {
        std::fprintf(stderr, "usage: charcreate_capture <output_dir> [--eq-path <dir>] [--w N] [--h M]\n");
        return 1;
    }

    // The capture directory is created here (the driver does not pre-make it);
    // writeImageToFile does not mkdir. The same idiom --dump-frames-dir uses.
    {
        std::error_code ec;
        std::filesystem::create_directories(outDir, ec);
        if (ec) {
            std::fprintf(stderr, "ERROR: cannot create %s: %s\n", outDir.c_str(), ec.message().c_str());
            return 1;
        }
    }

    // Shipped data: era tables + string tables. All of it is in the repo, so a
    // CI job runs this without any external asset.
    cc::CharCreateData data;
    if (!data.loadShipped()) {
        std::fprintf(stderr, "ERROR: char_create.tsv: %s\n", data.error().c_str());
        return 1;
    }
    EQT::WilleqDatabase db;
    if (!db.open("data/willeq.db")) {
        std::fprintf(stderr, "ERROR: failed to open data/willeq.db\n");
        return 1;
    }
    EQT::WilleqDatabase::setGlobalInstance(&db);
    EQT::StringDatabase strings;
    if (!strings.initFromDatabase()) {
        std::fprintf(stderr, "ERROR: StringDatabase::initFromDatabase failed\n");
        return 1;
    }
    const EqstrResolver eqstr = makeEqstr(strings);

    // The select screen's string table: every id the select AND creation
    // screens draw, resolved to the client's own text.
    auto stringsTable = std::make_shared<eqt::login_world::EqstrTable>();
    for (uint32_t id : eqt::login_world::CharacterSelectEqstrIds(data))
        (*stringsTable)[id] = strings.getString(id);

    // Device: the same headless GLES2 backend the visual gate and
    // ui_inventory_capture use. Surfaceless EGL (llvmpipe) on a headless box.
    irr::SIrrlichtCreationParameters params;
#ifdef EQT_HAS_GLES2
    params.DriverType = irr::video::EDT_OGLES2;
#else
    params.DriverType = irr::video::EDT_OPENGL;
#endif
    params.WindowSize = irr::core::dimension2d<irr::u32>(w, h);
    params.Fullscreen = true;
    eqt::ConfigureGles2ShaderCache();
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) {
        std::fprintf(stderr, "ERROR: failed to create Irrlicht device at %dx%d\n", w, h);
        return 1;
    }
    irr::video::IVideoDriver* driver = device->getVideoDriver();
    TextBatch text;
    text.init(device->getGUIEnvironment());

    // The UIAtlas (real glyphs). The pack lives outside the checkout; try the
    // --eq-path one (for a rig with a staged copy) then the canonical path the
    // pixel tests use. If neither is present the fallback path (solid rects +
    // the built-in font) still renders; note it so a CI failure is not
    // misread as a layout defect.
    std::unique_ptr<UIAtlas> atlas;
    std::unique_ptr<ConstrainedTextureCache> cache;
    EQE1Loader loader;
    std::string eqe1 = eqPath + "/willeq.eqe1";
    const std::vector<std::string> packCandidates = {eqe1, kWilleqEqe1Default};
    const char* pack = nullptr;
    for (const auto& cand : packCandidates) {
        if (hasFile(cand)) { pack = cand.c_str(); break; }
    }
    if (pack) {
        if (loader.open(pack)) {
            auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
            cache = std::make_unique<ConstrainedTextureCache>(config);
            cache->setDriver(driver);
            cache->addEQE1Loader(&loader);
            atlas = std::make_unique<UIAtlas>();
            if (!atlas->load(driver, cache.get())) {
                std::fprintf(stderr, "NOTE: UIAtlas::load failed against %s; using the fallback path\n", pack);
                atlas.reset();
            }
        } else {
            std::fprintf(stderr, "NOTE: could not open %s; using the fallback path\n", pack);
        }
    } else {
        std::fprintf(stderr, "NOTE: no willeq.eqe1 found (tried %s and %s); using the fallback path\n",
                     eqe1.c_str(), kWilleqEqe1Default);
    }

    UIRenderer ui;
    ui.init(driver, atlas.get(), &text);
    UILayout layout;
    layout.computeLayout(w, h);

    CaptureCtx ctx;
    ctx.driver = driver;
    ctx.ui = &ui;
    ctx.text = &text;
    ctx.layout = layout;
    ctx.outDir = outDir.c_str();

    const int sw = layout.screenW, sh = layout.screenH;
    bool ok = true;

    // --- character select states ---
    {
        eqt::login_world::CharacterSelectRow row;
        row.slot = 0;
        row.name = "Testcaster";
        row.level = 1;
        row.cls = cc::Class::Cleric;
        row.race = Race::Human;
        row.zone = 37;
        row.infoLine = "Testcaster [1 Cleric]";
        row.zoneLine = "Qeynos";

        // select_empty: an empty account (no rows).
        {
            CharSelectScreenState s;
            s.strings = stringsTable;
            ok &= capture(ctx, [&](UIRenderer& u) { renderCharSelectScreen(u, layout, s); }, "select_empty");
        }
        // select_one: one character, the first row selected (rule 4).
        {
            CharSelectScreenState s;
            s.strings = stringsTable;
            s.rows.push_back(row);
            s.selected = 0;
            ok &= capture(ctx, [&](UIRenderer& u) { renderCharSelectScreen(u, layout, s); }, "select_one");
        }
        // select_delete_prompt: the confirm modal up (Delete acts on the
        // selected row; eqstr 9065 with the name substituted).
        {
            CharSelectScreenState s;
            s.strings = stringsTable;
            s.rows.push_back(row);
            s.selected = 0;
            pressDelete(s);
            ok &= capture(ctx, [&](UIRenderer& u) { renderCharSelectScreen(u, layout, s); }, "select_delete_prompt");
        }
    }

    // --- creation states ---
    // Each is built from a FRESH open so one state's mutation never leaks into
    // the next -- the oracle stills are each a separate capture, not one
    // scrolling session, and the gate scores them independently.
    ok &= capture(ctx, [&](UIRenderer& u) {
        auto s = makeCreate(data);
        renderCharCreateScreen(u, sw, sh, s, eqstr);
    }, "create_initial");

    ok &= capture(ctx, [&](UIRenderer& u) {
        auto s = makeCreate(data);
        selectGender(s, Gender::Female);
        renderCharCreateScreen(u, sw, sh, s, eqstr);
    }, "create_after_gender");

    ok &= capture(ctx, [&](UIRenderer& u) {
        auto s = makeCreate(data);
        selectRace(s, Race::Barbarian);
        renderCharCreateScreen(u, sw, sh, s, eqstr);
    }, "create_after_race");

    ok &= capture(ctx, [&](UIRenderer& u) {
        auto s = makeCreate(data);
        selectClass(s, cc::Class::Warrior);
        renderCharCreateScreen(u, sw, sh, s, eqstr);
    }, "create_after_class");

    ok &= capture(ctx, [&](UIRenderer& u) {
        auto s = makeCreate(data);
        for (int i = 0; i < 4; ++i) statUp(s, STR);   // 4 points, 6 left
        renderCharCreateScreen(u, sw, sh, s, eqstr);
    }, "create_stats_part");

    ok &= capture(ctx, [&](UIRenderer& u) {
        auto s = makeCreate(data);
        for (int i = 0; i < 4; ++i) statUp(s, STR);
        statMax(s, STA);                               // every remaining point
        renderCharCreateScreen(u, sw, sh, s, eqstr);
    }, "create_stats_all");

    ok &= capture(ctx, [&](UIRenderer& u) {
        auto s = makeCreate(data);
        statMax(s, STR);                               // all spent, then page 1
        pressNext(s);
        renderCharCreateScreen(u, sw, sh, s, eqstr);
    }, "page1_initial");

    ok &= capture(ctx, [&](UIRenderer& u) {
        auto s = makeCreate(data);
        statMax(s, STR);
        pressNext(s);
        const auto deities = s.offeredDeities();
        if (!deities.empty()) {
            selectDeity(s, deities.size() > 1 ? deities[1] : deities.front());
        }
        const auto cities = s.offeredCities();
        if (!cities.empty()) selectCity(s, cities.front());
        renderCharCreateScreen(u, sw, sh, s, eqstr);
    }, "page1_deity_city");

    ok &= capture(ctx, [&](UIRenderer& u) {
        auto s = makeCreate(data);
        statMax(s, STR);
        pressNext(s);
        pressGetName(s);
        applyRandomName(s, "Orangecaster");
        renderCharCreateScreen(u, sw, sh, s, eqstr);
    }, "page1_getname");

    ok &= capture(ctx, [&](UIRenderer& u) {
        auto s = makeCreate(data);
        statMax(s, STR);
        pressNext(s);
        s.nameEntry.inputText = "Orangecaster";
        s.nameEntry.cursorPos = s.nameEntry.inputText.size();
        applySubmitRefused(s, strings.getString(1581));   // "Name has been rejected. ..."
        renderCharCreateScreen(u, sw, sh, s, eqstr);
    }, "page1_name_rejected");

    ok &= capture(ctx, [&](UIRenderer& u) {
        auto s = makeCreate(data);
        selectRace(s, Race::Barbarian);
        if (!openFacePick(s)) {
            std::fprintf(stderr, "NOTE: openFacePick refused (no face rows for the pair)\n");
        }
        renderCharCreateScreen(u, sw, sh, s, eqstr);
    }, "facepick_open");

    device->drop();
    return ok ? 0 : 1;
}
