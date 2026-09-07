// T41 -- PublishFullStateSnapshot() on a headless zone-in: it fires exactly
// once, it carries a pre-existing pet across, and it populates GameState.
//
// T446 split test_zone_connection_flow.cpp (1,367 lines) by functional
// group; the packet builders and the RecordingBridge fake live in
// zone_connection_flow_test_fixture.h. Same target, same suites, same
// case names.

#include "zone_connection_flow_test_fixture.h"

// ============================================================================
// 4. T41: PublishFullStateSnapshot() fires exactly once per zone-in in
// headless mode, from OnGameStateComplete()'s no-graphics branch. This test
// binary never initializes graphics, so it exercises the same
// !m_graphics_initialized path a real --no-graphics run (or an
// EQT_GRAPHICS=OFF build) takes -- before T41 this path never called
// PublishFullStateSnapshot() at all, so headless GameState started empty
// and stayed that way.
// ============================================================================

TEST(FullStateSnapshot, HeadlessZoneInPublishesSnapshotExactlyOnce) {
    RecordingBridge bridge;
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.setBridge(&bridge);
    const uint16_t kMySpawnId = 5006;

    // Same handshake shape as ObservableZoneState.FullHandshake above, just
    // with a bridge attached so the events PublishFullStateSnapshot() pushes
    // are observable.
    {
        auto payload = BuildPayload(2 + 700, [](EQ::Net::DynamicPacket& p) {
            p.PutCString(66, "qeytoqrg");
            p.PutUInt16(686, 100);
        });
        Replay(eq, HC_OP_NewZone, payload);
    }

    {
        auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt32(2 + 8, 1);
            p.PutUInt32(2 + 12, 1);
            p.PutUInt8(2 + 20, 10);
            p.PutUInt32(2 + 2232, 100);
            p.PutCString(2 + 12940, "Testchar");
            p.PutUInt32(2 + 14384, kMySpawnId);
        });
        Replay(eq, HC_OP_PlayerProfile, payload);
    }
    ASSERT_EQ(eq.GetEntities().count(kMySpawnId), 1u);

    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);
    Replay(eq, HC_OP_GuildMOTD, EmptyPayload());

    // Drain whatever the handshake pushed so far (NetworkReady etc. are not
    // this test's concern) and confirm the snapshot has NOT fired yet --
    // OnGameStateComplete() (and therefore PublishFullStateSnapshot()) only
    // runs once ClientUpdate confirms our own spawn id, matching the
    // existing ObservableZoneState.FullHandshake test's EXPECT_FALSE(
    // eq.IsGameStateReady()) at the equivalent point.
    auto preClientUpdate = bridge.drainEvents();
    int preCount = 0;
    for (const auto& e : preClientUpdate) {
        if (e.type == eqt::state::GameEventType::SkillsSnapshot) preCount++;
    }
    EXPECT_EQ(preCount, 0);

    {
        auto payload = BuildPayload(2 + 22, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt16(2, kMySpawnId);
            p.PutUInt32(4, 0);
            p.PutUInt32(8, 0);
            p.PutUInt32(12, 0);
            p.PutUInt32(16, 0);
            p.PutUInt32(20, 0);
        });
        Replay(eq, HC_OP_ClientUpdate, payload);
    }

    ASSERT_TRUE(eq.IsGameStateReady());
    ASSERT_EQ(eq.GetLoadingPhase(), LoadingPhase::COMPLETE);

    // SkillsSnapshot is only ever pushed from PublishFullStateSnapshot()
    // (grep-confirmed: no per-field packet handler publishes it -- unlike
    // CharacterInfoChanged, which ZoneProcessPlayerProfile also pushes, so
    // it can't be used as a snapshot-only proxy), so counting it is a clean
    // proxy for "the snapshot fired" and "how many times".
    auto postClientUpdate = bridge.drainEvents();
    int snapshotFires = 0;
    for (const auto& e : postClientUpdate) {
        if (e.type == eqt::state::GameEventType::SkillsSnapshot) snapshotFires++;
    }
    EXPECT_EQ(snapshotFires, 1)
        << "PublishFullStateSnapshot() should fire exactly once from "
           "OnGameStateComplete()'s headless branch on zone-in";

    // Further packets in the same zone session must not refire it --
    // OnGameStateComplete() is gated on !m_update_running (StartUpdateLoop()
    // sets it), so a second, unrelated ClientUpdate for the same spawn must
    // not push a second snapshot.
    {
        auto payload = BuildPayload(2 + 22, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt16(2, kMySpawnId);
            p.PutUInt32(4, 0);
            p.PutUInt32(8, 0);
            p.PutUInt32(12, 0);
            p.PutUInt32(16, 0);
            p.PutUInt32(20, 0);
        });
        Replay(eq, HC_OP_ClientUpdate, payload);
    }
    auto afterSecondUpdate = bridge.drainEvents();
    int refires = 0;
    for (const auto& e : afterSecondUpdate) {
        if (e.type == eqt::state::GameEventType::SkillsSnapshot) refires++;
    }
    EXPECT_EQ(refires, 0) << "A second ClientUpdate must not republish the snapshot";
}

// ============================================================================
// 5. T41 addendum (orchestrator request, 2026-08-04): a pet that already
// exists when the player zones in must still surface as PetCreated in
// headless mode. T40 event-fed pet state via GameStateSyncBridge and
// deleted syncPetState() -- PetCreated is one of the events
// PublishFullStateSnapshot() republishes (eq.cpp, "--- Pet ---" block,
// `if (m_pet_spawn_id != 0)`), so a pre-existing pet is a second thing this
// unit's headless-snapshot fix needed to reach, not just inventory.
//
// eq_entities.cpp has two places that discover a pre-existing pet from the
// zone's spawn list: the "second pass" in ZoneProcessSpawnAll (fires once,
// right after the ZoneSpawns packet finishes parsing, for the case where our
// own spawn already has a spawn_id by then) and the deferred check inside
// ClientUpdate (for the case where spawn IDs arrive out of order). Either
// way, `m_pet_spawn_id = ...` itself is NOT graphics-gated -- only the
// immediate `OnPetCreated()` delta-publish alongside it is
// (`#ifdef EQT_HAS_GRAPHICS`). So:
//   - In an EQT_GRAPHICS=OFF build (this test runs in both configs), the
//     delta never fires -- PublishFullStateSnapshot()'s pet block is the
//     ONLY path a pre-existing pet's PetCreated event reaches the bridge.
//     Before this unit's headless snapshot fix, that path never fired
//     either, so a pre-existing pet was invisible outright in that build.
//   - In an EQT_HAS_GRAPHICS build (regardless of runtime --no-graphics),
//     the delta fires immediately during spawn processing AND the snapshot
//     republishes it later -- pre-existing, harmless duplication (the same
//     thing already happened in graphics mode before this unit, since
//     OnGraphicsComplete always called the snapshot after any pre-existing
//     pet had already been spawn-detected).
// This test accepts either shape (asserts total coverage >= 1) but records
// which one happened, and additionally asserts the strict single-source
// count when the delta didn't fire -- which is what actually runs when this
// test executes in the build-nogfx tree.
// ============================================================================

TEST(FullStateSnapshot, PreExistingPetSurvivesHeadlessZoneInViaSnapshot) {
    RecordingBridge bridge;
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.setBridge(&bridge);
    const uint16_t kMySpawnId = 5007;
    const uint32_t kPetSpawnId = 9001;

    {
        auto payload = BuildPayload(2 + 700, [](EQ::Net::DynamicPacket& p) {
            p.PutCString(66, "qeytoqrg");
            p.PutUInt16(686, 100);
        });
        Replay(eq, HC_OP_NewZone, payload);
    }

    // PlayerProfile establishes our own spawn id -- the pet-detection passes
    // below key off m_my_spawn_id being nonzero.
    {
        auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt32(2 + 8, 1);
            p.PutUInt32(2 + 12, 1);
            p.PutUInt8(2 + 20, 10);
            p.PutUInt32(2 + 2232, 100);
            p.PutCString(2 + 12940, "Testchar");
            p.PutUInt32(2 + 14384, kMySpawnId);
        });
        Replay(eq, HC_OP_PlayerProfile, payload);
    }
    ASSERT_EQ(eq.GetEntities().count(kMySpawnId), 1u);

    // The pet is ALREADY in the zone's spawn list -- this is what makes it
    // "pre-existing" rather than a mid-session GM-summon delta (which is all
    // T40's live leg exercised). ZoneProcessSpawnAll's "second pass" runs
    // synchronously inside this call, since m_my_spawn_id is already nonzero.
    {
        auto payload = BuildPayload(2 + 385, [&](EQ::Net::DynamicPacket& p) {
            WritePetSpawnStruct(p, 2, kPetSpawnId, "Testchars_pet", kMySpawnId);
        });
        Replay(eq, HC_OP_ZoneSpawns, payload);
    }
    ASSERT_EQ(eq.GetEntities().count(kPetSpawnId), 1u);

    auto afterSpawns = bridge.drainEvents();
    bool deltaFired = false;
    for (const auto& e : afterSpawns) {
        if (e.type == eqt::state::GameEventType::PetCreated) deltaFired = true;
    }

    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);
    Replay(eq, HC_OP_GuildMOTD, EmptyPayload());
    bridge.drainEvents();  // discard NetworkReady etc., not this test's concern

    {
        auto payload = BuildPayload(2 + 22, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt16(2, kMySpawnId);
            p.PutUInt32(4, 0);
            p.PutUInt32(8, 0);
            p.PutUInt32(12, 0);
            p.PutUInt32(16, 0);
            p.PutUInt32(20, 0);
        });
        Replay(eq, HC_OP_ClientUpdate, payload);
    }
    ASSERT_TRUE(eq.IsGameStateReady());

    auto afterClientUpdate = bridge.drainEvents();
    int postSnapshotPetCreated = 0;
    for (const auto& e : afterClientUpdate) {
        if (e.type == eqt::state::GameEventType::PetCreated) postSnapshotPetCreated++;
    }

    if (deltaFired) {
        // EQT_HAS_GRAPHICS build: the pre-existing-spawn delta already
        // published PetCreated during ZoneProcessSpawnAll; the snapshot
        // republishes it once more (pre-existing behavior, not introduced by
        // this unit -- see comment block above).
        EXPECT_EQ(postSnapshotPetCreated, 1)
            << "Snapshot should still republish PetCreated for the pet even "
               "though a delta already fired for it";
    } else {
        // EQT_GRAPHICS=OFF build: no delta path exists at all (OnPetCreated
        // is compile-gated out), so the snapshot is the ONLY way this event
        // reaches the bridge -- exactly the addendum's concern, and exactly
        // what this unit's headless-snapshot fix closes.
        EXPECT_EQ(postSnapshotPetCreated, 1)
            << "PublishFullStateSnapshot() should be the sole source of "
               "PetCreated for a pre-existing pet in a headless "
               "(EQT_GRAPHICS=OFF) build";
    }
    EXPECT_TRUE(deltaFired || postSnapshotPetCreated >= 1)
        << "A pre-existing pet must reach the bridge as PetCreated one way "
           "or the other -- it did neither";
}

// ============================================================================
// 6. T43: the regression test the deferred.md "EQT_HAS_GRAPHICS gates around
// non-graphics code" pattern entry asked for -- a headless zone-in, driven
// entirely through the server-free ReplayZonePacket seam (this file's own
// idiom, extended rather than replaced), that asserts game state actually
// POPULATED across every dimension the pattern's five historical instances
// broke: inventory, pet, spell gems, and player stats. Runs in both the
// EQT_GRAPHICS=ON and EQT_GRAPHICS=OFF trees (like every test in this file);
// the point is that it must pass in EQT_GRAPHICS=OFF without ever touching a
// renderer or a live server.
//
// Of the five historical instances (refactor/deferred.md's pattern entry),
// this test would have caught THREE outright:
//   - P28 (eq.h guards enclosing non-graphics members): the EQT_GRAPHICS=OFF
//     tree wouldn't have compiled this test binary at all -- a build failure
//     is "failed immediately" in the bluntest possible sense.
//   - T41 (m_inventory_manager never constructed): the InventorySlotChanged
//     assertion below would find no populated slot -- the manager would be
//     null and PublishFullStateSnapshot()'s inventory loop would silently
//     skip.
//   - T42 (HC_OP_ItemPacket/CharInventory dispatch dropped): even with the
//     manager constructed, the CharInventory replay below wouldn't reach it,
//     so the slot this test targets would stay empty.
// The other two would NOT have been caught here, and that's expected, not a
// gap in this test -- they need different oracles:
//   - T29 (no slash commands registered): a command-availability check, not
//     a zone-in state-population check.
//   - T42 (SetEQClientPath() never called): only bites over a REAL network
//     connection (LoadZoneSources() calling RequestQuit() on empty client
//     path) -- ReplayZonePacket bypasses networking entirely, so this test
//     cannot exercise it. That is exactly why the plan's verification bar
//     separately requires a live eqemu EQT_GRAPHICS=OFF acceptance leg.
// ============================================================================

TEST(FullStateSnapshot, HeadlessZoneInPopulatesGameState) {
    RecordingBridge bridge;
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.setBridge(&bridge);
    const uint16_t kMySpawnId = 6001;
    const uint32_t kPetSpawnId = 9101;
    const uint32_t kMemorizedSpellId = 42;   // arbitrary nonzero spell id -- gem storage
                                              // doesn't require a loaded spell database
    const uint32_t kItemId = 10001;
    const uint32_t kItemIcon = 500;
    const std::string kItemName = "T43 Test Sword";

    {
        auto payload = BuildPayload(2 + 700, [](EQ::Net::DynamicPacket& p) {
            p.PutCString(66, "qeytoqrg");
            p.PutUInt16(686, 100);
        });
        Replay(eq, HC_OP_NewZone, payload);
    }

    // PlayerProfile: identity, HP/mana (player stats), and one memorized
    // spell gem (spellbook/mem_spells offsets per eq_zone_entry.cpp's
    // ZoneProcessPlayerProfile comment: spell_book at 2312, mem_spells at
    // 4360, 8 x uint32 each).
    {
        auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt32(2 + 8, 1);            // race
            p.PutUInt32(2 + 12, 1);           // class
            p.PutUInt8(2 + 20, 10);           // level
            p.PutUInt32(2 + 2232, 100);       // cur_hp
            p.PutUInt32(2 + 2228, 50);        // mana
            p.PutCString(2 + 12940, "Testchar");
            p.PutUInt32(2 + 14384, kMySpawnId);
            p.PutUInt32(2 + 4360, kMemorizedSpellId);  // mem_spells[0] (gem slot 1)
        });
        Replay(eq, HC_OP_PlayerProfile, payload);
    }
    ASSERT_EQ(eq.GetEntities().count(kMySpawnId), 1u);

    // Pre-existing pet, already in the zone's spawn list (T41/T43 residual
    // fix: OnPetCreated() is un-guarded as of this unit, so the delta fires
    // here too, not just via the snapshot).
    {
        auto payload = BuildPayload(2 + 385, [&](EQ::Net::DynamicPacket& p) {
            WritePetSpawnStruct(p, 2, kPetSpawnId, "Testchars_pet", kMySpawnId);
        });
        Replay(eq, HC_OP_ZoneSpawns, payload);
    }
    ASSERT_EQ(eq.GetEntities().count(kPetSpawnId), 1u);

    // Inventory: one item at the primary weapon slot, via HC_OP_CharInventory
    // -- the same dispatch path T42 found dropped entirely under
    // EQT_GRAPHICS=OFF before its fix.
    {
        std::string itemStr = BuildItemString(eqt::inventory::SLOT_PRIMARY, kItemId, kItemName, kItemIcon);
        auto payload = BuildPayload(2 + itemStr.size(), [&](EQ::Net::DynamicPacket& p) {
            p.PutData(2, const_cast<char*>(itemStr.data()), itemStr.size());
        });
        Replay(eq, HC_OP_CharInventory, payload);
    }

    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);
    Replay(eq, HC_OP_GuildMOTD, EmptyPayload());
    bridge.drainEvents();  // discard NetworkReady etc., not this test's concern

    // ClientUpdate completes the zone-in handshake -> OnGameStateComplete()
    // -> PublishFullStateSnapshot() (headless path, T41).
    {
        auto payload = BuildPayload(2 + 22, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt16(2, kMySpawnId);
            p.PutUInt32(4, 0);
            p.PutUInt32(8, 0);
            p.PutUInt32(12, 0);
            p.PutUInt32(16, 0);
            p.PutUInt32(20, 0);
        });
        Replay(eq, HC_OP_ClientUpdate, payload);
    }
    ASSERT_TRUE(eq.IsGameStateReady());

    auto events = bridge.drainEvents();

    bool sawPlayerStats = false;
    bool sawInventoryItem = false;
    bool sawSpellGem = false;
    bool sawPetCreated = false;

    for (const auto& e : events) {
        switch (e.type) {
        case eqt::state::GameEventType::PlayerStatsChanged: {
            const auto& d = std::get<eqt::state::PlayerStatsChangedData>(e.data);
            if (d.curHP == 100 && d.maxHP == 100 && d.curMana == 50) {
                sawPlayerStats = true;
            }
            break;
        }
        case eqt::state::GameEventType::InventorySlotChanged: {
            const auto& d = std::get<eqt::state::InventorySlotChangedData>(e.data);
            if (d.slotId == eqt::inventory::SLOT_PRIMARY && d.hasItem && d.itemId == kItemId) {
                sawInventoryItem = true;
            }
            break;
        }
        case eqt::state::GameEventType::SpellGemChanged: {
            const auto& d = std::get<eqt::state::SpellGemChangedData>(e.data);
            if (d.spellId == kMemorizedSpellId) {
                sawSpellGem = true;
            }
            break;
        }
        case eqt::state::GameEventType::PetCreated: {
            const auto& d = std::get<eqt::state::PetCreatedData>(e.data);
            if (d.spawnId == kPetSpawnId) {
                sawPetCreated = true;
            }
            break;
        }
        default:
            break;
        }
    }

    EXPECT_TRUE(sawPlayerStats) << "PlayerStatsChanged with the PlayerProfile's HP/mana never reached the bridge";
    EXPECT_TRUE(sawInventoryItem) << "InventorySlotChanged for the CharInventory item never reached the bridge";
    EXPECT_TRUE(sawSpellGem) << "SpellGemChanged for the memorized spell never reached the bridge";
    EXPECT_TRUE(sawPetCreated) << "PetCreated for the pre-existing pet never reached the bridge";
}
