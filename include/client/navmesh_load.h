#pragma once

// B941 (GitLab #259): the two pieces of PathfinderNavmesh::Load that decide
// what the navmesh costs in memory.
//
//  * NavmeshStreamReader inflates a server `EQNAVMESH` v2 file on demand, so
//    each tile is inflated straight into its own Detour allocation. The old
//    load read the whole compressed file, inflated the whole payload, then
//    copied every tile out of it: three copies of the data alive at the peak.
//
//  * TrimUnusedNavmeshLinks() gives back each tile's unused dtLink capacity
//    once every tile is loaded and connected. Detour sizes a tile's link
//    array for the worst case (`maxLinkCount`: every edge plus every portal
//    twice) and hands links out from a free list IN INDEX ORDER; tiles are
//    never removed, so after the last addTile() the used links are exactly
//    the prefix [0, used) and the free list is the tail. The tail is cut out
//    of the tile's data blob and every section after it moved down. No link
//    index changes, so every poly walks the same links in the same order and
//    every query result is bit-identical (tests/test_game_state_identity.cpp).
//    A tile whose free list is NOT a clean tail, or whose sections are not
//    where Detour's own layout puts them, is left untouched.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class dtNavMesh;

class NavmeshStreamReader {
public:
    NavmeshStreamReader();
    ~NavmeshStreamReader();
    NavmeshStreamReader(const NavmeshStreamReader&) = delete;
    NavmeshStreamReader& operator=(const NavmeshStreamReader&) = delete;

    // Opens `path`, checks the "EQNAVMESH" magic and version 2, reads the
    // compressed/inflated sizes and starts the inflate. False on any mismatch.
    bool open(const std::string& path);
    // Inflates exactly `n` bytes into `dst`. False if the stream ends early,
    // is corrupt, or would pass the inflated size the file header declares.
    bool read(void* dst, size_t n);

    uint32_t compressedBytes() const { return compressedSize_; }

private:
    bool refill();

    FILE* f_ = nullptr;
    void* zs_ = nullptr;             // z_stream, kept opaque so zlib.h stays out of the header
    bool zInit_ = false;
    bool ended_ = false;
    uint32_t compressedSize_ = 0;
    uint32_t inflatedSize_ = 0;
    uint32_t compressedLeft_ = 0;
    uint64_t produced_ = 0;
    std::vector<unsigned char> in_;
};

struct NavmeshLinkTrim {
    uint64_t linksUsed = 0;
    uint64_t linksCapacity = 0;   // before the trim
    uint32_t tilesTrimmed = 0;
    uint32_t tilesSkipped = 0;    // free list not a clean tail, or unexpected layout
    uint64_t bytesFreed = 0;      // tile data bytes given back
};

NavmeshLinkTrim TrimUnusedNavmeshLinks(dtNavMesh* nav);
