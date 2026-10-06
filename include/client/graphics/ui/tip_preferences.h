#pragma once
/*
 * B834 (#202): the per-character `[Tips]` section of UI_<char>_<server>.ini,
 * device-free so ui_preferences.h can carry it. Key names are the official
 * client's own (27 real player files under /mnt/projects/EverQuestP1999:
 * `ShowTips=0`, `ShowTOTD=0`, `Inventory=0`, `Trainer=0`, ...); see
 * tip_window.h for the oracle behind each.
 */

#include <set>
#include <string>

namespace EQT {
namespace Graphics {

struct TipPreferences {
    bool showTips = true;            // [Tips] ShowTips ("Don't show any tips")
    bool showTotd = true;            // [Tips] ShowTOTD (the Tip of the Day)
    std::set<std::string> off;       // [Tips] <Key>=0 ("Don't show this tip again")
    bool operator==(const TipPreferences& o) const {
        return showTips == o.showTips && showTotd == o.showTotd && off == o.off;
    }
    bool operator!=(const TipPreferences& o) const { return !(*this == o); }
};

}  // namespace Graphics
}  // namespace EQT
