// B1183 (#502): the client's per-spawn stand-state byte (+0x251) ported as
// one StandState owner per spawn (include/client/state/stand_state.h).
//
// THE ORACLE IS THE CLIENT'S CODE (the 143-reference census is
// refactor/B1183/README.md; the decompile is
// review/B1141/ghidra/out/eqgame.exe.c). The machine has TWO writer kinds,
// exactly as the binary does:
//
//  * DIRECT writes (apply / onAppearanceAnim): spawn init, the server's
//    AT_Anim stand value (103463), the local zone-in 100, the cast path's 100
//    (71254), the stun feed's 100/102 (onStunFeed; 80336/80377/80459, the
//    0x1e51 case writes nothing), loot start 105 (28061/105797), death 120
//    (105881/111059). No gate.
//
//  * THE SETTER FUN_00483556 (transition): the user-initiated transitions,
//    gated -- the no-op (104742), the mid-cast sit (104768, model +0x15c),
//    the crouch the camera would not follow (104798-104807).
//
// Device-free: the machine is header-only, so this links only gtest.

#include <gtest/gtest.h>

#include <cstdint>
#include <iostream>
#include <sstream>

#include "client/state/stand_state.h"
#include "client/spell/spell_action_anim.h"  // the B1174 gate (the reader)

using eqt::state::StandState;
using eqt::state::StandStateContext;
using eqt::state::StandValue;
using eqt::state::StunFeed;

namespace {

// The client's named vocabulary, byte for byte.
TEST(StandState, TheValuesMatchTheClientBytes) {
    EXPECT_EQ(static_cast<uint8_t>(StandValue::Uninitialized), 0);
    EXPECT_EQ(static_cast<uint8_t>(StandValue::Standing), 100);   // 'd'
    EXPECT_EQ(static_cast<uint8_t>(StandValue::Frozen), 102);     // 'f'
    EXPECT_EQ(static_cast<uint8_t>(StandValue::Looting), 105);    // 'i'
    EXPECT_EQ(static_cast<uint8_t>(StandValue::Sitting), 110);    // 'n'
    EXPECT_EQ(static_cast<uint8_t>(StandValue::Crouching), 111);  // 'o'
    EXPECT_EQ(static_cast<uint8_t>(StandValue::Lying), 115);      // 's'
    EXPECT_EQ(static_cast<uint8_t>(StandValue::Corpse), 120);     // 'x'
}

// B1174's documented default: a spawn the server has not given a stand value
// to reads 100 (standing), so the cast-anim gate passes for it.
TEST(StandState, AFreshSpawnReadsStanding) {
    StandState s;
    EXPECT_EQ(static_cast<uint8_t>(s), 100);
    EXPECT_EQ(s, 100);
    EXPECT_NE(s, 110);
}

// apply() is the DIRECT-write half: it stores the value with no gate, from
// whatever state the spawn is in (the cast path's 100, stun 102, death 120,
// loot 105 all land this way regardless of the previous byte).
TEST(StandState, ApplyIsADirectWriteWithNoGate) {
    StandState s;
    s.apply(StandValue::Lying);
    EXPECT_EQ(static_cast<uint8_t>(s), 115);
    // death (120) lands even from lying (105881/111059 write it unconditionally)
    s.apply(StandValue::Corpse);
    EXPECT_EQ(static_cast<uint8_t>(s), 120);
    // stun (102) lands even from corpse
    s.apply(StandValue::Frozen);
    EXPECT_EQ(static_cast<uint8_t>(s), 102);
    // the cast path's 100 (71254)
    s.apply(StandValue::Standing);
    EXPECT_EQ(static_cast<uint8_t>(s), 100);
}

// onAppearanceAnim() is the server's AT_Anim stand value, under the B1174
// rule (SpawnStandStateAfterAppearance): 0 -> 100, 100..255 as sent, anything
// else leaves the byte (103463 writes the blob's value; eqemu's ANIM_STAND is
// 0 and means standing).
TEST(StandState, OnAppearanceAnimFollowsTheServerRule) {
    StandState s;  // 100
    s.onAppearanceAnim(0);     // ANIM_STAND -> standing
    EXPECT_EQ(static_cast<uint8_t>(s), 100);
    s.onAppearanceAnim(110);   // sitting, as sent
    EXPECT_EQ(static_cast<uint8_t>(s), 110);
    s.onAppearanceAnim(111);   // crouching
    EXPECT_EQ(static_cast<uint8_t>(s), 111);
    s.onAppearanceAnim(105);   // looting
    EXPECT_EQ(static_cast<uint8_t>(s), 105);
    s.onAppearanceAnim(115);   // lying
    EXPECT_EQ(static_cast<uint8_t>(s), 115);
    // an emote-style action animation (< 100) leaves the byte as it was
    s.onAppearanceAnim(12);
    EXPECT_EQ(static_cast<uint8_t>(s), 115);
    // the server can put any value 100..255 in it through an appearance
    s.onAppearanceAnim(200);
    EXPECT_EQ(static_cast<uint8_t>(s), 200);
}

// THE SETTER, gate 1 (104742): new == current -> no-op, the byte does not
// move, the transition reports it did not land.
TEST(StandState, TheSetterNoOpsOnTheSameState) {
    StandState s;  // 100
    StandStateContext ctx;
    ctx.in_world = true;
    EXPECT_FALSE(s.transition(StandValue::Standing, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 100);
}

// THE SETTER, gate 2 (104768): you cannot sit down mid-cast. The actor model's
// casting word (model +0x15c) set -> the sit is a no-op.
TEST(StandState, TheSetterBlocksSittingWhileCasting) {
    StandState s;  // 100 standing
    StandStateContext ctx;
    ctx.in_world = true;
    ctx.casting = true;
    EXPECT_FALSE(s.transition(StandValue::Sitting, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 100);  // still standing
}

// ...but a stand/crouch/loot transition is not gated on the casting word --
// only the sit is.
TEST(StandState, TheSetterAllowsTheOtherTransitionsWhileCasting) {
    StandState s;  // 100
    StandStateContext ctx;
    ctx.in_world = true;
    ctx.casting = true;
    EXPECT_TRUE(s.transition(StandValue::Crouching, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 111);
}

// THE SETTER, gate 3 (104798-104807): a crouching spawn asked to stand or sit
// goes through the camera check; when the camera cannot follow (it does not
// follow, or it is not above the spawn's top) the setter writes 0x6f back and
// returns EARLY -- the spawn STAYS CROUCHING.
TEST(StandState, TheSetterKeepsTheCrouchWhenTheCameraCannotFollow) {
    // camera does not follow
    StandState s;  // 100
    StandStateContext ctx;
    ctx.in_world = true;
    ctx.camera_follows = false;
    ASSERT_TRUE(s.transition(StandValue::Crouching, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 111);
    EXPECT_FALSE(s.transition(StandValue::Standing, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 111);  // still crouching

    // camera follows but is not above the top
    StandState s2;
    StandStateContext ctx2;
    ctx2.in_world = true;
    ctx2.camera_follows = true;
    ctx2.camera_above_top = false;
    ASSERT_TRUE(s2.transition(StandValue::Crouching, ctx2));
    EXPECT_FALSE(s2.transition(StandValue::Sitting, ctx2));
    EXPECT_EQ(static_cast<uint8_t>(s2), 111);  // still crouching
}

// ...and the same crouch->stand/sit DOES land when the third-person camera
// follows from above the spawn's top (the default, and the one the lane-5
// sequence runs under).
TEST(StandState, TheSetterLandsTheStandWhenTheCameraFollows) {
    StandState s;  // 100
    StandStateContext ctx;
    ctx.in_world = true;  // camera_follows/camera_above_top default true
    ASSERT_TRUE(s.transition(StandValue::Crouching, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 111);
    EXPECT_TRUE(s.transition(StandValue::Standing, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 100);

    // and crouch -> sit
    StandState s2;
    ASSERT_TRUE(s2.transition(StandValue::Crouching, ctx));
    EXPECT_TRUE(s2.transition(StandValue::Sitting, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s2), 110);
}

// OUT OF the world (ctx.in_world false, the client's FUN_0043745d() false)
// the crouch camera branch does not run at all -- a crouch->stand lands
// regardless of the camera.
TEST(StandState, TheCrouchCameraBranchNeedsTheWorld) {
    StandState s;
    StandStateContext ctx;
    ctx.in_world = false;  // camera_follows false, but the branch is skipped
    ctx.camera_follows = false;
    ASSERT_TRUE(s.transition(StandValue::Crouching, ctx));
    EXPECT_TRUE(s.transition(StandValue::Standing, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 100);
}

// The full lane-5 pose sequence, through the setter, as the local player's
// keys drive it (the default third-person camera follows):
//   stand -> duck -> stand -> sit -> stand
TEST(StandState, TheLaneFivePoseSequence) {
    StandState s;  // 100 standing (zone-in)
    StandStateContext ctx;
    ctx.in_world = true;
    EXPECT_EQ(static_cast<uint8_t>(s), 100);          // stand
    EXPECT_TRUE(s.transition(StandValue::Crouching, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 111);          // duck
    EXPECT_TRUE(s.transition(StandValue::Standing, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 100);          // stand
    EXPECT_TRUE(s.transition(StandValue::Sitting, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 110);          // sit
    EXPECT_TRUE(s.transition(StandValue::Standing, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 100);          // stand
}

// Loot open (the 28061 gate: standing -> looting through the setter) and loot
// close (230933: looting -> standing through the setter).
TEST(StandState, TheLootTransitions) {
    StandState s;  // 100
    StandStateContext ctx;
    ctx.in_world = true;
    // open: standing -> looting (105)
    EXPECT_TRUE(s.transition(StandValue::Looting, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 105);
    // close: looting -> standing (100)
    EXPECT_TRUE(s.transition(StandValue::Standing, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 100);
    // opening from sitting is NOT the 28061 gate ('d' only): the caller does
    // not issue the transition, so the byte does not move on its own.
    ASSERT_TRUE(s.transition(StandValue::Sitting, ctx));
    EXPECT_EQ(static_cast<uint8_t>(s), 110);
}

// The READER: the B1174 cast-anim gate reads the byte the machine owns. A
// standing (100) or frozen (102) source is asked; sitting (110), looting
// (105), lying (115), corpse (120) are not. This is the gate behaviour that
// must not change for the server-echoed cases (acceptance B).
TEST(StandState, TheReaderGateReadsTheMachinesByte) {
    auto gate = [](uint8_t stand) {
        EQ::ActionCastAnimInputs in;
        in.spellId = 100;          // in 1..9999
        in.recordExists = true;
        in.targetType = 1;         // non-zero
        in.sourceStandState = stand;
        in.spellAffectIndex = -1;  // not 61..64
        in.sourceClass = 0;        // not Bard
        return EQ::ActionPlaysSourceCastAnim(in);
    };
    StandState s;
    s.apply(StandValue::Standing);
    EXPECT_TRUE(gate(static_cast<uint8_t>(s)));
    s.apply(StandValue::Frozen);
    EXPECT_TRUE(gate(static_cast<uint8_t>(s)));
    s.apply(StandValue::Sitting);
    EXPECT_FALSE(gate(static_cast<uint8_t>(s)));
    s.apply(StandValue::Looting);
    EXPECT_FALSE(gate(static_cast<uint8_t>(s)));
    s.apply(StandValue::Lying);
    EXPECT_FALSE(gate(static_cast<uint8_t>(s)));
    s.apply(StandValue::Corpse);
    EXPECT_FALSE(gate(static_cast<uint8_t>(s)));
}

// The full lane-5 sequence (the card's step list) driven through the machine,
// printed as the fable-side column of the acceptance-C step table. The
// oracle-side column (the client's +0x251 under the B1164 gdb pin) is the
// pending live leg; this column is what fable's state machine actually
// produces, measured by running it -- not asserted by hand. The stun ends
// (80336 -> 100) before the loot, so the 28061 gate (standing -> looting)
// fires from standing, not from the frozen byte.
TEST(StandState, TheLane5FullSequenceTable) {
    StandState s;
    StandStateContext ctx;
    ctx.in_world = true;
    std::ostringstream out;
    out << "step\tfeed\tfable_byte\n";
    auto step = [&](const char* name, const char* feed) {
        out << name << '\t' << feed << '\t' << static_cast<int>(s) << '\n';
    };
    // stand (zone-in: the local player starts standing, 100)
    EXPECT_EQ(static_cast<int>(s), 100);
    step("stand", "zone-in default (128478)");
    // duck
    ASSERT_TRUE(s.transition(StandValue::Crouching, ctx));
    EXPECT_EQ(static_cast<int>(s), 111);
    step("duck", "key C (setter 104878)");
    // stand
    ASSERT_TRUE(s.transition(StandValue::Standing, ctx));
    EXPECT_EQ(static_cast<int>(s), 100);
    step("stand", "key (setter 104878)");
    // sit
    ASSERT_TRUE(s.transition(StandValue::Sitting, ctx));
    EXPECT_EQ(static_cast<int>(s), 110);
    step("sit", "key (setter 104878)");
    // stand
    ASSERT_TRUE(s.transition(StandValue::Standing, ctx));
    EXPECT_EQ(static_cast<int>(s), 100);
    step("stand", "key (setter 104878)");
    // cast start (71254: the cast path writes 100; a standing actor is a no-op)
    s.apply(StandValue::Standing);
    EXPECT_EQ(static_cast<int>(s), 100);
    step("cast", "cast start (71254)");
    // stun start (B1199: fable's feed is the 0x1e51 OP_Stun opcode, whose
    // client case 79576-79581 writes NO +0x251 -- the pose holds 100)
    s.onStunFeed(StunFeed::OpStun0x1e51);
    EXPECT_EQ(static_cast<int>(s), 100);
    step("stunned", "OP_Stun 0x1e51 (79576-79581: no +0x251 write)");
    // stun end (80336: back to standing, the 0x210b case)
    s.onStunFeed(StunFeed::StunEnd0x210b);
    EXPECT_EQ(static_cast<int>(s), 100);
    step("stun end", "stun end 0x210b (80336)");
    // loot start (28061 gate: standing -> looting, via the setter)
    ASSERT_TRUE(s.transition(StandValue::Looting, ctx));
    EXPECT_EQ(static_cast<int>(s), 105);
    step("loot", "loot open (28061)");
    std::cout << out.str() << std::flush;
}

// B1199 (#518): the stun feed, byte for byte as the client writes it
// (eqgame.exe.c, the message handler family).
//
// 0x1e51 (OP_Stun -- the stun the server applies, the GM #stun): the
// client's case (79576-79581: `if (DAT_00905d00 != 0) FUN_00417839();`)
// writes NO +0x251, so the pose byte HOLDS whatever it had. Fable's B1183
// port wrote 102 on this feed -- the measured fable/oracle difference
// (B1183 legF: oracle 100 vs fable 102 on GM #stun); the write is removed.
TEST(StandState, TheOpStunFeed0x1e51WritesNoStandByte) {
    StandState s;  // 100 (the measured lane-5 pose: standing)
    s.onStunFeed(StunFeed::OpStun0x1e51);
    EXPECT_EQ(static_cast<uint8_t>(s), 100);  // unchanged, stays 100

    // the byte holds ANY pose it had, not just 100: the client writes
    // nothing, so a sitting or crouching actor keeps its pose byte
    s.apply(StandValue::Sitting);  // 110
    s.onStunFeed(StunFeed::OpStun0x1e51);
    EXPECT_EQ(static_cast<uint8_t>(s), 110);
    s.apply(StandValue::Crouching);  // 111
    s.onStunFeed(StunFeed::OpStun0x1e51);
    EXPECT_EQ(static_cast<uint8_t>(s), 111);
}

// The client's 102 (0x66, frozen) writer #1: the 0x25aa case
// (eqgame.exe.c:80377). A direct write -- 102 lands from whatever pose the
// spawn had. (Fable does not process 0x25aa; the feed is kept in the
// machine so its vocabulary is the client's, byte for byte.)
TEST(StandState, TheFrozenFeed0x25aaWrites102) {
    StandState s;  // 100
    s.onStunFeed(StunFeed::Frozen0x25aa);
    EXPECT_EQ(static_cast<uint8_t>(s), 102);
    // direct: it lands even from sitting (110)
    s.apply(StandValue::Sitting);
    s.onStunFeed(StunFeed::Frozen0x25aa);
    EXPECT_EQ(static_cast<uint8_t>(s), 102);
}

// The client's 102 writer #2: the 0x3013 case (eqgame.exe.c:80459) -- the
// same direct 102 write.
TEST(StandState, TheFrozenFeed0x3013Writes102) {
    StandState s;  // 100
    s.onStunFeed(StunFeed::Frozen0x3013);
    EXPECT_EQ(static_cast<uint8_t>(s), 102);
}

// The stun end (the 0x210b case, eqgame.exe.c:80326-80340: 100 at 80336,
// the stun-flag clear at 80337) is the direct 100 write -- it lands from
// any pose, including the 102 a frozen feed would have left.
TEST(StandState, TheStunEndFeed0x210bWrites100) {
    StandState s;
    s.onStunFeed(StunFeed::Frozen0x25aa);
    ASSERT_EQ(static_cast<uint8_t>(s), 102);
    s.onStunFeed(StunFeed::StunEnd0x210b);
    EXPECT_EQ(static_cast<uint8_t>(s), 100);
}

}  // namespace
