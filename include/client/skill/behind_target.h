/*
 * behind_target.h -- "am I behind my target?", ported byte-for-byte from the
 * server's own definition (T317).
 *
 * AUTHORITY
 *
 *   eqemu, akk-stack/code/zone/mob.h:222 and zone/mob.cpp:4576:
 *
 *       // greater than 90 is behind
 *       inline bool BehindMob(Mob *other, float ourx, float oury) const
 *           { return (!other || other == this) ? true
 *                                              : MobAngle(other, ourx, oury) > 90.0f; }
 *
 *   `Mob::MobAngle` builds a length-10 vector along the TARGET's facing and
 *   takes the angle between it and the target->attacker vector. Backstab is
 *   gated on exactly this in `Mob::TryBackstab` (zone/special_attacks.cpp:745).
 *   Nothing here is inferred: it is the same arithmetic, in the same order,
 *   including eqemu's `3.1415` (not M_PI) and its acos domain clamps, so the
 *   client and the server agree at the boundary rather than merely nearby.
 *
 * WHICH HEADING CONVENTION -- READ THIS BEFORE EDITING
 *
 *   docs/CLAUDE.md's Heading/Rotation Conventions warning applies to this
 *   file's callers. This client stores the PLAYER's own heading flipped
 *   (`90 - server_heading_deg`, eq.cpp's PlayerProfile parse) and every OTHER
 *   entity's heading unflipped (`server_heading`, eq_entities.cpp). That
 *   asymmetry is load-bearing and must not be harmonised.
 *
 *   This test SIDESTEPS it: `MobAngle` uses the TARGET's heading and both
 *   parties' positions, and never the attacker's heading. Every entity in
 *   `EverQuest::GetEntities()` -- player-characters and NPCs alike -- carries
 *   the unflipped server heading, so there is exactly one convention in play
 *   here and no flip to get wrong. The only thing a caller must undo is the
 *   x/y SWAP: the client stores `entity.x = server_y, entity.y = server_x`,
 *   so pass `entity.y` as the server x and `entity.x` as the server y.
 *   Everything below is in SERVER coordinates and SERVER-derived degrees.
 */

#ifndef EQT_CLIENT_SKILL_BEHIND_TARGET_H
#define EQT_CLIENT_SKILL_BEHIND_TARGET_H

namespace EQ {
namespace skill {

/**
 * The angle, in degrees, between where the target is facing and the direction
 * the attacker is standing in. 0 = attacker is dead ahead of the target,
 * 180 = directly behind it.
 *
 * All positions are SERVER x/y (undo the client's x/y swap before calling).
 * `target_heading_deg` is the target's heading already converted to degrees
 * on the server's 512-unit circle -- which is exactly what `Entity::heading`
 * holds for every entity in the client's entity map.
 *
 * Returns 0 (i.e. "dead ahead", not behind) when the two are at the same
 * point. eqemu divides by zero there; refusing to is the one deliberate
 * departure from the port.
 */
float MobAngleDegrees(float target_server_x, float target_server_y,
                      float target_heading_deg,
                      float attacker_server_x, float attacker_server_y);

/** eqemu's `BehindMob`: strictly greater than 90 degrees. */
bool IsBehindTarget(float target_server_x, float target_server_y,
                    float target_heading_deg,
                    float attacker_server_x, float attacker_server_y);

}  // namespace skill
}  // namespace EQ

#endif  // EQT_CLIENT_SKILL_BEHIND_TARGET_H
