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

**Lane-5-only fixes (B1073, #393)** are in the same script and are inert on
lanes 1-4 (the run passes its lane as the third argument to
`oracle_remote.sh`, `ORACLE_LANE`):
- **The NVIDIA GLX stack** (server + client): `:99` registers only NVIDIA's
  `glxserver_nvidia` — `/etc/X11/oracle/xorg.conf` carries
  `Section "Module" Disable "glx"`, so the generic Mesa `glx` module never
  loads and the log has no `GLX: Another vendor is already registered` line
  (`DISPLAY=:99 glxinfo` = NVIDIA). On the client side the run exports
  `__GLX_VENDOR_LIBRARY_NAME=nvidia`, pinning Wine's 32-bit libglvnd
  `libGL.so.1` dispatcher to `libnvidia-gl-580:i386` instead of the Mesa i386
  wrapper. Lanes 1-4 have no NVIDIA vendor lib, so the pin is lane-5 only.
  Background and the Set Face regression this fixed: `docs/thinkpad.md`.
- **The no-WM focus**: the rig has no window manager, so after a client
  re-render the EverQuest window does not hold input focus and a motion click
  (the `jclick` 1px drag, the Set Face net-zero drag) lands on the root window
  and does nothing. `oracle_remote.sh` defines `client_focus`
  (`xdotool windowfocus --sync --name '^EverQuest$'`, never fatal) and, on
  lane 5, redefines `jclick`/`jrclick` and focuses before the raw-xdotool
  `hold` path, so every motion click carries its own focus. The rig-side
  `/mnt/projects/eq-oracle/scripts/ui-lib.sh` stays untouched.
- **The Set Face open shape** (lane 5 only): matrix3's net-zero drag does not
  open the modal on the re-provisioned rig (FINDINGS-setface-regression.txt
  measured it "NO (5 tries)"); the focus-wrapped `jclick` 1px-net drag does.
  `face_open_click` therefore delegates to the lane-5 `jclick` when
  `ORACLE_LANE=5` and keeps matrix3's net-zero drag for lanes 1-4. The
  `facepick-rows` verb additionally logs each Set Face open attempt to
  `timeline.tsv` (`face-open attempt=N at=37,251`), so a lane-5 run
  proves whether the FIRST click opened the modal (the acceptance bar: a
  retry is a focus/sequence defect, not an acceptable settle).
- **The Set Face classic/Luclin state** (B1086, #406): a lane-5 one-knob
  FALSE → TRUE → FALSE A/B of the 26 in-era `UseLuclin*` keys produced
  1 → 7 → 1 visible rows for the same Barbarian-male picker. The old
  "half-open render regression" diagnosis and its KVM-host suspect are
  withdrawn (`refactor/B1086/README.md`). The `facepick-rows` gate still
  rejects a FACE-only still. Its ImageMagick probe must clear the screenshot's
  `+0+30` virtual page origin *before* cropping the seven arrow strips;
  clearing it afterward sampled 30 pixels too high and falsely rejected the
  seven-row Luclin still. The `full26` plan opts into Luclin models and the
  driver restores the original global INI byte-for-byte after the client
  stops. Other plans retain the stock model preference.

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
The optional `# char-ini-set=Section.Key=VALUE` directive (B1162) sets one
key in the login character's INI the same way (same backup, same
byte-identical restore at stop); it and `# ini-unset=` are mutually exclusive.
B1162 uses `Abilities.Ability1=71` so `/doability 1` reaches the Intimidation
skill (the Actions-window ability slots live in that INI).
The optional `# use-luclin-models=true` directive enables the 26 in-era
per-race Luclin keys in the global `eqclient.ini` before Wine starts. The
driver saves the original bytes before the toggle and restores them after
stopping Wine, even if the capture fails. Only the face-picker full26 plan
uses it; a plan without the directive leaves the global model preference
alone.
The optional `# pose=x,y,z,heading` directive (B1065; heading in EQ units,
0-512) seeds the login character's position together with the plan's `zone`
row, so an official still is taken from a pose the willeq client can be put at
exactly; the snapshot restore puts the old x/y/z/heading back.

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

Exit codes: `0` complete; `64` invalid invocation, lane lock held, or a gem
cast the login character cannot make (below); `65`
server/capture/driver failure; `66` restore or lock-release failure; `67`
client disconnected mid-plan. The disconnect result appends world/zone log
tail evidence to `summary.md` and saves `disconnect-logs.txt`. Never treat a
partial summary or a green process alone as a valid observation: inspect
`ledger_check.txt`, the relevant step files, and the cleanup line in
`driver.log`. The character-creation plan deletes its newly created character
with the world stopped, then verifies the snapshot.

## Gem casts and class legs (B1129)

**The official client refuses a gem cast its class cannot use** and sends
nothing: "You are not high enough level to cast this spell!", no
`OP_CastSpell` (`refactor/B1118/README.md`). That holds for a `spells_us.txt`
class level of 255 (fields 104..119, classes 1..16 WAR..BER) and for a level
above the character's. So, with the lock held and the snapshot taken, the
driver reads the login character's class, level and memorised gems and runs
`oracle_plan_check.py check` over the plan; a refused cast exits **64**
before Wine starts, naming the plan line, spell and class
(`plan-check.txt`). It replays the plan's `#memspell <spell_id> <slot>` (slot
0 = gem 1) and `#level N` rows; a gem cast is `command /cast N` or a `click`
on a gem of the lane's measured bar. `spells_us.txt` comes from
`$EQ_CLIENT_PATH` (default `/mnt/projects/EverQuestP1999`).

**The gem bar is measured per lane, never derived**:
`scripts/eqemu/oracle/gem_pitch.tsv`. Lane 5 (1280x800): pitch 30 px, gem i
(1-based) centred at root (25, 213+30i) (B1118 `oracle_calib/`). Lanes 1-4
are unmeasured: measure on first use (B1118's `plans/oracle_calib.tsv`
recipe, a still of a known memorisation) and add the row; until then their
gem-column clicks are reported as unchecked, not guessed.
`oracle_plan_check.py gem-xy --lane 5 --gem 3` prints a click target.

**Several classes = one leg per class**: `oracle_class_leg.sh <lane> <class>
<plan> <out> [--char-id 67] [--home-class 12] [--gems 93,38,54,41,253,458,656]
[--pre-sql-file F] [--post-sql-file F]`. It checks the plan offline, waits for the lease (never
breaks it) and runs the driver with four hooks the driver runs only inside
its locked session: `ORACLE_STATE_CHAR` (class/level, gems, spellbook and
buffs census before and after), `ORACLE_PRE_SQL` (the class switch, only
from the home class), `ORACLE_EXPECT_CLASS` (refuse if the switch did not
take), and `ORACLE_POST_SQL`, run in cleanup **with the world down** after
the snapshot restore, which covers level but not class, gems or buffs: home
class back, the `--gems` list rewritten in slots 0.., buffs cleared, plus the
file. The character's skills, disciplines and persistent timers (none of them
in the snapshot; a song raises a skill, `#traindisc` scribes, a `/disc` starts
a reuse timer) are copied to `oracle_save_<table>_<id>` before the switch and
put back exactly in the undo (B1134; `SAVED`/`LEG`/`BACK` row counts in
`driver.log`). `--pre-sql-file` runs after the switch (a leg's server rule; its
undo goes in `--post-sql-file`). The driver then deletes spellbook rows the leg added. The helper exits 66
when the after-census does not show the home class and exactly `--gems`.

## The observer: a second player character (B1134)

**An FX leg's other target and its camera are a SECOND PLAYER CHARACTER on a
SECOND official client, never a spawned NPC** (B1134, #454). B1132 lost four
legs trying to move a `#spawn`ed NPC off the caster; a caster's own camera
cannot see a landing that happens after it zones (a port) and the gem rig
cannot cast what is not a gem (a discipline). The observer sees all of it:
"unmeasurable" is never a finding, a rig limit is.

**Prior work this follows.** `/mnt/projects/eq-oracle/scripts/two-client-capture.sh`
(H05) already ran two official clients on one VM: display `:99` + `:98`, a
second client directory and a second Wine prefix, one account each. fable's own
two-client scenarios (`two_client_survive`, `equip_observed`/`equip_actor`,
`scripts/scenario.sh`) are the same idea for the fable client.

**How it runs.** A plan with `# observer=<Character>` runs two clients:

| role | display | client | Wine prefix | rows |
| --- | --- | --- | --- | --- |
| observer | `:99` (the grabbed display: `video.mp4` is ITS window) | `~/eq-oracle/client-observer` | `~/eq-oracle/wineprefix-observer` | `obs <verb> ...` |
| caster (the plan's `login` character) | a session Xvfb `:98` (`ORACLE_CASTER_DISPLAY`; Mesa GLX, llvmpipe, one thread: more exhaust the 32-bit client) | `~/eq-oracle/client` | `~/eq-oracle/wineprefix` | every other row |

An `obs` row is any driver row with `obs` in front (`obs login Oraclewatch`,
`obs zone qeynos2`, `obs press F9`, `obs click 1229,555`); its still is taken
on the observer's display. The session seeds the observer's zone (the plan's
`zone`) and `# observer-pose=x,y,z,heading`, adds it to the inventory
snapshot, records its census (`char-*-observer-before/after`, which must
match), refuses an observer with buffs, and restores HP/mana/endurance and the
PvP flag of BOTH characters with the world down (`vitals-before/after.tsv`).
`# obs-ini-set=Section.Key=VALUE` edits the observer's `eqclient.ini` and is
undone at stop; `# fps=30` sets the x11grab rate (passthrough, crf 18; FX legs
need >= 30). The pcap holds both clients; a measurement selects the
observer's stream by the client port of its C->S `OP_ZoneEntry`
(`review/B1134/scripts/obs_measure.py`).

**Setup, once per lane** (no eqgame running):
`oracle_observer_setup.sh <lane> Oraclewatch --account willeqwatch` copies the
client and the prefix, gives the observer the caster's UI layout and writes
`~/eq-oracle/observer-Oraclewatch.cred` with a password GENERATED on the
oracle host (it never leaves it; `oracle_login_patch.py --credentials` makes
the login helper source the file). The loginserver's `auto_create_accounts`
creates the account at the first login, and a `# provision=Oraclewatch` leg
creates the character through the official creation screen (`obs create
Oraclewatch Human Warrior Male`); that account, its login row and that
character are the only rows its restore and census let through, and they are
kept (`provisioned.tsv`). Lane 5: character 103 Oraclewatch (level-1 Human
Warrior), account 80 `willeqwatch` (B1134 leg P1).

**The geometry** (B1134, the B1130 survey's own): the observer stands where
B1130's caster stood after its `#goto` (`-67.6,434.4,3.75`, heading 160) and
the caster where B1130's `#spawn`ed TrollF stood (`-74,428,3.75`, heading
160), 9.05 world units = about 1.4 caster heights to the observer's right. The
observer's F9 x3 is B1130's fixed camera, so the caster is framed like
B1130's `fixed_npc` target and a landing on the observer like B1130's
`fixed_self`. Plans: `review/B1134/plans/p2_rig93.tsv` (the rig check),
`p3_ports.tsv`, `p4_discs.tsv`.

**A detrimental cast on the observer** needs both characters `#set pvp on`
(eqemu `Mob::IsAttackAllowed`: client vs client only when both are PvP or
dueling) and, near guards, `Character:PVPEnableGuardFactionAssist` off for the
leg (eqemu `Mob::SpellFinished` (its spells.cpp, the eqemu source under the akk-stack on the ThinkPad): guards within 70 units add the caster to their hate
list): `oracle_class_leg.sh --pre-sql-file` / `--post-sql-file`, then
`#reload rules global` in the plan.

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


## Reading text (B1059)

`scripts/eqemu/oracle/oracle_glyphs.py` reads the official client's UI text
from a frame rect with a glyph atlas instead of a tesseract subprocess:

```bash
python3 scripts/eqemu/oracle/oracle_glyphs.py read <frame.png> x,y,w,h [--rig lane5] [--font F] [--edit] [--expect TEXT]
# -> "text<TAB>score<TAB>path"; exit 0 read (and matches --expect), 1 UNKNOWN,
#    4 read but differs from --expect, 3 the rig has no atlas, 2 error
```

**What the atlas is.** Measured over 3,153 labelled crops of the archived
frames (the B1059 evidence README under /mnt/projects/fable/review/B1059): one string in one control state over one
background is the same binarised bitmap sequence on lanes 1, 3, 4 (llvmpipe),
lane 5 (Quadro) and the dev-u24 Xvfb rig, at 1280x800 and 1024x768. What
changes a glyph's bitmap is its colour state (white / yellow / grey / green)
and the background under its anti-aliased edge, not the rig. So there is one
atlas, `glyphs/titanium/` (`atlas.png` + `atlas.tsv`, every bitmap citing the
frame, crop and glyph rect it was cut from and the pcap/DB/plan/XML source of
its label), and `glyphs/rigs.tsv` names the rigs it was measured on. A rig not
listed there (lane 2 today) has no atlas: every caller then keeps its legacy
check unchanged.

**How a read works.** Mask = luma above `med + 0.5 (max - med)` of the crop;
8-connected components; components overlapping in columns are one cluster.
Because the text is anti-aliased, a glyph can fall into two clusters ('M',
'N', 'W') and two glyphs can touch ("rd", "rt"): the reader parses the
clusters into tokens of 1..3 clusters, and an atlas entry is one character or
a run of touching characters. Every token must match an atlas bitmap EXACTLY
(the NCC tier is implemented, but its derived threshold is `off`: no score gap
separates variants of one character from different characters). Parses that
disagree, or any token the atlas lacks, make the read `UNKNOWN`, never a
guess. 'I' and 'l' are one bitmap in this font: the read prints `[Il]` and
`--expect` accepts either. `--edit` drops an edit field's caret.

**Who uses it** (each records which reader decided):

| caller | atlas read | fallback |
| --- | --- | --- |
| `oracle_remote.sh` Name check, `# create-clicks=template` | the Name field (165 px left of / 8 px above the Get Name centre) must read the typed name; a mismatch re-types | tesseract confirms only; otherwise the B1058 presence check `name_filled_at` |
| `oracle_remote.sh` column-five check (`assert_visible`), template plans | the text regions of `glyphs/regions.tsv` (character-select title and slots, page-2 labels, the Name field, the name dialog line, the Tip caption) must match the regex | the box's tesseract `screen_text`, as before |
| `scripts/llmbench/verify_labels.py` target window | the name line (910,476,113,15) of a window grab, font 3, rig `VERIFY_LABELS_GLYPH_RIG` (default `devu24`) | the four tesseract crops, as before |
| `scripts/oracle/find_tip_dialog.py` OCR signal | the caption of the matched title bar, rig `FIND_TIP_GLYPH_RIG` (default `devu24`) | tesseract over the dialog rect, as before |

The remote asks the same broker that answers template locates (driver host;
`oracle_session.sh` passes `--rig lane<N>`): request
`read:<edit|line|screen>:<x,y,w,h>:<expected as hex>`, answer
`<seq> TEXT <atlas|tesseract|unknown> <ok> <score> <path> <text as hex>`.
Each read is a row of `text-reads.tsv` (step, check, mode, decider, ok, text,
score, path) and of `broker/text-reads.tsv`; `oracle_report.py` prints them as
the summary's "Text reads" table. A plan without templates starts no broker
and uses the legacy checks only.

**Adding glyphs.** Labels never come from looking at the image. Add labelled
crops to `review/B1059/tools/label_crops.py` (truth from the pcap, the server
DB, the plan or the UI XML / `eqstr_us.txt`), regenerate `crops_all.tsv`, and
rebuild: `python3 scripts/eqemu/oracle/oracle_glyphs.py build
/mnt/projects/fable/review/B1059/crops_all.tsv --rigs lane1,lane3,lane4,lane5,devu24`.
The build aligns each label with its clusters (one-to-one first, then a unique
dynamic-programming alignment around known bitmaps), drops a crop whose
alignment is outvoted, and logs every crop it could not align. Then
regenerate `tests/glyph_corpus.tsv` (`review/B1059/tools/make_test_table.py`)
and run `OracleGlyphReader` on both paths.
