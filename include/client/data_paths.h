/*
 * WillEQ shipped-data path resolution (T397, board B221).
 *
 * Every shipped data file the client reads -- config/spell_effects.json,
 * config/race_models.json, data/willeq.db, data/textures/*.bmp, and ~40 more
 * (the census is in refactor/T397_spell_effects_config_path.md and board
 * B235) -- is named by a literal that is RELATIVE TO THE CURRENT WORKING
 * DIRECTORY. That works only when the client is started from the repo root,
 * and there is no other resolution anywhere in the tree: `willeq.json` itself
 * defaults to a bare CWD-relative name, so there was nothing to reuse.
 *
 * B221 is the case that made it a defect rather than an inconvenience:
 * ParticleManager::init() loads config/spell_effects.json, and when the load
 * misses, SpellEffectsConfig::load() falls back to its BUILT-IN style table --
 * a different effect, not a degraded one (the shipped `impact` burst is 36
 * particles, the built-in is 12; with no load call at all the presets get
 * EmitterConfig's own struct defaults, a permanent 10/s emitter). The load
 * returned true either way and logged at WARN, so nothing said so.
 *
 * ResolveDataPath() is that one resolution. Its FIRST candidate is the
 * CWD-relative path itself, so a client started from the repo root -- the
 * visual gate, every script, every developer run -- reads exactly the file it
 * read before this existed. Only when that misses does it look further.
 */
#pragma once

#include <string>

namespace eqt {

/**
 * Resolve a repo-relative shipped-data path (e.g. "config/spell_effects.json").
 *
 * Search order, first existing file wins:
 *   1. the path itself if it is absolute;
 *   2. the path as given, i.e. relative to the current working directory
 *      (this is the pre-T397 behaviour and is deliberately first);
 *   3. $WILLEQ_DATA_ROOT/<relative>, if that variable is set and non-empty;
 *   4. the directory holding the running executable, then each of its
 *      parents up to kMaxParentWalk levels -- which finds <repo>/config/...
 *      from <repo>/build/bin/willeq, <repo>/build-gles2/bin/willeq and
 *      <repo>/build-arm-noble/bin/willeq alike.
 *
 * If nothing exists, the input is returned unchanged so the caller's error
 * message names the path the caller asked for.
 */
std::string ResolveDataPath(const std::string& relative);

/*
 * ---------------------------------------------------------------------------
 * GENERATED CACHES (T402, board B248) -- a different rule from shipped data.
 *
 * config/snd_index_cache.json and config/gfx_index_cache.json are not shipped
 * content: they are derived indices keyed on one absolute EQ-data path, and
 * the client REWRITES them on every run whose scan misses. While they lived
 * under config/ they were git-tracked, so a plain client run left the working
 * tree dirty and a careless `git add -A` committed one machine's index.
 *
 * They are caches: a miss just means "rescan", which costs ~0.4 s for the 17
 * sound archives and ~30 ms for the character archives (measured, T402). So
 * the write side moves out of the checkout entirely, to the first WRITABLE of:
 *
 *   1. $WILLEQ_CACHE_DIR
 *   2. $WILLEQ_DATA_ROOT/cache        (the staged-install data root)
 *   3. <directory of the running executable>/cache   (build/bin/cache, ...)
 *   4. cache/                          (CWD-relative last resort)
 *
 * The read side takes the first candidate where the file EXISTS, so a
 * read-only staged cache still serves a run that cannot write one.
 * ---------------------------------------------------------------------------
 */

/**
 * Path to read a generated cache from: the first candidate directory that
 * already holds `name`. Returns "" when no candidate has it -- the caller
 * treats that as a cache miss and rescans.
 */
std::string ResolveCachePathForRead(const std::string& name);

/**
 * Path to write a generated cache to: the first candidate directory that is
 * writable, created if need be. Returns "" when none is -- the caller then
 * skips saving (a cache it cannot write is not an error).
 */
std::string ResolveCachePathForWrite(const std::string& name);

/** Directory of the running executable, or "" if it cannot be determined. */
std::string ExecutableDirectory();

}  // namespace eqt
