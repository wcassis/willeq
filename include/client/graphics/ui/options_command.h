/*
 * `/option` and `/filter` -- the slash-command half of the Options window
 * (B430/B433). Device-free grammar over OptionsPanelState; the renderer's
 * FrameComponent::runOptionsCommand() applies whatever it changed through the
 * same applyOptionsSetting() a click goes through, so a command and a click
 * write one model.
 *
 *   /option                    list every wired setting and its value
 *   /option list               the same
 *   /option <name>             print one
 *   /option <name> <value>     set one (on/off, a step, or a choice's text or
 *                              value: `/option bardsongs showmineonly`)
 *   /option textcolor ...      B993: the Colors page's [TextColors] table --
 *                              `reset`, `<1-86|row name>` to print a row,
 *                              `<1-86|row name> <r> <g> <b>` (0-255) to set it
 *   /filter                    eqstr_us.txt 1354-1360, the era usage text
 *   /filter <name>             flip that filter between Show and Hide and
 *                              reply eqstr 1351 "%1 *ON*" (now hidden) or
 *                              1348 "%1 *OFF*" (now shown)
 *
 * Names are the settings table's (options_settings.cpp): the filters use the
 * era `/filter` vocabulary (badword, guildsay, social, gsay, shout, auction,
 * ooc, mymisses, othersmisses, otherhits, atkmissesme, pcspells, npcspells,
 * bardsongs, dmgshield, critspells, critmelee).
 */

#pragma once

#include <string>
#include <vector>

#include "client/graphics/ui/options_settings.h"

namespace EQT {
namespace Graphics {

struct OptionsPanelState;

struct OptionsCommandResult {
    std::vector<std::string> lines;
    std::vector<OptionsLayout::Setting> changed;
};

/** `cmd` is "/option", "/filter" or (B1165) "/showspelleffects"
 *  (case-insensitive, slash optional). */
OptionsCommandResult RunOptionsCommand(OptionsPanelState& state,
                                       const std::string& cmd,
                                       const std::string& args);

/** A setting's value as `/option` prints it. */
std::string OptionsValueText(const OptionsPanelState& state,
                             OptionsLayout::Setting setting);

}  // namespace Graphics
}  // namespace EQT
