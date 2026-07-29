// Shared WLD test fixtures.
//
// Extracted from test_wld.cpp (T11 split, 2026-07-29) so that
// WldHeaderTest / WldIntegrationTest can be reused by several test .cpp
// files without an ODR violation (defining the same fixture class in
// multiple translation units). This header is included by all of the
// test_wld_*.cpp files that derive fixtures from these two classes.
//
// Pure mechanical extraction: no behavior changed from the original
// test_wld.cpp inline definitions.

#pragma once

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <cstdlib>

#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/pfs.h"

using namespace EQT::Graphics;

// ============================================================================
// Phase 2.1: WLD Header Parsing Tests
// ============================================================================

class WldHeaderTest : public ::testing::Test {
protected:
    // Constants matching eqsage (wld.js)
    static constexpr uint32_t WLD_MAGIC = 0x54503D02;
    static constexpr uint32_t WLD_VERSION_OLD = 0x00015500;
    static constexpr uint32_t WLD_VERSION_NEW = 0x1000C800;

    // XOR key for string hash decoding - matches eqsage (util.js)
    static constexpr uint8_t HASH_KEY[8] = {0x95, 0x3A, 0xC5, 0x2A, 0x95, 0x7A, 0x95, 0x6A};

    std::string eqClientPath_;

    void SetUp() override {
        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        if (envPath) {
            eqClientPath_ = envPath;
        } else {
            eqClientPath_ = EQ_CLIENT_DATA_PATH;
        }
    }

    bool fileExists(const std::string& path) {
        std::ifstream f(path);
        return f.good();
    }

    // Parse WLD header directly from buffer
    bool parseWldHeader(const std::vector<char>& buffer, WldHeader& outHeader) {
        if (buffer.size() < sizeof(WldHeader)) {
            return false;
        }
        std::memcpy(&outHeader, buffer.data(), sizeof(WldHeader));
        return true;
    }

    // Decode string hash - matches eqsage's decodeString function
    std::string decodeStringHash(const char* data, size_t length) {
        std::vector<char> decoded(length);
        for (size_t i = 0; i < length; ++i) {
            decoded[i] = data[i] ^ HASH_KEY[i % 8];
        }
        return std::string(decoded.begin(), decoded.end());
    }

    // Get string from hash table using negative index (matches eqsage getString)
    std::string getString(const std::string& hashTable, int32_t idx) {
        if (idx >= 0) {
            return "";
        }
        size_t offset = static_cast<size_t>(-idx);
        if (offset >= hashTable.size()) {
            return "";
        }
        // Find null terminator
        size_t end = hashTable.find('\0', offset);
        if (end == std::string::npos) {
            return hashTable.substr(offset);
        }
        return hashTable.substr(offset, end - offset);
    }
};

// ============================================================================
// Integration Tests with Real EQ Files
// ============================================================================

class WldIntegrationTest : public WldHeaderTest {
protected:
    // Load WLD file from S3D archive
    bool loadWldFromArchive(const std::string& s3dPath, const std::string& wldName,
                           std::vector<char>& outBuffer) {
        PfsArchive archive;
        if (!archive.open(s3dPath)) {
            return false;
        }
        return archive.get(wldName, outBuffer);
    }
};
