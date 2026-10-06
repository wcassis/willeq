// renderer_frame_tips.cpp - the tip windows' triggers, text loading and click
// pass (B834, GitLab #202). The window itself (state, parse, draw, hit test)
// is tip_window.{h,cpp}; its geometry tip_layout.h; the persisted switches
// the [Tips] section of UiPreferences.
//
// Triggers are RISING EDGES of the windows fable has, observed once a frame,
// so every opener -- key, slash command, click or server event -- pops the
// tip without calling into this file (the B460 ESC stack's device).

#include "client/graphics/frame/frame_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/renderer_event_receiver.h"
#include "client/graphics/ui/options_layout.h"
#include "client/graphics/ui/static_ui_input.h"
#include "common/logging.h"

#include <fstream>
#include <sstream>

namespace EQT {
namespace Graphics {

namespace {

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

}  // namespace

std::set<std::string> FrameComponent::activeTipTriggers() const {
    std::set<std::string> on;
    if (inventoryState_.activePopup == PopupType::Inventory) on.insert("Inventory");
    if (spellbookState_.isOpen) on.insert("Spellbook");
    if (skillsPopupState_.isOpen) on.insert("SkillsWindow");
    if (optionsState_.isOpen &&
        optionsState_.activeTab == static_cast<int>(OptionsLayout::Tab::Keys)) {
        on.insert("OptKeys");
    }
    if (friendsState_.isOpen) on.insert("Friends");
    if (merchantState_.isOpen) on.insert("Merchant");
    if (bankState_.isOpen) on.insert("Bank");
    if (trainerState_.isOpen) on.insert("Trainer");
    if (lootState_.isOpen) on.insert("Loot");
    if (containerState_.windows[ContainerWindowsState::WORLD_WINDOW].isOpen) {
        on.insert("WorldContainer");
    }
    if (staticUIInput_ && staticUIInput_->quantityWindow().isOpen) on.insert("Quantity");
    // B896's trade / give window: "Trade Tip window" / "Give Items Tip window"
    // in the observed table (review/UIGAP-ORACLE/202/README.md).
    if (tradeState_.isOpen) on.insert(tradeState_.isNpc ? "GiveNPC" : "Trade");
    return on;
}

const TipDocument* FrameComponent::tipDocument(const std::string& key) {
    auto it = tipDocs_.find(key);
    if (it != tipDocs_.end()) return it->second.title.empty() && it->second.paragraphs.empty()
                                         ? nullptr : &it->second;
    TipDocument doc;
    const char* file = tipFileForKey(key);
    std::string text;
    if (file && !ctx_.config.eqClientPath.empty() &&
        readFile(ctx_.config.eqClientPath + "/help/tips/" + file, text)) {
        doc = parseTipHtml(text);
    } else {
        LOG_DEBUG(MOD_UI, "[TIP] no text for '{}' under {}/help/tips", key, ctx_.config.eqClientPath);
    }
    auto& slot = tipDocs_[key] = std::move(doc);  // an empty entry caches the miss
    return slot.title.empty() && slot.paragraphs.empty() ? nullptr : &slot;
}

void FrameComponent::updateTipWindows() {
    // Only in the world, with this character's [Tips] switches read.
    if (!ctx_.zoneReady || ctx_.loadingScreenVisible || !uiPrefsLoaded_) {
        tipTriggersLast_.clear();
        return;
    }
    if (shouldPopTipOfTheDay(tipPrefs_, tipState_)) {
        if (!totdLoaded_) {
            totdLoaded_ = true;
            std::string text;
            if (!ctx_.config.eqClientPath.empty() &&
                readFile(ctx_.config.eqClientPath + "/help/tips/TOTD.txt", text)) {
                totdEntries_ = parseTipOfTheDay(text);
            }
        }
        if (!totdEntries_.empty()) {
            openTipOfTheDay(tipState_, totdEntries_, uiLayout_);
        } else {
            tipState_.totdShownThisSession = true;  // nothing to show; do not retry
        }
    }
    const std::set<std::string> now = activeTipTriggers();
    for (const std::string& key : now) {
        if (tipTriggersLast_.count(key)) continue;  // not a rising edge
        if (!shouldPopTip(tipPrefs_, tipState_, key)) continue;
        if (const TipDocument* doc = tipDocument(key)) {
            // A tip already up is replaced by the newer one (ASSUMPTION:
            // the oracle's History combobox suggests it keeps the old one).
            if (tipState_.isOpen) closeTipWindow(tipState_, tipPrefs_);
            openContextTip(tipState_, key, *doc, uiLayout_);
            LOG_INFO(MOD_UI, "[TIP] {}", tipState_.title);
        }
    }
    tipTriggersLast_ = now;
}

bool FrameComponent::processTipWindowInput() {
    if (!tipState_.isOpen || !ctx_.eventReceiver) return false;
    const int mx = ctx_.eventReceiver->getMouseX();
    const int my = ctx_.eventReceiver->getMouseY();
    const TipHit hit = hitTestTipWindow(uiLayout_, tipState_, mx, my);
    tipState_.closeHovered = (hit == TipHit::Close);
    tipState_.nextHovered = (hit == TipHit::NextTip);
    if (!pendingUIClick_ || hit == TipHit::None) return false;
    return applyTipClick(tipState_, tipPrefs_, hit, totdEntries_, uiLayout_);
}

}  // namespace Graphics
}  // namespace EQT
