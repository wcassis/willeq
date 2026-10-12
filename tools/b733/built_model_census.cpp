// B733 census (built-model side): for one archive, print for every character
// the production S3DLoader stores: "CHAR <name> parts=<p> pts=<t> raw=<r>
// names=<n1,n2>". Run against the HEAD build and the fixed build and diff.
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include <iostream>
#include <string>

using namespace EQT::Graphics;

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: " << argv[0] << " <archive>\n"; return 1; }
    S3DLoader loader;
    if (!loader.loadZone(argv[1])) { std::cerr << "loadZone failed\n"; return 1; }
    auto zone = loader.getZone();
    if (!zone) return 1;
    for (const auto& ch : zone->characters) {
        if (!ch) continue;
        std::string s = "CHAR " + ch->name + " parts=" + std::to_string(ch->parts.size()) +
                        " pts=" + std::to_string(ch->partsWithTransforms.size()) +
                        " raw=" + std::to_string(ch->rawParts.size()) + " names=";
        for (const auto& pt : ch->partsWithTransforms)
            if (pt.geometry) s += pt.geometry->name + ",";
        std::cout << s << "\n";
    }
    return 0;
}
