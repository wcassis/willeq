# T377 — Owed live proofs 5

Measurement only; eqemu lock (atomic; must be ABSENT — if present read it
and STOP); the slot that frees after T370 releases the lock. Reuse the
T358/T361/T367 drivers under `review/T358|T361|T367/scripts/`; verify every
chat line against the client's own `Chat submit:` line. Binary rebuilt from
the slot at the current tip.

1. **B115 — official client key bindings screenshot.** Official client under
   Wine (T361's rig, character Oraclealpha): open Options → Keys, page to
   the UI window bindings, screenshot every page that names the group, pet
   and buff windows; record the default keys and compare with
   `hotkey_manager.cpp` (G / Alt+P, P, Alt+B). Also capture the empty group
   window (no group, window open) — T368 left its content UNVERIFIED. If
   the defaults differ: board it (B145+), do not rebind here.
2. **T361 vision residuals re-measured.** With T367's light-culling fix in
   the binary, repeat T361's table (oracle night no-buff / Ultravision /
   Infravision vs fable, same spot, same crop, video stepped). Record the
   new ratios against the oracle's 1.554 / 1.253 on the B067 strike.
3. **B036 corpse-loot coin row.** A clutter-free zone (T342 said qeynos2
   was too noisy — try `arena` or `befallen`), one `#npctypespawn`ed mob
   killed, loot the coin: which UI sound (the tone probe rig,
   `review/T328/scripts`) on the official client, and what fable plays.
   Fill the row in the B036 strike; board any gap.
4. **T375 `/report` live row.** From fable, `/bug test T377` once (eqemu
   allows ONE report per zone session — `CanUseReport`); SELECT from
   eqemu's `reports`: `name` = character, `reported` = '', `reported_text`
   = the text. Record the row on the B023 strike.
5. **B027 — who sets `Oraclerogue`'s `gm` flag.** Before starting any
   client: SELECT the flag; grep eqemu's source for writers of
   `character_data.gm` and the `#gm` handler; check `quest`/`lua` scripts
   and `login` for an account-status promotion; start the client, log in as
   Oraclerogue, SELECT again after login and after one zone. Name the
   writer or narrow it to a phase. Strike or annotate B027.
6. `review/T377/README.md` (one section per item, PASS/FAIL first). Restore
   every server value; `down.sh`; delete the lock.

STOP: lock present; a proof FAILS (record, board, continue).

---

## Review (executor, 2026-09-05, slot2, `task/T377-owed-live-proofs-5`, base `032de79a`)

Measurement only. **No source file changed.** Both slot2 trees rebuilt from this
tip first: `cmake --build build-gles2 -j24` **exit 0**, `cmake --build build -j24`
**exit 0**. Full evidence, tables and method: **`review/T377/README.md`**.
The eqemu lock was taken atomically (absent on arrival), the server brought up
with `scripts/eqemu/up.sh --launcher`, and both released at the end.

| # | item | verdict | headline |
|---|---|---|---|
| 1 | B115 official key bindings | **FAIL** | group `Alt P`, buff `Alt B`, pet **`Alt F1`**; fable's `G` and `P` are wrong, and `P` is the oracle's Customer Service key -> **B145** |
| 2 | B067 vision residuals re-measured | **PASS on the ratio** | oracle reproduces to 0.4% (1.560 / 1.252); fable **1.449 / 1.237**; T367 moved UV slightly DOWN. Per-channel gains are wrong -> **B146** |
| 3 | B036 corpse-loot coin | **PASS (1 trial)** | `BuyItem.WAV` 138; the loot window's opening is silent; fable plays nothing and drops the coin -> **B147** |
| 4 | B023 `/report` live row | **PASS** | `name=Testchar`, `reported=''`, `reported_text='test T377'` |
| 5 | B027 gm flag | **PASS — premise REFUTED** | one writer (`Client::SetGM`), pure DB round trip; set to 0 and it stayed 0 through login/save/zone/camp. B027 STRUCK |

### 1 — B115, the official defaults (FAIL)

Options > Keys > category **UI**, paged to the end, Oraclealpha under Wine/Xvfb.
`Toggle party window = Alt P`, `Toggle buff window = Alt B`,
`Toggle pet info window = Alt F1`, all with an empty Alternate column — and
`Toggle Customer Service Win = P`. `hotkey_manager.cpp:486-492` binds group to
`G` + `Alt+P`, pet to `P`, buff to `Alt+B`. Buff matches; group's primary `G` is
an invention; pet's `P` is wrong AND collides. `Alt+P` verified live (hides and
re-shows the Group window). **The empty group window paints only its title bar**
— T368's other unverified half, now answered. Boarded **B145**; no rebinding done.

**Rig note worth keeping:** the client's window sits at `-1,+30` on this Xvfb, so
a coordinate read off an `import -window <WID>` capture must be shifted before
`xdotool mousemove` uses it. Three clicks landed in the title bar before this was
found.

### 2 — B067, re-measured with T367's fix (owe closed)

Both clients re-measured in one session at the T358/T361 spot, 700x420+40+40,
linear light, fable stepped over 1167 dumped frames and the oracle over a
2164-frame video. Oracle **1.560 / 1.252** against T361's 1.554 / 1.253 — the
control. fable **1.449 / 1.237**. Infravision now matches the oracle (99%);
Ultravision is at 93% and T367's light-culling fix moved it *down* from T361's
1.473. Absolute levels are deliberately NOT compared (T367's composition rule).
The new finding is per-channel: the oracle's Ultravision is near-neutral with a
blue lift while fable's green lags 12.5 points, and the oracle's Infravision
raises all three channels while fable's drives blue to **x0.821**. Boarded **B146**.

An instrument note: the oracle's `#castspell` OCR verification FAILED all three
attempts while the buff landed anyway — the video's luma steps at t=88.2 s,
before the first RETRY mark, and is flat for 195 frames after. The screen check
was wrong, not the cast; the video is what settled it.

### 3 — B036, the corpse-loot coin row (PASS, single trial)

Tone probe rebuilt from T342's script; `tonemap.json` md5-identical to T328's and
T342's, so all three units' rows are one measurement. Loopback verified with a
1000 Hz reference (measured 1000.1 Hz). **`BuyItem.WAV` (138)**, duration check
OK, against SILENT controls either side and two missed right-clicks that also
resolve silent. The loot window's own opening is silent (`itemclth.wav`'s chord
absent). **The gesture is not "click the coin"** — the client's own tip says
coins are taken automatically by the first looter, and the `You receive …` line
fires when the window opens. **fable plays nothing and banks nothing**:
`ZoneProcessMoneyOnCorpse` logs and returns -> **B147**.

Deviation, flagged: **one trial, not two.** Nine further right-clicks on two
later corpses never reopened the loot window (the ui-lib RULE 1 intermittency),
and a footstep asset also resolves in the good window. The recipe that DID work
is recorded on the B036 strike so the next unit does not re-derive it: zone
`arena`, `#npctypespawn 113423`, `#damage 100000` (not `#kill`), step back 20
units, **F9 to third person**, right-click the body.

Two rig facts for anyone repeating it: the Wine client plays **FLOAT_LE / 2ch /
48000**, so T328's 44100/s16 ffmpeg line fails outright and `chord.py`'s `SR`
must be 48000; and `/dev/snd/*` is `root:audio 0660` with this user outside the
group, which surfaces as alsa-lib's misleading `Cannot get card index for 1`.

### 4 — B023, the `/report` row (PASS)

`Chat submit: '/bug test T377'` -> `Report submitted` -> row 3 of `reports`:
`name = Testchar`, `reported = ''`, `reported_text = test T377`. The two
pre-T375 rows in the same table are the control (empty name, empty
`reported_text`, the message buried in `reported` behind four zero bytes).
Exactly one `/bug` was sent, per `CanUseReport`.

### 5 — B027, the gm flag (PASS, premise refuted)

`character_data.gm` has one writer (`Client::SetGM`), reached only by
`#set gm`, the Lua/Perl bindings, and an `OP_GMBecomeNPC` arm that only clears
it; load/save is a pure round trip; nothing derives it from account status. Set
to 0 by hand, it stayed 0 through login, an explicit `#save`, `#zone qeynos2`
and camp. Four earlier units saw a value persisted from an old `#set gm on`, not
a live writer. **B027 struck**; the flag restored to 1 and verified.

### Server values changed and restored (all verified by SELECT)

Oraclerogue `gm` (1 -> 0 -> **1**), Oraclerogue zone/pos, Testmelee zone/pos and
currency (the 1pp 8gp 3sp 4cp the corpse gave was subtracted back out), and the
`eqtime` world clock (`#set time` was used on both clients) restored to
`26 / 24 / 6 / 4 / 4004 / 1788601087` **with mariadb up and the world server
DOWN** — the first restore of Testmelee's row was clobbered by the world server's
own shutdown write-back, which is worth knowing. Testchar and Oraclealpha never
moved. The `reports` row 3 was left in place as item 4's evidence.

Host state: `~/.asoundrc` deleted, `snd-aloop` unloaded (it was not loaded before
the session), the probe client and the 456 MB capture deleted.
`scripts/eqemu/down.sh` **exit 0**, `docker ps` empty,
`/mnt/projects/fable/.eqemu-lock` **deleted**.
