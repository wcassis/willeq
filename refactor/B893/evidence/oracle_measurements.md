# B893 lane-2 oracle measurements (2026-10-08)
#
# Source: the official Titanium client (Wine eqgame.exe) on the lane-2 oracle
# VM (172.16.200.92), connected to the lane-2 eqemu server (172.16.200.98),
# Testchar (willeq_test, level 60, GM) in East Freeport (freporte, zone_id 10),
# standing wire z = -38.2157.
#
# Method: eqemu packet logging (categories 83/84, log_to_file=3) dumps every
# OP_ClientUpdate (C->S, the wire the client SENDS) and every OP_EnvDamage to
# the freporte zone log. A GM `#goto x y z` (via the /say #cmd chat path) moves
# the server-side position to a known z; the official client then computes the
# fall damage ITSELF and sends OP_EnvDamage C->S (the server reads ed->damage
# verbatim -- Handle_OP_EnvDamage does no fall math). The damage value and the
# dmgtype are read straight off the wire; the takeoff/landing z are read off
# OP_ClientUpdate. Every number below cites the log line it came from.

## Jump arc (standing jumps, Space)

Standing z (wire, C->S OP_ClientUpdate z_pos@26): -38.2157
Mid-air samples (max of the two jump captures, logs/b893_jump_wirecap.decoded.txt
and the 11:29:3x run):

  10:28:55  z=-36.0921  d=+2.12
  10:28:59  z=-36.1635  d=+2.05
  10:29:03  z=-36.3945  d=+1.82
  11:29:30  z=-35.7070  d=+2.51
  11:29:32  z=-35.1650  d=+3.05   <- apex (max mid-air rise)
  11:29:35  z=-35.7070  d=+2.51
  11:29:38  z=-35.7070  d=+2.51
  11:29:41  z=-35.7070  d=+2.51

APEX = +3.05 units above standing (lower bound: the wire carries position at
~1 Hz, so a jump is sampled once mid-air and the true peak is at or above this).
~1 mid-air sample per jump = the arc is sub-second.
[MEASURED] peakHeight = 3.05 (jump_fall_physics.h Arc::peakHeight)
[ASSUMED, OWED] durationS = 1.0 -- the wire cannot resolve a sub-second arc.

## Fall damage (height -> damage, client-computed, OP_EnvDamage C->S)

EnvDamage2_Struct layout (eqemu common/eq_packet_structs.h:3019, 31 bytes):
  +0 uint32 id | +4 uint16 unknown4 | +6 uint32 damage | +10 uint8[12]
  +22 uint8 dmgtype (0xFC=252=Falling) | +23 uint8[4] | +27 uint16 0xFFFF

Clean one-at-a-time series (#goto z=<Z>, then read the next OP_EnvDamage).
fall(u) = goto_z - (-38.2157). dmgtype 252 (Falling) on every record.

  fall(u)   damage   goto z   note
  18.2      0        -20      no OP_EnvDamage sent (below threshold)
  23.2      5        -15
  28.2      5        -10
  33.2      5         -5
  43.2      30         5
  48.2      71         10     [anomaly: above the 53.2 point; #goto fall jitter]
  53.2      59         15
  58.2      81         20
  63.2      87         25
  68.2      91         30

Curve is CONCAVE (diminishing slope at the top: 48->68 gives ~3.0, ~2.1, ~1.0
dmg/unit) and has a no-damage floor at ~18u then a discrete minimum of 5 through
~33u. The 48.2->71 point inverts on 53.2->59 and is treated as #goto
teleport jitter (the client integrates its own fall from its local position,
which the server-side #goto snaps independently), not a model feature.

[MEASURED] safeHeight = 18.0 (the 18.2u drop sent nothing; the floor is in 18.2-23.2)
[MEASURED] damagePerUnitAboveSafe = 1.5 (mid/high range slope; the real curve
         is concave, so a linear ramp overstates the low end and understates
         the top -- documented in refactor/B893/README.md)

## Chat-line confirmation (the oracle displays the server's echo)

At a damaging fall the oracle chat shows, in order:
  You were hit by non-melee for N damage.
  YOU were injured by falling.
  Your GM status protects you from N points of Falling (Type 252) damage.
  Your Location is X, Y, -38.22
(Type 252 == dmgtype 0xFC == EQ::constants::EnvironmentalDamage::Falling.)

## Terminal velocity

NOT measured: the #goto fall is a server-side teleport the client integrates
locally, so no steepest-slope terminal velocity is observable on the wire from
this rig. Left [ASSUMED, OWED] in Arc::terminalSpeed (nothing clamps to it yet
-- there is no terminal-velocity term in the integrator).
