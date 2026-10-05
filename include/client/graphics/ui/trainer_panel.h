/*
 * The skill trainer window -- renderer-side state and draw (B898, GitLab
 * #214).
 *
 * WHAT WAS BROKEN. `/train` (and T) opened a server-side trainer session:
 * the server's OP_GMTraining reply was decoded by
 * MerchantComponent::ZoneProcessGMTraining into a TrainerWindowOpenedData
 * (npc, money, practices, every trainable skill with its current value, cap
 * and eqemu's cubic price) and pushed across the bridge -- where the
 * IrrlichtBridge arm was a LOG_TRACE that DISCARDED it, the shape T290 found
 * the loot arms in and T436 the merchant/bank arms. Nothing was drawn.
 *
 * The geometry is include/client/graphics/ui/train_layout.h, which quotes
 * EQUI_TrainWindow.xml; this file's .cpp computes no rect.
 *
 * NO NEW INTENT, NO NEW EVENT. TRNW_TrainButton pushes `TrainSkillIntent`
 * with the selected row's skill id -- the intent `/train <skill|id>` pushes,
 * landing on the same game-thread refusals and the same OP_GMTrainSkill
 * sender. TW_DoneButton and the close box push `CloseTrainerIntent`, the
 * intent `/trainclose` pushes; its handler is the existing
 * CloseTrainerWindow(), which sends OP_GMEndTraining exactly as the official
 * client ends a session. The panel never closes itself and never moves a
 * value: it closes on `TrainerWindowClosed` and its rows, practices and coins
 * change only when the game thread re-publishes `TrainerWindowOpened` after
 * the server's OP_SkillUpdate (MerchantComponent::PublishTrainerWindow) --
 * T290's rule and Will's "server is authoritative".
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "client/graphics/ui/train_layout.h"
#include "client/graphics/ui/ui_layout.h"

namespace eqt { namespace state {
struct TrainerWindowOpenedData;
struct WindowClosedData;
} }

namespace EQT {
namespace Graphics {

class UIRenderer;

/** One TRNW_SkillList row. The fields TrainerSkillEntryData carries. */
struct TrainerSkillRow {
    uint8_t skillId = 0;
    std::string name;            // -> "Skill Name"
    uint32_t currentValue = 0;   // -> "Value"
    uint32_t maxTrainable = 0;   // this trainer's cap (not drawn; see .cpp)
    uint32_t cost = 0;           // copper -> the four coin columns
};

struct TrainerPanelState {
    static constexpr int COIN_COUNT = TrainLayout::COIN_COUNT;  // 4

    bool isOpen = false;

    uint16_t npcId = 0;
    std::string trainerName;

    std::vector<TrainerSkillRow> rows;       // -> TRNW_SkillList
    uint32_t practicePoints = 0;             // -> TRNW_PracticeCount
    // TRNW_CoinCount0..3, platinum/gold/silver/copper in element order.
    uint32_t coins[COIN_COUNT] = {0, 0, 0, 0};

    // Index into `rows`, or -1. A row click selects; Train acts on it. Held
    // by SKILL across a refresh (the row set can change under it).
    int selectedRow = -1;
    // `Style_VScroll true`: first row shown, counted from the TOP (the loot
    // and skills convention).
    int scrollOffset = 0;

    int hoveredRow = -1;
    bool trainHovered = false;
    bool doneHovered = false;
    bool closeHovered = false;

    /** Highest legal scrollOffset. */
    int maxScrollOffset() const;
    /** How many rows the list draws now (<= TrainLayout::ROWS_VISIBLE). */
    int visibleRows() const;
    /** The selected row's skill id, or -1 when nothing is selected. */
    int selectedSkillId() const;
};

// --- the bridge arms, as free functions (T290's idiom) ---------------------

/**
 * TrainerWindowOpened. Opens the window -- or, when it is already open at
 * the SAME trainer, refreshes it in place: the game thread re-publishes this
 * event after every OP_SkillUpdate of a session, so selection (by skill id)
 * and scroll survive the refresh.
 */
void applyTrainerWindowOpened(TrainerPanelState& state,
                              const eqt::state::TrainerWindowOpenedData& data);

/** TrainerWindowClosed. */
void applyTrainerWindowClosed(TrainerPanelState& state,
                              const eqt::state::WindowClosedData& data);

// --- draw and hit test -----------------------------------------------------

/**
 * Draw the window. Only when state.isOpen. `expProgress` (0..1) feeds
 * TRNW_ExpGauge (`<EQType>4</EQType>`, the experience gauge), which is the
 * XP bar's own figure -- the renderer already holds it.
 */
void renderTrainerWindow(UIRenderer& ui, const UILayout& layout,
                         const TrainerPanelState& state, float expProgress);

/** Which `rows` index is under the point? -1 on a miss or a closed window. */
int hitTestTrainerRow(const UILayout& layout, const TrainerPanelState& state,
                      int mouseX, int mouseY);

bool hitTestTrainerTrainButton(const UILayout& layout,
                               const TrainerPanelState& state, int mouseX,
                               int mouseY);
/** TW_DoneButton, and Style_Closebox, which do the same thing. */
bool hitTestTrainerDoneButton(const UILayout& layout,
                              const TrainerPanelState& state, int mouseX,
                              int mouseY);
bool hitTestTrainerCloseBox(const UILayout& layout,
                            const TrainerPanelState& state, int mouseX,
                            int mouseY);
/** The scroll bar's arrows: -1 up, +1 down, 0 neither. */
int hitTestTrainerScrollArrow(const UILayout& layout,
                              const TrainerPanelState& state, int mouseX,
                              int mouseY);

/** Is the point anywhere inside the open window? (Click consumption.) */
bool trainerContains(const UILayout& layout, const TrainerPanelState& state,
                     int mouseX, int mouseY);

/**
 * The cost column split: copper -> the digit shown under Header_PlatCoin
 * (0), Header_GoldCoin (1), Header_SilvCoin (2), Header_CoppCoin (3). The
 * 1000/100/10/1 scale the merchant window's price columns use.
 */
uint32_t trainerCostDigit(uint32_t costCopper, int denom);

}  // namespace Graphics
}  // namespace EQT
