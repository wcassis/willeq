#pragma once

// B888 (GitLab #204): the character-creation ERA DATA -- data/char_create.tsv,
// parsed. The file's header is the documentation of record (columns, sources,
// which rows are measured and which are assumptions).
//
// Will's era ruling (2026-09-23) is applied HERE and nowhere else: an entry
// whose era is not Classic, Kunark or Velious is dropped from the in-era lists
// (`visibleRaces()` etc.), so the screen cannot draw it, a click cannot select
// it and the scripted path cannot send it. The full rows stay readable
// (`allRaces()`) so a test can prove the post-Velious ones are present in the
// oracle and absent from what is offered.

#include <cstdint>
#include <string>
#include <vector>

namespace eqt {
namespace charcreate {

struct EraEntry {
    int xmlIndex = -1;       // CC0_Race<n> / CC0_Class<n> / CC1_Deity<n> / CC1_City<n>
    uint32_t id = 0;         // wire id; for a city, the StartZoneIndex (== xmlIndex)
    std::string label;       // the XML <Text>, verbatim
    std::string era;         // Classic | Kunark | Velious | Luclin | LoY | GoD
    uint32_t eqstrId = 0;    // description pane text (0 = none)
    std::vector<std::string> homeZones;  // cities only
};

/** True for Classic, Kunark and Velious -- the eras Will ruled in. */
bool isInEra(const std::string& era);

class CharCreateData {
public:
    /** Parse the file at `path` (already resolved). False + error() on failure. */
    bool loadFile(const std::string& path);
    /** Parse TSV text (tests). */
    bool loadText(const std::string& text);
    /** data/char_create.tsv through eqt::ResolveDataPath(). */
    bool loadShipped();
    const std::string& error() const { return error_; }

    const std::vector<EraEntry>& allRaces() const { return races_; }
    const std::vector<EraEntry>& allClasses() const { return classes_; }
    const std::vector<EraEntry>& allDeities() const { return deities_; }
    const std::vector<EraEntry>& allCities() const { return cities_; }

    /** In-era entries in BUTTON ORDER (xmlIndex order is not label order:
     *  CC0_Race14 Froglok sits between Erudite and Gnome on the screen). */
    std::vector<EraEntry> visibleRaces() const;
    std::vector<EraEntry> visibleClasses() const;
    std::vector<EraEntry> visibleDeities() const;
    std::vector<EraEntry> visibleCities() const;

    const EraEntry* raceById(uint32_t id) const;
    const EraEntry* classById(uint32_t id) const;
    const EraEntry* deityById(uint32_t id) const;
    const EraEntry* cityByIndex(uint32_t startZone) const;
    /** Case-insensitive, spaces ignored: "darkelf" == "Dark Elf". In-era only. */
    const EraEntry* raceByName(const std::string& name) const;
    const EraEntry* classByName(const std::string& name) const;
    /** Matches the label ("THE FACELESS") or, without "THE ", ("FACELESS"). */
    const EraEntry* deityByName(const std::string& name) const;
    const EraEntry* cityByName(const std::string& name) const;

    /** The deities the screen offers (enables) for race+class. In-era only. */
    std::vector<uint32_t> allowedDeities(uint32_t race, uint32_t cls) const;
    /** The cities it offers for race+class+deity, as StartZoneIndex values. */
    std::vector<uint32_t> allowedCities(uint32_t race, uint32_t cls, uint32_t deity) const;
    /** The `source` column of the row allowedCities() used (tests, README). */
    std::string citySource(uint32_t race, uint32_t cls, uint32_t deity) const;

private:
    struct FilterRow {
        std::string race, cls, deity;   // labels or "*"
        std::vector<std::string> values;
        std::string source;
    };
    const FilterRow* bestRow(const std::vector<FilterRow>& rows, uint32_t race, uint32_t cls,
                             uint32_t deity, bool useDeity) const;

    std::vector<EraEntry> races_, classes_, deities_, cities_;
    std::vector<FilterRow> deityRows_, cityRows_;
    std::string error_;
};

}  // namespace charcreate
}  // namespace eqt
