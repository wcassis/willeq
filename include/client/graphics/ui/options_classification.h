/*
 * Every Options-window control row, classified (B430/B431/B433).
 *
 * options_layout.h holds the geometry of every control EQUI_OptionsWindow.xml
 * declares; this is the matching DECISION for each one -- wired, or excluded
 * with the reason it stays greyed. A row may not be silently skipped:
 * OptionsClassification.EveryControlRowIsClassifiedExactlyOnce fails if a
 * kControls row has no entry (or two), and
 * ...WiredIffItHasASetting fails if the table and the layout's `setting`
 * disagree.
 *
 * Device-free.
 */

#pragma once

#include <cstdint>

namespace EQT {
namespace Graphics {
namespace OptionsLayout {

enum class Disposition : uint8_t {
    Wired,           // has a setting behind it (a caption or readout of one too)
    PostVelious,     // a post-Velious feature: IGNORED by Will's era ruling
    WillOut,         // Will's 2026-09-23 ruling on the `?` rows (#144): OUT
    NoCapability,    // the capability does not exist in fable; `reason` names the owning issue
    AwaitingOracle,  // what the control does cannot be read from the oracle files
};

struct ControlClass {
    const char* xmlItem;
    Disposition disposition;
    const char* reason;
};

/** The classification table and its length. */
const ControlClass* controlClassifications();
int controlClassificationCount();

/** The row for `xmlItem`, or nullptr. */
const ControlClass* findControlClass(const char* xmlItem);

/** A short label for a disposition ("WIRED", "POST-VELIOUS", ...). */
const char* dispositionText(Disposition d);

}  // namespace OptionsLayout
}  // namespace Graphics
}  // namespace EQT
