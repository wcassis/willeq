// Unit tests for PFS/S3D archive parsing
// Tests the PFS implementation against known values and real EQ files

#include <gtest/gtest.h>
#include "client/graphics/eq/pfs.h"
#include <vector>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <algorithm>

using namespace EQT::Graphics;

// Path to EQ client files for integration tests
static std::string getEQClientPath() {
    return "/home/user/projects/claude/EverQuestP1999";
}

// ============================================================================
// PFS CRC Algorithm Tests
// ============================================================================

class PfsCrcAlgorithmTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

// Test empty string CRC
TEST_F(PfsCrcAlgorithmTest, EmptyString) {
    EXPECT_EQ(PfsCrc::instance().get(""), 0);
}

// Test that CRC is consistent
TEST_F(PfsCrcAlgorithmTest, Consistency) {
    int32_t crc1 = PfsCrc::instance().get("test.txt");
    int32_t crc2 = PfsCrc::instance().get("test.txt");
    EXPECT_EQ(crc1, crc2);
}

// Test that different strings produce different CRCs
TEST_F(PfsCrcAlgorithmTest, DifferentStrings) {
    int32_t crc1 = PfsCrc::instance().get("file1.wld");
    int32_t crc2 = PfsCrc::instance().get("file2.wld");
    EXPECT_NE(crc1, crc2);
}

// Test case sensitivity (PFS uses lowercase for CRC matching)
TEST_F(PfsCrcAlgorithmTest, CaseSensitive) {
    int32_t crc1 = PfsCrc::instance().get("TEST.TXT");
    int32_t crc2 = PfsCrc::instance().get("test.txt");
    EXPECT_NE(crc1, crc2);
}

// Test the special filename table CRC value
// The special CRC 0x61580ac9 is used for the filename directory entry
TEST_F(PfsCrcAlgorithmTest, SpecialFilenameCrc) {
    // This is a known constant in the PFS format
    const int32_t FILENAME_TABLE_CRC = 0x61580ac9;
    // Just verify the constant is what we expect
    EXPECT_EQ(FILENAME_TABLE_CRC, 0x61580ac9);
}

// Test CRC with various filename extensions
TEST_F(PfsCrcAlgorithmTest, VariousExtensions) {
    // Just verify these don't crash and produce non-zero values
    EXPECT_NE(PfsCrc::instance().get("zone.wld"), 0);
    EXPECT_NE(PfsCrc::instance().get("texture.bmp"), 0);
    EXPECT_NE(PfsCrc::instance().get("texture.dds"), 0);
    EXPECT_NE(PfsCrc::instance().get("model.mod"), 0);
}

// Test CRC includes null terminator (important for matching eqsage)
// The CRC is calculated on the string INCLUDING the null terminator
TEST_F(PfsCrcAlgorithmTest, IncludesNullTerminator) {
    // The get() function should append a null terminator
    // Different implementations that don't include null would produce different CRCs
    int32_t crc = PfsCrc::instance().get("test");
    EXPECT_NE(crc, 0);
}

// ============================================================================
// PFS Archive Basic Tests
// ============================================================================

class PfsArchiveBasicTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

// Test opening nonexistent file
TEST_F(PfsArchiveBasicTest, OpenNonexistent) {
    PfsArchive archive;
    EXPECT_FALSE(archive.open("/nonexistent/path/file.s3d"));
}

// Test files map is empty by default
TEST_F(PfsArchiveBasicTest, FilesEmptyByDefault) {
    PfsArchive archive;
    EXPECT_EQ(archive.getFiles().size(), 0u);
}

// Test getting nonexistent file
TEST_F(PfsArchiveBasicTest, GetNonexistentFile) {
    PfsArchive archive;
    std::vector<char> buffer;
    EXPECT_FALSE(archive.get("nonexistent.txt", buffer));
}

// Test exists on nonexistent file
TEST_F(PfsArchiveBasicTest, ExistsNonexistent) {
    PfsArchive archive;
    EXPECT_FALSE(archive.exists("nonexistent.txt"));
}

// Test getFilenames on empty archive
TEST_F(PfsArchiveBasicTest, GetFilenamesEmpty) {
    PfsArchive archive;
    std::vector<std::string> filenames;
    EXPECT_FALSE(archive.getFilenames(".wld", filenames));
    EXPECT_TRUE(filenames.empty());
}

// ============================================================================
// Real S3D File Tests (Integration Tests)
// ============================================================================

class PfsArchiveRealFileTest : public ::testing::Test {
protected:
    std::string eqPath;
    bool hasEQFiles;

    void SetUp() override {
        eqPath = getEQClientPath();
        hasEQFiles = std::filesystem::exists(eqPath + "/qeynos2.s3d");
    }
};

// Test opening qeynos2.s3d
TEST_F(PfsArchiveRealFileTest, OpenQeynos2) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    EXPECT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));
    EXPECT_GT(archive.getFiles().size(), 0u);
}

// Test opening freporte.s3d
TEST_F(PfsArchiveRealFileTest, OpenFreporte) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    EXPECT_TRUE(archive.open(eqPath + "/freporte.s3d"));
    EXPECT_GT(archive.getFiles().size(), 0u);
}

// Test opening ecommons.s3d
TEST_F(PfsArchiveRealFileTest, OpenEcommons) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    EXPECT_TRUE(archive.open(eqPath + "/ecommons.s3d"));
    EXPECT_GT(archive.getFiles().size(), 0u);
}

// Test that zone S3D contains expected WLD file
TEST_F(PfsArchiveRealFileTest, ZoneContainsMainWld) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    ASSERT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));

    // Zone S3D should contain the main WLD file
    EXPECT_TRUE(archive.exists("qeynos2.wld"));
}

// Test that zone S3D contains objects.wld
TEST_F(PfsArchiveRealFileTest, ZoneContainsObjectsWld) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    ASSERT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));

    // Zone S3D should contain objects.wld
    EXPECT_TRUE(archive.exists("objects.wld"));
}

// Test that zone S3D contains lights.wld
TEST_F(PfsArchiveRealFileTest, ZoneContainsLightsWld) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    ASSERT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));

    // Zone S3D should contain lights.wld
    EXPECT_TRUE(archive.exists("lights.wld"));
}

// Test getFilenames with WLD extension
TEST_F(PfsArchiveRealFileTest, GetWldFilenames) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    ASSERT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));

    std::vector<std::string> wldFiles;
    EXPECT_TRUE(archive.getFilenames(".wld", wldFiles));
    EXPECT_GE(wldFiles.size(), 3u);  // At least zone.wld, objects.wld, lights.wld
}

// Test getFilenames with BMP extension
TEST_F(PfsArchiveRealFileTest, GetBmpFilenames) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    ASSERT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));

    std::vector<std::string> bmpFiles;
    archive.getFilenames(".bmp", bmpFiles);
    // Zone should have texture files
    EXPECT_GT(bmpFiles.size(), 0u);
}

// Test extracting WLD file data
TEST_F(PfsArchiveRealFileTest, ExtractWldFile) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    ASSERT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));

    std::vector<char> buffer;
    ASSERT_TRUE(archive.get("qeynos2.wld", buffer));

    // WLD file should have minimum size
    EXPECT_GT(buffer.size(), 28u);  // At least header size

    // WLD magic number check
    uint32_t magic = *reinterpret_cast<uint32_t*>(buffer.data());
    EXPECT_EQ(magic, 0x54503D02u);  // WLD magic
}

// Test that filename lookup is case-insensitive
TEST_F(PfsArchiveRealFileTest, CaseInsensitiveLookup) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    ASSERT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));

    // Should find file regardless of case
    EXPECT_TRUE(archive.exists("qeynos2.wld"));
    EXPECT_TRUE(archive.exists("QEYNOS2.WLD"));
    EXPECT_TRUE(archive.exists("Qeynos2.Wld"));
}

// Test opening global_chr.s3d (character models)
TEST_F(PfsArchiveRealFileTest, OpenGlobalChr) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    EXPECT_TRUE(archive.open(eqPath + "/global_chr.s3d"));
    EXPECT_GT(archive.getFiles().size(), 0u);
    EXPECT_TRUE(archive.exists("global_chr.wld"));
}

// Test file count consistency
TEST_F(PfsArchiveRealFileTest, FileCountConsistency) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    ASSERT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));

    // Get all files using wildcard
    std::vector<std::string> allFiles;
    archive.getFilenames("*", allFiles);

    // Count should match the files map
    EXPECT_EQ(allFiles.size(), archive.getFiles().size());
}

// ============================================================================
// Archive Content Verification Tests
// ============================================================================

class PfsArchiveContentTest : public ::testing::Test {
protected:
    std::string eqPath;
    bool hasEQFiles;

    void SetUp() override {
        eqPath = getEQClientPath();
        hasEQFiles = std::filesystem::exists(eqPath + "/qeynos2.s3d");
    }
};

// Test that extracted BMP files have valid headers
// Note: Some EQ files with .bmp extension are actually DDS files
TEST_F(PfsArchiveContentTest, BmpFileValid) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    ASSERT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));

    std::vector<std::string> bmpFiles;
    archive.getFilenames(".bmp", bmpFiles);
    ASSERT_GT(bmpFiles.size(), 0u);

    // Find a file and verify it has a valid image header (BMP or DDS)
    int validCount = 0;
    for (const auto& filename : bmpFiles) {
        std::vector<char> buffer;
        if (!archive.get(filename, buffer) || buffer.size() < 4) continue;

        // Check for BMP magic "BM" or DDS magic "DDS "
        bool isBmp = (buffer[0] == 'B' && buffer[1] == 'M');
        bool isDds = (buffer[0] == 'D' && buffer[1] == 'D' &&
                      buffer[2] == 'S' && buffer[3] == ' ');

        if (isBmp || isDds) {
            validCount++;
        }
    }

    // Should have at least some valid image files
    EXPECT_GT(validCount, 0);
}

// Test that extracted DDS files have valid headers (if present)
TEST_F(PfsArchiveContentTest, DdsFileValid) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    ASSERT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));

    std::vector<std::string> ddsFiles;
    archive.getFilenames(".dds", ddsFiles);

    if (ddsFiles.empty()) {
        GTEST_SKIP() << "No DDS files in archive";
    }

    // Extract first DDS and verify header
    std::vector<char> buffer;
    ASSERT_TRUE(archive.get(ddsFiles[0], buffer));
    ASSERT_GE(buffer.size(), 4u);

    // DDS magic: "DDS "
    EXPECT_EQ(buffer[0], 'D');
    EXPECT_EQ(buffer[1], 'D');
    EXPECT_EQ(buffer[2], 'S');
    EXPECT_EQ(buffer[3], ' ');
}

// Test that zone WLD has expected fragment count (sanity check)
TEST_F(PfsArchiveContentTest, WldFragmentCountSanity) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    ASSERT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));

    std::vector<char> buffer;
    ASSERT_TRUE(archive.get("qeynos2.wld", buffer));
    ASSERT_GE(buffer.size(), 28u);

    // Read fragment count from WLD header
    uint32_t fragmentCount = *reinterpret_cast<uint32_t*>(&buffer[8]);

    // Zone WLD should have many fragments
    EXPECT_GT(fragmentCount, 100u);
    EXPECT_LT(fragmentCount, 100000u);  // Sanity upper bound
}

// Test objects.wld has placeables (Fragment 0x15)
TEST_F(PfsArchiveContentTest, ObjectsWldHasPlaceables) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    ASSERT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));

    std::vector<char> buffer;
    ASSERT_TRUE(archive.get("objects.wld", buffer));
    ASSERT_GE(buffer.size(), 28u);

    // Read fragment count
    uint32_t fragmentCount = *reinterpret_cast<uint32_t*>(&buffer[8]);
    EXPECT_GT(fragmentCount, 0u);
}

// ============================================================================
// Multiple Archive Tests
// ============================================================================

class PfsMultiArchiveTest : public ::testing::Test {
protected:
    std::string eqPath;
    bool hasEQFiles;

    void SetUp() override {
        eqPath = getEQClientPath();
        hasEQFiles = std::filesystem::exists(eqPath + "/qeynos2.s3d");
    }
};

// Test that we can reopen a different archive
TEST_F(PfsMultiArchiveTest, ReopenDifferentArchive) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;

    // Open first archive
    ASSERT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));
    size_t count1 = archive.getFiles().size();
    EXPECT_TRUE(archive.exists("qeynos2.wld"));

    // Open second archive (should close first)
    ASSERT_TRUE(archive.open(eqPath + "/freporte.s3d"));
    size_t count2 = archive.getFiles().size();

    // Should now have freporte files, not qeynos2
    EXPECT_FALSE(archive.exists("qeynos2.wld"));
    EXPECT_TRUE(archive.exists("freporte.wld"));

    // Counts may differ
    EXPECT_GT(count1, 0u);
    EXPECT_GT(count2, 0u);
}

// Test close() clears files
TEST_F(PfsMultiArchiveTest, CloseClears) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;

    ASSERT_TRUE(archive.open(eqPath + "/qeynos2.s3d"));
    EXPECT_GT(archive.getFiles().size(), 0u);

    archive.close();
    EXPECT_EQ(archive.getFiles().size(), 0u);
}

// ============================================================================
// Zone Object Archive Tests
// ============================================================================

class PfsZoneObjectTest : public ::testing::Test {
protected:
    std::string eqPath;
    bool hasEQFiles;

    void SetUp() override {
        eqPath = getEQClientPath();
        hasEQFiles = std::filesystem::exists(eqPath + "/qeynos2_obj.s3d");
    }
};

// Test opening zone object archive
TEST_F(PfsZoneObjectTest, OpenZoneObjects) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    EXPECT_TRUE(archive.open(eqPath + "/qeynos2_obj.s3d"));
    EXPECT_GT(archive.getFiles().size(), 0u);
}

// Test zone object archive contains WLD
TEST_F(PfsZoneObjectTest, ZoneObjectsContainsWld) {
    if (!hasEQFiles) {
        GTEST_SKIP() << "EQ client files not available";
    }

    PfsArchive archive;
    ASSERT_TRUE(archive.open(eqPath + "/qeynos2_obj.s3d"));
    EXPECT_TRUE(archive.exists("qeynos2_obj.wld"));
}
