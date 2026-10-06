#pragma once
/*
 * B460 (GitLab #156, UIGAP-small) -- what ESC closes: the MOST RECENTLY
 * OPENED window, one per press.
 *
 * MEASURED on the official client (review/UIGAP-ORACLE/156/README.md): with
 * the Inventory, its Tip window and a bag open, Escape #1 closed the tip,
 * #2 the bag, #3 the inventory, and further presses did nothing. Before this
 * fable used a FIXED list (options -> friends -> item display -> containers ->
 * tracking -> trainer -> bank -> merchant -> the centre popups), so a window
 * opened after another one lower in that list was closed second.
 *
 * This is a stack of OPEN ORDER, device-free. FrameComponent reports which
 * windows are open every frame (observe()); a window that was not open the
 * last time it looked gets the next sequence number, so the stack sees every
 * opener -- hotkey, slash command, click or server event -- without any of
 * them having to call into it. Windows that become open in the SAME frame are
 * ordered as the caller lists them (bottom of the draw first), so the one
 * drawn on top is the newer.
 *
 * Not in the stack, deliberately:
 *   - the Quantity window: ESC does NOT close it (measured, ../151 G09);
 *   - the HUD windows (group, pet, buff, combat abilities): whether the
 *     official ESC closes them is unmeasured, and fable never has;
 *   - the target: with nothing in the stack ESC clears it. Whether the
 *     official first press ALSO clears the target while closing a window is
 *     NOT separated by the oracle run (both happened on press 1) -- fable
 *     keeps its behaviour (a window-closing press leaves the target), an
 *     ASSUMPTION AWAITING ORACLE.
 */

#include <cstdint>
#include <optional>
#include <vector>

namespace EQT {
namespace Graphics {

enum class EscWindow : uint8_t {
    Options,
    Friends,
    ItemDisplay,
    Tip,          // the tip windows (B834 / #202): measured, closed first
    Container,    // sub = ContainerWindowsState index
    Tracking,
    Trainer,
    Bank,
    Merchant,
    Tradeskill,   // the T415 recipe window (no opener since B899)
    Loot,
    Spellbook,
    Skills,
    Inventory,
    Count         // "any" in StaticUIInput::processKey()
};

struct EscWindowKey {
    EscWindow kind = EscWindow::Count;
    int sub = 0;
    bool operator==(const EscWindowKey& o) const { return kind == o.kind && sub == o.sub; }
};

const char* escWindowName(EscWindow kind);

class EscWindowStack {
public:
    /** The windows open NOW, bottom of the draw first. */
    void observe(const std::vector<EscWindowKey>& openNow);

    /** The most recently opened window still open, if any. */
    std::optional<EscWindowKey> newest() const;


private:
    struct Entry {
        EscWindowKey key;
        uint64_t seq = 0;
    };
    std::vector<Entry> open_;
    uint64_t next_ = 1;
};

}  // namespace Graphics
}  // namespace EQT
