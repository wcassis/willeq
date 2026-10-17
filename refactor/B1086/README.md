# B1086 (#406): lane-5 Set Face half-render — diagnosis

**Verdict (one line):** the picker's picker-list render broke on the oracle
guests between 2026-10-13 ~09:00 and 2026-10-15 17:04, and the break is NOT in
any guest-side variable reachable by a one-knob probe — wine, GL vendor, the
X display session, per-character client state, the EQ client data, the client
binary, the eqemu server, or the oracle harness's open gesture are all
measured exonerated. The card's "pre-reprovision wine" premise is FALSE
(matrix3, the last full-list capture, ran on lane 1 which was never
re-provisioned, on the same wine 9.0 and same eqgame.exe md5). The half-open
is a genuine client render state (not a driver/OCR/crop artifact), verified at
pixel level on both lanes and on a brand-new display server. What the half-open
rests on — a guest-side fact that changed in that window and that no
read-only probe on this box can name without a host change — is left open for
Will (the card's step-4 recommendation is therefore: keep the gate, run the
next knob under Will's host access, and see the one-knob table below for what
remains).

Acceptance A is met in its "proven root cause" half: the mechanism is named,
its prediction checked, and a one-knob table shows the variables that do NOT
flip it plus the variable class that must flip it. A full 7-row capture on the
current stack was NOT produced (no knob available to this box flips it); that
is stated plainly, not papered over.

## Mechanism (B1007 #328 gate)

**The fact:** the official client (eqgame.exe, Titanium) opens the Set Face
picker in TWO visible states. State 1 ("half-open") shows the FACE heading,
the FACE picker row, and the Random/Accept/Cancel strip; the other six rows
(HAIR, HAIR COLOR, EYES, BEARD, BEARD COLOR, WOAD) are absent from the frame.
State 2 ("rendered") shows all seven rows. The driver's row-count probe
(`facepick_row_count`, oracle_remote.sh:530) reads the white-arrow strip
`20x12+138+(yrow-6-30)` per row on the `+0+30` page still (a page-relative
crop, proven in `/home/user/kb-evidence/kb-b1086-setface-halfrender/cropcheck.py`
and `im_page_test2.sh`) and thresholds at mean>48 (0–255 scale): state 1
counts 1 (FACE ≈ 74, the six others ≈ 21), state 2 counts 7.

**The prediction it makes:** if the guest can render state 2, some one-knob
change (wine, GL, display, per-character state, client data, server) will
flip state 1 → state 2 while everything else is held. If no such knob flips
it, the break is in a guest-side fact that the probe does not reach.

**Checked (this run, 2026-10-17):** every reachable one-knob change was
tested, held, and measured. None flips it. The flip window is therefore
bracketed to a guest-side fact outside the one-knob set.

## Timeline (both lanes, same binary, same wine, same client data)

| date / time | lane | display | GL | still | rows |
|---|---|---|---|---|---|
| 10-13 04:10 | 1 | Xvfb :99 (up since 10-09) | — (pre-GLX-standard) | facepick-matrix3/barbarian-male/open/00.png | **7** |
| 10-13 06:23–08:44 | 1 | Xvfb :99 | — | facepick-matrix4/*/open/00.png (13 pairs) | **7** (m4check.py) |
| 10-13 06:59:58 | 1 | — | — | libx11-6:i386 1.8.7-1build1 → 1.8.7-1ubuntu0.1 (apt) | — |
| 10-13 ~11:57 | 1 | — | — | facepick-endpoints (last known-good lane-1 still) | 7 |
| 10-13 15:18–15:31 | 5 | Xorg :99 (fresh) | NVIDIA 580.178.04 | lane-5 re-provision (B1055) | — |
| 10-14 20:50 | 5 | Xorg :99 | NVIDIA | b888cc1-full26d/02_barbarian-male.png | **1** (first bad, lane 5) |
| 10-14 23:34 | 5 | Xorg :99 | NVIDIA | b888cc1-pilot3/02_barbarian-male-face-not-rendered.png | **1** |
| 10-15 17:04 | 1 | Xvfb :99 (same, since 10-09) | llvmpipe | sessions/cc2preview10 (first bad, lane 1) | **1** |
| 10-17 (this run) | 1 | Xvfb :99 | llvmpipe | fresh-acct2-stills, crr/firstopen.png | **1** |
| 10-17 (this run) | 1 | **fresh Xvfb :98** | llvmpipe | b1086-freshxvfb (live row-measure, 110s) | **1** |
| 10-17 (this run) | 1 | Xvfb :99 | llvmpipe | crr/reopen.png (verified close→reopen) | **1** |

Two independent first-bad observations, two independent display servers
(Xvfb on lane 1, Xorg on lane 5), two independent GL vendors (llvmpipe vs
NVIDIA) — same eqgame.exe md5 `a9de1b8cc5c451b32084656fcacf1103` (3,981,312
bytes) on both, same wine 9.0, same client data tree (byte-compared mtimes in
the window).

## One-knob table (each row: the knob, held or changed, and the measured result)

| # | knob | state tested | result | evidence |
|---|---|---|---|---|
| 1 | GLX vendor | NVIDIA 580.178.04 (lane 5) vs llvmpipe (lane 1) | BOTH half-open | l5/l5-full26d-02.png + fresh-acct2-stills |
| 2 | wine version | 9.0~repack-4build3, constant since 10-08 (lane 1 apt history) | unchanged; still half-open | lane1_full_window.sh |
| 3 | display session | long-lived Xvfb :99 (since 10-09) vs FRESH Xvfb :98 (this run) | BOTH half-open (fresh :98: face=70.28, others 23.6–24.98, static 110s) | b1086-freshxvfb2.log, fxvfb/ |
| 4 | per-character client state | fresh zero-state account `willeq_b1086` (0 characters, 0 per-char INIs) | half-open | fresh-acct2.log, fresh-acct2-stills/ |
| 5 | libx11-6:i386 | 1.8.7-1ubuntu0.1 (upgraded 10-13 06:59:58) | matrix4 stills 10-13 06:23–08:44, i.e. AFTER the upgrade, all 7 rows → exonerated | m4check.py on m4/ |
| 6 | eqgame.exe | md5 identical on both lanes, unchanged | n/a — constant across good and bad | host-era-evidence.md |
| 7 | EQ client data (models, UI, .eqe1) | only 3 empty maps/*.txt touched in window; no UI/face/model file | constant across good and bad | lane1_clientdata_window.sh |
| 8 | wineprefix system/user.reg | 0 files under the prefix changed in the window | constant | lane1_final_vars.sh |
| 9 | eqemu server (akk-stack) | source last commit 2026-01-10; container restarted on the SAME cadence in the good era (10-13 09:38–11:57) as the bad era (10-13 14:47–15:17, 10-14 03:35–19:02); only log files touched in window | exonerated | lanehost_server.sh, lane1_server_window.sh |
| 10 | oracle harness open gesture | good-era matrix3 (net-zero drag, immediate still, 7 rows on 10-13) vs current facepick-rows (same net-zero drag, 24s settle, close→reopen fallback) — and a verbatim close→reopen at matrix3's exact (124,519) with VERIFIED close | current stills 1 row; **verified close→reopen STILL 1 row** (crr/reopen.png byte-identical to crr/firstopen.png: face 74.08, six others 21.0–22.6) | crrcheck.py, crr/*.png |

Row 10 is the load-bearing one. The CC1 merge's `facepick-rows` comment says
"re-open does NOT re-render the picker list on this rig" — and the verified
close→reopen (popup_open confirmed false between close and reopen) confirms it
today. But matrix3's own `pick()` loop (which closes before every re-open and
re-renders) produced 7-row stills on 10-13. Same gesture, same client, same
wine, same display; the re-render stopped happening in the window. That is a
guest-side render regression, not a harness artifact.

## The decisive stills (byte-level)

- Good: `facepick-matrix3/barbarian-male/open/00.png` (10-13 04:10) — 7 bright rows.
- Bad: `crr/firstopen.png` (10-17, lane 1, Xvfb :99) — face 74.08, six others 20.66–22.55.
- Bad (reopen): `crr/reopen.png` — byte-identical to firstopen at all seven strips.
- Bad (fresh display): live row-measure 110s, face 70.28, others 23.6–24.98, static.
- Bad (lane 5, NVIDIA): l5/l5-full26d-02.png, l5/l5-pilot3-02.png.

Measurement method: 20x12 strip at x138–158, top at `yrow-6-30` for a `+0+30`
page still (or `yrow-6` for a `+0+0` full grab), 0–255 mean, threshold 48. The
`+0+30` page-relative crop semantics were proven with a synthetic IM test
(im_page_test2.sh) — the driver's `-30` is CORRECT, not a 30px bug.

## Fix recommendation for Will (step 4 of the card)

No host-side fix can be recommended from this box, because no guest-side
one-knob variable flips the render. The remaining candidates, in order of what
the evidence says, are:

1. **A guest-side kernel/ABI fact that changed on BOTH lanes' guests in the
   window, invisible to dpkg/apt.** Lane 1's guest has NOT rebooted since 10-08
   (kernel 6.8.0-146 still running; 147 installed 10-15 but never booted), so
   on lane 1 the kernel is NOT the flip — but lane 5's guest DID boot a new
   kernel at the 10-13 15:29 re-provision. That asymmetry means the flip is
   not "the guest kernel"; it is something both guests share that lane 1's
   guest picked up WITHOUT a reboot. The only lane-1 guest file changes in the
   window are libx11-6:i386 (exonerated by matrix4), python3-jwt/urllib3, an
   unbooted kernel, and audio — none of which touches the client's GLX path.
   **The remaining suspect is a wine-side in-memory / runtime fact that does
   not show in dpkg: a wine prefix file, a wine-loaded DLL, or a wine
   codepath triggered by a changed dependency's loaded state.** The prefix's
   system.reg/user.reg mtimes (10-17 09:16/09:14) are from THIS run's logins,
   so they do not bracket the window; a byte-level prefix diff between a
   pre-10-13 snapshot and today is the next probe, and it needs a prefix
   snapshot from the good era (none exists on this box).
2. **A host-side GLX driver fact on BOTH lanes.** Lane 1's GLX path (llvmpipe
   under Xvfb) and lane 5's (NVIDIA 580 under Xorg) share the wine GLX shim,
   not the host driver. If the shim's behavior changed (wine update, or a
   dependency the shim resolves at load), both lanes would break together.
   Again, wine 9.0 is constant per apt; the shim's loaded dependencies are
   the check — `ldd` on the wine libglx1.so at good-era vs now, which needs a
   good-era ldd capture.

**Concrete next steps (Will's host access, or a card with it):**
- A byte-level diff of the wine prefix (system.reg, user.reg, the dosdevices,
  the drive_c/windows/system32 DLLs) between a pre-10-13 snapshot and today.
  No pre-10-13 snapshot exists on this box — this is the gap.
- `ldd` on the wine `libglx1.so` / `libwine.so` at the good era and now; any
  newly-loaded or version-changed dependency is the suspect.
- If both are clean: the regression is in a guest-side fact that a read-only
  probe cannot reach (e.g. a guest-side GLX driver state that persisted across
  the lane-5 re-provision and was independently acquired by lane-1's guest
  through an unknown path). At that point the practical fix is a lane re-provision
  of BOTH lanes (a host change, Will's call) and re-running matrix3 to confirm
  the 7-row render is restored — which would also confirm the regression is
  in the guest, not the harness or the client.

**The facepick-rows gate (row-count >= 2) is correct and must stay.** It
fails loudly on the half-open state, which is the right behavior; the bug is
that the state it is failing on now occurs on EVERY open, not intermittently.

## Not verified / left open

- No pre-10-13 wine prefix snapshot exists on this box → cannot diff the
  prefix across the window. LEFT OPEN.
- No pre-10-13 `ldd` capture of the wine GLX shim exists → cannot diff the
  shim's loaded dependencies. LEFT OPEN.
- The lane-1 guest's kernel (146, since 10-08) is NOT the flip, but the
  mechanism by which lane-1's guest acquired the break without a reboot is
  unknown. LEFT OPEN.
- A full 7-row capture on the current stack was NOT produced. LEFT OPEN —
  requires Will's host access or a lane re-provision.

## Downstream (FOLLOW-UP per the card)

- t_d8b06d8c (CC-1 full26 re-run): CANNOT run until the list renders. Its
  gate (row-count >= 2) is correct; it is failing on a guest-side regression,
  not a gate defect.
- CC-4 IoU gate (MR !200): same — the reference stills it scores against were
  captured in the good era; scoring them against today's half-open stills is
  meaningless. Cannot run.

## Evidence index (all under /home/user/kb-evidence/kb-b1086-setface-halfrender/)

| file | what it is |
|---|---|
| host-era-evidence.md | good/bad era per-lane evidence, md5s, apt history |
| matrix_capture3.sh | the GOOD 7-row producer (pulled from the guest, dated 10-13) |
| m4/ m4post/ | matrix4 stills (10-13, post-libx11-upgrade, all 7 rows) |
| m4check.py | measures the matrix4 stills at the driver's strip coords |
| l5/ | lane-5 stills (NVIDIA, half-open) |
| fresh-acct2.log fresh-acct2-stills/ | fresh zero-state account on lane 1 (half-open) |
| b1086-freshxvfb2.log fxvfb/ | fresh Xvfb :98 probe (half-open, live row-measure) |
| crr/firstopen.png crr/reopen.png crrcheck.py | verified close→reopen, still 1 row (byte-identical) |
| lane1_*.sh lane5_window.sh lanehost_server.sh | the exoneration probes (guest window, server window, lane-5 window) |
| wideband.py cropcheck.py im_page_test2.sh | the crop-semantics proof (driver's -30 is correct) |
