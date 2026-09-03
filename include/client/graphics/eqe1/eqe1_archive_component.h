#ifndef EQT_GRAPHICS_EQE1_EQE1_ARCHIVE_COMPONENT_H
#define EQT_GRAPHICS_EQE1_EQE1_ARCHIVE_COMPONENT_H

// Batch L (refactor/L_renderer_component_extraction.md), unit L02 -- the
// "effects" family, lifted out of class IrrlichtRenderer following the L01
// RdpComponent shape.
//
// The family name in the batch survey is misleading: 10 of the 14 bodies are
// EQE1 flat-pack archive plumbing (open/resolve/index/candidate-set/report)
// and 4 are the fire-glow icosphere renderer, which shares nothing with them
// except the TU it happened to live in. The component is named for the
// majority -- Eqe1ArchiveComponent -- and the icosphere quartet sits under
// its own banner in the same TU, where it already was.
//
// Every body in src/client/graphics/eqe1/eqe1_archive_component.cpp is the
// body that used to live in src/client/graphics/renderer_effects.cpp, moved
// verbatim. The only edits inside a body are the batch's sanctioned
// substitutions:
//   1. a RendererContext core member: camera_ -> ctx_.camera, config_ ->
//      ctx_.config, constrainedTextureCache_ -> ctx_.constrainedTextureCache,
//      currentZoneName_ -> ctx_.currentZoneName, zoneShader_ -> ctx_.zoneShader
//   2. an IrrlichtRenderer member that is shared with another family and so
//      stays on the renderer (K/L Mechanism 3): foo_ -> owner_.foo_ -- 17 of
//      them, listed in the TU banner
//   3. an IrrlichtRenderer-scoped name: IrrlichtRenderer::isPersistentEQE1Archive
//      -> isPersistentEQE1Archive (it moved here)
// The 9 icoLoc* uniform locations are single-family (measured: referenced by
// this TU and nothing else) and moved into the component, as did the
// EQE1ArchiveEntry struct and the eqe1ArchiveEntries_ map it keys.
//
// Because 17 IrrlichtRenderer private members are still reached through the
// transition handle, this component DOES need friendship -- unlike L01's
// RdpComponent (K13 correction ii, both axes).

#include "client/graphics/renderer_context.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

class EQE1Loader;
class IrrlichtRenderer;

class Eqe1ArchiveComponent {
public:
	Eqe1ArchiveComponent(RendererContext& ctx, IrrlichtRenderer& owner)
		: ctx_(ctx), owner_(owner) {}

	// EQE1 archive configuration -- loaded from config/eqe1_archives.json
	struct EQE1ArchiveEntry {
		std::vector<std::string> zoneArchives;    // entries with {zone} placeholder
		std::vector<std::string> globalArchives;  // loaded as-is
	};

	bool loadEQE1ArchiveConfig(const std::string& path);
	EQE1ArchiveEntry resolveEQE1Archives(const std::string& zoneName) const;

	// L02: renderer_init.cpp guards loadEQE1ArchiveConfig() on the entry map
	// being empty; that member moved here with the struct, so the guard reads
	// it through this accessor rather than the renderer's private state.
	const std::map<std::string, EQE1ArchiveEntry>& archiveEntries() const {
		return eqe1ArchiveEntries_;
	}

	// Try to open an EQE1 file and register with cache. Returns loader pointer or nullptr.
	// Thread-safe -- called from entity prep worker and equipment loader threads.
	const EQE1Loader* getOrOpenEQE1(const std::string& archiveBaseName);

	// T214: archives that are NOT zone assets and must survive unloadZone().
	// "willeq" is the client's own pack (UI atlas + generated textures like
	// detail_atlas); it is opened once at renderer init for the life of the
	// process. Tearing it down with the zone's archives left detail_atlas
	// unreachable, so the miss resolver opened textures.eqe1 (2.6MB) to serve
	// that ONE name -- in every zone.
	static bool isPersistentEQE1Archive(const std::string& archiveBaseName) {
		return archiveBaseName == "willeq";
	}

	// T199/T266: queue the archive race_models names for `raceId`
	// (race_models.json s3d_file, keyed by stem) as an EQE1 candidate, unless
	// it is already there. Returns true when it added one. Called for every
	// spawned race at zone load (T199) and for every race that reaches the
	// entity prep queue after load (T266).
	bool addSpawnedRaceEQE1Candidate(uint16_t raceId);

	// T207: resolve `name` to the one candidate archive that holds it and open
	// exactly that archive, or return nullptr having opened nothing. Replaces
	// tryOpenNextGlobalEQE1(), which probed the whole configured list.
	// T266: a name held by NO candidate but by some archive in the tree opens
	// that archive (rule R2, tagged `late-open` in the request log) instead of
	// returning nullptr -- which, under all-or-nothing, was a client exit for
	// any race that spawned after zone load. Only a name in NO archive still
	// returns nullptr (a real asset gap; decision #2 keeps that fatal).
	const EQE1Loader* openArchiveContaining(const std::string& name);

	// T207: at zone-in-complete, report every opened EQE1 archive with the
	// number of lookups it actually served. Archives with zero hits were opened
	// by the linear probe in tryOpenNextGlobalEQE1() and were never needed --
	// this is the before/after metric for the name->archive index fix.
	void logEQE1ArchiveUsage() const;
	// Sum of getMappedBytes() across every currently-open EQE1Loader (opened
	// zone archives + whatever global archives lazy-open has pulled in so
	// far). Used by --load-mem-audit and /pmem's accounted-vs-RSS breakdown.
	size_t getEQE1MappedBytesTotal() const;

	// Icosphere rendering (animated fire glow volumes). Defined only under
	// EQT_HAS_DRM, exactly as they were on IrrlichtRenderer -- the callers in
	// renderer_zone_load_env.cpp / renderer_frame_render.cpp carry the guard.
	void buildIcosphereMesh();
	void compileIcosphereShader();
	void renderFireGlowIcospheres();
	void updateFireGlowLighting();  // Select nearest N and set shader uniforms

private:
	// T284: getOrOpenEQE1's body, parameterised by directory. Exists so the
	// composite-sheet fallback (openCompositeSheetEQE1) can open from a
	// --composite-entities directory that is not --eqe1-path without a second
	// copy of the open/register/negative-cache logic. Keyed by stem in the
	// same eqe1Loaders_ map, so a stem is opened at most once per zone.
	// L02: in-family callers only (getOrOpenEQE1, openCompositeSheetEQE1), so
	// it is private here rather than deleted (K03 correction a).
	const EQE1Loader* getOrOpenEQE1FromDir(const std::string& archiveBaseName,
	                                       const std::string& dir);

	// T284: open the self-named composite sheet pack for `name` from a
	// composite directory the index does not cover. Returns nullptr (opening
	// nothing) when composites are off, when the composite directory is the
	// indexed eqe1Path (the shipped layout -- the index already answered), or
	// when no such pack exists.
	const EQE1Loader* openCompositeSheetEQE1(const std::string& name);

	RendererContext& ctx_;
	IrrlichtRenderer& owner_;

	std::map<std::string, EQE1ArchiveEntry> eqe1ArchiveEntries_;  // "default" + per-zone overrides

#ifdef EQT_HAS_DRM
	// Cached icosphere shader uniform locations -- the family's only other
	// single-family state.
	int32_t icoLocViewProj_ = -1;
	int32_t icoLocCenter_ = -1;
	int32_t icoLocRadius_ = -1;
	int32_t icoLocTime_ = -1;
	int32_t icoLocJitterAmt_ = -1;
	int32_t icoLocColor_ = -1;
	int32_t icoLocFogStart_ = -1;
	int32_t icoLocFogEnd_ = -1;
	int32_t icoLocFogColor_ = -1;
#endif
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_EQE1_EQE1_ARCHIVE_COMPONENT_H
