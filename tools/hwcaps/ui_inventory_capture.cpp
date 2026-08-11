// ui_inventory_capture — T57 visual-acceptance capture harness.
//
// There was no existing path to capture a screenshot of the static UI
// (--dump-png-prefix belongs to gles2_zone_benchmark, which renders zones
// and never draws a popup). This is the smallest tool that renders
// renderInventoryPopup() against a real, headless GLES2 device (the same
// surfaceless-EGL/llvmpipe backend the visual gate and gles2_zone_benchmark
// use — see docker/irrlicht-drm-x86 and src/client/graphics/CLAUDE.md) and
// writes the result to a PNG, so Will can judge the T57 layout — this tool
// never self-assesses the output.
//
// Usage: ui_inventory_capture <output_dir>
// Writes:
//   <output_dir>/inventory_800x600_full.png     -- every slot populated
//   <output_dir>/inventory_1280x800_full.png    -- larger size, same state
//   <output_dir>/inventory_800x600_sparse.png   -- a few slots populated
//   <output_dir>/inventory_800x600_hover.png    -- one slot hovered

#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/text_batch.h"
#include "client/inventory/inventory_constants.h"
#include "common/logging.h"

#include <irrlicht.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

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

} // namespace

int main(int argc, char** argv) {
    std::string outputDir = argc > 1 ? argv[1] : "ui_capture";
    mkdir(outputDir.c_str(), 0755);

    bool ok = true;
    ok &= runAtResolution(800, 600, outputDir, "800x600");
    ok &= runAtResolution(1280, 800, outputDir, "1280x800");

    return ok ? 0 : 1;
}
