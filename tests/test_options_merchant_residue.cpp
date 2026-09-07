// T439 -- the T434/T436 residue: absolute volume (B432), the zone-in snapshot
// (B444), and the unused Options popup enumerator (B434).
//
// Three unrelated defects that share one property: each is a seam a previous
// unit LEFT, and each is invisible to every existing test because the missing
// half is a PRODUCER, not a consumer. T436 gave slot 30 and 2000..2007
// consumers and the zone-in snapshot never published them; T434 wired the
// Options sliders and nothing published a volume back.
//
// TWO KINDS OF CASE HERE, and the distinction is what makes the pre-fix run
// meaningful:
//
//   * `*Census.*` reads the TREE at runtime through WILLEQ_REPO_ROOT, so the
//     same binary measures the pre-fix and the post-fix sources. That is the
//     pre-fix red for all three items (the recipe is T436's: build, export a
//     patch of the changed sources, `git apply -R`, run the census filter,
//     re-apply). No stash -- the stash stack is shared across worktrees.
//   * everything else is ordinary behaviour over the real free functions.
//     Those cannot exist before the fix (they name types the fix creates),
//     which is the documented normal case for a unit that adds a seam.
//
// Device-free, database-free, server-free.

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "client/audio/volume_steps.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/bank_panel.h"
#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/options_layout.h"
#include "client/graphics/ui/options_panel.h"
#include "client/graphics/ui/static_panels.h"
#include "client/inventory/inventory_constants.h"
#include "client/state/event_bus.h"

using namespace EQT::Graphics;

namespace {

const std::string kRoot = WILLEQ_REPO_ROOT;

std::string slurp(const std::string& rel) {
    std::ifstream in(kRoot + "/" + rel);
    EXPECT_TRUE(in.good()) << "cannot open " << rel;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// A deleted symbol's TOMBSTONE names it on purpose, so a census that asserts
// "nothing reads this any more" has to look at code and not at comments.
std::string codeOnly(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    std::istringstream lines(src);
    std::string line;
    bool inBlock = false;
    while (std::getline(lines, line)) {
        if (inBlock) {
            const size_t end = line.find("*/");
            if (end == std::string::npos) continue;
            line = line.substr(end + 2);
            inBlock = false;
        }
        const size_t block = line.find("/*");
        if (block != std::string::npos && line.find("*/", block) == std::string::npos) {
            line = line.substr(0, block);
            inBlock = true;
        }
        const size_t slashes = line.find("//");
        if (slashes != std::string::npos) line = line.substr(0, slashes);
        out += line;
        out += '\n';
    }
    return out;
}

int countOf(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (size_t at = hay.find(needle); at != std::string::npos;
         at = hay.find(needle, at + needle.size())) {
        ++n;
    }
    return n;
}

eqt::state::InventorySlotChangedData Slot(int16_t id, bool hasItem,
                                          uint32_t icon) {
    eqt::state::InventorySlotChangedData d;
    d.slotId = id;
    d.hasItem = hasItem;
    d.itemId = hasItem ? 1234u : 0u;
    d.itemName = hasItem ? "Rusty Dagger" : "";
    d.iconId = hasItem ? icon : 0u;
    d.quantity = hasItem ? 1 : 0;
    return d;
}

eqt::state::VolumesChangedData Volumes(int music, int sound) {
    eqt::state::VolumesChangedData d;
    d.musicStep = music;
    d.soundStep = sound;
    return d;
}

}  // namespace

// ===========================================================================
// B432 -- the volume round trip
// ===========================================================================

// The pre-fix red. The two volume intents carried `float delta`, which is the
// whole mechanism of the board item: a delta needs a previous value, so the
// renderer had to keep one, and a /musicvolume set the audio manager without
// moving it.
TEST(B432Census, TheVolumeIntentsCarryAnAbsoluteStepNotADelta) {
    const std::string intents =
        codeOnly(slurp("include/client/events/renderer_intents.h"));

    const size_t music = intents.find("struct MusicVolumeChangeIntent");
    const size_t effects = intents.find("struct EffectsVolumeChangeIntent");
    ASSERT_NE(music, std::string::npos);
    ASSERT_NE(effects, std::string::npos);

    const std::string musicBody = intents.substr(music, intents.find('}', music) - music);
    const std::string effectsBody =
        intents.substr(effects, intents.find('}', effects) - effects);

    EXPECT_TRUE(has(musicBody, "int step"))
        << "MusicVolumeChangeIntent must carry an absolute 0..20 step";
    EXPECT_FALSE(has(musicBody, "float delta"))
        << "MusicVolumeChangeIntent still carries a delta (B432)";
    EXPECT_TRUE(has(effectsBody, "int step"))
        << "EffectsVolumeChangeIntent must carry an absolute 0..20 step";
    EXPECT_FALSE(has(effectsBody, "float delta"))
        << "EffectsVolumeChangeIntent still carries a delta (B432)";
}

// The shadow records are the SYMPTOM of the delta, and B432 names them
// explicitly. With an absolute intent and a published-back applied value,
// optionsState_ is the only record on the renderer side.
TEST(B432Census, TheRendererKeepsNoShadowOfTheVolumeItLastSent) {
    const std::string frameHeader =
        codeOnly(slurp("include/client/graphics/frame/frame_component.h"));
    const std::string applier =
        codeOnly(slurp("src/client/graphics/frame/renderer_frame_options.cpp"));

    // The member DECLARATIONS are gone. The tombstone comment naming them is
    // deliberate, so match the declaration form rather than the bare name.
    EXPECT_FALSE(has(frameHeader, "int optionsAppliedMusicStep_"))
        << "the music shadow record still exists (B432)";
    EXPECT_FALSE(has(frameHeader, "int optionsAppliedSoundStep_"))
        << "the sound shadow record still exists (B432)";

    // And nothing computes a difference any more.
    EXPECT_FALSE(has(applier, "optionsAppliedMusicStep_"))
        << "applyOptionsSetting still reads the music shadow (B432)";
    EXPECT_FALSE(has(applier, "optionsAppliedSoundStep_"))
        << "applyOptionsSetting still reads the sound shadow (B432)";
    EXPECT_TRUE(has(applier, "MusicVolumeChangeIntent{step}"))
        << "the music slider must push its ABSOLUTE step";
    EXPECT_TRUE(has(applier, "EffectsVolumeChangeIntent{step}"))
        << "the sound slider must push its ABSOLUTE step";
}

// B432 names FOUR sites that move a volume, and the defect was that none of
// them told the renderer. Every one now calls the single publisher, and the
// bridge has an arm that consumes it. A fifth site added later without a
// publish is what this case is here to catch.
TEST(B432Census, EverySiteThatMovesAVolumePublishesTheAppliedValueBack) {
    const std::string configApply =
        slurp("src/client/bridge/bridge_component_ui_audio.cpp");
    const std::string intentHandlers =
        codeOnly(slurp("src/client/bridge/bridge_component_intents.cpp"));
    const std::string slash = slurp("src/client/commands/commands_spells.cpp");
    const std::string bridge = slurp("src/client/bridge/irrlicht_bridge.cpp");

    // Site 1: the config apply in InitializeAudio().
    EXPECT_TRUE(has(configApply, "PublishVolumesChanged();"))
        << "the audio config apply does not publish its volumes (B432)";
    // ... and the publisher itself lives there, exactly once.
    EXPECT_EQ(countOf(configApply,
                      "void eqt::bridge::BridgeComponent::PublishVolumesChanged()"),
              1)
        << "there must be exactly ONE publisher body";

    // Sites 2 and 3: the two volume intents.
    EXPECT_EQ(countOf(intentHandlers, "PublishVolumesChanged();"), 2)
        << "both volume intent handlers must publish the applied value";
    EXPECT_FALSE(has(intentHandlers, "getMusicVolume() + i.delta"))
        << "the music intent handler still adds a delta (B432)";
    EXPECT_FALSE(has(intentHandlers, "getEffectsVolume() + i.delta"))
        << "the effects intent handler still adds a delta (B432)";

    // Sites 4 and 5: /musicvolume and /effectsvolume, the paths that set the
    // audio manager ABSOLUTELY and were the board item's worked example.
    EXPECT_EQ(countOf(slash, "PublishVolumesChanged();"), 2)
        << "/musicvolume and /effectsvolume must publish the applied value";

    // And the renderer consumes it.
    EXPECT_TRUE(has(bridge, "case state::GameEventType::VolumesChanged:"))
        << "the bridge has no arm for VolumesChanged (B432)";
    EXPECT_TRUE(has(bridge, "applyVolumesChanged("))
        << "the VolumesChanged arm must reach the Options panel state";
}

// The unit's shape, stated once: the slider's travel and the wire's volume
// unit are the SAME number, and it is the oracle's (T434 measured 0..20).
TEST(OptionsVolumeT439, TheWireUnitIsTheOraclesSliderStep) {
    EXPECT_EQ(eqt::audio::kVolumeSteps, OptionsLayout::SLIDER_STEPS);
    EXPECT_EQ(eqt::audio::kVolumeSteps, 20);

    // The measured pair from eqclient.ini: Music=18 shows 90 %.
    EXPECT_FLOAT_EQ(eqt::audio::volumeFractionFromStep(18), 0.9f);
    EXPECT_EQ(eqt::audio::volumeStepFromFraction(0.9f), 18);
    // SoundVolume=10 shows 50 %.
    EXPECT_FLOAT_EQ(eqt::audio::volumeFractionFromStep(10), 0.5f);
    EXPECT_EQ(eqt::audio::volumeStepFromFraction(0.5f), 10);

    // Both ends, and both directions of clamping.
    EXPECT_FLOAT_EQ(eqt::audio::volumeFractionFromStep(0), 0.0f);
    EXPECT_FLOAT_EQ(eqt::audio::volumeFractionFromStep(20), 1.0f);
    EXPECT_EQ(eqt::audio::clampVolumeStep(-7), 0);
    EXPECT_EQ(eqt::audio::clampVolumeStep(41), 20);
    EXPECT_EQ(eqt::audio::volumeStepFromFraction(-1.0f), 0);
    EXPECT_EQ(eqt::audio::volumeStepFromFraction(9.0f), 20);

    // Every step round-trips.
    for (int step = 0; step <= eqt::audio::kVolumeSteps; ++step) {
        EXPECT_EQ(eqt::audio::volumeStepFromFraction(
                      eqt::audio::volumeFractionFromStep(step)),
                  step)
            << "step " << step;
    }
}

TEST(OptionsVolumeT439, TheApplierWritesBothSlidersAndClamps) {
    OptionsPanelState s;
    EXPECT_EQ(s.musicVolume, 14);  // fable's shipped 0.7 on the oracle scale
    EXPECT_EQ(s.soundVolume, 20);  // fable's shipped 1.0

    EXPECT_TRUE(applyVolumesChanged(s, Volumes(4, 9)));
    EXPECT_EQ(s.musicVolume, 4);
    EXPECT_EQ(s.soundVolume, 9);

    // Idempotent: the same value again is not a move, so a save-on-change
    // comparison does not churn.
    EXPECT_FALSE(applyVolumesChanged(s, Volumes(4, 9)));

    // A value assembled in code cannot get through.
    EXPECT_TRUE(applyVolumesChanged(s, Volumes(-3, 77)));
    EXPECT_EQ(s.musicVolume, 0);
    EXPECT_EQ(s.soundVolume, 20);

    // The clip plane is a different setting and is never touched by this.
    EXPECT_EQ(s.clipPlane, 5);
}

// THE BOARD ITEM'S OWN SCENARIO, run over the real functions on both sides of
// the bridge. This is the case that fails on the delta shape.
//
// Pre-T439 the renderer sent `fraction(new) - fraction(lastSent)` and only
// `lastSent` moved, so the sequence below ended with the audio manager at
// 0.4 + (9-5)/20 = 0.6 -- step 12 -- while the slider read 9. Post-T439 the
// slider's own number IS what the audio manager is told.
TEST(OptionsVolumeT439, ASlashCommandThenADragLeavesTheAudioManagerOnTheSlider) {
    OptionsPanelState panel;

    // 1. The player drags the music slider to 5.
    setOptionsSettingStep(panel, OptionsLayout::Setting::MusicVolume, 5);
    int sentStep = panel.musicVolume;                       // the ABSOLUTE intent
    float audioManager = eqt::audio::volumeFractionFromStep(sentStep);
    EXPECT_FLOAT_EQ(audioManager, 0.25f);
    // ... and the applied value comes back.
    applyVolumesChanged(panel, Volumes(eqt::audio::volumeStepFromFraction(audioManager),
                                       panel.soundVolume));
    EXPECT_EQ(panel.musicVolume, 5);

    // 2. `/musicvolume 40` sets the audio manager ABSOLUTELY, behind the
    //    window's back. This is the path B432 is about.
    audioManager = 40 / 100.0f;
    applyVolumesChanged(panel, Volumes(eqt::audio::volumeStepFromFraction(audioManager),
                                       panel.soundVolume));
    EXPECT_EQ(panel.musicVolume, 8) << "the slider must follow /musicvolume";

    // 3. The player now drags the slider to 9. The audio manager must end up
    //    AT 9, not at 8 plus a difference computed from a stale record.
    setOptionsSettingStep(panel, OptionsLayout::Setting::MusicVolume, 9);
    sentStep = panel.musicVolume;
    audioManager = eqt::audio::volumeFractionFromStep(sentStep);
    EXPECT_EQ(sentStep, 9);
    EXPECT_FLOAT_EQ(audioManager, eqt::audio::volumeFractionFromStep(9));
    EXPECT_NE(eqt::audio::volumeStepFromFraction(audioManager), 12)
        << "this is the stale-shadow answer the delta shape produced";

    applyVolumesChanged(panel, Volumes(eqt::audio::volumeStepFromFraction(audioManager),
                                       panel.soundVolume));
    EXPECT_EQ(panel.musicVolume, 9);
}

// The volume HOTKEYS travel the same distance they always did, said in the
// oracle's unit: 0.1 of a 0..1 volume is 2 of 20 steps.
TEST(OptionsVolumeT439, AVolumeHotkeyMovesTwoStepsAndSaturates) {
    const std::string receiver =
        slurp("src/client/graphics/renderer/renderer_event_receiver.cpp");
    EXPECT_TRUE(has(receiver, "musicVolumeStepDelta_ = 2"));
    EXPECT_TRUE(has(receiver, "musicVolumeStepDelta_ = -2"));
    EXPECT_TRUE(has(receiver, "effectsVolumeStepDelta_ = 2"));
    EXPECT_TRUE(has(receiver, "effectsVolumeStepDelta_ = -2"));

    // 2 steps is the old 0.1f, exactly.
    EXPECT_FLOAT_EQ(eqt::audio::volumeFractionFromStep(2) -
                        eqt::audio::volumeFractionFromStep(0),
                    0.1f);

    // The consumer adds the delta to the panel's own step and clamps.
    OptionsPanelState panel;
    panel.musicVolume = 19;
    EXPECT_EQ(eqt::audio::clampVolumeStep(panel.musicVolume + 2), 20);
    panel.musicVolume = 1;
    EXPECT_EQ(eqt::audio::clampVolumeStep(panel.musicVolume - 2), 0);

    const std::string input =
        codeOnly(slurp("src/client/graphics/frame/renderer_frame_input.cpp"));
    EXPECT_TRUE(has(input, "getMusicVolumeStepDelta()"));
    EXPECT_TRUE(has(input, "getEffectsVolumeStepDelta()"));
    EXPECT_FALSE(has(input, "getMusicVolumeDelta()"))
        << "the float delta accessor must be gone (B432)";
}

// ===========================================================================
// B444 -- the zone-in snapshot
// ===========================================================================

// The pre-fix red. The loop literally read `slot < 30`.
TEST(B444Census, TheZoneInSnapshotCoversTheCursorAndTheBank) {
    const std::string eq = codeOnly(slurp("src/client/core/everquest.cpp"));

    EXPECT_FALSE(has(eq, "for (int16_t slot = 0; slot < 30; ++slot)"))
        << "the zone-in snapshot still stops at slot 30 (B444)";
    EXPECT_TRUE(has(eq, "eqt::inventory::ZoneInSnapshotSlots()"))
        << "the snapshot must iterate the named slot table (B444)";
}

// The table itself: every slot family that has a CONSUMER is in it, and the
// three consumers between them accept every entry. That last assertion is the
// point -- a slot published with nowhere to land is the same class of defect
// as a consumer with nothing published to it.
TEST(ZoneInSnapshotT439, EverySnapshotSlotReachesAConsumer) {
    const auto slots = eqt::inventory::ZoneInSnapshotSlots();

    // 22 equipment + 8 general + the cursor + the bank's eight.
    ASSERT_EQ(slots.size(), 39u);

    std::set<int16_t> unique(slots.begin(), slots.end());
    EXPECT_EQ(unique.size(), slots.size()) << "a slot is published twice";

    for (int16_t s = 0; s < 30; ++s) {
        EXPECT_TRUE(unique.count(s)) << "possession slot " << s << " is missing";
    }
    EXPECT_TRUE(unique.count(eqt::inventory::CURSOR_SLOT)) << "the cursor (B444)";
    for (int16_t i = 0; i < eqt::inventory::BANK_SNAPSHOT_COUNT; ++i) {
        EXPECT_TRUE(unique.count(static_cast<int16_t>(eqt::inventory::BANK_BEGIN + i)))
            << "bank slot " << i << " (B444)";
    }

    // Eight, not sixteen: the era's EQUI_BankWnd.xml declares BW_BankSlot0..7,
    // and 2008..2015 have no consumer. BANK_COUNT stays the WIRE's range.
    EXPECT_EQ(eqt::inventory::BANK_SNAPSHOT_COUNT, BankPanelState::SLOT_COUNT);
    EXPECT_EQ(eqt::inventory::BANK_COUNT, 16);
    EXPECT_FALSE(unique.count(static_cast<int16_t>(eqt::inventory::BANK_BEGIN + 8)));

    // Now drive the real appliers with the real event struct, exactly as the
    // bridge arm does, and require every published slot to be claimed by one
    // of the three families.
    CursorAttachmentState cursor;
    BankPanelState bank;
    for (const int16_t s : slots) {
        const auto data = Slot(s, /*hasItem=*/true, /*icon=*/700);
        const bool possession = (s >= 0 && s < 30);
        const bool onCursor = applyCursorSlotChanged(cursor, data);
        const bool inBank = applyBankSlotChanged(bank, data);
        EXPECT_TRUE(possession || onCursor || inBank)
            << "slot " << s << " is published and nothing consumes it";
        // ... and exactly one family claims it.
        EXPECT_EQ(static_cast<int>(possession) + static_cast<int>(onCursor) +
                      static_cast<int>(inBank),
                  1)
            << "slot " << s << " is claimed by more than one family";
    }
}

// A character who zones in HOLDING something draws the attachment, and a bank
// slot that already held an item draws it -- the two cases the board names.
TEST(ZoneInSnapshotT439, ACursorItemAndABankItemLandOnZoneIn) {
    CursorAttachmentState cursor;
    BankPanelState bank;
    ASSERT_FALSE(cursor.hasItem);
    ASSERT_FALSE(bank.slots[3].hasItem);

    for (const int16_t s : eqt::inventory::ZoneInSnapshotSlots()) {
        // The snapshot publishes an event for every slot; only two of them
        // carry an item in this fixture.
        const bool occupied = (s == eqt::inventory::CURSOR_SLOT) ||
                              (s == eqt::inventory::BANK_BEGIN + 3);
        const auto data = Slot(s, occupied, occupied ? 812u : 0u);
        applyCursorSlotChanged(cursor, data);
        applyBankSlotChanged(bank, data);
    }

    EXPECT_TRUE(cursor.hasItem) << "a held item drew nothing at zone-in (B444)";
    EXPECT_EQ(cursor.iconId, 812u);
    EXPECT_EQ(cursor.itemName, "Rusty Dagger");

    EXPECT_TRUE(bank.slots[3].hasItem) << "a banked item drew nothing (B444)";
    EXPECT_EQ(bank.slots[3].iconId, 812u);
    EXPECT_FALSE(bank.slots[2].hasItem);
    EXPECT_FALSE(bank.slots[4].hasItem);
}

// ===========================================================================
// B434 -- the unused Options popup enumerator
// ===========================================================================

TEST(B434Census, PopupTypeHasNoOptionsEnumerator) {
    const std::string panels =
        codeOnly(slurp("include/client/graphics/ui/static_panels.h"));

    const size_t at = panels.find("enum class PopupType");
    ASSERT_NE(at, std::string::npos);
    const std::string body = panels.substr(at, panels.find("};", at) - at);

    EXPECT_FALSE(has(body, "Options"))
        << "PopupType::Options is still declared (B434). T434 built the "
           "Options window as an INDEPENDENT window -- EQUI_OptionsWindow.xml "
           "is RelativePosition false at its own <Location> -- so the "
           "enumerator has no producer and no consumer.";

    // The enumerators that remain. T442 (B450) then deleted five more that
    // nothing named, leaving the three fable actually drives; the census that
    // pins the new set is B450Census in test_dead_renderer_members.cpp.
    EXPECT_TRUE(has(body, "None = 0"));
    EXPECT_TRUE(has(body, "Vendor"));
    EXPECT_EQ(body.find("Vendor"), body.rfind("Vendor"));
}

// The whole tree, not just the declaration: nothing names the enumerator any
// more, in code or in a comment that claims it exists.
TEST(B434Census, NothingInTheTreeNamesPopupTypeOptions) {
    for (const char* rel : {
             "include/client/graphics/ui/static_panels.h",
             "include/client/graphics/ui/options_panel.h",
             "include/client/graphics/frame/frame_component.h",
             "src/client/graphics/frame/renderer_frame_options.cpp",
             "tests/test_options_window.cpp",
         }) {
        EXPECT_FALSE(has(codeOnly(slurp(rel)), "PopupType::Options"))
            << rel << " still names PopupType::Options (B434)";
    }
}

// The enumerator is gone and nothing that DID work stopped working: the one
// popup fable actually drives through this enum still round-trips, and the
// Options window's own visibility is its own bool.
TEST(B434Census, TheOptionsWindowIsNotACentrePopup) {
    InventoryPanelState inv;
    EXPECT_EQ(inv.activePopup, PopupType::None);
    inv.activePopup = PopupType::Inventory;
    EXPECT_EQ(inv.activePopup, PopupType::Inventory);

    // Opening the Options window closes nothing -- it is drawn and hit-tested
    // above every centre popup (T434), which is exactly why it never became
    // one of them.
    OptionsPanelState opts;
    EXPECT_FALSE(optionsWindowIsVisible(opts));
    opts.isOpen = true;
    EXPECT_TRUE(optionsWindowIsVisible(opts));
    EXPECT_EQ(inv.activePopup, PopupType::Inventory);
}
