// eqe1_texture_index_builder — build a name -> owning-archive index for an EQE1 tree
//
// T207. Deliberately NOT folded into equipment_index_builder: that tool maps
// equipment *model IDs* to archive/actor/textures for the equipment path, and
// data/equipment_models.json has its own consumer. This one answers a different
// and narrower question — "which single archive holds the entry named X" — for
// every texture and raw-data entry in an EQE1 tree.
//
// Why it exists: ConstrainedTextureCache used to answer that question by opening
// archives one at a time until a name hit (a linear probe over the configured
// global list). A cold befallen zone-in opened 37 archives of which 24 served
// zero lookups, mapping 103MB of 149MB for nothing. With this index a lookup
// opens exactly the one archive that can satisfy it, or none at all.
//
// Usage:
//   ./eqe1_texture_index_builder --eqe1-path /path/to/EQE1 --output data/eqe1_texture_index.json
//   ./eqe1_texture_index_builder --eqe1-path /path/to/EQE1 --verbose
//
// Output shape (archive names are interned; entries index into that array so
// the file stays small enough to parse on the Pi at startup):
//   {
//     "archives": ["befallen", "global_chr", ...],
//     "entries":  { "arm01": [1, 7, 22], "befallenfloor": [0] }
//   }
//
// Each entry lists EVERY archive that holds that name, not just one. EQ packs
// the same texture name into many archives (182468 total entries collapse to
// 50447 unique names -- 72% duplication), so picking a single winner would
// resolve e.g. "arm01" to whichever archive sorts first alphabetically and open
// an unrelated zone's file. The runtime resolver picks from this list using the
// current zone's candidate set, preferring archives already open.

#include "client/graphics/eqe1_loader.h"

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using EQT::Graphics::EQE1Loader;

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

int main(int argc, char** argv) {
    std::string eqe1Path;
    std::string outputPath = "data/eqe1_texture_index.json";
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--eqe1-path") == 0 && i + 1 < argc) {
            eqe1Path = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else {
            fprintf(stderr,
                    "Usage: %s --eqe1-path <dir> [--output <file>] [--verbose]\n", argv[0]);
            return 1;
        }
    }

    if (eqe1Path.empty()) {
        fprintf(stderr, "ERROR: --eqe1-path is required\n");
        return 1;
    }
    if (!fs::is_directory(eqe1Path)) {
        fprintf(stderr, "ERROR: not a directory: %s\n", eqe1Path.c_str());
        return 1;
    }

    // Deterministic archive order so the output is reproducible and diffable.
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(eqe1Path)) {
        if (e.is_regular_file() && e.path().extension() == ".eqe1") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());

    std::vector<std::string> archives;
    std::map<std::string, std::vector<size_t>> entries;  // ordered => stable JSON
    size_t collisions = 0, failed = 0, totalEntries = 0;

    for (const auto& f : files) {
        EQE1Loader loader;
        if (!loader.open(f.string())) {
            fprintf(stderr, "WARN: cannot open %s\n", f.string().c_str());
            ++failed;
            continue;
        }
        const std::string base = f.stem().string();
        const size_t archiveIdx = archives.size();
        archives.push_back(base);

        size_t added = 0;
        auto record = [&](const std::string& raw) {
            ++totalEntries;
            // Match the runtime lookup key: ConstrainedTextureCache strips a
            // .bmp/.dds extension and lowercases before calling into the loader.
            std::string name = raw;
            if (name.size() > 4) {
                const std::string ext = name.substr(name.size() - 4);
                if (ext == ".bmp" || ext == ".dds") name = name.substr(0, name.size() - 4);
            }
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });

            auto& owners = entries[name];
            if (!owners.empty()) ++collisions;
            // Sorted archive iteration means owners is already ascending, and a
            // name cannot repeat within one archive's two name lists in a way
            // that matters -- but guard anyway so the list stays a clean set.
            if (owners.empty() || owners.back() != archiveIdx) {
                owners.push_back(archiveIdx);
                ++added;
            }
        };

        for (const auto& n : loader.getTextureNames()) record(n);
        for (const auto& n : loader.getRawDataNames()) record(n);

        if (verbose) {
            printf("%-24s %6zu new entries\n", base.c_str(), added);
        }
    }

    std::ofstream out(outputPath);
    if (!out) {
        fprintf(stderr, "ERROR: cannot write %s\n", outputPath.c_str());
        return 1;
    }
    out << "{\n  \"archives\": [";
    for (size_t i = 0; i < archives.size(); ++i) {
        if (i) out << ", ";
        out << "\"" << jsonEscape(archives[i]) << "\"";
    }
    out << "],\n  \"entries\": {\n";
    bool first = true;
    for (const auto& [name, owners] : entries) {
        if (!first) out << ",\n";
        first = false;
        out << "    \"" << jsonEscape(name) << "\": [";
        for (size_t i = 0; i < owners.size(); ++i) {
            if (i) out << ",";
            out << owners[i];
        }
        out << "]";
    }
    out << "\n  }\n}\n";
    out.close();

    size_t multiOwner = 0, maxOwners = 0;
    for (const auto& [name, owners] : entries) {
        if (owners.size() > 1) ++multiOwner;
        maxOwners = std::max(maxOwners, owners.size());
    }
    printf("Wrote %s: %zu archive(s), %zu unique names from %zu entries "
           "(%zu names in >1 archive, max %zu owners, %zu collisions, "
           "%zu archive(s) unreadable)\n",
           outputPath.c_str(), archives.size(), entries.size(), totalEntries,
           multiOwner, maxOwners, collisions, failed);
    return 0;
}
