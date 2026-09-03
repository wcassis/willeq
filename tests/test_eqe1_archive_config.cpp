// T223 (T198 finding): config/eqe1_archives.json listed "gequip7" in
// globalArchives, but no gequip7.s3d exists in the client (gequip, 2-6, 8
// do), so every zone load logged "cannot open gequip7.eqe1". Pins that
// every configured global archive exists as an .eqe1 pack in the EQE1 asset
// dir -- the file the loader actually opens (EQE1 is all-or-nothing, no S3D
// fallback; "textures" exists ONLY as an .eqe1, so the .s3d dir is the wrong
// oracle) -- and that the built-in fallback list in eqe1_archive_component.cpp (a
// copy of the config) does not carry the phantom either.

#include <gtest/gtest.h>
#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {
const char* kEqe1Dir = "/mnt/projects/EverQuest_ETC1";
}

TEST(Eqe1ArchiveConfigTest, EveryGlobalArchiveExistsAsEqe1Pack) {
    std::error_code ec;
    if (!fs::is_directory(kEqe1Dir, ec)) {
        GTEST_SKIP() << "EQE1 asset dir not available: " << kEqe1Dir;
    }
    std::ifstream in(EQE1_ARCHIVES_JSON);
    ASSERT_TRUE(in.is_open()) << EQE1_ARCHIVES_JSON;
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    ASSERT_TRUE(Json::parseFromStream(builder, in, &root, &errors)) << errors;
    const Json::Value& globals = root["default"]["globalArchives"];
    ASSERT_TRUE(globals.isArray());
    ASSERT_GT(globals.size(), 0u);
    for (const auto& v : globals) {
        const std::string name = v.asString();
        const fs::path pack = fs::path(kEqe1Dir) / (name + ".eqe1");
        EXPECT_TRUE(fs::exists(pack, ec)) << "configured global archive has no EQE1 pack: " << pack;
    }
}

TEST(Eqe1ArchiveConfigTest, BuiltInFallbackListHasNoPhantomGequip7) {
    std::ifstream in(RENDERER_EFFECTS_SOURCE);
    ASSERT_TRUE(in.is_open()) << RENDERER_EFFECTS_SOURCE;
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string src = buf.str();
    EXPECT_EQ(src.find("\"gequip7\""), std::string::npos)
        << "eqe1_archive_component.cpp's built-in EQE1 archive fallback still lists gequip7";
}
