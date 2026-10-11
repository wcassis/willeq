// B733 selection-identity census: for every character in one archive, compare
// (a) what the OLD name-based selection draws (parts named {CODE}_DMSPRITEDEF
// or {CODE}HE00_DMSPRITEDEF; all parts if none match) with (b) what the NEW
// rule draws (the character's stored part set, i.e. every mesh the skeleton
// references after the B733 loader fix). Prints a DIFFER line for any
// character whose drawn part set changes.
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <set>
#include <string>

using namespace EQT::Graphics;

static std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: " << argv[0] << " <archive>\n"; return 1; }
    S3DLoader loader;
    if (!loader.loadZone(argv[1])) { std::cerr << "loadZone failed\n"; return 1; }
    auto zone = loader.getZone();
    if (!zone) return 1;

    for (const auto& ch : zone->characters) {
        if (!ch || !ch->skeleton) continue;
        // character code as the builders derive it: before "_HS_DEF", else first 3
        std::string code = upper(ch->skeleton->name);
        size_t p = code.find("_HS_DEF");
        if (p != std::string::npos) code = code.substr(0, p);
        else code = code.size() >= 3 ? code.substr(0, 3) : code;
        std::set<std::string> oldSet, newSet;
        bool anyMatch = false;
        for (const auto& pt : ch->partsWithTransforms) {
            if (!pt.geometry) continue;
            std::string n = upper(pt.geometry->name);
            newSet.insert(n);
            if (n == code + "_DMSPRITEDEF" || n == code + "HE00_DMSPRITEDEF") {
                oldSet.insert(n);
                anyMatch = true;
            }
        }
        if (!anyMatch) oldSet = newSet;   // the builder's "use all parts" fallback
        if (oldSet != newSet) {
            std::cout << "DIFFER " << ch->skeleton->name << " code=" << code;
            std::cout << " old=[";
            for (auto& n : oldSet) std::cout << n << ",";
            std::cout << "] new=[";
            for (auto& n : newSet) std::cout << n << ",";
            std::cout << "]\n";
        }
    }
    return 0;
}
