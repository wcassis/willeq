# One-command official-client oracle capture (B1048)

Run from a willeq worktree on hermes-ext or dev-u24 (both need NumPy/Pillow for
the host-side report). The driver pairs `EQEMU_LANE=1..5`
with its official Titanium Wine client, takes the eqemu lock, starts the stack,
snapshots character/currency and inventory, starts pcap and video **before
login**, drives each TSV row, decodes packets, writes a step ledger and
summary, then stops the world before restoring and releases its exact lock.

```bash
bash scripts/eqemu/oracle/install.sh
bash scripts/eqemu/oracle/oracle_session.sh 2 \
  scripts/eqemu/oracle/plans/hotbutton_place.tsv \
  /home/user/kb-evidence/kb-b1048-oracle-driver/hotbutton
bash scripts/eqemu/oracle/oracle_session.sh 3 \
  scripts/eqemu/oracle/plans/charcreate_untouched.tsv \
  /home/user/kb-evidence/kb-b1048-oracle-driver/charcreate
bash scripts/eqemu/oracle/oracle_session.sh 4 \
  scripts/eqemu/oracle/plans/charcreate_barbarian_male.tsv \
  /home/user/kb-evidence/kb-b1048-oracle-driver/barbarian
```

**Lane 5 (the ThinkPad, `docs/thinkpad.md`) is the default oracle lane for
oracle legs** (B1057, #377): the stack and the Wine client share one x86 box
whose `:99` is a real Xorg on the Quadro (`oracle-xorg.service`), so the run
finds the display up and starts no Xvfb, and stop only ever kills an Xvfb the
session itself recorded in `xvfb.pid` (`scripts/eqemu/oracle/tests/remote_stop_safety.sh`). Use
lanes 1-4 (llvmpipe VMs) only when lane 5's lock is held or the leg needs a
second oracle in parallel. Two lane-5 differences are handled in
`oracle_remote.sh`: the capture drops the eqemu containers' veth copy of each
packet (`-i any` otherwise records every datagram twice, saved filter in
`pcap.filter`), and the run resets the long-lived Xorg's screen saver and
bounds the timing-marker wait. Measured per-step times against lanes 1/3/4
(`review/B1057/README.md`): the steps are bound by the driver's fixed sleeps,
so lane 5 saves about 25 s on login and little on create, not minutes.

`install.sh` also writes `~/eq-oracle/uigap-scripts/.oracle-repo`, so the
installed copy can be invoked from outside the checkout until that checkout
is moved or removed; reinstall it after moving the repo.

The four tab-separated columns are `action`, `target`, `hold_ms`, `still_name`.
`login <character>` starts Wine and enters character select; `zone <shortname>`
sets that character's zone **before** login and then enters world. `open`
accepts `hotbar`, `inventory`, `character-create`; `click` and `hold` use
root-screen `x,y`; `hold` uses its duration column in milliseconds. `press`
takes an xdotool keysym, `command` sends a slash command, `random`, `accept`,
`cancel`, `create <name> <race> <class> <gender>`, `still <name>`, and `wait <ms>`
are available. A create row has six columns: `create`, name, race, class,
gender, still name. Its coordinates come from the in-script table; each
control gets one click and is verified by the yellow selected-state (the
race and class lists share one selection cursor, and the screen restores
the previous session's selection with a settle animation that scrolls the
lists, so a missed or mis-landed click is retried once after an 8 s
settle and then fails the run). The selected race, class and gender are
re-verified after the ability-point allocation. The Name field is verified
non-empty before submit (a lost keystroke otherwise reaches submit and the
client answers with a "Name is not valid" dialog instead of a create
packet). The host report then decodes the step's `OP_CharacterCreate` and
requires its class/gender/race fields to match the row
(`scripts/eqemu/oracle/oracle_create.py`), so a wrong identity fails the
report even if the screen looked right. The shipped untouched
plans use Human-male and Barbarian-male Warrior paths without changing any
appearance control. Other verbs may put an expected OCR regex in column five;
the built-in verb assertions otherwise check the expected window or label.
Set `ORACLE_OWNER` to the unit's lock-owner prefix (default `oracle_session`).
Use a fresh output directory and a unique creation name. A plan cannot reuse an output directory containing
`session.started`.
The optional `# ini-unset=Section.Key` plan directive temporarily removes one
key from the login character's writable INI before capture. The driver saves
the original bytes, records the seeded before/changed after copies, then
restores the original byte-identically when the client stops. The hotbutton
plan uses it to make its placement diff reproducible even after an earlier
session filled that slot.

Output is under the given directory: `summary.md`, `steps.tsv`, per-step PNG,
frame PNG, opcode TSV and INI diff, `contact.png`, `step_frames.tsv`, raw
`video.mp4` and `capture.pcap`, `ledger_check.txt`, plus before/after DB and
inventory snapshots. A step's packet count in the summary cites its TSV;
`steps.tsv` records the interval and artifact names. The bundled decoder is
the existing UIGAP `eqpcap.py` with its Titanium opcode table pinned alongside;
the bundled video stepper is the existing `step_frames.py` and is run on
short action intervals. The client VM lacks NumPy/Pillow, so decoding and
frame analysis run on hermes-ext after copying raw capture bytes.
The driver also snapshots world and login-account IDs/names, removes only new
unowned test-account rows from a mistyped Wine login, and requires their
before/after censuses to match. `ledger_check.txt` comes from the adapted
UIGAP `check_ledgers.py` consistency pass. The final character-INI diff records
the post-session value separately; an individual step diff is empty until the
official client flushes the setting, so the report never assigns a later write
to an earlier step.

Exit codes: `0` complete; `64` invalid invocation or lane lock held; `65`
server/capture/driver failure; `66` restore or lock-release failure; `67`
client disconnected mid-plan. The disconnect result appends world/zone log
tail evidence to `summary.md` and saves `disconnect-logs.txt`. Never treat a
partial summary or a green process alone as a valid observation: inspect
`ledger_check.txt`, the relevant step files, and the cleanup line in
`driver.log`. The character-creation plan deletes its newly created character
with the world stopped, then verifies the snapshot.

## Template clicks (B1058)

A fixed root coordinate lands on whatever the client drew there; a template
lands on the control. `scripts/eqemu/oracle/oracle_locate.py` finds a
template in a screenshot with OpenCV's `TM_CCOEFF_NORMED` on the 8-bit luma,
via `cv2.matchTemplate` when OpenCV is importable and an equivalent NumPy FFT
implementation otherwise (`ORACLE_LOCATE_FORCE_NUMPY=1` forces it). Every
answer names the path that ran. Both paths agree to 1e-3 on the corpus.
Matching is brightness-invariant, so one label template matches the label
white (enabled), yellow (selected) or grey (disabled). The selected state is
still verified by the yellow check at the located point.

Plan verbs (the third column is the argument):

| verb | target | column 3 | effect |
| --- | --- | --- | --- |
| `click-template` | template name | `0` or `dx,dy` | locate, then the 1 px drag click (`jclick`) at the centre plus the offset; fails 65 if the best score is below the template's `min_score` |
| `wait-template` | template name | timeout ms | poll until the template scores at least `min_score` (ui-lib.sh RULE 2: never a fixed sleep) |

The directive `# create-clicks=template` makes a `create` row locate the race,
class and gender rows (`race_<Race>`, `class_<Class>`, `gender_<Gender>`) and
the Name field (103 px left of `name_getname`) instead of using the in-script
table. It also polls for page 2 with `wait-template name_getname`. The yellow
state is polled at each located point. If the first check fails, the
driver still makes one retry after an 8 s settle, and counts it in
`settle-retries.txt`. Every located click is a row of `template-clicks.tsv`:
step, template, root x/y, score, path, `first` / `retry`, retries. Example:
`plans/charcreate_barbarian_male_templates.tsv`.

**How a locate travels.** The oracle boxes have no NumPy, Pillow or OpenCV,
so the remote only takes the shot and clicks. When a plan uses templates,
`oracle_session.sh` starts `oracle_locate.py broker` on the driver host. The
remote writes `locate/shot-N.png` + `req-N.tsv` and waits for `ans-N.tsv`. Each
broker ssh exchange (`oracle_remote.sh broker`) does three things: it stores
the previous answer, waits up to 20 s for the next request, and streams that
request's PNG back. That is one exchange per locate. The broker's own table is
`broker/locate.tsv` (score, `min_score`, runner-up score, path, ms). The shot
is `import -window root -crop 1280x800+0+30`, so its PNG keeps a `+0+30` page
offset. Pillow ignores that offset and ImageMagick honours it. The broker
answers in Pillow (file) pixels, and the remote clicks root `(x, y + 30)`.

**Adding a template.** Cut it with the tool, never by hand, so its manifest
row is written for you:

```bash
python3 scripts/eqemu/oracle/oracle_locate.py cut-sheet \
  /mnt/projects/EverQuestP1999/uifiles/default/window_pieces01.tga 100,220,14,16 \
  scripts/eqemu/oracle/templates/sheet_plus_btn.png --name sheet_plus_btn \
  --xml EQUI_Animations.xml:3981 --note '...'
python3 scripts/eqemu/oracle/oracle_locate.py cut-frame <frame.png> x,y,w,h \
  scripts/eqemu/oracle/templates/<name>.png --name <name> --note '...'
```

**The manifest rule.** Every row of `templates/manifest.tsv` names its source:
the UI sheet plus the `EQUI_*.xml` line of its sub-rect (`kind=sheet`), or the
capture it was cut from (`kind=frame`). The row also gives the rect and the
sha256 of both the source and the template. Prefer a sheet sub-rect. When
the composite differs from the sheet, cut from a known-good frame and say so in
`note`. A control with font text is always in this case: the text is not on
any sheet, and the buttons stretch `A_BtnNormal`. A template's `min_score` is
never typed. To set it, add the template's positive and negative rows to
`tests/locate_corpus.tsv`, measured independently of the matcher (the generator
is `review/B1058/tools/build_library.py`), then run
`python3 scripts/eqemu/oracle/tests/oracle_locate_test.py --write-thresholds`.
That writes the midpoint of the gap between the highest negative and the
lowest positive score. A template with no gap is `rejected`, and the driver
refuses it. The `OracleLocateTemplates` hygiene test fails if a `min_score`
is not that midpoint.

