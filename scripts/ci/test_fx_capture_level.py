#!/usr/bin/env python3
"""B1171 (#490): scripts/fx/capture_level.py -- the oracle capture's `#level`, the
harness run's level, and the refusal of a comparison row whose levels differ.

Fresh temp fixtures per test, in the shape of a lane-5 capture (plan.tsv +
steps.tsv, review/B1130/oracle/<capture>/) and a model_viewer --spell-test
frames dir (timing.tsv, with and without B1171's caster_level column).
"""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'fx', 'capture_level.py')
sys.path.insert(0, os.path.join(HERE, '..', 'fx'))
import capture_level as CL  # noqa: E402

STEPS_HDR = 'step\tbegin_epoch\tend_epoch\taction\ttarget\tstill\tframe\topcodes\tini_diff\tpacket_rows\n'
TIMING_HDR = 'frame\tt\tevent\tspell\ttarget\tdistance\tcast_ms\tt_event'


class CaptureLevel(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def capture(self, name, plan_cmds, step_cmds):
        d = os.path.join(self.root, name)
        os.makedirs(d)
        with open(os.path.join(d, 'plan.tsv'), 'w') as f:
            f.write('# action\ttarget\thold_ms\tstill_name\nlogin\tTestcaster\t0\tchar_select\n')
            for c in plan_cmds:
                f.write(f'command\t{c}\t0\tx\n')
        with open(os.path.join(d, 'steps.tsv'), 'w') as f:
            f.write(STEPS_HDR)
            for i, c in enumerate(step_cmds):
                f.write(f'{i:02d}_x\t1.0\t2.0\tcommand\t{c}\tx.png\tx.frame.png\tx.tsv\tx.diff\t1\n')
        return d

    def frames(self, name, levels, with_column=True):
        d = os.path.join(self.root, name)
        os.makedirs(d)
        with open(os.path.join(d, 'timing.tsv'), 'w') as f:
            f.write(TIMING_HDR + ('\tcaster_level\n' if with_column else '\n'))
            f.write('0\t0.0\t-\t-\t-\t-\t-\t-' + ('\t-\n' if with_column else '\n'))
            for ev, lv in zip(('BeginCast', 'Action'), levels):
                f.write(f'45\t1.5\t{ev}\t93\tother\t0.000\t1500\t1.5' + (f'\t{lv}\n' if with_column else '\n'))
        return d

    def plan(self, name, level):
        p = os.path.join(self.root, name)
        with open(p, 'w') as f:
            f.write('t_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\tpackets\n')
            f.write(f'1.5\t93\tother\t0\t1500\t{level}\n')
        return p

    # -- the oracle side --
    def test_the_executed_level_command_is_the_capture_level(self):
        d = self.capture('r51', ['#memspell 93 0', '#level 60', '#mana'], ['#memspell 93 0', '#level 60', '#mana'])
        self.assertEqual(CL.capture_level(d), 60)

    def test_eqemus_set_level_form_is_read(self):
        d = self.capture('c', ['#set level 45'], ['#set level 45'])
        self.assertEqual(CL.capture_level(d), 45)

    def test_plan_and_executed_steps_must_agree(self):
        d = self.capture('c', ['#level 60'], ['#level 50'])
        with self.assertRaises(CL.LevelError):
            CL.capture_level(d)

    def test_a_capture_without_an_executed_level_step_is_an_error_not_a_default(self):
        d = self.capture('c', ['#level 60'], ['#mana'])
        with self.assertRaises(CL.LevelError):
            CL.capture_level(d)

    def test_the_record_level_comes_from_the_capture_not_the_class_level_column(self):
        cap = self.capture('r51', ['#level 60'], ['#level 60'])
        table = os.path.join(self.root, 'record_table.tsv')
        with open(table, 'w') as f:
            f.write('record\tspells_f123\tcapture\tf144_group\tspells_in_f144_group\tspell_id\tname\tclass\tlevel\n')
            f.write('0\t1\tr51\t1\t93\t93\tBurst of Flame\tDRU\t1\n')
        self.assertEqual(CL.b1130_record_capture('0', table), 'r51')
        self.assertEqual(CL.b1130_record_level('0', table, os.path.dirname(cap)), 60)

    def test_a_tour_spell_reads_its_oracle_legs_level(self):
        self.capture('oracle_druid', ['#level 60'], ['#level 60'])
        fx = os.path.join(self.root, 'spell_fx_table.tsv')
        with open(fx, 'w') as f:
            f.write('spell\tname\tstage\tclient\tvideo\n')
            f.write('93\tBurst of Flame\tSOURCE\toracle\toracle_druid/video.mp4 25fps\n')
            f.write('93\tBurst of Flame\tSOURCE\tfable\tfable_B/video.mkv 30fps\n')
        self.assertEqual(CL.b1118_spell_capture('93', fx), 'oracle_druid')
        self.assertEqual(CL.b1118_spell_level('93', fx, self.root), 60)

    # -- the harness side --
    def test_the_harness_level_is_timing_tsvs_column(self):
        self.assertEqual(CL.harness_level(self.frames('f', [60, 60])), (60, 'timing.tsv'))

    def test_an_older_run_falls_back_to_its_plan(self):
        lv, src = CL.harness_level(self.frames('f', [], with_column=False), self.plan('r0.plan', 1))
        self.assertEqual(lv, 1)
        self.assertIn('plan', src)

    def test_a_dash_plan_level_is_unknown_not_the_binary_default(self):
        lv, _ = CL.harness_level(self.frames('f', [], with_column=False), self.plan('r0.plan', '-'))
        self.assertIsNone(lv)

    def test_a_run_with_two_levels_is_an_error(self):
        with self.assertRaises(CL.LevelError):
            CL.harness_level(self.frames('f', [1, 60]))

    # -- the comparison --
    def test_a_row_whose_harness_level_differs_from_the_oracle_is_refused(self):
        with self.assertRaises(CL.LevelMismatch):
            CL.refuse(60, 1)
        with self.assertRaises(CL.LevelMismatch):
            CL.refuse(60, None)
        CL.refuse(60, 60)
        self.assertEqual(CL.verdict_or_refusal(60, 1, 'short'), 'REFUSED(L1!=60)')
        self.assertEqual(CL.verdict_or_refusal(60, 60, 'short'), 'short')

    def test_the_cli_check_exits_3_on_a_mismatch(self):
        r = subprocess.run([sys.executable, '-I', SCRIPT, 'check', '60', '1'], capture_output=True, text=True)
        self.assertEqual(r.returncode, 3, r.stderr)
        self.assertIn('REFUSED', r.stderr)
        r = subprocess.run([sys.executable, '-I', SCRIPT, 'check', '60', '60'], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == '__main__':
    unittest.main()
