// B1088 (GitLab #408): the zone loading screen's zone name -- ONE lookup,
// ONE value, device-free.
//
// The loading screen shows "Entering <human-readable zone name>..." centred
// above the loading (stage) text and the progress bar. The name is the zone's
// LONG name, verbatim from its source (no "The" stripping -- Will's
// correction):
//   primary   data/era_zones.tsv, zone id -> long name, through the ONE parser
//             the character-select screen already uses
//             (eqt::login_world::LoadEraZoneLongNames,
//             src/client/login_world/character_select.cpp);
//   refresh   the server's OP_NewZone zone_long_name[278], which arrives for
//             EVERY zone the server can send and covers ids the era table
//             deliberately omits (tutorialb among them).
// An id neither source knows maps to the EMPTY string: the line is simply not
// drawn (never "Zone <id>", never the raw short name).
//
// The two loading-screen draw bodies (LoadingThread's -- the render thread's
// 0-50 % network phase -- and OverlaysComponent's -- the loading thread's
// loadZoneSequential and the frame tick) both read entryText() at draw time
// through the shared layout (include/client/graphics/overlays/
// loading_frame_layout.h), so they show the SAME line and cannot drift.
// refresh() runs on the GAME thread, at the moment a target zone id is known
// (the character's enter-world zone, the approved zone change, and
// OP_NewZone's own name); it logs the INFO line the scenario arm greps for.
// The era table is small, read-only after parse, and is re-parsed at most
// once per lookup (a file read; the table is tiny).

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace eqt {

class ZoneDisplayNames {
public:
    static ZoneDisplayNames& instance();

    /** data/era_zones.tsv, zone id -> long name (empty if the file is missing). */
    std::map<uint32_t, std::string> eraNames() const;

    /**
     * Store the long name `longName` as the override for `zoneId`. Empty
     * clears any override for that zone (the era table then answers). Called
     * by OP_NewZone with the bounded zone_long_name read, and by refresh()
     * with the era table's own name -- so a later OP_NewZone is a REFRESH of
     * the same slot, not a second source.
     */
    void setOverride(uint32_t zoneId, std::string longName);

    /** era id -> name; the non-empty override wins; unknown -> "" (line not drawn). */
    std::string lookup(uint32_t zoneId) const;

    /** "Entering <name>..." when `name` is non-empty, "" otherwise. */
    static std::string entryLine(const std::string& name);

    /**
     * The line the loading screen is CURRENTLY showing: the entry line of
     * the last zone whose name was made known, "" until then / when that
     * zone has no name. Written by refresh()/noteNewZone() (game thread),
     * read at draw time by the render and loading threads -- mutex-guarded
     * both ways.
     */
    std::string entryText() const;

    /**
     * The one refresh point (game thread), called the moment a target zone
     * id is known (the character's enter-world zone, an approved zone
     * change): look up the era table's name, park it as that zone's
     * override, make "Entering <name>..." the shown line ("" when the
     * zone has no name), and log that line at INFO (the scenario arm's
     * assertion). Returns the shown line.
     */
    std::string refresh(uint32_t zoneId);

    /**
     * The OP_NewZone refresh: store the server's own zone_long_name as the
     * override for `zoneId` (authoritative; the only source that knows the
     * ids the era table omits), and re-derive the shown line when this IS
     * the zone being entered. Logs at INFO when the line changes (e.g. the
     * era table had no row and the server now says "Tutorialb").
     */
    void noteNewZone(uint32_t zoneId, const std::string& longName);

    /**
     * "Zone ready" clear: forget the shown zone so a stale name never shows
     * for the NEXT load before its id is known. The next refresh()/
     * noteNewZone() re-derives the line (and re-logs it). Called when the
     * loading screen is hidden (the renderer's render thread -- the singleton
     * is mutex-guarded for exactly this).
     */
    void clearEntry();

private:
    ZoneDisplayNames() = default;

    /** entry_ recomputed for shownZoneId_ ("" when it has no name). */
    void recomputeShown();   // acquires its own locks; safe from any thread

    mutable std::mutex mutex_;
    std::map<uint32_t, std::string> overrides_;
    std::atomic<uint32_t> shownZoneId_{0};
    std::string entry_;   // the loading screen's current "Entering ..."/"" line
};

}  // namespace eqt
