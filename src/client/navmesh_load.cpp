// B941 (GitLab #259): streaming navmesh inflate + the unused-link trim.
// See include/client/navmesh_load.h for why each is safe.

#include "client/navmesh_load.h"

#include <cstring>
#include <zlib.h>
#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>

namespace {
constexpr size_t kReadChunk = 64 * 1024;
z_stream* Z(void* p) { return static_cast<z_stream*>(p); }
}

NavmeshStreamReader::NavmeshStreamReader() : zs_(new z_stream()) {}

NavmeshStreamReader::~NavmeshStreamReader() {
    if (zInit_) inflateEnd(Z(zs_));
    delete Z(zs_);
    if (f_) fclose(f_);
}

bool NavmeshStreamReader::open(const std::string& path) {
    f_ = fopen(path.c_str(), "rb");
    if (!f_) return false;
    char magic[9] = {0};
    uint32_t version = 0;
    if (fread(magic, 9, 1, f_) != 1 || std::strncmp(magic, "EQNAVMESH", 9) != 0) return false;
    if (fread(&version, sizeof(version), 1, f_) != 1 || version != 2) return false;
    if (fread(&compressedSize_, sizeof(compressedSize_), 1, f_) != 1) return false;
    if (fread(&inflatedSize_, sizeof(inflatedSize_), 1, f_) != 1) return false;
    compressedLeft_ = compressedSize_;
    z_stream* zs = Z(zs_);
    std::memset(zs, 0, sizeof(*zs));
    // Same stream format as EQ::InflateData (zlib header, 15-bit window).
    if (inflateInit2(zs, 15) != Z_OK) return false;
    zInit_ = true;
    in_.resize(kReadChunk);
    return true;
}

bool NavmeshStreamReader::refill() {
    if (compressedLeft_ == 0) return false;
    const size_t want = compressedLeft_ < kReadChunk ? compressedLeft_ : kReadChunk;
    if (fread(in_.data(), want, 1, f_) != 1) return false;
    compressedLeft_ -= static_cast<uint32_t>(want);
    z_stream* zs = Z(zs_);
    zs->next_in = in_.data();
    zs->avail_in = static_cast<uInt>(want);
    return true;
}

bool NavmeshStreamReader::read(void* dst, size_t n) {
    if (!zInit_) return false;
    if (produced_ + n > inflatedSize_) return false;  // past what the header declared
    z_stream* zs = Z(zs_);
    zs->next_out = static_cast<Bytef*>(dst);
    zs->avail_out = static_cast<uInt>(n);
    while (zs->avail_out > 0) {
        if (ended_) return false;
        if (zs->avail_in == 0 && !refill()) return false;
        const int rc = inflate(zs, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) {
            ended_ = true;
        } else if (rc != Z_OK) {
            return false;
        }
    }
    produced_ += n;
    return true;
}

NavmeshLinkTrim TrimUnusedNavmeshLinks(dtNavMesh* nav) {
    NavmeshLinkTrim r;
    if (!nav) return r;
    const dtNavMesh* cnav = nav;
    for (int ti = 0; ti < cnav->getMaxTiles(); ++ti) {
        // getTile() is Detour's public const accessor; the tile is the
        // navmesh's own (non-const) storage.
        dtMeshTile* tile = const_cast<dtMeshTile*>(cnav->getTile(ti));
        if (!tile || !tile->header || !tile->data) continue;
        dtMeshHeader* h = tile->header;
        const unsigned int maxLinks = static_cast<unsigned int>(h->maxLinkCount);
        r.linksCapacity += maxLinks;

        // The free list must be exactly used, used+1, ..., maxLinks-1.
        const unsigned int used = tile->linksFreeList == DT_NULL_LINK ? maxLinks : tile->linksFreeList;
        bool cleanTail = used <= maxLinks && (tile->flags & DT_TILE_FREE_DATA) != 0;
        unsigned int expect = used;
        unsigned int cur = tile->linksFreeList;
        while (cleanTail && cur != DT_NULL_LINK) {
            if (cur != expect || cur >= maxLinks) {
                cleanTail = false;
                break;
            }
            cur = tile->links[cur].next;
            ++expect;
        }
        if (cleanTail && expect != maxLinks) cleanTail = false;

        // Detour's own tile layout (dtNavMesh::addTile): header, verts, polys,
        // links, detail meshes, detail verts, detail tris, BV tree, off-mesh.
        const int headerSize = dtAlign4(sizeof(dtMeshHeader));
        const int vertsSize = dtAlign4(sizeof(float) * 3 * h->vertCount);
        const int polysSize = dtAlign4(sizeof(dtPoly) * h->polyCount);
        const int linksSize = dtAlign4(sizeof(dtLink) * h->maxLinkCount);
        const int newLinksSize = dtAlign4(sizeof(dtLink) * static_cast<int>(used));
        const int linksOff = headerSize + vertsSize + polysSize;
        const bool layoutOk = reinterpret_cast<unsigned char*>(tile->links) == tile->data + linksOff &&
                              reinterpret_cast<unsigned char*>(tile->detailMeshes) == tile->data + linksOff + linksSize &&
                              linksOff + linksSize <= tile->dataSize;
        if (!cleanTail || !layoutOk) {
            r.linksUsed += maxLinks;  // unknown: counted as all used
            ++r.tilesSkipped;
            continue;
        }
        r.linksUsed += used;
        const int delta = linksSize - newLinksSize;
        if (delta <= 0) continue;

        const int newSize = tile->dataSize - delta;
        unsigned char* nd = static_cast<unsigned char*>(dtAlloc(newSize, DT_ALLOC_PERM));
        if (!nd) { ++r.tilesSkipped; continue; }
        unsigned char* od = tile->data;
        const int tailOff = linksOff + linksSize;
        std::memcpy(nd, od, static_cast<size_t>(linksOff + newLinksSize));
        std::memcpy(nd + linksOff + newLinksSize, od + tailOff, static_cast<size_t>(tile->dataSize - tailOff));

        auto rebase = [&](auto* p) -> decltype(p) {
            if (!p) return p;
            const long off = reinterpret_cast<unsigned char*>(p) - od;
            return reinterpret_cast<decltype(p)>(nd + (off >= tailOff ? off - delta : off));
        };
        tile->header = reinterpret_cast<dtMeshHeader*>(nd);
        tile->verts = rebase(tile->verts);
        tile->polys = rebase(tile->polys);
        tile->links = reinterpret_cast<dtLink*>(nd + linksOff);
        tile->detailMeshes = rebase(tile->detailMeshes);
        tile->detailVerts = rebase(tile->detailVerts);
        tile->detailTris = rebase(tile->detailTris);
        tile->bvTree = rebase(tile->bvTree);
        tile->offMeshCons = rebase(tile->offMeshCons);
        tile->header->maxLinkCount = static_cast<int>(used);
        tile->linksFreeList = DT_NULL_LINK;
        tile->data = nd;
        tile->dataSize = newSize;  // DT_TILE_FREE_DATA stays set: Detour frees the new blob
        dtFree(od);
        ++r.tilesTrimmed;
        r.bytesFreed += static_cast<uint64_t>(delta);
    }
    return r;
}
