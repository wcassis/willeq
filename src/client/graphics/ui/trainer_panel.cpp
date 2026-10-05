/*
 * B898 (GitLab #214) -- the skill trainer window.
 *
 * Its own TU (M00). The geometry is in
 * include/client/graphics/ui/train_layout.h, which quotes
 * EQUI_TrainWindow.xml; this file computes no rect. The window's click
 * handling, StaticUIInput::processTrainerInput(), is defined here too, beside
 * the hit tests it calls, so static_ui_input.cpp stays untouched.
 */

#include "client/graphics/ui/trainer_panel.h"

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/text_batch.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/state/event_bus.h"

#include <algorithm>
#include <string>

namespace EQT {
namespace Graphics {

namespace {

const irr::video::SColor kText(255, 225, 225, 225);
const irr::video::SColor kTitle(255, 255, 215, 0);
const irr::video::SColor kHeading(255, 255, 215, 0);
const irr::video::SColor kFrame(255, 80, 80, 80);
const irr::video::SColor kFieldBg(200, 18, 18, 24);
// TRNW_ExpGauge <FillTint> (240,240,0).
const irr::video::SColor kExpFill(255, 240, 240, 0);
const irr::video::SColor kGaugeBg(200, 30, 30, 36);

irr::video::SColor buttonColor(bool hovered) {
    return hovered ? irr::video::SColor(220, 120, 60, 60)
                   : irr::video::SColor(200, 56, 62, 84);
}

}  // namespace

// --- state -----------------------------------------------------------------

int TrainerPanelState::maxScrollOffset() const {
    const int over = static_cast<int>(rows.size()) - TrainLayout::ROWS_VISIBLE;
    return over > 0 ? over : 0;
}

int TrainerPanelState::visibleRows() const {
    const int remaining = static_cast<int>(rows.size()) - scrollOffset;
    if (remaining <= 0) return 0;
    return std::min(remaining, TrainLayout::ROWS_VISIBLE);
}

int TrainerPanelState::selectedSkillId() const {
    if (selectedRow < 0 || selectedRow >= static_cast<int>(rows.size())) return -1;
    return rows[static_cast<size_t>(selectedRow)].skillId;
}

uint32_t trainerCostDigit(uint32_t costCopper, int denom) {
    switch (denom) {
        case 0: return costCopper / 1000;
        case 1: return (costCopper / 100) % 10;
        case 2: return (costCopper / 10) % 10;
        default: return costCopper % 10;
    }
}

// --- bridge arms -----------------------------------------------------------

void applyTrainerWindowOpened(TrainerPanelState& state,
                              const eqt::state::TrainerWindowOpenedData& data) {
    // A refresh of the session already on screen keeps what the player was
    // looking at; a different trainer (or a closed window) starts clean.
    const bool refresh = state.isOpen && state.npcId == data.npcId;
    const int keepSkill = refresh ? state.selectedSkillId() : -1;

    state.isOpen = true;
    state.npcId = data.npcId;
    state.trainerName = data.npcName;
    state.practicePoints = data.practicePoints;
    state.coins[0] = data.platinum;
    state.coins[1] = data.gold;
    state.coins[2] = data.silver;
    state.coins[3] = data.copper;

    state.rows.clear();
    state.rows.reserve(data.skills.size());
    for (const auto& s : data.skills) {
        TrainerSkillRow row;
        row.skillId = s.skillId;
        row.name = s.name;
        row.currentValue = s.currentValue;
        row.maxTrainable = s.maxTrainable;
        row.cost = s.cost;
        state.rows.push_back(std::move(row));
    }

    state.selectedRow = -1;
    if (keepSkill >= 0) {
        for (size_t i = 0; i < state.rows.size(); ++i) {
            if (state.rows[i].skillId == keepSkill) {
                state.selectedRow = static_cast<int>(i);
                break;
            }
        }
    }
    if (!refresh) state.scrollOffset = 0;
    state.scrollOffset = std::clamp(state.scrollOffset, 0, state.maxScrollOffset());
    state.hoveredRow = -1;
}

void applyTrainerWindowClosed(TrainerPanelState& state,
                              const eqt::state::WindowClosedData& data) {
    (void)data;
    state.isOpen = false;
    state.npcId = 0;
    state.rows.clear();
    state.selectedRow = -1;
    state.scrollOffset = 0;
    state.hoveredRow = -1;
    state.trainHovered = state.doneHovered = state.closeHovered = false;
}

// --- draw ------------------------------------------------------------------

void renderTrainerWindow(UIRenderer& ui, const UILayout& layout,
                         const TrainerPanelState& state, float expProgress) {
    if (!state.isOpen) return;

    auto* tb = ui.getTextBatch();
    ui.drawPanel(TrainLayout::windowRect(layout));

    // --- Style_Titlebar: <Text>Training Window</Text> ---
    const auto title = TrainLayout::titleRect(layout);
    ui.drawRect(title, irr::video::SColor(200, 26, 26, 34));
    if (tb) {
        tb->addText("Training Window", title.UpperLeftCorner.X + 6,
                    title.UpperLeftCorner.Y + 2, kTitle);
    }

    // --- Style_Closebox (Style_Minimizebox is not drawn: train_layout.h) ---
    const auto close = TrainLayout::closeBoxRect(layout);
    ui.drawRect(close, state.closeHovered ? irr::video::SColor(220, 120, 60, 60)
                                          : irr::video::SColor(200, 60, 60, 72));
    if (tb) tb->addTextCentered("X", close, kText);

    // --- TRNW_SkillList: WDT_Inner, Style_Border, Style_VScroll ---
    const auto list = TrainLayout::listRect(layout);
    ui.drawRect(list, kFieldBg);
    ui.drawFrame(list, kFrame);

    // The headings. The four coin columns carry a <Header> (art) rather than
    // a <Heading> (text), so the coin decal goes there -- MerchantLayout's
    // reading of the same four Header_*Coin entries.
    static const char* kHeadings[TrainLayout::COLUMN_COUNT] = {
        "Skill Name", "Rank", "Value", nullptr, nullptr, nullptr, nullptr};
    for (int c = 0; c < TrainLayout::COLUMN_COUNT; ++c) {
        const auto cell = TrainLayout::headerCellRect(layout, c);
        if (kHeadings[c]) {
            if (tb) {
                tb->addText(kHeadings[c], cell.UpperLeftCorner.X + 2,
                            cell.UpperLeftCorner.Y + 2, kHeading);
            }
            continue;
        }
        const int denom = c - TrainLayout::COL_PLAT;
        ui.drawSprite(cell, static_cast<uint8_t>(
            static_cast<int>(UISprite::CoinPlatinum) + denom));
    }

    const int rows = state.visibleRows();
    for (int row = 0; row < rows; ++row) {
        const int idx = state.scrollOffset + row;
        const auto& r = state.rows[static_cast<size_t>(idx)];
        const auto rowRect = TrainLayout::rowRect(layout, row);
        if (idx == state.selectedRow) {
            ui.drawRect(rowRect, irr::video::SColor(90, 90, 110, 160));
        } else if (idx == state.hoveredRow) {
            ui.drawRect(rowRect, irr::video::SColor(60, 255, 255, 255));
        }
        if (!tb) continue;

        const auto name = TrainLayout::cellRect(layout, row, TrainLayout::COL_NAME);
        tb->addText(r.name, name.UpperLeftCorner.X + 2, name.UpperLeftCorner.Y + 2,
                    kText);

        // "Rank" is left EMPTY. The oracle (review/UIGAP-ORACLE/214) shows a
        // WORD scale, but only two points of it were seen -- "Awful" at
        // values 0..8 and "Master" at 200 -- so the thresholds and the words
        // between are unknown. Inventing them is the guess Will's ruling
        // forbids; still oracle-owed.

        const auto value = TrainLayout::cellRect(layout, row, TrainLayout::COL_VALUE);
        tb->addText(std::to_string(r.currentValue), value.UpperLeftCorner.X + 2,
                    value.UpperLeftCorner.Y + 2, kText);

        // MEASURED (review/UIGAP-ORACLE/214, train.tsv row 3): a free row
        // shows "--" in all four coin columns; a priced one shows each digit,
        // zeros included (Tailoring 200: "68 5 9 0").
        for (int denom = 0; denom < 4; ++denom) {
            const auto cell = TrainLayout::cellRect(layout, row,
                                                    TrainLayout::COL_PLAT + denom);
            tb->addText(r.cost == 0 ? std::string("--")
                                    : std::to_string(trainerCostDigit(r.cost, denom)),
                        cell.UpperLeftCorner.X + 2, cell.UpperLeftCorner.Y + 2,
                        kText);
        }
    }

    // --- the scroll bar: track + two arrows ---
    const auto bar = TrainLayout::scrollBarRect(layout);
    ui.drawRect(bar, irr::video::SColor(200, 30, 30, 36));
    const auto up = TrainLayout::scrollUpRect(layout);
    const auto down = TrainLayout::scrollDownRect(layout);
    ui.drawRect(up, irr::video::SColor(200, 56, 62, 84));
    ui.drawFrame(up, kFrame);
    ui.drawRect(down, irr::video::SColor(200, 56, 62, 84));
    ui.drawFrame(down, kFrame);
    if (tb) {
        tb->addTextCentered("^", up, kText);
        tb->addTextCentered("v", down, kText);
    }
    const int maxOff = state.maxScrollOffset();
    if (maxOff > 0) {
        // The thumb: its position in the track is scrollOffset / maxOff.
        const irr::s32 trackTop = up.LowerRightCorner.Y;
        const irr::s32 trackH = down.UpperLeftCorner.Y - trackTop;
        const irr::s32 thumbH = std::max<irr::s32>(
            8, trackH * TrainLayout::ROWS_VISIBLE /
                   static_cast<irr::s32>(state.rows.size()));
        const irr::s32 y = trackTop + (trackH - thumbH) * state.scrollOffset / maxOff;
        ui.drawRect(irr::core::rect<irr::s32>(bar.UpperLeftCorner.X + 3, y,
                                              bar.LowerRightCorner.X - 3,
                                              y + thumbH),
                    irr::video::SColor(220, 110, 110, 130));
    }

    // --- TRNW_ExpLabel + TRNW_ExpGauge ---
    if (tb) tb->addTextCentered("Experience", TrainLayout::expLabelRect(layout), kText);
    ui.drawBar(TrainLayout::expGaugeRect(layout),
               std::clamp(expProgress, 0.0f, 1.0f), kExpFill, kGaugeBg);
    ui.drawFrame(TrainLayout::expGaugeRect(layout), kFrame);

    // --- TRNW_PracticeLabel (AlignRight) + TRNW_PracticeCount (AlignCenter) ---
    if (tb) {
        const auto pl = TrainLayout::practiceLabelRect(layout);
        const std::string label = "Practices:";
        tb->addText(label, pl.LowerRightCorner.X - tb->getTextWidth(label),
                    pl.UpperLeftCorner.Y + 2, kText);
        tb->addTextCentered(std::to_string(state.practicePoints),
                            TrainLayout::practiceCountRect(layout), kText);
    }

    // --- TRNW_TrainButton ---
    // MEASURED (train.tsv rows 1 and 5): DISABLED until a row is selected.
    const auto train = TrainLayout::trainButtonRect(layout);
    const bool trainEnabled = state.selectedSkillId() >= 0;
    ui.drawRect(train, trainEnabled ? buttonColor(state.trainHovered)
                                    : irr::video::SColor(160, 40, 40, 46));
    ui.drawFrame(train, kFrame);
    if (tb) {
        tb->addTextCentered("Train", train,
                            trainEnabled ? kText
                                         : irr::video::SColor(255, 120, 120, 128));
    }

    // --- TRNW_MoneyLabel, TRNW_Purse0..3, TRNW_CoinCount0..3 ---
    if (tb) tb->addTextCentered("Money", TrainLayout::moneyLabelRect(layout), kText);
    for (int denom = 0; denom < TrainerPanelState::COIN_COUNT; ++denom) {
        ui.drawSprite(TrainLayout::purseRect(layout, denom), static_cast<uint8_t>(
            static_cast<int>(UISprite::CoinPlatinum) + denom));
        if (!tb) continue;
        const auto count = TrainLayout::coinCountRect(layout, denom);
        tb->addText(std::to_string(state.coins[denom]), count.UpperLeftCorner.X,
                    count.UpperLeftCorner.Y + 2, kText);
    }

    // --- TW_DoneButton ---
    const auto done = TrainLayout::doneButtonRect(layout);
    ui.drawRect(done, buttonColor(state.doneHovered));
    ui.drawFrame(done, kFrame);
    if (tb) tb->addTextCentered("Done", done, kText);
}

// --- hit tests -------------------------------------------------------------

int hitTestTrainerRow(const UILayout& layout, const TrainerPanelState& state,
                      int mouseX, int mouseY) {
    if (!state.isOpen) return -1;
    const irr::core::vector2di p(mouseX, mouseY);
    const int rows = state.visibleRows();
    for (int row = 0; row < rows; ++row) {
        if (TrainLayout::rowRect(layout, row).isPointInside(p)) {
            return state.scrollOffset + row;
        }
    }
    return -1;
}

bool hitTestTrainerTrainButton(const UILayout& layout,
                               const TrainerPanelState& state, int mouseX,
                               int mouseY) {
    if (!state.isOpen) return false;
    return TrainLayout::trainButtonRect(layout).isPointInside(
        irr::core::vector2di(mouseX, mouseY));
}

bool hitTestTrainerDoneButton(const UILayout& layout,
                              const TrainerPanelState& state, int mouseX,
                              int mouseY) {
    if (!state.isOpen) return false;
    return TrainLayout::doneButtonRect(layout).isPointInside(
        irr::core::vector2di(mouseX, mouseY));
}

bool hitTestTrainerCloseBox(const UILayout& layout,
                            const TrainerPanelState& state, int mouseX,
                            int mouseY) {
    if (!state.isOpen) return false;
    return TrainLayout::closeBoxRect(layout).isPointInside(
        irr::core::vector2di(mouseX, mouseY));
}

int hitTestTrainerScrollArrow(const UILayout& layout,
                              const TrainerPanelState& state, int mouseX,
                              int mouseY) {
    if (!state.isOpen) return 0;
    const irr::core::vector2di p(mouseX, mouseY);
    if (TrainLayout::scrollUpRect(layout).isPointInside(p)) return -1;
    if (TrainLayout::scrollDownRect(layout).isPointInside(p)) return 1;
    return 0;
}

bool trainerContains(const UILayout& layout, const TrainerPanelState& state,
                     int mouseX, int mouseY) {
    if (!state.isOpen) return false;
    return TrainLayout::windowRect(layout).isPointInside(
        irr::core::vector2di(mouseX, mouseY));
}

// --- clicks ----------------------------------------------------------------

bool StaticUIInput::processTrainerInput(int mouseX, int mouseY,
                                        bool leftClicked,
                                        const UILayout& layout,
                                        TrainerPanelState& trainer) {
    trainer.hoveredRow = -1;
    trainer.trainHovered = false;
    trainer.doneHovered = false;
    trainer.closeHovered = false;
    if (!trainer.isOpen) return false;

    const bool onDone = hitTestTrainerDoneButton(layout, trainer, mouseX, mouseY);
    const bool onClose = hitTestTrainerCloseBox(layout, trainer, mouseX, mouseY);
    if (onDone || onClose) {
        trainer.doneHovered = onDone;
        trainer.closeHovered = onClose;
        if (leftClicked && bridge_) {
            // /trainclose's intent. Its handler sends OP_GMEndTraining and
            // publishes TrainerWindowClosed; the panel closes on THAT.
            bridge_->pushIntent(eqt::events::CloseTrainerIntent{});
        }
        return true;
    }

    if (hitTestTrainerTrainButton(layout, trainer, mouseX, mouseY)) {
        const int skill = trainer.selectedSkillId();
        // Disabled with no selection (measured): consumed, no hover, no intent.
        trainer.trainHovered = skill >= 0;
        if (leftClicked && bridge_ && skill >= 0) {
            // /train <id>'s intent, carrying the SKILL id (never the row).
            // Every refusal is the game thread's; nothing local changes.
            bridge_->pushIntent(
                eqt::events::TrainSkillIntent{static_cast<uint8_t>(skill)});
        }
        return true;
    }

    const int arrow = hitTestTrainerScrollArrow(layout, trainer, mouseX, mouseY);
    if (arrow != 0) {
        if (leftClicked) {
            trainer.scrollOffset = std::clamp(trainer.scrollOffset + arrow, 0,
                                              trainer.maxScrollOffset());
        }
        return true;
    }

    const int row = hitTestTrainerRow(layout, trainer, mouseX, mouseY);
    if (row >= 0) {
        trainer.hoveredRow = row;
        // Selection is client-side; the server is told nothing until Train.
        if (leftClicked) trainer.selectedRow = row;
        return true;
    }

    return trainerContains(layout, trainer, mouseX, mouseY);
}

}  // namespace Graphics
}  // namespace EQT
