#!/usr/bin/env python3
"""T492 Leg C: generate scripts/llmbench/timeline.tsv (the recording timeline).

Kept as a generator, not a hand-edited TSV, for one reason: the SPACING is
load-bearing.  The driver's own cost per row is not uniform -- an official
`cmd` retries up to three times against the client's chat (up to ~25 s), a
`hold`/`video` row blocks for its whole duration, and EVERY row costs an
official window+root grab -- so a row whose slot is shorter than its action
pushes the whole rest of the timeline late and the three players stop being
contemporaneous.  Spacing is therefore derived from the action, here, once.

What `expect=match` means in THIS corpus, exactly: both clients are in the
First Person camera, which is the one camera pair a prior unit fitted against
the oracle (T466/T471 measured the official client's untouched login camera as
First Person), in the same zone at the same heading -- but the three players
stand 10 units apart, because a three-player corpus needs them to see each
other.  It does NOT mean the frames are pixel pairs.  No model judged anything
here and no frame was looked at to choose a label.

B793 (issue #75): every `held=` claim is DERIVED from the inventory snapshot
the run started from, never hand-typed.  The values the committed Leg C
timeline claimed (`13=9998 short sword, 14=9979 lantern` for Testcaster) were
a misread of the inventory dump and were simply false: the snapshot the run
started from says Testcaster holds 14337 / 20656 in slots 13 / 14, while
Testbuddy's `13=9997, 14=9979` does match -- the verifier's db_inventory
check discriminates, which is exactly why a hand check never caught it.  So
this generator reads the snapshot through the SAME corpus-relative convention
the verifier already uses (verify_labels.py: invf =
`<corpus>/shared/db_before/inventory.tsv`): `--db-before <dir>` names the
`shared/db_before/` directory (its inventory.tsv + snapshot.tsv), defaulting
to the T492 reference snapshot committed under scripts/llmbench/fixtures/.
A missing or empty snapshot is a hard error (exit 2), never a fallback to
constants.

The `/equip` step keeps its row and its ACTION (step-id continuity the
segment generators rely on), but its old `held=13=9990` after-claim is gone,
and the `E_heldB_*` frames carry NO `held=` claim: the source slot's
eligibility for Primary (13) cannot be established from any file in the repo
-- the inventory snapshot carries no slot-type / slots bitmask and
data/item_idfile.tsv (T498's committed export) carries the item's model
(IT<n>), not its TYPE -- and Leg C's move was refused by the client ("Bread
Cakes* cannot go in Primary (13) -- wrong slot type for this item"), so the
after-state that was claimed never happened.  Both are parked
`expect=unlabelled` with a `why=` naming the missing input, per the corpus's
own rule for parked steps; the re-record that seeds a real weapon after the
snapshot is Leg H / record.sh --seed-weapon.

    make_timeline.py [out.tsv] [--db-before <corpus>/shared/db_before]
"""
import argparse
import os
import re, sys
from collections import Counter

# The T492 reference snapshot, committed beside this script so the generator
# is usable offline.  It is a copy of the recorded run's
# <corpus>/shared/db_before/ (verify_labels.py:133), taken from
# /home/user/kb-evidence/reference/T492/db_before/; inventory.tsv is whole
# and snapshot.tsv is reduced to the three characters this timeline drives.
HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DB_BEFORE = os.path.join(HERE, "fixtures", "T492", "db_before")

# Driver cost per row, measured shape (see the plan's Review):
#   every row       ~3 s   official window+root grab + chat OCR
#   p1 cmd          ~10-25 s  three Enter/type/Enter attempts with an OCR gate
#   p1 key/nop      ~4 s
#   p1 hold N       N + 3 s
#   p1 video N      N + 3 s
GAP_AFTER_P1_CMD = 26
GAP_AFTER_P1_KEY = 12
GAP_AFTER_WILLEQ = 9


def gap_for(player, action):
    if player != "p1":
        return GAP_AFTER_WILLEQ
    if action.startswith("cmd"):
        return GAP_AFTER_P1_CMD
    if action.startswith("hold") or action.startswith("video"):
        return int(action.split()[-1]) + 8
    return GAP_AFTER_P1_KEY


class InventoryError(Exception):
    """A held= claim cannot be derived from the inventory snapshot."""


def read_inventory(db_before):
    """{character_id: {slot_id: item_id}} from
    <db_before>/inventory.tsv -- the SAME file and the SAME corpus-relative
    convention the verifier scores held= claims against (verify_labels.py:
    `invf = os.path.join(C, "shared", "db_before", "inventory.tsv")`).  The
    slot numbers are inventory.slot_id, the server's numbering (CLAUDE.md
    "Inventory slot numbering") -- NOT converted.  A missing or empty
    snapshot raises; it never degrades to constants."""
    path = os.path.join(db_before, "inventory.tsv")
    if not os.path.isfile(path):
        raise InventoryError(f"no inventory snapshot at {path}: every held= "
                             f"claim must come from the run's snapshot; a "
                             f"missing snapshot is a hard error (B793)")
    inv = {}
    with open(path) as f:
        for line in f:
            f_ = line.rstrip("\n").split("\t")
            if len(f_) >= 3 and f_[0].isdigit() and f_[1].isdigit():
                inv.setdefault(int(f_[0]), {})[int(f_[1])] = int(f_[2])
    if not inv:
        raise InventoryError(f"{path} carried no rows")
    return inv


def read_characters(db_before):
    """{character_name: character_id} from <db_before>/snapshot.tsv (the
    character_data rows the snapshot took) -- the same name->id join the
    verifier does for the T492 trio, read from the file instead of a
    hardcoded map."""
    path = os.path.join(db_before, "snapshot.tsv")
    if not os.path.isfile(path):
        raise InventoryError(f"no character snapshot at {path}: the character "
                             f"name -> id join must come from the run's "
                             f"snapshot (B793)")
    chars = {}
    with open(path) as f:
        for line in f:
            f_ = line.rstrip("\n").split("\t")
            if len(f_) >= 2 and f_[0].isdigit() and f_[1].strip():
                chars[f_[1]] = int(f_[0])
    if not chars:
        raise InventoryError(f"{path} carried no rows")
    return chars


def item_names():
    """{item_id: name} from data/item_idfile.tsv (T498's committed export).
    The name only -- this file carries id / name / idfile (the IT model),
    NOT the item TYPE and NOT a slots bitmask, which is exactly why
    Primary(13) eligibility cannot be established from it (B793)."""
    path = os.path.join(HERE, "..", "..", "data", "item_idfile.tsv")
    names = {}
    with open(path) as f:
        for line in f:
            if line.startswith("#"):
                continue
            f_ = line.rstrip("\n").split("\t")
            if len(f_) >= 2 and f_[0].isdigit():
                names[int(f_[0])] = f_[1]
    return names


def held_a(char_inv, names):
    """The held-A claim for slots 13/14 read straight from the snapshot:
    `13=<id>,14=<id>` -- every number derived, the exact pair
    verify_labels.py's db_inventory check scores (`re.findall
    r"(\\d{1,2})=(\\d{3,6})", held)`).  A slot with no item is a loud error:
    held-A means the character IS holding something there.  The names are
    NOT in the claim: the committed reference claims carry only the numbers
    (AC4: Testbuddy's rows stay `13=9997,14=9979`), and a name the verifier
    cannot score would be a second hand-typed value in disguise."""
    if not char_inv:
        raise InventoryError("held-A: the snapshot has no rows for this "
                             "character (B793)")
    for slot in (13, 14):
        if char_inv.get(slot) is None:
            raise InventoryError(f"held-A: the snapshot has no item at slot "
                                 f"{slot}; the claim cannot be derived "
                                 f"(B793)")
    return f"13={char_inv[13]},14={char_inv[14]}"


# The /equip step's action is a driver command: its source slot is read from
# the snapshot -- general slot 23 (inventory.slot_id) is the slot Leg C's
# `/equip 23 13` addressed, and both willeq characters hold an item there.
# What the snapshot CANNOT establish is whether that item may go in Primary
# (13): no repo file carries the item TYPE or a slots bitmask, so the
# after-state is parked, not claimed (B793, AC3).
EQUIP_SRC_SLOT = 23

# The E_heldB_* frames' parking note: the missing input, named.
HELD_B_WHY = ("B793: no repo file establishes a general slot's eligibility "
              "for Primary (13) -- the inventory snapshot carries no "
              "slot-type / slots bitmask and data/item_idfile.tsv carries "
              "the item model (IT<n>), not the item TYPE; Leg C's `/equip "
              "23 13` was refused by the client (\"cannot go in Primary "
              "(13) -- wrong slot type for this item\"), so the after-state "
              "was never claimed.  Re-recorded with a weapon seeded after "
              "the snapshot by record.sh --seed-weapon (Leg H)")


def build_timeline(db_before):
    """The full timeline, every `held=` value read from the snapshot in
    `db_before`.  Raises InventoryError before returning if any claim cannot
    be derived -- main() turns that into a loud error (exit 2)."""
    inv = read_inventory(db_before)
    chars = read_characters(db_before)
    names = item_names()

    def slots_of(name):
        cid = chars.get(name)
        if cid is None:
            raise InventoryError(f"the snapshot has no character row named "
                                 f"{name}; the held= claim cannot be derived "
                                 f"(B793)")
        return inv.get(cid, {})

    ha2 = held_a(slots_of("Testcaster"), names)
    ha3 = held_a(slots_of("Testbuddy"), names)

    def equip_note(name):
        item = slots_of(name).get(EQUIP_SRC_SLOT)
        item_txt = (f"{item} {names.get(item, 'unknown item')}"
                    if item is not None else "no item")
        return (f"source slot {EQUIP_SRC_SLOT} holds {item_txt} per the "
                f"inventory snapshot (B793); no repo file establishes that "
                f"item's eligibility for Primary (13), so the after-state is "
                f"not claimed -- see the E_heldB_* why=")

    rows = []

    def R(player, action, **kv):
        t = 0.0 if not rows else rows[-1][0] + gap_for(rows[-1][1], rows[-1][2])
        rows.append((round(t, 1), player, action,
                     ";".join(f"{k}={v}" for k, v in kv.items())))

    Q = dict(zone="qeynos2")
    # match: both sides in the fitted First Person camera, p1's untouched login one
    M = dict(camera="FirstPerson", official_camera="FirstPerson_login_untouched",
             expect="match")
    U = dict(expect="unlabelled")
    PARKED_B = dict(expect="unlabelled", why=HELD_B_WHY)

    # === A -- zone in, and the First Person block (p1 stays in its LOGIN camera) ==
    R("p1", "nop", label="A_zonein_p1", char="Oraclealpha",
      camera="FirstPerson_login_untouched", f9_presses="0", **Q, **U)
    R("p2", "/camera firstperson", label="A_cam_p2_firstperson", char="Testcaster", **Q, **M)
    R("p3", "/camera firstperson", label="A_cam_p3_firstperson", char="Testbuddy", **Q, **M)
    R("p1", "cmd /loc|Location", label="A_loc_p1", **Q, **U)
    R("p2", "/loc", label="A_loc_p2", **Q, **M)
    R("p3", "/loc", label="A_loc_p3", **Q, **M)
    R("p1", "cmd /who|Players", label="A_who_p1", **Q, **U)
    R("p2", "/who", label="A_who_p2", **Q, **M)
    R("p3", "/who", label="A_who_p3", **Q, **M)
    R("p1", "nop", label="A_idle0_p1", camera="FirstPerson_login_untouched", **Q, **U)
    R("p2", "/camera get", label="A_camget_p2", **Q, **M)
    R("p3", "/camera get", label="A_camget_p3", **Q, **M)
    R("p1", "nop", label="A_idle_p1", camera="FirstPerson_login_untouched", **Q, **U)

    # === B -- /say and /tell, so the logs cross-reference ========================
    R("p1", "cmd /say T492 p1 Oraclealpha greets Testcaster and Testbuddy|T492",
      label="B_say_p1", said="T492 p1 Oraclealpha greets Testcaster and Testbuddy", **Q, **U)
    R("p2", "/say T492 p2 Testcaster greets Oraclealpha and Testbuddy", label="B_say_p2",
      said="T492 p2 Testcaster greets Oraclealpha and Testbuddy", **Q, **M)
    R("p3", "/say T492 p3 Testbuddy greets Oraclealpha and Testcaster", label="B_say_p3",
      said="T492 p3 Testbuddy greets Oraclealpha and Testcaster", **Q, **M)
    R("p1", "cmd /tell Testcaster T492 p1 tells p2|", label="B_tell_p1_p2", told="Testcaster", **Q, **U)
    R("p2", "/tell Oraclealpha T492 p2 tells p1", label="B_tell_p2_p1", told="Oraclealpha", **Q, **M)
    R("p3", "/tell Oraclealpha T492 p3 tells p1", label="B_tell_p3_p1", told="Oraclealpha", **Q, **M)
    R("p1", "cmd /tell Testbuddy T492 p1 tells p3|", label="B_tell_p1_p3", told="Testbuddy", **Q, **U)
    R("p2", "/tell Testbuddy T492 p2 tells p3", label="B_tell_p2_p3", told="Testbuddy", **Q, **M)
    R("p3", "/tell Testcaster T492 p3 tells p2", label="B_tell_p3_p2", told="Testcaster", **Q, **M)
    R("p1", "nop", label="B_idle2_p1", camera="FirstPerson_login_untouched", **Q, **U)
    R("p2", "/say T492 p2 heard the tells", label="B_say2_p2", said="T492 p2 heard the tells", **Q, **M)
    R("p3", "/say T492 p3 heard the tells", label="B_say2_p3", said="T492 p3 heard the tells", **Q, **M)

    # === C -- targeting each other ===============================================
    R("p1", "key F8", label="C_target_p1_f8", target="F8_nearest",
      camera="FirstPerson_login_untouched", **Q, **U)
    R("p2", "/target Oraclealpha", label="C_target_p2_p1", target="Oraclealpha", **Q, **M)
    R("p3", "/target Oraclealpha", label="C_target_p3_p1", target="Oraclealpha", **Q, **M)
    R("p1", "cmd /target Testcaster|", label="C_target_p1_p2", target="Testcaster", **Q, **U)
    R("p2", "/target Testbuddy", label="C_target_p2_p3", target="Testbuddy", **Q, **M)
    R("p3", "/target Testcaster", label="C_target_p3_p2", target="Testcaster", **Q, **M)
    R("p1", "cmd /loc|Location", label="C_loc_p1", **Q, **U)
    R("p2", "/loc", label="C_loc2_p2", target="Testbuddy", **Q, **M)
    R("p3", "/loc", label="C_loc2_p3", target="Testcaster", **Q, **M)
    R("p1", "nop", label="C_idle_p1", camera="FirstPerson_login_untouched", **Q, **U)
    R("p2", "/who", label="C_who2_p2", **Q, **M)
    R("p3", "/who", label="C_who2_p3", **Q, **M)

    # === D -- the camera cycle.  p1 leaves First Person here; nothing after this
    #          row is a match step, because no other camera pair has been fitted. =
    R("p1", "key F9", label="D_cam_p1_f9x1", f9_presses="1",
      camera="unnamed_by_the_client", **Q, **U)
    R("p2", "/camera overhead", label="D_cam_p2_overhead", camera="Overhead", **Q, **U)
    R("p3", "/camera overhead", label="D_cam_p3_overhead", camera="Overhead", **Q, **U)
    R("p1", "key F9", label="D_cam_p1_f9x2", f9_presses="2", camera="unnamed_by_the_client", **Q, **U)
    R("p2", "/camera chase", label="D_cam_p2_chase", camera="Chase", **Q, expect="discrepancy:B721")
    R("p3", "/camera chase", label="D_cam_p3_chase", camera="Chase", **Q, expect="discrepancy:B721")
    R("p1", "key F9", label="D_cam_p1_f9x3", f9_presses="3", camera="unnamed_by_the_client", **Q, **U)
    R("p2", "/camera user1", label="D_cam_p2_user1", camera="User1", **Q, **U)
    R("p3", "/camera user1", label="D_cam_p3_user1", camera="User1", **Q, **U)
    R("p1", "key F9", label="D_cam_p1_f9x4", f9_presses="4", camera="unnamed_by_the_client", **Q, **U)
    R("p2", "/camera user2", label="D_cam_p2_user2", camera="User2", **Q, **U)
    R("p3", "/camera user2", label="D_cam_p3_user2", camera="User2", **Q, **U)
    R("p1", "key F9", label="D_cam_p1_f9x5", f9_presses="5", camera="unnamed_by_the_client", **Q, **U)
    R("p2", "/camera tether", label="D_cam_p2_tether", camera="Tether", **Q, **U)
    R("p3", "/camera tether", label="D_cam_p3_tether", camera="Tether", **Q, **U)
    R("p1", "key F9", label="D_cam_p1_f9x6", f9_presses="6", camera="unnamed_by_the_client", **Q, **U)
    R("p2", "/camera get", label="D_camget_p2", **Q, **U)
    R("p3", "/camera get", label="D_camget_p3", **Q, **U)
    R("p1", "key F9", label="D_cam_p1_f9x7", f9_presses="7",
      camera="unnamed_by_the_client",
      note="hermes-ext measured the in-world F9 cycle at period ~7 against T466's "
           "six named modes; presses are COUNTED here and nothing is resolved", **Q, **U)

    # === E -- held items (B722/B725); idle frames at Chase and Tether ==========
    # B793: every held= below is read from the inventory snapshot in
    # db_before (the same file the verifier scores against); the /equip
    # after-state and the held-B frames are parked with a why=, because no
    # repo file establishes the source slot's eligibility for Primary (13).
    R("p2", "/camera chase", label="E_heldA_p2_chase", camera="Chase",
      held=ha2, **Q, expect="discrepancy:B722")
    R("p3", "/camera chase", label="E_heldA_p3_chase", camera="Chase",
      held=ha3, **Q, expect="discrepancy:B722")
    R("p1", "cmd #wc 7 10|", label="E_wc_p1_it10",
      held_visual="#wc slot 7 material 10 (IT10 arrow) -- visual only, no relogin (T478)", **Q, **U)
    R("p2", "/camera tether", label="E_heldA_p2_tether", camera="Tether",
      held=ha2, **Q, expect="discrepancy:B722")
    R("p3", "/camera tether", label="E_heldA_p3_tether", camera="Tether",
      held=ha3, **Q, expect="discrepancy:B722")
    R("p1", "nop", label="E_idle_p1", **Q, **U)
    R("p2", f"/equip {EQUIP_SRC_SLOT} 13", label="E_equip_p2_23to13",
      note=equip_note("Testcaster"), **Q, **U)
    R("p3", f"/equip {EQUIP_SRC_SLOT} 13", label="E_equip_p3_23to13",
      note=equip_note("Testbuddy"), **Q, **U)
    R("p1", "cmd #wc 7 4|", label="E_wc_p1_m4", held_visual="#wc slot 7 material 4", **Q, **U)
    R("p2", "/camera chase", label="E_heldB_p2_chase", camera="Chase", **Q, **PARKED_B)
    R("p3", "/camera chase", label="E_heldB_p3_chase", camera="Chase", **Q, **PARKED_B)
    R("p1", "cmd /loc|Location", label="E_loc_p1", **Q, **U)
    R("p2", "/camera tether", label="E_heldB_p2_tether", camera="Tether", **Q, **PARKED_B)
    R("p3", "/camera tether", label="E_heldB_p3_tether", camera="Tether", **Q, **PARKED_B)

    # === F -- movement, the sky (B781) and the distant trees (B782) =============
    R("p2", "/camera chase", label="F_pre_p2_chase", camera="Chase", **Q, **U)
    R("p3", "/camera chase", label="F_pre_p3_chase", camera="Chase", **Q, **U)
    R("p1", "hold w 20", label="F_run_p1_hold_w_20s", moved="held w for 20 s", **Q, **U)
    R("p2", "/move -84 528 3.751", label="F_run_p2_move", moved="/move -84 528 3.751 (CLIENT frame)", **Q, **U)
    R("p3", "/move -64 528 3.751", label="F_run_p3_move", moved="/move -64 528 3.751 (CLIENT frame)", **Q, **U)
    R("p2", "nop", label="F_run_p2_mid", **Q, **U)
    R("p3", "nop", label="F_run_p3_mid", **Q, **U)
    R("p1", "cmd /loc|Location", label="F_loc_p1_after_run", **Q, **U)
    R("p2", "/loc", label="F_loc_p2_after_run", **Q, **U)
    R("p3", "/loc", label="F_loc_p3_after_run", **Q, **U)
    R("p2", "/turn 90", label="F_turn_p2", moved="/turn 90", **Q, **U)
    R("p3", "/turn 90", label="F_turn_p3", moved="/turn 90", **Q, **U)
    R("p2", "/camera overhead", label="F_sky_p2_overhead", camera="Overhead", sky="in frame", **Q, expect="discrepancy:B781")
    R("p3", "/camera overhead", label="F_sky_p3_overhead", camera="Overhead", sky="in frame", **Q, expect="discrepancy:B781")
    R("p1", "nop", label="F_sky_p1", sky="in frame", **Q, **U)

    # === G -- the fight, a death and a respawn ==================================
    R("p1", "cmd #npctypespawn 77002|", label="G_spawn_p1_dummy",
      spawned="npc 77002 a_training_dummy (runtime spawn; removed with the world "
              "down before the restore -- B720)", **Q, **U)
    R("p2", "/camera chase", label="G_pre_p2_chase", camera="Chase", **Q, expect="discrepancy:B721")
    R("p3", "/camera chase", label="G_pre_p3_chase", camera="Chase", **Q, expect="discrepancy:B721")
    R("p1", "key F8", label="G_target_p1_dummy", target="F8_nearest (the dummy)", **Q, **U)
    R("p2", "/target a_training_dummy000", label="G_target_p2_dummy", target="a_training_dummy000", **Q, **U)
    R("p3", "/target a_training_dummy000", label="G_target_p3_dummy", target="a_training_dummy000", **Q, **U)
    R("p1", "cmd /attack on|attack", label="G_attack_p1", attacking="a training dummy", **Q, **U)
    R("p2", "/attack", label="G_attack_p2", attacking="a_training_dummy000", **Q, **U)
    R("p3", "/attack", label="G_attack_p3", attacking="a_training_dummy000", **Q, **U)
    R("p1", "video G_attack_p1_30s 30", label="G_attack_p1_video",
      attacking="a training dummy",
      note="temporal step: 30 fps lossless root grab (CLAUDE.md never-sample-a-"
           "transient-with-stills); the stills at each mark remain primary", **Q, **U)
    R("p2", "nop", label="G_attack_p2_mid", attacking="a_training_dummy000", **Q, **U)
    R("p3", "nop", label="G_attack_p3_mid", attacking="a_training_dummy000", **Q, **U)
    R("p2", "nop", label="G_attack_p2_late", attacking="a_training_dummy000", **Q, **U)
    R("p3", "nop", label="G_attack_p3_late", attacking="a_training_dummy000", **Q, **U)
    R("p1", "cmd /target Oraclealpha|", label="G_selftarget_p1", target="Oraclealpha", **Q, **U)
    R("p1", "cmd #kill|", label="G_death_p1",
      died="Oraclealpha killed its own targeted self with the GM #kill", **Q, **U)
    R("p2", "/say T492 p2 saw Oraclealpha die", label="G_death_seen_p2",
      said="T492 p2 saw Oraclealpha die", **Q, **U)
    R("p3", "/say T492 p3 saw Oraclealpha die", label="G_death_seen_p3",
      said="T492 p3 saw Oraclealpha die", **Q, **U)
    R("p1", "cmd /loc|Location", label="G_respawn_p1_loc", respawned="after #kill", **Q, **U)
    R("p1", "cmd #summonitem 9998|", label="H_summonitem_p1",
      inventory="#summonitem 9998 short sword onto the cursor", **Q, **U)
    R("p2", "/equip list", label="H_equiplist_p2", **Q, **U)
    R("p3", "/equip list", label="H_equiplist_p3", **Q, **U)
    R("p2", "/gems", label="H_gems_p2", **Q, **U)
    R("p3", "/gems", label="H_gems_p3", **Q, **U)

    # === H -- the zone line, qeynos2 -> qeytoqrg, all three =====================
    R("p1", "cmd #movechar Testcaster qeytoqrg|", label="H_zoneline_p2_moved",
      zoning="Testcaster -> qeytoqrg by the GM's #movechar", **Q, **U)
    R("p1", "cmd #movechar Testbuddy qeytoqrg|", label="H_zoneline_p3_moved",
      zoning="Testbuddy -> qeytoqrg by the GM's #movechar", **Q, **U)
    R("p2", "/loc", label="H_after_zone_p2", zone="qeytoqrg", **U)
    R("p3", "/loc", label="H_after_zone_p3", zone="qeytoqrg", **U)
    R("p1", "cmd #zone qeytoqrg|", label="H_zoneline_p1",
      zone="qeytoqrg", zoning="Oraclealpha -> qeytoqrg by the GM's #zone", **U)
    R("p2", "/who", label="H_who_p2_qeytoqrg", zone="qeytoqrg", **U)
    R("p3", "/who", label="H_who_p3_qeytoqrg", zone="qeytoqrg", **U)
    R("p1", "cmd /loc|Location", label="H_loc_p1_qeytoqrg", zone="qeytoqrg", **U)
    R("p2", "/camera chase", label="H_cam_p2_qeytoqrg_chase", zone="qeytoqrg", camera="Chase",
      trees="a distant tree line is in frame", expect="discrepancy:B782")
    R("p3", "/camera chase", label="H_cam_p3_qeytoqrg_chase", zone="qeytoqrg", camera="Chase",
      trees="a distant tree line is in frame", expect="discrepancy:B782")
    R("p1", "nop", label="H_idle_p1_qeytoqrg", zone="qeytoqrg", **U)
    R("p2", "/camera firstperson", label="H_cam_p2_qeytoqrg_fp", zone="qeytoqrg", camera="FirstPerson", **U)
    R("p3", "/camera firstperson", label="H_cam_p3_qeytoqrg_fp", zone="qeytoqrg", camera="FirstPerson", **U)
    R("p2", "/say T492 p2 is about to camp", label="I_say_p2_camp", zone="qeytoqrg", **U)
    R("p3", "/say T492 p3 is about to camp", label="I_say_p3_camp", zone="qeytoqrg", **U)
    R("p1", "cmd /camp|", label="I_camp_p1", zone="qeytoqrg",
      note="camp order is p3, p2 (from their own scripts' trailing /camp) then p1", **U)

    return rows


HDR = """# T492 RECORDING timeline (Leg C) -- GENERATED by scripts/llmbench/make_timeline.py.
#   t_offset<TAB>player<TAB>action<TAB>truth k=v;...
# p1 = official Titanium (Oraclealpha, willeq_a, GM 250 -- the ONLY account
#      that issues a `#` command anywhere in this file), driven live.
# p2 = willeq, dev host    (Testcaster, willeq_test), --script.
# p3 = willeq, hermes-ext  (Testbuddy,  willeq_buddy), --script.
# EVERY row also takes an official window+root pair, so every step of every
# player has a paired official frame.
# expect= comes from a refactor/BOARD.md B-number or a prior oracle fit and
# from nowhere else; everything else is `unlabelled` and the grader ignores it.
# The E block's held= values are read from the run's inventory snapshot
# (make_timeline.py --db-before, the same <corpus>/shared/db_before/
# inventory.tsv verify_labels.py scores against) and no hand-typed item id
# remains in a held= claim (B793); the E_heldB_* frames are parked with a
# why= naming the missing input.
# Nothing here is a visual judgement: the truth is what the driver COMMANDED.
"""


def main():
    ap = argparse.ArgumentParser(
        description="generate the T492 Leg C recording timeline")
    ap.add_argument("out", nargs="?", default="timeline.tsv")
    ap.add_argument("--db-before", default=DEFAULT_DB_BEFORE,
                    help="the run's <corpus>/shared/db_before/ directory "
                         "(verify_labels.py's convention); every held= claim "
                         "is read from its inventory.tsv + snapshot.tsv")
    args = ap.parse_args()
    try:
        rows = build_timeline(args.db_before)
    except InventoryError as e:
        print(f"make_timeline.py: ERROR: {e}", file=sys.stderr)
        return 2
    with open(args.out, "w") as f:
        f.write(HDR + "".join(f"{a}\t{b}\t{c}\t{d}\n" for a, b, c, d in rows))
    per = Counter(r[1] for r in rows)
    exp = Counter(re.search(r"expect=([^;]*)", r[3]).group(1) for r in rows)
    print(f"{args.out}: {len(rows)} rows, {rows[-1][0]/60:.1f} min of timeline")
    print("  own actions per player:", dict(per))
    print("  steps recorded per player:", len(rows), "(every row frames every player)")
    print("  expect labels:", dict(exp))
    return 0


if __name__ == "__main__":
    sys.exit(main())
