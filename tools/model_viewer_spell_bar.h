// Model Viewer Spell Bar - UI component for spell casting visualization
// Part of the standalone model viewer tool
//
// T385 (2026-09-05, board B167): this header was 1,965 lines of enums,
// parsers, UI widgets and a particle driver in one file. It is now an
// umbrella over the three headers below, so existing includes keep working
// and each part is independently readable. Nothing was rewritten -- the
// declarations are verbatim.

#pragma once

#include "model_viewer/spell_effect_db.h"
#include "model_viewer/spell_bar_ui.h"
#include "model_viewer/model_viewer_fx.h"
