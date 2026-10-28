#pragma once

// B1183 (#502): the client's per-spawn stand-state byte -- spawn +0x251 in
// eqgame.exe (B1141 Ghidra decompile, review/B1141/ghidra/out/eqgame.exe.c).
//
// The official client keeps ONE BYTE per spawn (the "stand state"). The
// values the client itself writes (the 143-reference census is
// refactor/B1183/README.md, one row per eqgame.exe.c line):
//
//   0    uninitialized -- the spawn constructor (eqgame.exe.c:103347)
//   100  'd' standing   (zone-in 75121/75215/76903/119126/128478, cast path
//                        71254, stun end 80336, new spawn 106109)
//   102  'f' frozen      (stun start 80377/80459, the 0x25aa/0x3050 messages)
//   105  'i' looting     (loot start 105797, direct)
//   110  'n' sitting     (the setter, sit key)
//   111  'o' crouching   (the setter, duck key)
//   115  's' lying       (the setter; the value the wire carries on death)
//   120  'x' corpse      (death 105881/111059, the +0x244 > 1 checks)
//
// The client has TWO writer kinds, kept apart here exactly as in the binary:
//
//  * DIRECT writes -- no gate: spawn init/appearance (103347/103463), the
//    local player's zone-in 100 (75121/75215/76903/119126/128478), the cast
//    path's 100 (71254), stun 100/102 (80336/80377/80459), loot start 105
//    (105797), new spawn 100 (106109), death 120 (105881/111059).
//      -> apply() / onAppearanceAnim()
//
//  * THE SETTER FUN_00483556 (decompiled inside the FUN_0048236e listing,
//    eqgame.exe.c:104742-104878; 0x00483556-0x00483955 in eqgame.dis) -- the
//    user-initiated transitions (the duck/sit/stand keys, loot open/close,
//    cast start): gated, -> transition(). Its gates, in order:
//      104742  new == current -> no-op;
//      104768  new == 'n' (sit) while the actor model's casting word
//              (model +0x15c) is set -> no-op (you cannot sit down mid-cast);
//      104798  current == 'o' (crouching) and new == 'd'/'n': the setter
//              writes the new state, asks the camera check (FUN_0048bb31,
//              0x48bb31-0x48bb79) and compares the camera z against the
//              spawn's top (0x238); when the camera cannot follow (AL == 0
//              or camZ - spawnZ <= height) it writes 0x6f back and RETURNS
//              EARLY (0x483850 -> 0x483951, skipping the final write 104878)
//              -- the spawn STAYS CROUCHING. Third-person (the default
//              camera, and the one the lane-5 sequence runs under) follows,
//              so the requested state lands.
//      104878  the final write: the requested state, on every other path
//              (the stand->crouch branch's transient 100 at 104780/104795 is
//              unobservable -- the final write reinstates 0x6f).
//
// Entity::stand_state is one of these objects -- ONE StandState owner per
// spawn -- and converts to the raw byte, so the readers (the B1174 cast-anim
// gate, spell_action_anim.h:73) keep reading exactly the byte they read.

#include <cstdint>

#include "client/spell/spell_action_anim.h"  // EQ::SpawnStandStateAfterAppearance (the server rule)

namespace eqt {
namespace state {

// The values the client writes. The byte itself is a raw uint8_t (the
// server can put any value 100..255 in it through an appearance, and the
// machine keeps whatever was written); this enum is the named vocabulary
// for the writer side.
enum class StandValue : uint8_t {
    Uninitialized = 0,   // 'spawn constructor' value
    Standing      = 100, // 'd'
    Frozen        = 102, // 'f'
    Looting       = 105, // 'i'
    Sitting       = 110, // 'n'
    Crouching     = 111, // 'o'
    Lying         = 115, // 's'
    Corpse        = 120, // 'x'
};

// The inputs FUN_00483556 consults, the ones fable can know (README sec. 3
// names each client line and its fable source).
struct StandStateContext {
    // FUN_0043745d() -- the client is in a zone.
    bool in_world = false;
    // The spawn is the local player (DAT_00905ce4). The setter's local
    // half (0x48359c-0x4835c3 chair/message cleanup, the 0x4836a5-0x4836f0
    // UI mirror) has no fable equivalent (no sit-on-chair, no UI byte).
    bool is_local = false;
    // The camera-follow check (FUN_0048bb31) and the camera-z vs top-of-
    // spawn compare in the crouch->stand/sit branch (0x483821-0x483844).
    // fable's default third-person camera follows from above the spawn's
    // top, so callers pass the third-person answers; see README sec. 3.
    bool camera_follows = true;
    bool camera_above_top = true;
    // The actor model's casting word (model +0x15c, read at 104768).
    bool casting = false;
};

// One per spawn (Entity::stand_state). Owns the byte and the transition
// logic, fed by the same events the client uses (see the fable feed points
// in refactor/B1183/README.md sec. 4).
class StandState {
public:
    // The byte, and the implicit read the existing readers use
    // (spell_packet_handlers.cpp: d.sourceStandState = e.stand_state).
    uint8_t value() const { return state_; }
    operator uint8_t() const { return state_; }
    bool operator==(uint8_t v) const { return state_ == v; }
    bool operator!=(uint8_t v) const { return state_ != v; }
    bool operator==(StandValue v) const { return state_ == static_cast<uint8_t>(v); }
    bool operator!=(StandValue v) const { return state_ != static_cast<uint8_t>(v); }

    // The DIRECT writes (the census lines in the banner): spawn init,
    // zone-in 100, cast path 100, stun 100/102, loot start 105, death 120.
    void apply(StandValue v) { state_ = static_cast<uint8_t>(v); }

    // The server's AT_Anim stand value for this spawn (appearance packets),
    // the value the client writes from the appearance data (103463), under
    // the B1174 rule (spell_action_anim.h): 0 -> 100, 100..255 as sent,
    // anything else leaves the byte.
    void onAppearanceAnim(uint32_t animParam)
    {
        state_ = EQ::SpawnStandStateAfterAppearance(state_, animParam);
    }

    // THE SETTER (FUN_00483556): the user-initiated transition. Returns
    // false when the byte did not move (the no-op, the mid-cast sit, or
    // the crouch the camera would not follow).
    bool transition(StandValue v, const StandStateContext& ctx)
    {
        const uint8_t want = static_cast<uint8_t>(v);
        if (state_ == want) {
            return false;  // 104742
        }
        if (v == StandValue::Sitting && ctx.casting) {
            return false;  // 104768: model +0x15c set
        }
        if (ctx.in_world) {
            if (state_ == 111 && (v == StandValue::Standing || v == StandValue::Sitting)) {
                // 104798-104807: write the new state; the camera check; on
                // a camera that cannot follow, 0x6f goes back and the setter
                // returns early -- the spawn stays crouching.
                if (!ctx.camera_follows || !ctx.camera_above_top) {
                    return false;  // 0x483850: still 111, final write skipped
                }
            }
            // stand->crouch (104774-104780): the transient 100 the camera
            // check reads is unobservable -- the final write below reinstates
            // 0x6f, so no branch here changes the byte.
        }
        state_ = want;  // 104878: the final write (0x483941-0x483946)
        return true;
    }

private:
    // fable default: 100. The client constructs with 0 (103347) and the
    // appearance/zone-in write follows; B1174's documented default is 100
    // (the gate passes for a spawn the server has not given a stand value
    // to), and keeping it means no gate behaviour change (acceptance B).
    uint8_t state_ = 100;
};

}  // namespace state
}  // namespace eqt
