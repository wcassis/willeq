#pragma once

// T395 Leg B (board B205): server chat text can carry EQ LINKS, and fable was
// printing their wire body as literal characters.
//
// MEASURED T386 (review/T386/logs/fable_session_selfcast.log:1041,1879 -- the
// bytes, seen through `cat -A`, are `^R` = 0x12):
//
//   Your spell \x120FFFFF000000004F00000000000000000000000000000Burst of Flame\x12
//   has failed to land on yourself because you are invulnerable.
//
// The producer is eqemu's Mob::SpellOnTarget (zone/spells.cpp), which wraps
// the spell name in Saylink::Silent("#castspell <id>", <name>) --
// EQ::SayLinkEngine::GenerateLink() in common/say_link.cpp. A link is
//
//   0x12  <fixed-width hex body>  <display text>  0x12
//
// and the body's width is client-version specific; the Titanium/"6.2" layout
// this client speaks is 45 characters:
//
//   %1X  action_id      (1)
//   %05X item_id        (5)   FFFFF = SAYLINK_ITEM_ID, the saylink proxy item
//   %05X augment_1      (5)   a NON-silent saylink's row id
//   %05X augment_2      (5)   a SILENT saylink's row id
//   %05X augment_3..5  (15)
//   %1X  is_evolving    (1)
//   %04X evolve_group   (4)
//   %1X  evolve_level   (1)
//   %08X hash           (8)
//
// So the two measured blobs decode as item_id 0xFFFFF with augment_2 = 0x4F
// (79) and 0x50 (80): consecutive `saylink` table row ids for `#castspell 93`
// and `#castspell 54`. They are not spell ids and carry nothing the player
// needs -- the official client renders only the display text, as a clickable
// name. fable has no link UI, so it renders the display text plainly.
//
// (The board's transcription of the second blob is one character short: both
// are 45 characters on the wire.)

#include <string>

namespace eqt {

// The Titanium ("6.2") say-link body width, in characters, between the two
// 0x12 delimiters and before the display text.
inline constexpr size_t kTitaniumSayLinkBodyChars = 45;

// The link delimiter eqemu emits around a link body + text.
inline constexpr char kSayLinkDelimiter = '\x12';

// Returns `text` with every well-formed 0x12-delimited link replaced by its
// display text. Text with no delimiter is returned unchanged (this is the
// overwhelmingly common case and costs one scan). An unpaired delimiter, or a
// link whose payload is shorter than the body width, is left exactly as it
// arrived rather than guessed at -- a mangled line is better evidence than a
// silently truncated one.
std::string StripSayLinks(const std::string& text);

}  // namespace eqt
