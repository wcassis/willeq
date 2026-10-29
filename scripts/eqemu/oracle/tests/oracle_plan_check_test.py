#!/usr/bin/env python3
"""B1129 (#449): device-free net for the oracle plan's gem-cast class/level check.

The official client refuses a gem cast its class cannot use and sends nothing
(refactor/B1118/README.md); oracle_plan_check.py must refuse such a plan with the
spell and the class named, and pass a castable one. The spells fixture carries
the real spells_us.txt class-level fields of four spells; when the EQ client
files are present they are cross-checked against the real file."""
import io
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import oracle_plan_check as opc  # noqa: E402

# id, name, {class: level} -- every other class 255 (spells_us.txt fields 104..119)
SPELLS = {
    93: ('Burst of Flame', {6: 1, 10: 1, 13: 1}),
    253: ('Whirling Wind', {6: 5}),
    54: ('Frost Bolt', {12: 1}),
    458: ("Fire Spiral of Al'Kabor", {12: 20}),
}


def fixture_row(sid):
    name, levels = SPELLS[sid]
    f = ['0'] * 160
    f[0], f[1] = str(sid), name
    for c in range(1, 17):
        f[103 + c] = str(levels.get(c, 255))
    return '^'.join(f)


def run(plan_text, tmp, spells, *args):
    plan = Path(tmp) / 'plan.tsv'
    plan.write_text(plan_text)
    out = io.StringIO()
    argv = ['check', str(plan), '--spells', str(spells)] + [str(a) for a in args]
    real_stdout, sys.stdout = sys.stdout, out
    try:
        rc = opc.main(argv)
    finally:
        sys.stdout = real_stdout
    return rc, out.getvalue()


HEAD = 'login\tTestcaster\t0\tselect\nzone\tqeynos2\t0\tin_world\n'
MEM = ''.join(f'command\t#memspell {sid} {slot}\t0\tm{slot}\n' for slot, sid in enumerate([93, 54, 253, 458]))


def main():
    # The lane table: lane 5 measured (B1118), lanes 1-4 NOT guessed.
    assert opc.load_pitch(5) == {'screen': '1280x800', 'x': 25, 'y1': 243, 'pitch': 30, 'gems': 8}
    for lane in (1, 2, 3, 4):
        assert opc.load_pitch(lane) is None, f'lane {lane} must stay unmeasured until measured'
    bar = opc.load_pitch(5)
    assert opc.gem_xy(bar, 1) == (25, 243) and opc.gem_xy(bar, 7) == (25, 423)
    assert opc.gem_at(bar, 25, 243) == 1 and opc.gem_at(bar, 21, 299) == 3 and opc.gem_at(bar, 1229, 555) is None

    with tempfile.TemporaryDirectory() as tmp:
        spells = Path(tmp) / 'spells_us.txt'
        spells.write_text('\n'.join(fixture_row(s) for s in SPELLS) + '\n')

        # 1. a druid-only spell on a wizard: refused, spell + class named.
        rc, out = run(HEAD + MEM + 'click\t25,303\t0\tgem3_cast253\n', tmp, spells, '--class', 12, '--lane', 5)
        assert rc == 1, out
        assert 'REFUSED' in out and '253 "Whirling Wind"' in out and 'WIZ (class 12)' in out and 'DRU 5' in out, out
        # ... and the same plan for a druid is castable.
        rc, out = run(HEAD + MEM + 'click\t25,303\t0\tgem3_cast253\n', tmp, spells, '--class', 6, '--level', 10, '--lane', 5)
        assert rc == 0 and 'OK line' in out and 'DRU (class 6) level 5' in out, out

        # 2. a castable one passes (wizard, Frost Bolt, gem 2).
        rc, out = run(HEAD + MEM + 'click\t25,273\t0\tgem2_cast54\n', tmp, spells, '--class', 12, '--level', 1, '--lane', 5)
        assert rc == 0 and '54 "Frost Bolt"' in out and 'REFUSED' not in out, out

        # 3. level: a level-10 wizard cannot cast the level-20 spell; '#level 60' first makes it castable.
        cast458 = 'click\t25,333\t0\tgem4_cast458\n'
        rc, out = run(HEAD + MEM + cast458, tmp, spells, '--class', 12, '--level', 10, '--lane', 5)
        assert rc == 1 and 'needs WIZ (class 12) level 20' in out and 'level 10' in out, out
        rc, out = run(HEAD + MEM + 'command\t#level 60\t0\tlvl\n' + cast458, tmp, spells, '--class', 12, '--level', 10, '--lane', 5)
        assert rc == 0, out

        # 4. '/cast N' is a gem cast too, on any lane; gems memorised before the plan come from --memmed.
        rc, out = run(HEAD + 'command\t/cast 1\t0\tc1\n', tmp, spells, '--class', 12, '--memmed', '0:93', '--lane', 1)
        assert rc == 1 and '93 "Burst of Flame"' in out and '/cast 1' in out, out

        # 5. an unmeasured lane does not map clicks to gems: noted, never guessed.
        rc, out = run(HEAD + MEM + 'click\t25,303\t0\tgem3\n', tmp, spells, '--class', 12, '--lane', 1)
        assert rc == 0 and 'unmeasured' in out and 'REFUSED' not in out, out

        # 6. a plan with no gem casts needs no spells file.
        rc, out = run(HEAD + 'click\t1229,555\t0\tclose_tip\n', tmp, Path(tmp) / 'missing.txt', '--class', 12, '--lane', 5)
        assert rc == 0, out

        # 7. B1192 (#511): /loot must be corpse-targeted AFTER the last #damage.
        #    The run-6 shape (/target <name> on the live mob, death clears HoTT)
        #    is refused with the reason named...
        KILL = ('command\t#spawn TrollF 1 1 0 50\t0\tspawn\n'
                'command\t/target TrollF\t0\ttgt\n'
                'command\t#damage 999999\t0\tkill\n')
        rc, out = run(HEAD + KILL + 'command\t/loot\t0\tloot\n',
                      tmp, Path(tmp) / 'missing.txt', '--class', 1, '--lane', 5)
        assert rc == 1 and 'REFUSED' in out and '/loot without corpse targeting' in out \
            and 'F8' in out, out
        # ... press F8 (TARGETNPC) after the kill clears it...
        rc, out = run(HEAD + KILL + 'press\tF8\t0\tf8\ncommand\t/loot\t0\tloot\n',
                      tmp, Path(tmp) / 'missing.txt', '--class', 1, '--lane', 5)
        assert rc == 0 and 'OK line' in out and 'REFUSED' not in out, out
        # ... and a click (the corpse-blob corrector) after the kill does too...
        rc, out = run(HEAD + KILL + 'click\t405,700\t0\tc\ncommand\t/loot\t0\tloot\n',
                      tmp, Path(tmp) / 'missing.txt', '--class', 1, '--lane', 5)
        assert rc == 0 and 'REFUSED' not in out, out
        # ... a target set before the kill does NOT count (that IS the run-6
        #    shape), and a looting plan with no kill still needs the targeting...
        rc, out = run(HEAD + 'command\t/target TrollF\t0\ttgt\ncommand\t/loot\t0\tloot\n',
                      tmp, Path(tmp) / 'missing.txt', '--class', 1, '--lane', 5)
        assert rc == 1 and 'REFUSED' in out, out
        rc, out = run(HEAD + 'command\t/loot\t0\tloot\n',
                      tmp, Path(tmp) / 'missing.txt', '--class', 1, '--lane', 5)
        assert rc == 1 and 'REFUSED' in out, out
        rc, out = run(HEAD + 'press\tF8\t0\tf8\ncommand\t/loot\t0\tloot\n',
                      tmp, Path(tmp) / 'missing.txt', '--class', 1, '--lane', 5)
        assert rc == 0 and 'REFUSED' not in out, out

    # The fixture's class levels are the real file's, when the client files are here.
    real = os.path.join(os.environ.get('EQ_CLIENT_PATH', '/mnt/projects/EverQuestP1999'), 'spells_us.txt')
    if os.path.isfile(real):
        got = opc.load_spells(real, set(SPELLS))
        for sid, (name, levels) in SPELLS.items():
            want = [levels.get(c, 255) for c in range(1, 17)]
            assert got[sid] == (name, want), (sid, got.get(sid), want)
        print('cross-checked the fixture against', real)
    print('PASS: druid-only gem cast on a wizard refused with spell + class; castable casts pass; '
          'level, /cast and unmeasured-lane cases; lane 5 pitch 30 at (25, 213+30i); '
          'B1192: /loot refused without corpse targeting (run-6 shape), cleared by press F8 or a click')


if __name__ == '__main__':
    main()
