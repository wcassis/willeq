/*
 * The confirmation dialog -- EQUI_ConfirmationDialog.xml's ConfirmationDialogBox
 * (B442b, GitLab #151 rows 31-35). A REUSABLE widget: a text the game thread
 * composed, and either Yes/No or a single Ok.
 *
 * WHO ASKS. The two measured users are the cursor gestures:
 *   - a motionless world click with an item held: "ARE YOU SURE you wish to
 *     drop your %1 on the ground?" (eqstr 9057), Yes -> OP_GroundSpawn;
 *   - IW_Destroy with an item held: "Are you absolutely sure you want to
 *     destroy your %1?" (eqstr 2043), Yes -> OP_MoveItem 30 -> -1.
 * The text arrives on GameEventType::ConfirmationRequested with the asker's
 * own `purpose` token; the dialog echoes the token back to its owner on Yes
 * and knows nothing about what it confirms. A further user -- B899's eqstr
 * 8504 stack refusal, today a chat line -- can open it in the Ok form
 * (`Buttons::Ok`, CD_OK_Button); that is noted, not done.
 *
 * Like the QuantityWnd it is `RelativePosition false` and drawn and hit-tested
 * above every other window. No answers locally: a No (or Ok) only closes it,
 * because nothing has been sent; a Yes is the owner's to turn into an intent.
 *
 * NOT DONE, measured: the official dialog closes itself if left unanswered
 * (gestures.tsv row 34: "it auto-dismisses if unanswered"); its timeout was
 * not measured, so fable's stays up until answered (assumption awaiting
 * oracle).
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "client/graphics/ui/ui_layout.h"

namespace eqt { namespace state {
struct ConfirmationRequestedData;
} }

namespace EQT {
namespace Graphics {

class UIRenderer;
class TextBatch;

struct ConfirmationDialogState {
    enum class Buttons : uint8_t { YesNo = 0, Ok };

    bool isOpen = false;
    uint8_t purpose = 0;     // the asker's token, echoed back on Yes
    std::string text;        // CD_TextOutput
    Buttons buttons = Buttons::YesNo;

    bool yesHovered = false;
    bool noHovered = false;
    bool okHovered = false;

    void open(uint8_t askerPurpose, const std::string& prompt, Buttons b = Buttons::YesNo);
    void close() { *this = ConfirmationDialogState{}; }
};

/** What a click on the dialog did. */
enum class ConfirmationClick : uint8_t {
    None = 0,   // not over the dialog, or it is closed
    Consumed,   // over the dialog, on no button
    Yes,        // CD_Yes_Button -- the owner acts on `purpose`, then close()
    No,         // CD_No_Button  -- nothing was sent; the caller closes
    Ok,         // CD_OK_Button  (the Ok form)
};

/** Hover + click. Pure: pushes nothing and does not close itself. */
ConfirmationClick processConfirmationClick(const UILayout& layout,
                                           ConfirmationDialogState& state,
                                           int mouseX, int mouseY, bool leftClicked);

/** The ConfirmationRequested bridge arm: opens the dialog (Yes/No). */
void applyConfirmationRequested(ConfirmationDialogState& state,
                                const eqt::state::ConfirmationRequestedData& data);

/**
 * CD_TextOutput's lines: `text` word-wrapped to `width` pixels with the font's
 * own advances (`tb`); with no font, every 6 px is one character, the
 * built-in font's advance.
 */
std::vector<std::string> wrapConfirmationText(const std::string& text, int width,
                                              const TextBatch* tb);

void renderConfirmationDialog(UIRenderer& ui, const UILayout& layout,
                              const ConfirmationDialogState& state);

}  // namespace Graphics
}  // namespace EQT
