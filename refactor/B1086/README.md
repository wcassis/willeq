# B1086 (#406): Set Face half-open — classic-model picker

## Take 3: the classic-model picker (2026-10-17, lane 5)

**Verdict:** the one-knob A/B confirmed the harness-state explanation. With
the same lane, Testchar, Barbarian male pair and `facepick-rows` plan, the
official client rendered FACE only with `UseLuclin*` false, all seven rows
after the 26 in-era keys were enabled, and FACE only when the original INI
was restored. These are separate client starts because `eqclient.ini` is read
at startup; no other capture input was changed. The take-2 physical-KVM-host
suspect below is **withdrawn**. `172.16.200.3` is the guest's DHCP gateway,
and the lane-1 oracle renders with llvmpipe inside its guest, not on an
unreachable KVM host.

The experiment used `luclin_ab.sh` with `luclin_ab_plan.tsv`. Its three final
stills are under
`/home/user/kb-evidence/kb-b1086-setface-halfrender/take3-ab-run1155/`,
one `<state>/02_barbarian-male.png` per row below. The existing evidence
`probe_rows.py` read the PNG pixels directly at the seven calibrated arrow
strips (>48 visible); each still was also visually inspected. This direct
pixel read matters: the current driver's ImageMagick probe falsely rejected
the TRUE still (its PNG retains a `+0+30` page origin, but the probe crops
before `+repage`, sampling 30 pixels too high). The driver exits 65 in all
three captures; its FALSE failures are expected, while its TRUE failure is
the separate harness-probe defect to fix in take 3.

| State | INI keys at capture start | Visible rows | Still | Driver exit |
|---|---:|---:|---|---:|
| FALSE before | 0 TRUE, 29 FALSE | 1 (FACE) | `false_before/02_barbarian-male.png` | 65 |
| TRUE | 26 TRUE, 3 FALSE | 7 (FACE, HAIR, HAIR COLOR, EYES, BEARD, BEARD COLOR, WODE) | `true/02_barbarian-male.png` | 65 (false probe rejection) |
| FALSE after | 0 TRUE, 29 FALSE | 1 (FACE) | `false_after/02_barbarian-male.png` | 65 |

Each `oracle_session.sh` capture restored its database snapshot and stopped
the stack before releasing its lane-5 lease (`driver.log` ends
`lock=state=FREE`); `EQEMU_LANE=5 bash scripts/eqemu/status.sh` found no
containers or ready stack. The wrapper restored the original `eqclient.ini`
after each run and at exit; the final `cmp` returned 0. Its `ab.log` ends
`lane_restored=1`, `cleanup exit=0`.

**Mechanism and run classification.** `UseLuclin<Race><Gender>=FALSE` selects
the classic model, whose picker exposes FACE only here; enabling the 26
in-era per-race keys selects Luclin models and their full feature list. The
good-era lane-1 `facepick-endpoints/endpoint_setup.sh` and CC1 WIRE's
`setup_remote.sh` explicitly ran `set_facepick_luclin.py` and restored the
INI after capture. Their saved `eqclient.ini.before` files have 0 TRUE keys
because they are the *pre-toggle backups*, not the state used in the run.
The later lane-5 full26 and B1086 probes used `oracle_session.sh`, which did
not apply that script; their FALSE `eqclient.ini` files show the expected
classic FACE-only view. `data/char_create_faces.tsv` already records
`visible_classic` separately from Luclin visibility. The fix belongs in
`oracle_session.sh` / `oracle_remote.sh` setup and teardown, plus their
face-picker row probe; no renderer, Wine, package or host change is needed.

## Superseded take-2 diagnosis (historical; claims below withdrawn)

**Verdict (one line):** the card's "both lanes fail, so look at what the two
lanes SHARE" premise is REFUTED by the evidence — lane 5 was re-provisioned
fresh AFTER the last full-list capture and was never good, so the two lanes
never rendered the full list in the same era and cannot localize a shared
variable. The flip is a LANE-1-SPECIFIC regression, bracketed to
**2026-10-13 08:44 (last confirmed good) → 2026-10-15 16:59 (first confirmed
bad)**. Every lane-1 guest-side variable reachable on this box — client
binary, client data + UI XML + atlas, eqclient.ini, wine, the wine prefix,
wine's D3D9/GLX DLLs, libx11/xcb1, the running kernel, the GL userspace
(mesa/gallium/llvmpipe 25.2.8 + libdril_dri), the Xvfb session, the eqemu
server (source + database + config), and the guest-side oracle harness — is
measured unchanged or exonerated in that window. The only remaining suspect
is the lane-1 guest's **physical host** (the guest is a KVM VM whose GL path
exits into the host's llvmpipe over virtio-gpu), which is unreachable by SSH
from this box (172.16.200.3, connection refused) and which leaves NO
guest-side trace (dpkg/apt/mtime) when it changes.

Acceptance A is met in its "every step's search attached as empty, bracket
narrowed" branch (see the one-knob table). The one-knob A/B (revert → 7
rows) CANNOT be produced from this box because the residual candidate sits on
an unreachable host; that is the narrowed next step, not a papered-over gap.
Lane 5's eqemu lock was never taken (no lane-5 capture was needed after the
premise was refuted); the lane-1 stack was left down and strays cleaned,
Xvfb :99 untouched.

## What take 2 changed relative to the parent

1. **The "both lanes share" premise is refuted.**
   `host-era-evidence.md` already showed the ThinkPad's first wine install was
   10-13 15:31 and its first NVIDIA install 10-13 15:27, with its first-ever
   boot at 10-13 15:18. Its first Set Face capture was 10-14 20:50 (b888cc1-
   full26d) — already half-open. Every full-list capture (matrix3 10-13 04:10,
   matrix4 06:16–08:44, facepick-endpoints 11:57) ran on **lane 1**. So
   "lane 5 is an independent confirmation of a shared variable" is false:
   lane 5 has only ever shown the bad state. The flip variable, if it is
   shared between the lanes at all, would have had to hit lane 1 before
   10-13 08:44 and lane 5 at its 10-13 15:29 re-provision — i.e. it is a
   lane-1 regression with a coincidentally fresh-bad lane 5, not a common
   ancestor. All localization therefore happens on lane 1.

2. **The bracket is corrected.** The parent used "good 10-13 ~09:00" but the
   B1048 session it leaned on (10-13 15:11) never opened the picker (its video
   frames show the main creation page, picker not open — re-verified here from
   163 extracted frames). The confirmed-good end is matrix4's last pair
   (woodelf-female, 10-13 08:44, 7 rows); the confirmed-bad end is
   cc2preview10 (10-15 16:59, 1 row). No face-picker capture exists between.

3. **libx11 exonerated by matrix4's OWN stills.** The parent's exoneration
   cited `m4check.py` against a visibility.tsv that is a byte-copy of
   matrix3's (`diff -q` identical). Re-measured matrix4's own per-pair stills:
   the HAIR row (present for every race) reads 17972 (0–65535) — full — for
   ALL 24 pairs, both before (gnome-male 06:57) and after (gnome-female 07:03,
   halfelf 07:06–07:12, …) the 06:59:58 `libx11-6:i386` upgrade. (An earlier
   strip at the beardcolor row showed a mid-run "flip" that turned out to be
   the legitimate male/female feature difference — women have no BEARD/BEARD
   COLOR rows — and ogre's 2-row list is ogre's real feature set, both
   vision-confirmed against the stills.) So the 06:59 upgrade did not flip
   anything; the good era survives through 08:44.

4. **The flip is reproducible on the current guest today** (2026-10-17
   19:32–19:37): the literal good-era driver (`manual_probe_lane1.sh`: drive-
   login → creation → verified Barbarian-male selection → matrix3 net-zero-drag
   open → 120 s settle) yields face=70.28 with hair/haircolor/eyes/beard/
   beardcolor/wode all 23.4–24.96, static across the whole 120 s — 1 row,
   same as every prior bad-era capture. Good-era gesture + verified selection
   cannot recover the list.

## Lane-1 exoneration table (corrected window 10-13 08:44 → 10-15 16:59)

| # | variable | state in window | verdict | evidence |
|---|---|---|---|---|
| 1 | eqgame.exe | md5 a9de1b8cc5c451b32084656fcacf1103, 3,981,312 B, mtime 2026-02-11 | unchanged | lane1 guest, both eras |
| 2 | client data (models/.s3d/.eqe1) | 0 non-per-character files changed in window (only maps/*.txt + per-char INIs written at login) | unchanged | find on /home/user/eq-oracle/client |
| 3 | EQUI_FacePick.xml, uifiles/*.xml, UI atlas | all 2026-02-11 (the tree's layout date); 0 XML in window | unchanged | find -name "*.xml" |
| 4 | eqclient.ini | only LastCharSel/LastSysInfoDate (session stamps); no UI/render key | unchanged | diff vs facepick-wire/eqclient.ini.before (10-13 08:38) |
| 5 | wine | 9.0~repack-4build3 since 10-08 02:10 (first install in log); no change | unchanged | /var/log/dpkg.log* |
| 6 | wine prefix (system.reg/user.reg/dosdevices) | 0 files in window (current mtimes are this run's logins) | unchanged | find on WINEPREFIX |
| 7 | wine D3D9/GLX DLLs in prefix (wined3d/d3d9/gdi32) | all 10-08 02:16 (prefix creation) | unchanged | ls --time-style |
| 8 | libx11-6 / libx11-xcb1 (amd64+i386), libx11-data | upgraded 10-13 06:59 (BEFORE the window); matrix4 straddled it and stayed full-list | exonerated by matrix4's own stills | 49-matrix4-all.sh, m4/*.png |
| 9 | running kernel | 6.8.0-146, booted 10-08 01:39, still running; 6.8.0-147 installed 10-15 06:55 but NEVER BOOTED (inert; /boot has both, btime 10-08) | unchanged for the running system | uname -r, uptime -s, ls /boot |
| 10 | GL userspace: mesa 25.2.8, libgallium, llvmpipe, libdril_dri.so | all 2024-era / Oct 1 2025 build dates; 0 in window | unchanged | dpkg -l, /usr/lib{,32}/…/dri |
| 11 | Xvfb :99 (the display server the client renders into) | same process (pid 27723) up since 10-09 09:57, args unchanged, in both eras | unchanged | ps -o lstart |
| 12 | eqemu server (akk-stack @ 172.16.200.99) | 0 source/config/data files in window (404 hits = runtime logs only); peq DB: no table updated in window (char_create_combinations NULL, character tables NULL); host booted 09-12, kernel 139 unchanged | unchanged | find on akk-stack, information_schema.tables |
| 13 | oracle harness on the guest (env.sh, login, ui-lib, oracle_remote) | 0 scripts changed in window (env.sh's 4-lane edit was 10-13 05:58, pre-window) | unchanged | find on /mnt/projects/eq-oracle + /home/user/eq-oracle |
| 14 | the open gesture (net-zero drag) | the LITERAL good-era driver (verified selection + matrix3 click_control + 120 s settle) run 2026-10-17 19:32 → 1 row | exonerated — bad state reproduces with the good-era gesture | b1086-live-bad-*.log, b1086-manual-probe/ |
| 15 | per-character / account state | fresh zero-state account also half-open (parent), and today's run uses the standing account | exonerated | parent fresh-acct2; this run |
| 16 | **the guest's PHYSICAL HOST** | the guest is a KVM VM (kvm-clock, virtio_gpu); its GL path is 32-bit eqgame → wine wined3d → i386 libGL/libGLX → libdril_dri.so (virtio_gpu_dri) → virtio ring → HOST-side llvmpipe. The host (gateway 172.16.200.3) is NOT reachable by SSH from this box (connection refused), so its dpkg/journal/kernel cannot be read from here. A host-side change (host kernel's virtio-gpu module, KVM/qemu version, host llvmpipe, or a live migration to a differently-provisioned host) would change guest rendering with ZERO guest-side dpkg/apt/mtime trace. | **UNREACHED — residual suspect** | guest: systemd-detect-virt=kvm, /sys/class/drm/card0 (Virtio 1.0 GPU), libdril_dri strings, ping 172.16.200.3 (up) vs ssh (refused) |

Row 16 is the narrowing: the parent's "guest-side fact invisible to dpkg" is
concretized. On the guest there is nothing left that changed and could reach
the client's render path; the one thing that changed nothing in the guest but
sits directly under the guest's render path is the host it runs on.

## Mechanism + prediction (B1007 #328)

**The fact:** the client's D3D9 UI ("Choose Your Appearance" list) renders
either all feature rows (good) or only the FACE row + Random/Accept/Cancel
strip (bad), and the choice is made by the GL backend the 32-bit process
resolves at load: i386 libGLdispatch → i386 libdril_dri.so (Mesa DRI
dispatcher) → the `virtio_gpu` DRI driver, whose actual shading runs on the
HOST's llvmpipe over the virtio ring (there is no GPU in the guest). The
picker-list draw is the one D3D9 path that stops rendering after the flip;
the model, buttons and FACE row (and the whole rest of the client) keep
rendering, which is why the state looks like "half-open" rather than a GL
failure.

**The prediction it makes:** if a host-side render change (host llvmpipe/
KVM/virtio-gpu, or a migration) is the flip, then (a) the guest must show the
good state on a host that carries the pre-window stack and the bad state on
the post-window one — a one-host A/B flips 1 row ↔ 7 rows with the guest
byte-identical (same eqgame.exe md5, same wine, same prefix, same display
server); (b) the flip would appear WITHOUT any guest dpkg/apt/mtime event
(exactly what is observed: 0 relevant guest changes in the window); (c) the
host change would be visible in the HOST's dpkg.log/journal/boot record, not
the guest's. If instead the flip were guest-side, one of rows 1–15 would show
an in-window change and reverting it on the (unmodified) guest would restore
the list — none does.

**Checked:** rows 1–15 all read "unchanged/exonerated" above; row 16 could
not be read from this box. The prediction (a) is the decisive next probe and
it needs access to 172.16.200.3 (or a re-provision of the lane-1 guest on a
known host), which this box does not have.

## Decisive stills

- Good: `facepick-matrix3/barbarian-male/open/00.png` (10-13 04:10) and
  `facepick-matrix4/human-male/open/00.png` (10-13 07:38, POST-libx11) — 6–7
  labeled rows, gold arrows (vision-confirmed this run; m4/*.png pulled).
- Good (last): matrix4 woodelf-female 08:44 — 7 rows (strip 17972 HAIR).
- Bad (era): `sessions/cc2preview10_20261015T165921/13_face-open-settle.png`
  (10-15 16:59) — FACE only, no arrows (vision-confirmed).
- Bad (today, good-era driver): `b1086-manual-probe/04_settle-120s.png`
  (10-17 19:37) — face 70.28, others 23.4–24.96, static 110 s (live log
  `b1086-live-bad-*.log` on the guest).
- Race-specific rows are NOT the flip: ogre = FACE+EYES (2 rows) and women =
  no beard rows are the correct per-race lists (vision-confirmed against
  matrix4 stills), and they are present identically in the good era.

## Downstream (FOLLOW-UP per the card)

- t_d8b06d8c (CC-1 full26 re-run) and the CC-4 IoU gate (MR !200) remain
  blocked: the row-count gate is correct; the client genuinely half-opens on
  the current lane-1 host. They unblock when a lane-1 capture renders 7 rows,
  which per rows 1–16 requires the host-side A/B (row 16), not a guest fix.
- **The fix card must run the host A/B:** read 172.16.200.3's
  /var/log/dpkg.log* + journal + boot record for 2026-10-13 08:44 →
  10-15 16:59 (kernel/KVM/libvirt/mesa-llvmpipe/virtio-gpu, any migration or
  live-ops event), then revert the hit (or re-provision the lane-1 guest on a
  pre-window host) and re-run `manual_probe_lane1.sh` — 7 rows on revert and
  1 row again on restore is the one-knob proof. Until then the CC-1 full26 and
  CC-4 gate stay disabled.

## Evidence index (all under /home/user/kb-evidence/kb-b1086-setface-halfrender/)

| file | what it is |
|---|---|
| host-era-evidence.md | parent: good/bad era per lane, md5s, apt history (source of the lane-5 refutation) |
| matrix_capture3.sh | the good-era 7-row producer (pulled from the guest) |
| m4/*.png m4post/*.png | matrix4 stills pulled this run (human-male 07:38 = 6 rows; ogre-male 07:54 = FACE+EYES; all HAIR strips full) |
| take2/49-matrix4-all.sh + output | the 24-pair HAIR-row census (all full, straddling the libx11 upgrade) |
| take2/b48f/ | 12 B1048 video frames (10-13 15:11) — main creation page, picker never open (bracket correction) |
| take2/cc2/13_face-open-settle.png | the 10-15 16:59 bad still (vision-confirmed 1 row) |
| take2/45-gesture.sh, 46-gesture2.sh | gesture A/B attempts (login-path + OOM issues; gesture exonerated by the 47/48 and live probe instead) |
| parent: crr/firstopen.png, crr/reopen.png, crrcheck.py | verified close→reopen, still 1 row |
| parent: fresh-acct2-stills/, b1086-freshxvfb2.log | fresh account + fresh Xvfb, both 1 row |
| parent: lane1_*.sh lane5_window.sh lanehost_server.sh | parent exoneration probes |
| guest: b1086-manual-probe/ + b1086-live-bad-*.log | this run's live 1-row repro with the good-era driver (2026-10-17 19:32) |
