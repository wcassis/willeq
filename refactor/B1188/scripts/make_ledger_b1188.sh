#!/usr/bin/env bash
# B1184: the three ledger clips (93, 38, 253) at the fable tip a99b3822.
# Oracle LEFT = the B1178 v3 pinned-camera (b1118 rig) official-client capture
# (review/B1178/worker/measure-v3-r3, the reference set this gate judges
# against); willeq RIGHT = this gate's tour renders (model_viewer --camera
# b1118, same rig). Clip/strip/index via the B1158 scripts (make_clip.py +
# make_index.py -- the make_ledger.sh pipeline); the spec JSONs use the exact
# ledger_inventory.py schema (oracle {video,t0,bc_epoch,act_epoch} over
# willeq {video,bc_t,act_t}) because the oracle side here is the B1178 v3
# capture, not a B1130 sweep capture (93/38 are tour spells, not b1130
# records; 253's b1130-camera oracle video is not on this host).
set -euo pipefail
EV=/mnt/scratch/kb-evidence/kb-b1188-landing-pixels/gate2
V3=/mnt/projects/fable/review/B1178/worker/measure-v3-r3
B1158=/home/user/src/willeq-wt/kb-b1188-landing-pixels/refactor/B1158/scripts
TOUR_TABLE=/home/user/src/willeq-wt/kb-b1188-landing-pixels/refactor/B1188/tables/tour.tsv
OUT="/home/user/kb-evidence/VISUAL-FX/b1188-gate"
mkdir -p "$OUT/clips" "$OUT/specs"
rm -f "$OUT/alignments.jsonl" "$OUT/clip.log"

gen_spec() {  # $1 spell, $2 leg dir
  python3 - "$EV" "$V3" "$1" "$2" "$TOUR_TABLE" "$OUT" <<'PYEOF'
import json, sys, os
ev, v3, spell, leg, tour_table, out = sys.argv[1:7]
ometas = f'{v3}/{leg}/spell{spell}/meta.json'
mdir = f'{ev}/measure/tour_{spell}'
wmeta = f'{mdir}/fable_{spell}/meta.json'
om = json.load(open(ometas)); wm = json.load(open(wmeta))
ovid = om['video']
if not ovid.startswith('/'):
    ovid = f"{v3}/{ovid.split('measure-v3-r3/', 1)[-1]}"
# the tour-table verdicts for this spell (Landing / End columns)
verdicts = []
if os.path.exists(tour_table):
    for line in open(tour_table):
        c = line.rstrip('\n').split('\t')
        if c and c[0] == spell:
            hdr = open(tour_table).readline().rstrip('\n').split('\t')
            idx = {h: i for i, h in enumerate(hdr)}
            verdicts = [['Landing', c[idx['v_land_cov']]], ['End', c[idx['v_end']]]]
spec = {
    'id': f'tour_{spell}',
    'group': 'gate',
    'title': f'B1184 | spell {spell} | tour (b1118 rig) | fable a99b3822 vs B1178 v3',
    'oracle': {'video': ovid, 't0': om['offset'], 'bc_epoch': om['t_begincast'],
               'act_epoch': om['t_action'],
               'label': f'B1178 v3 {leg} (pinned b1118, 30 fps)'},
    'willeq': {'video': f'{mdir}/video.mkv', 'bc_t': wm['t_begincast'],
               'act_t': wm['t_action'], 'label': f'gate tour_{spell} (fable a99b3822)'},
    'verdicts': verdicts,
    'note': ('oracle = the B1178 reference capture (measure-v3-r3); '
             'willeq = the B1184 re-render at the same b1118 rig'),
}
with open(f'{out}/specs/tour_{spell}.json', 'w') as f:
    json.dump(spec, f, indent=1)
print(f"spec tour_{spell}: oracle={ovid} bc-{om['offset']:.2f}.. willeq bc={wm['t_begincast']} act={wm['t_action']}", file=sys.stderr)
PYEOF
}

gen_spec 93 b1118ref_dru5
gen_spec 38 b1118ref_wiz5
gen_spec 253 b1118ref_dru5

fail=0
for f in "$OUT"/specs/*.json; do
  python3 "$B1158/make_clip.py" "$f" "$OUT/clips" >> "$OUT/alignments.jsonl" 2>>"$OUT/clip.log" || fail=1
done
[ "$fail" = 0 ] || { echo "CLIP FAILED (see $OUT/clip.log)" >&2; exit 1; }
python3 "$B1158/make_index.py" "$OUT/alignments.jsonl" "$OUT"
echo "[done] $OUT" >&2
