# T369 — character-archive path derived from the FILENAME, not the whole path (B105)

Class B, slot3, one commit. Read the B105 board text (T366 measured it).

`S3DLoader::loadCharacters` (`src/client/graphics/eq/s3d_loader.cpp`)
derives `<zone>_chr.s3d` by `find("_chr")` over the WHOLE archive path, so
a directory component containing `_chr` truncates the path and every
character load returns zero — silently, because the search path caches
"no characters" as a negative entry.

1. Derive from the filename component only (`std::filesystem::path`:
   parent_path / stem), and match `_chr` as a SUFFIX of the stem, not the
   first occurrence anywhere (a zone named `x_chr_y` is not a thing, but a
   stem ending in `_chr` is exactly the case). Keep the behaviour for the
   normal case byte-identical (same string out for every archive under
   `/mnt/projects/EverQuestP1999` — assert it in the test over the real
   directory listing).
2. The silence: a zero-character result from a `_chr` archive that EXISTS
   on disk is a defect signal, not a negative-cache entry — log it at WARN
   with the path once (find where `searchZoneChrFilesForModel` files the
   negative entry and add the log there; do not change the caching).
3. Tests, pre-fix failing: (a) an archive under a directory named
   `/tmp/.../foo_chr_bar/` resolves to `<that dir>/<zone>_chr.s3d`; (b) the
   real-directory identity assertion above (passes both sides — a pin);
   (c) the WARN line appears for an existing-but-empty `_chr` archive
   (T366's junk-archive fixture in `tests/test_chr_cache_lru.cpp` shows
   how).
4. Strike B105.

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, file sizes;
`src/client/graphics` changes → visual gate standalone + ARM.

STOP: none expected.
