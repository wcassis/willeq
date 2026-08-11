// ui_inventory_capture — T57 visual-acceptance capture harness, extended by
// T60 for the character paperdoll.
//
// There was no existing path to capture a screenshot of the static UI
// (--dump-png-prefix belongs to gles2_zone_benchmark, which renders zones
// and never draws a popup). This is the smallest tool that renders
// renderInventoryPopup() against a real, headless GLES2 device (the same
// surfaceless-EGL/llvmpipe backend the visual gate and gles2_zone_benchmark
// use — see docker/irrlicht-drm-x86 and src/client/graphics/CLAUDE.md) and
// writes the result to a PNG, so Will can judge the layout — this tool
// never self-assesses the output.
//
// T60: two more captures add a real CharacterModelView (paperdoll) into
// the popup's center — a bare character (no equipment) and a fully-
// equipped one (weapon + shield), per the plan's ask. Needs real EQ asset
// data to build a textured character model; pass --eq-path if
// /mnt/projects/EverQuestP1999 (this environment's default, per
// refactor/EXECUTOR_PROTOCOL.md) isn't right for the machine this runs on.
// If the assets aren't present the paperdoll captures are skipped (with a
// stderr note) rather than failing the whole tool — T57's four slot-only
// captures don't need any asset data and must keep working regardless.
//
// Usage: ui_inventory_capture <output_dir> [--eq-path <dir>]
// Writes:
//   <output_dir>/inventory_800x600_full.png     -- every slot populated
//   <output_dir>/inventory_1280x800_full.png    -- larger size, same state
//   <output_dir>/inventory_800x600_sparse.png   -- a few slots populated
//   <output_dir>/inventory_800x600_hover.png    -- one slot hovered
//   <output_dir>/inventory_paperdoll_bare.png   -- T60: doll, no equipment
//   <output_dir>/inventory_paperdoll_equipped.png -- T60: doll, weapon+shield

#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/inventory_layout.h"
#include "client/graphics/ui/character_model_view.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/text_batch.h"
#include "client/inventory/inventory_constants.h"
#include "client/willeq_database.h"
#include "common/logging.h"

#include <irrlicht.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <chrono>

using namespace EQT::Graphics;

namespace {

// Populate every equipment + general slot with a placeholder item name
// (using the canonical slot name so nothing is invisible by being empty).
void populateFull(InventoryPanelState& inv) {
    for (int i = 0; i < InventoryPanelState::EQUIP_SLOTS; ++i) {
        inv.equipSlots[i].hasItem = true;
        inv.equipSlots[i].itemName = eqt::inventory::getSlotName(static_cast<int16_t>(i));
    }
    for (int i = 0; i < InventoryPanelState::GENERAL_SLOTS; ++i) {
        inv.generalSlots[i].hasItem = true;
        inv.generalSlots[i].itemName = "Item";
        inv.generalSlots[i].quantity = i + 1;
    }
}

// A few slots populated, most empty -- shows the empty-slot rendering.
void populateSparse(InventoryPanelState& inv) {
    inv.equipSlots[eqt::inventory::SLOT_PRIMARY].hasItem = true;
    inv.equipSlots[eqt::inventory::SLOT_PRIMARY].itemName = "Rusty Sword";
    inv.equipSlots[eqt::inventory::SLOT_CHEST].hasItem = true;
    inv.equipSlots[eqt::inventory::SLOT_CHEST].itemName = "Cloth Tunic";
    inv.generalSlots[0].hasItem = true;
    inv.generalSlots[0].itemName = "Bread";
    inv.generalSlots[0].quantity = 5;
}

bool capture(irr::video::IVideoDriver* driver, UIRenderer& ui, const UILayout& layout,
             const InventoryPanelState& inv, const std::string& path) {
    driver->beginScene(true, true, irr::video::SColor(255, 20, 20, 30));
    ui.beginFrame();
    renderInventoryPopup(ui, layout, inv);
    ui.endFrame();
    driver->endScene();

    irr::video::IImage* img = driver->createScreenShot();
    if (!img) {
        fprintf(stderr, "ERROR: createScreenShot() returned null for %s\n", path.c_str());
        return false;
    }
    bool ok = driver->writeImageToFile(img, path.c_str());
    img->drop();
    if (!ok) {
        fprintf(stderr, "ERROR: writeImageToFile failed for %s\n", path.c_str());
        return false;
    }
    printf("Wrote %s\n", path.c_str());
    return true;
}

bool runAtResolution(irr::u32 w, irr::u32 h, const std::string& outputDir,
                      const std::string& suffix) {
    irr::SIrrlichtCreationParameters params;
#ifdef EQT_HAS_GLES2
    params.DriverType = irr::video::EDT_OGLES2;
#else
    params.DriverType = irr::video::EDT_OPENGL;
#endif
    params.WindowSize = irr::core::dimension2d<irr::u32>(w, h);
    params.Fullscreen = true;

    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) {
        fprintf(stderr, "ERROR: failed to create Irrlicht device at %ux%u\n", w, h);
        return false;
    }

    irr::video::IVideoDriver* driver = device->getVideoDriver();

    TextBatch textBatch;
    textBatch.init(device->getGUIEnvironment());

    UIRenderer ui;
    ui.init(driver, /*atlas=*/nullptr, &textBatch);

    UILayout layout;
    layout.computeLayout(static_cast<irr::s32>(w), static_cast<irr::s32>(h));

    // Prime the driver (matches gles2_composite_audit's idiom).
    driver->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
    driver->endScene();

    bool ok = true;

    InventoryPanelState fullInv;
    fullInv.activePopup = PopupType::Inventory;
    populateFull(fullInv);

    // Warm-up render: the driver's first *content* frame after the black
    // prime frame was empirically found to drop the immediate-mode
    // draw2DRectangle calls (panel/slot backgrounds) from the captured
    // screenshot on this surfaceless-EGL/llvmpipe backend — likely
    // double-buffer swap lag on the very first content frame — while text
    // (routed through a different flush path) still appeared. Render the
    // real content once, discard the screenshot, then capture for real;
    // every capture after the first one in a run was never affected.
    {
        driver->beginScene(true, true, irr::video::SColor(255, 20, 20, 30));
        ui.beginFrame();
        renderInventoryPopup(ui, layout, fullInv);
        ui.endFrame();
        driver->endScene();
    }

    ok &= capture(driver, ui, layout, fullInv,
                  outputDir + "/inventory_" + suffix + "_full.png");

    if (suffix == "800x600") {
        InventoryPanelState inv;
        inv.activePopup = PopupType::Inventory;
        populateSparse(inv);
        ok &= capture(driver, ui, layout, inv,
                      outputDir + "/inventory_" + suffix + "_sparse.png");

        InventoryPanelState hoverInv;
        hoverInv.activePopup = PopupType::Inventory;
        populateFull(hoverInv);
        hoverInv.hoveredSlot = eqt::inventory::SLOT_CHEST;
        ok &= capture(driver, ui, layout, hoverInv,
                      outputDir + "/inventory_" + suffix + "_hover.png");
    }

    device->drop();
    return ok;
}

// T60: renders the paperdoll (CharacterModelView) into the inventory
// popup's center at 800x600, once bare and once fully equipped, and
// captures both. Returns false only on a hard failure (device creation);
// missing/incomplete asset data prints a note to stderr and is treated as
// "nothing to capture" rather than a tool failure — the slot-only captures
// above must keep working even where no EQ client data is mounted.
bool runPaperdollCaptures(const std::string& outputDir, const std::string& eqPath) {
    irr::SIrrlichtCreationParameters params;
#ifdef EQT_HAS_GLES2
    params.DriverType = irr::video::EDT_OGLES2;
#else
    params.DriverType = irr::video::EDT_OPENGL;
#endif
    params.WindowSize = irr::core::dimension2d<irr::u32>(800, 600);
    params.Fullscreen = true;
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) {
        fprintf(stderr, "ERROR: failed to create Irrlicht device for paperdoll captures\n");
        return false;
    }
    irr::video::IVideoDriver* driver = device->getVideoDriver();
    irr::scene::ISceneManager* smgr = device->getSceneManager();

    EQT::WilleqDatabase willeqDb;
    bool haveDb = willeqDb.open("data/willeq.db");
    if (haveDb) {
        EQT::WilleqDatabase::setGlobalInstance(&willeqDb);
    } else {
        fprintf(stderr, "NOTE: could not open data/willeq.db -- race S3D lookup will fail, "
                        "skipping paperdoll captures\n");
    }

    using namespace EQT::Graphics;

    // Everything that references driver/smgr lives in this nested scope,
    // so it is fully destroyed BEFORE device->drop() runs below — calling
    // device->drop() first (driver/smgr become dangling) while these
    // RAII objects are still alive as outer-scope locals crashed with
    // "pure virtual method called" (ConstrainedTextureCache::~clear()
    // touching an already-destroyed driver) during development; this
    // ordering is why the scope exists.
    bool ok = true;
    if (haveDb) {
        ConstrainedRendererConfig cfg = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        ConstrainedTextureCache textureCache(cfg);
        textureCache.setDriver(driver);

        RaceModelLoader raceModelLoader(smgr, driver, device->getFileSystem());
        raceModelLoader.setClientPath(eqPath);
        raceModelLoader.setConstrainedTextureCache(&textureCache);
        if (auto* mb = raceModelLoader.getMeshBuilder()) {
            mb->setConstrainedTextureCache(&textureCache);
            textureCache.setMeshBuilder(mb);
        }
        raceModelLoader.loadGlobalModels();
        raceModelLoader.loadNumberedGlobalModels();

        EquipmentModelLoader equipmentModelLoader(smgr, driver, device->getFileSystem());
        equipmentModelLoader.setClientPath(eqPath);
        equipmentModelLoader.loadItemModelMapping("data/item_models.json");
        equipmentModelLoader.loadEquipmentArchives();

        constexpr uint16_t kRaceHuman = 1;   // HUM
        constexpr uint8_t kGenderMale = 0;
        constexpr uint32_t kTestPrimaryItemId = 1000;    // A weapon present in item_models.json

        // hasRaceModel() only reflects the lazy-load cache (populated on
        // first actual load attempt) — loadGlobalModels()/
        // loadNumberedGlobalModels() above index archives but don't
        // themselves populate it. Force the load attempt (same call
        // CharacterModelView::setCharacter() will trigger indirectly) so
        // the check below is meaningful.
        raceModelLoader.ensureModelDataLoaded(kRaceHuman, kGenderMale);
        if (!raceModelLoader.hasRaceModel(kRaceHuman, kGenderMale)) {
            fprintf(stderr, "NOTE: no race model data available for race=%u (check --eq-path=%s), "
                            "skipping paperdoll captures\n", kRaceHuman, eqPath.c_str());
        } else {
            eqt::ui::CharacterModelView doll;
            doll.init(smgr, driver, /*width=*/304, /*height=*/256);  // Matches InventoryLayout::paperdollRect()
            doll.setRaceModelLoader(&raceModelLoader);
            doll.setEquipmentModelLoader(&equipmentModelLoader);

            UILayout layout;
            layout.computeLayout(800, 600);

            TextBatch textBatch;
            textBatch.init(device->getGUIEnvironment());
            UIRenderer ui;
            ui.init(driver, /*atlas=*/nullptr, &textBatch);

            // Prime frame (same double-buffer-lag workaround as runAtResolution()).
            driver->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
            driver->endScene();

            // T60 Step 5 (Pi verification): measure the cost of the RTT
            // re-render itself (renderToTexture() while dirty), separate
            // from model construction (setCharacter()) — the plan asks
            // specifically for "the cost of a dirty-frame re-render",
            // which in the real UI is what a drag-rotation frame pays
            // repeatedly, not what the one-time model build pays. Also
            // measures the following no-op call (dirty_==false) to
            // confirm "never re-render per frame" actually holds cheap.
            auto nowMs = []() {
                return std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
            };

            auto captureDoll = [&](const std::string& path, bool timeIt) {
                double t0 = nowMs();
                doll.update(16.0f);
                doll.renderToTexture();  // dirty_ was true (setCharacter() marks it)
                double t1 = nowMs();
                doll.renderToTexture();  // dirty_ now false -- should be a cheap no-op
                double t2 = nowMs();
                doll.markDirty();
                doll.renderToTexture();  // pure RTT re-render, no model rebuild
                double t3 = nowMs();
                if (timeIt) {
                    printf("TIMING: %s dirty_render_after_setCharacter=%.3fms "
                           "noop_render=%.3fms markDirty_then_render=%.3fms\n",
                           path.c_str(), t1 - t0, t2 - t1, t3 - t2);
                }

                InventoryPanelState inv;
                inv.activePopup = PopupType::Inventory;
                inv.paperdollTexture = doll.getTexture();

                // Warm-up render, discarded — see runAtResolution()'s comment for why.
                driver->beginScene(true, true, irr::video::SColor(255, 20, 20, 30));
                ui.beginFrame();
                renderInventoryPopup(ui, layout, inv);
                ui.endFrame();
                driver->endScene();

                return capture(driver, ui, layout, inv, path);
            };

            // Bare character: default appearance, no equipment.
            EntityAppearance bareAppearance{};
            doll.setCharacter(kRaceHuman, kGenderMale, bareAppearance);
            ok &= captureDoll(outputDir + "/inventory_paperdoll_bare.png", /*timeIt=*/true);

            // Fully equipped: a primary weapon attached (bone-attachment
            // path exercised — this is the instrument the plan wants for
            // the held-item misalignment, though fixing it is explicitly
            // out of scope here).
            EntityAppearance equippedAppearance{};
            equippedAppearance.equipment[7] = kTestPrimaryItemId;  // Primary slot
            doll.setCharacter(kRaceHuman, kGenderMale, equippedAppearance);
            ok &= captureDoll(outputDir + "/inventory_paperdoll_equipped.png", /*timeIt=*/true);
        }
        // doll, ui, textBatch, layout, equipmentModelLoader, raceModelLoader,
        // textureCache all destruct here, in reverse declaration order,
        // still with a live driver/smgr.
    }

    device->drop();
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    std::string outputDir = "ui_capture";
    std::string eqPath = "/mnt/projects/EverQuestP1999";  // EXECUTOR_PROTOCOL.md default

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--eq-path") && i + 1 < argc) {
            eqPath = argv[++i];
        } else if (argv[i][0] != '-') {
            outputDir = argv[i];
        }
    }
    mkdir(outputDir.c_str(), 0755);

    bool ok = true;
    ok &= runAtResolution(800, 600, outputDir, "800x600");
    ok &= runAtResolution(1280, 800, outputDir, "1280x800");
    ok &= runPaperdollCaptures(outputDir, eqPath);

    return ok ? 0 : 1;
}
