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
// T62: two more per-piece captures (weapon only, shield only) isolate the
// two items' contributions -- T60's "equipped" capture had both on at
// once, which made it impossible to tell which item (or both) produced
// the untextured white mass. Also: this tool's own EquipmentModelLoader
// never had setConstrainedTextureCache() called on it (unlike
// raceModelLoader above), so EquipmentModelLoader::buildMeshFromGeometry's
// texture-upload branch (gated entirely on constrainedTextureCache_ being
// non-null, no fallback) never ran -- every equipment mesh this tool ever
// built was untextured by construction, regardless of any in-world
// behaviour. Fixed below. Mesh bounding-box dimensions for the test
// weapon/shield are also printed (stderr, "BBOX:" lines) to establish real
// units for bone_attachment.h's grip-offset math.
//
// Usage: ui_inventory_capture <output_dir> [--eq-path <dir>]
// Writes:
//   <output_dir>/inventory_800x600_full.png     -- every slot populated
//   <output_dir>/inventory_1280x800_full.png    -- larger size, same state
//   <output_dir>/inventory_800x600_sparse.png   -- a few slots populated
//   <output_dir>/inventory_800x600_hover.png    -- one slot hovered
//   <output_dir>/inventory_paperdoll_bare.png   -- T60: doll, no equipment
//   <output_dir>/inventory_paperdoll_equipped.png -- T60: doll, weapon+shield
//   <output_dir>/inventory_paperdoll_weapon_only.png -- T62: primary weapon only
//   <output_dir>/inventory_paperdoll_shield_only.png -- T62: secondary shield only
//   <output_dir>/inventory_paperdoll_two_handed.png  -- T62: 2H weapon, primary only

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
        // T62: this call was missing. Without it, constrainedTextureCache_
        // stays null and EquipmentModelLoader::buildMeshFromGeometry's
        // entire texture-upload branch (equipment_model_loader.cpp, gated
        // on constrainedTextureCache_ with no fallback) never runs -- every
        // equipment mesh built by this tool was untextured regardless of
        // asset availability. Production (EntityRenderer::setConstrainedTextureCache,
        // called from IrrlichtRenderer init) already wires this correctly
        // for the in-world path; this tool did not, mirroring the same
        // omission in tools/model_viewer.cpp (which has no
        // ConstrainedTextureCache at all).
        equipmentModelLoader.setConstrainedTextureCache(&textureCache);
        equipmentModelLoader.loadItemModelMapping("data/item_models.json");
        equipmentModelLoader.loadEquipmentArchives();

        constexpr uint16_t kRaceHuman = 1;   // HUM
        constexpr uint8_t kGenderMale = 0;
        // T62 correction: the plan's original "kTestPrimaryItemId = 1000"
        // (inherited from T60) does NOT map to a weapon. willeq.db's
        // item_models table maps database item 1000 -> equipment model
        // IT64, and IT64's texture (per equipment_models table) is
        // "sack1.bmp" -- a sack/container model, not a sword. That
        // explains the huge, wrong-shaped brown mass in T60's original
        // capture far better than a grip-offset bug would: it was never a
        // weapon in the first place. Switched to real weapon/shield IT
        // model IDs, passed directly (no database item->model mapping
        // exists for any of these three in item_models, so
        // EquipmentModelLoader::getEquipmentMesh()'s DB-lookup-miss
        // fallback treats the ID as a literal model ID -- the same path
        // NPC spawn packets use).
        constexpr uint32_t kTestPrimaryItemId = 1;      // IT1: one-handed sword (flat/sword/swordhandle.bmp)
        constexpr uint32_t kTestTwoHandedItemId = 45;   // IT45: wizard staff (staffwiz-wood/stafwiz-jewl.bmp) -- 2H test case
        constexpr uint32_t kTestShieldModelId = 202;    // IT202: kiteshield-m, a representative shield
        constexpr uint32_t kTestDaggerModelId = 5;      // IT5: dagger -- T63's exact live-session repro item

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

            // T62: print the raw mesh bounding boxes for the test weapon
            // and shield BEFORE any scale/attachment logic touches them --
            // these are the ground-truth EQ-mesh-space dimensions
            // bone_attachment.h's computeWeaponGripOffset()'s "length > 3.0f"
            // two-handed threshold is implicitly assumed to compare
            // against.
            auto printBBox = [](const char* label, irr::scene::IMesh* mesh) {
                if (!mesh) {
                    fprintf(stderr, "BBOX: %s -- mesh is null\n", label);
                    return;
                }
                irr::core::aabbox3df bb = mesh->getBoundingBox();
                fprintf(stderr,
                        "BBOX: %s min=(%.3f,%.3f,%.3f) max=(%.3f,%.3f,%.3f) "
                        "extent=(%.3f,%.3f,%.3f)\n",
                        label, bb.MinEdge.X, bb.MinEdge.Y, bb.MinEdge.Z,
                        bb.MaxEdge.X, bb.MaxEdge.Y, bb.MaxEdge.Z,
                        bb.MaxEdge.X - bb.MinEdge.X, bb.MaxEdge.Y - bb.MinEdge.Y,
                        bb.MaxEdge.Z - bb.MinEdge.Z);
            };
            printBBox("1H weapon (IT1, sword)", equipmentModelLoader.getEquipmentMesh(kTestPrimaryItemId));
            printBBox("2H weapon (IT45, wizard staff)", equipmentModelLoader.getEquipmentMesh(kTestTwoHandedItemId));
            printBBox("shield (IT202)", equipmentModelLoader.getEquipmentMesh(kTestShieldModelId));

            // Bare character: default appearance, no equipment.
            EntityAppearance bareAppearance{};
            doll.setCharacter(kRaceHuman, kGenderMale, bareAppearance);
            ok &= captureDoll(outputDir + "/inventory_paperdoll_bare.png", /*timeIt=*/true);

            // T62: weapon only -- isolates the primary weapon's
            // contribution from the shield's.
            EntityAppearance weaponOnlyAppearance{};
            weaponOnlyAppearance.equipment[7] = kTestPrimaryItemId;  // Primary slot
            doll.setCharacter(kRaceHuman, kGenderMale, weaponOnlyAppearance);
            ok &= captureDoll(outputDir + "/inventory_paperdoll_weapon_only.png", /*timeIt=*/false);

            // T62: shield only -- isolates the secondary shield's
            // contribution from the weapon's.
            EntityAppearance shieldOnlyAppearance{};
            shieldOnlyAppearance.equipment[8] = kTestShieldModelId;  // Secondary slot
            doll.setCharacter(kRaceHuman, kGenderMale, shieldOnlyAppearance);
            ok &= captureDoll(outputDir + "/inventory_paperdoll_shield_only.png", /*timeIt=*/false);

            // T65: a plain (non-shield) secondary weapon, isolated -- T63's
            // live-session defect (secondary weapon slot renders nothing,
            // while the shield in the same slot renders fine). Reuses
            // kTestPrimaryItemId (IT1, a real 1H sword, model < 200 so
            // EquipmentModelLoader::isShield() is false) but places it in
            // the SECONDARY slot instead of primary, to hit
            // character_model_view.cpp's non-shield secondary branch
            // (rotation (180,0,0)) exactly as T63's dagger did.
            EntityAppearance secondaryWeaponOnlyAppearance{};
            secondaryWeaponOnlyAppearance.equipment[8] = kTestPrimaryItemId;  // Secondary slot, plain weapon
            doll.setCharacter(kRaceHuman, kGenderMale, secondaryWeaponOnlyAppearance);
            ok &= captureDoll(outputDir + "/inventory_paperdoll_secondary_weapon_only.png", /*timeIt=*/false);

            // T65: the SAME item T63 used live (IT5 dagger, secondary slot)
            // -- reproduces T63's exact repro rather than a different sword
            // model, to rule out "it's an IT1-specific geometry quirk".
            printBBox("secondary dagger (IT5)", equipmentModelLoader.getEquipmentMesh(kTestDaggerModelId));
            EntityAppearance secondaryDaggerAppearance{};
            secondaryDaggerAppearance.equipment[8] = kTestDaggerModelId;  // Secondary slot
            doll.setCharacter(kRaceHuman, kGenderMale, secondaryDaggerAppearance);
            ok &= captureDoll(outputDir + "/inventory_paperdoll_secondary_dagger_it5.png", /*timeIt=*/false);

            // T65 diagnostic: T63's exact race/gender (Dark Elf female,
            // race=6 gender=1) with the same dagger, to rule out "human
            // male renders a thin sliver, DAF renders truly nothing" as a
            // race-specific skeleton orientation difference. Not part of
            // the tool's normal capture set -- gated so it doesn't affect
            // default output if the DAF race model isn't available.
            constexpr uint16_t kRaceDarkElf = 6;
            constexpr uint8_t kGenderFemale = 1;
            raceModelLoader.ensureModelDataLoaded(kRaceDarkElf, kGenderFemale);
            if (raceModelLoader.hasRaceModel(kRaceDarkElf, kGenderFemale)) {
                EntityAppearance dafDaggerAppearance{};
                dafDaggerAppearance.equipment[8] = kTestDaggerModelId;
                doll.setCharacter(kRaceDarkElf, kGenderFemale, dafDaggerAppearance);
                ok &= captureDoll(outputDir + "/inventory_paperdoll_daf_secondary_dagger.png", /*timeIt=*/false);

                EntityAppearance dafShieldAppearance{};
                dafShieldAppearance.equipment[8] = kTestShieldModelId;
                doll.setCharacter(kRaceDarkElf, kGenderFemale, dafShieldAppearance);
                ok &= captureDoll(outputDir + "/inventory_paperdoll_daf_secondary_shield.png", /*timeIt=*/false);
            } else {
                fprintf(stderr, "NOTE: no DAF race model available, skipping race-specific diagnostic captures\n");
            }

            // T62: a two-handed weapon (bbox length > the 3.0f threshold
            // in bone_attachment.h), primary slot only -- exercises
            // computeWeaponGripOffset()'s two-handed branch specifically,
            // which none of the other captures reach.
            EntityAppearance twoHandedAppearance{};
            twoHandedAppearance.equipment[7] = kTestTwoHandedItemId;  // Primary slot
            doll.setCharacter(kRaceHuman, kGenderMale, twoHandedAppearance);
            ok &= captureDoll(outputDir + "/inventory_paperdoll_two_handed.png", /*timeIt=*/false);

            // Fully equipped: both weapon and shield attached (bone-
            // attachment path exercised — this is the instrument the plan
            // wants for the held-item misalignment).
            EntityAppearance equippedAppearance{};
            equippedAppearance.equipment[7] = kTestPrimaryItemId;   // Primary slot
            equippedAppearance.equipment[8] = kTestShieldModelId;   // Secondary slot
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
