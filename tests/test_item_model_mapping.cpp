// B824 (issue #86) -- the item -> IT-model lookup is willeq.db-only.
//
// Before this, EquipmentModelLoader kept a SECOND source of truth in memory:
// loadItemModelMapping() parsed all 3,661 rows of data/item_models.json into
// itemToModelMap_ at load (client, tools/model_viewer,
// tools/hwcaps/ui_inventory_capture), and adoptIndex() carried the same map
// across the loading thread. But getModelIdForItem() -- the only thing that
// would want a mapping -- queries WilleqDatabase::getItemModel() (the
// item_models table, which willeq.db is built from that JSON). The in-memory
// map was written on every startup and read by nothing.
//
// This test pins the surviving half: an item present in the database
// resolves through getModelIdForItem() with NO JSON map loaded. The loader
// below gets no client path, loads no archive and never parses
// data/item_models.json -- the only source it can read is the database, so
// a regression that consulted a (now-deleted) in-memory map, or that
// answered from anything other than the db, fails exactly here.
//
// The mechanical half of the pin lives in
// scripts/refactor/check_item_model_map.py (ctest ItemModelMapDeletion): the
// deleted map's identifiers must stay out of the tree, and the db query must
// stay behind getModelIdForItem(). Same ratchet discipline as B840
// (WriteOnlyFields) and B491 (ZeroCallers).
#include <gtest/gtest.h>

#include <sqlite3.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "client/graphics/eq/equipment_model_loader.h"
#include "client/willeq_database.h"

namespace {

std::string TempDbPath(const char* name) {
    const char* tmp = std::getenv("TMPDIR");
    std::string dir = tmp ? tmp : "/tmp";
    return dir + "/b824_item_model_map_" + name + "_" + std::to_string(::getpid()) + ".sqlite";
}

// A throwaway sqlite file: metadata.version at the schema version this
// client expects (open() fails loudly otherwise, T56) plus an item_models
// table with two rows taken from the shipped data/item_models.json (1000 ->
// 64, 1001 -> 63; data/willeq.db carries the same rows).
std::string BuildItemModelsDb(const char* name) {
    std::string path = TempDbPath(name);
    std::remove(path.c_str());

    sqlite3* raw = nullptr;
    EXPECT_EQ(sqlite3_open(path.c_str(), &raw), SQLITE_OK);
    std::string sql =
        "CREATE TABLE metadata (key TEXT PRIMARY KEY, value TEXT);";
    sql += "INSERT INTO metadata (key, value) VALUES ('version', '" +
           std::to_string(EQT::kWilleqDbExpectedVersion) + "');";
    sql += "CREATE TABLE item_models (item_id INTEGER PRIMARY KEY, model_id INTEGER NOT NULL);";
    sql += "INSERT INTO item_models (item_id, model_id) VALUES (1000, 64);";
    sql += "INSERT INTO item_models (item_id, model_id) VALUES (1001, 63);";
    char* err = nullptr;
    EXPECT_EQ(sqlite3_exec(raw, sql.c_str(), nullptr, nullptr, &err), SQLITE_OK) << (err ? err : "");
    sqlite3_close(raw);
    return path;
}

class ItemModelMappingTest : public ::testing::Test {
protected:
    void SetUp() override {
        dbPath_ = BuildItemModelsDb("fixture");
        ASSERT_TRUE(db_.open(dbPath_));
        EQT::WilleqDatabase::setGlobalInstance(&db_);
    }

    void TearDown() override {
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
        db_.close();
        std::remove(dbPath_.c_str());
    }

    EQT::WilleqDatabase db_;
    std::string dbPath_;
};

}  // namespace

// An item present in the database resolves through getModelIdForItem() with
// no JSON map loaded anywhere -- the db is the ONLY source this path reads.
// The loader never gets a client path, never loads archives and never parses
// data/item_models.json; the whole answer must come from the database.
TEST_F(ItemModelMappingTest, ResolvesFromDatabaseWithNoJsonMapLoaded) {
    EQT::Graphics::EquipmentModelLoader loader(nullptr, nullptr, nullptr);
    EXPECT_EQ(loader.getModelIdForItem(1000), 64);
    EXPECT_EQ(loader.getModelIdForItem(1001), 63);
}

// The negative half: an item with no row resolves to -1 (the same contract
// WilleqDatabase::getItemModel() documents). Before the deletion this path
// also had an in-memory map that could have answered -- or, worse, the
// caller could have believed loadItemModelMapping() had populated one.
TEST_F(ItemModelMappingTest, UnknownItemMissesInTheDatabase) {
    EQT::Graphics::EquipmentModelLoader loader(nullptr, nullptr, nullptr);
    EXPECT_EQ(loader.getModelIdForItem(999999), -1);
}
