# B1214 (#533): SPIKE -- MacroQuest2 inside the Titanium oracle client

**Verdict: it works.** A public Titanium MacroQuest2 build injects into OUR
`eqgame.exe` under Wine 9.0 on eq-oracle-1 (lane 1) with no patching, and all
three proofs passed: (a) facing and camera control from inside the client,
(b) a scripted walk with a millisecond-stamped position log, (c) a memory dump
of the client's live spell-emitter registry while Spirit of Wolf played on the
local player. The read is exact: the emitters it names match the spell's
`spellsnew.eff` record slot for slot. **Recommendation: adopt it for the r540
oracle rigs (lanes 1-4)** (sec. 6).

Session: 2026-10-30 12:16Z-13:05Z, lane 1 (172.16.200.99 eqemu +
172.16.200.91 eq-oracle-1). Lane 5 was not touched. The lane was restored
(sec. 7).

## 1. What was found, and where

Our client: `~/eq-oracle/client/eqgame.exe` on eq-oracle-1, 3,981,312 bytes,
sha256 `ffa64c072c0c5e834a982720fd395ddc8974df26d1597020e1dbeb651b7ed957`,
PE TimeDateStamp 0x4366642a (2005-10-31 18:36:26 UTC), build string
"Oct 31 2005" "10:33:37" (`review/B1141/ghidra/out/eqgame.exe.c:161091`).
This is the binary `review/B1141/ghidra/out/` decompiles.

| URL | result |
|---|---|
| web search "MacroQuest2 Titanium client EQEmu "Oct 31 2005"" | led to the two RedGuides threads below |
| https://www.redguides.com/community/threads/titanium-2005-10-31-mq2-compile.21939/ | "Titanium(2005-10-31) MQ2 Compile" (Spiken, 2009, built on unknown405's 20051101 MQ2). The attachment needs a RedGuides login. Not downloaded. A reply points to mqemulator.net's Titanium source. |
| https://www.redguides.com/community/threads/eqmac-test-compile.24998/ | quotes the Titanium `eqgame.h` header (`__ExpectedVersionDate "Oct 31 2005"`, `__ExpectedVersionTime "10:33:37"`), which confirms the target build. Reference only. |
| web search "MQ2-20051101" / "MQ2-20051031" zip; GitHub "ExpectedVersionDate" "Oct 31 2005" | nothing. gitlab.com/redguides/VeryVanilla is an archived LIVE-client tree, not Titanium. |
| https://www.mqemulator.net/downloads.php | **live.** Lists the "Legacy MacroQuest" builds per emulator client (ROF, ROF2, SOD, SOF, **Titanium**, Underfoot) and public `MQ2Emu_<client>_Source.zip` files |
| https://www.mqemulator.net/download.php?file=MQ2Emu_Titanium_Source.zip&category=registered | **downloaded.** 993,507 bytes, sha256 `2cda5f7269889ef17d246ad80b296e91e6c61d508ff533c3309878f8ad8f8c53`. MQ2 source dated 2005-11-01 (the 20051101 tree), GPLv2, Lax/Plazmic. Contains `MQ2Main/eqgame.h` (copied to `review/B1214/logs/eqgame.h.mq2emu-titanium-source`) |
| https://mqemulator.net/download.php?client=Titanium&build=Classic&type=macroquest_legacy&downloadToken=1 | **downloaded, and this is what ran.** `MQEmu_Legacy_Classic_Titanium.zip`, 3,574,285 bytes, sha256 `f96098644f4742a7eebc5b5d421a634e3c18bb3e46d18949500565443b3be0d9`. Assembled 2023-12-31, binaries dated 2022-10-02. `MacroQuest2.exe` sha256 `387fed1e4dd49a69d441d129e3abf50db7bf0e2ebed6b7cf46d4dc270446fe77`, `MQ2Main.dll` sha256 `4619dd78631bac622b5f6423c7cb077c444788143a5c0d52f07ff187972a33ff`, plus 44 plugin DLLs |

Handling: each archive went into its own empty directory under
`.spike-downloads/` (gitignored). Both were listed with `unzip -l` before
extraction. The binaries were inspected with `strings` before anything ran.
`MQ2Main.dll` carries "Expected Client version: Oct 31 2005 10:33:37" and
imports only system DLLs (ADVAPI32, KERNEL32, USER32, SHELL32, WINMM,
imagehlp; the CRT is static). The loader `MacroQuest2.exe` injects with
`SetWindowsHookExA` + `LoadLibraryA`. No VirusTotal-style scan was available
from this environment. Mitigation: the binaries ran only inside a throwaway
COPY of the lane's Wine prefix, which was deleted afterwards. Only a source we
could name and check against its published source was used. No Python read
the archives, so `python3 -I` never came up.

**Build path for the source:** not needed, because the published binary
matches. The source is an MSVC 2003 project (`MacroQuest2.sln` / `.dsw`,
Detours 1.5 libs). Rebuilding MQ2Main would need MSVC under Wine; that was not
attempted. Our own plugin did NOT need it (sec. 4).

## 2. What ran (the recipe)

`scripts/eqemu/oracle/mq2/install.sh install 1 <zip> [MQ2B1214.dll]`
re-ran end to end at 13:03Z (15 s), and `uninstall 1` removed it. It does
the following:

1. **Copies** the lane's `~/eq-oracle/client` (6.1 GB) and
   `~/eq-oracle/wineprefix` (581 MB) to `~/eq-oracle/b1214/`. The canonical
   prefix is never used (B1206: two clients sharing one prefix break).
2. **Installs MQ2** at `drive_c/MQ2` in the copy: `MacroQuest2.exe`,
   `MQ2Main.dll` and three plugins. MQ2ChatWnd gives the on-screen MQ window.
   MQ2Telnet gives a TCP command and echo channel on port 2323. MQ2MoveUtils
   gives `/moveto`. It also installs a trimmed `MacroQuest.ini` with
   `DebugSpewToFile=1` and the macros in `scripts/eqemu/oracle/mq2/macros/`.
3. **Launches** with `scripts/eqemu/oracle/mq2/launch.sh` on the VM: a fresh
   `Xvfb :99` (llvmpipe), then `wine MacroQuest2.exe` (the tray app; its
   global hook loads `MQ2Main.dll` into the next process), then
   `wine eqgame.exe patchme` from the client copy. **Injection under Wine
   works as-is.** `review/B1214/logs/DebugSpew.log` shows every MQ2 detour
   installing cleanly into our build ("AddDetour(0x4B6C04 ...) Detour
   success." and so on). The plugins loaded, and the MQ window printed
   "MQ2 Loaded." on the character-select screen
   (`review/B1214/shots/01_charselect_mq2_loaded.png`).
4. **Logs in** with the lane's login driver, patched by
   `oracle_login_patch.py` (`review/B1214/logs/login2.log`). The account is
   `willeq_test` (status 200, GM) and the character is Testcaster (60 Wizard,
   North Qeynos). The raw `drive-login.sh` stops at the empty-credentials
   modal, as the memory note on lanes records. The patched copy reaches
   character select.
5. **Enter World needs the EverQuest window FOCUSED.** `xdotool windowfocus
   --sync` comes before the `jclick 123 316` / `jclick 180 640` pair. Without
   it the click lands with no effect (`review/B1214/shots/03_enter_world_retry.png`
   shows this; `05_rotate_test.png` shows a focused click working). MQ2's
   `/notify` cannot press it: the character-select window list is empty to
   MQ2 (`${Window[CharacterListWnd].Open}` = NULL).
6. **Drives everything** through `scripts/eqemu/oracle/mq2/mqtel.py`, a small
   telnet client run on the VM. It sends MQ2 commands and stamps every echoed
   chat line with UTC milliseconds. The client window does not need focus for
   any MQ2 command, `/keypress` included: MQ2 calls the client's own command
   and keybind handlers.

## 3. Proof (a): facing and camera, deterministic

Driver: `scripts/eqemu/oracle/mq2/proof_a.sh` (one command per step, then a
still). Log: `review/B1214/proof_a/telnet.log`. Stills: `review/B1214/proof_a/a*.png`.

- `/face fast heading 90|180|270` sets the heading in one frame. MQ2's two
  heading conventions disagree: `/face heading 90` reads back as
  `${Me.Heading.Degrees}` = 270.00 (180 reads 180, 270 reads 90). It is
  deterministic, so map it once.
- `/look -40` and `/look 0` set the camera pitch exactly (read back as
  `${Me.Look}`: -40.00, 0.00; `a04_look_down.png`).
- The client's own camera binds are reachable by name through `/keypress`.
  `/bind eqlist` (`review/B1214/logs/bind-eqlist.log`) lists them:
  FIRST_PERSON_CAMERA, USER1_CAMERA, USER2_CAMERA, OVERHEAD_CAMERA,
  CHASE_CAMERA, TETHER_CAMERA, ZOOMIN, ZOOMOUT, FORWARD_CAM and others.
  `a06_first_person.png`, `a07_user1_cam.png` and `a08_user2_cam.png` show
  the three cameras switching. ZOOMIN and ZOOMOUT act only while held
  (`/keypress ZOOMOUT hold`). Ten single presses (`a09`/`a10`) changed
  nothing.
- **Camera distance, exactly.** The user cameras are client objects at
  `*0x925508` (user 1) and `*0x92550c` (user 2). `SaveCamerasToINI` reads
  Distance from +0x34, Heading from +0x28, Pitch from +0x30 and Zoom from
  +0x40 (`review/B1141/ghidra/out/eqgame.exe.c:132365` through
  `review/B1141/ghidra/out/eqgame.exe.c:132378`). The plugin's `/camdump`
  read user 1 = 30.000 and user 2 = 82.000. Those are the copied
  `eqclient.ini`'s `Camera1-Distance=30` / `Camera2-Distance=82`.
  `/poke <cam1+0x34> 10|30|60|120` wrote the value and `/camdump` read it
  back (`review/B1214/proof_a/telnet-cam.log`). `ZOOMOUT hold` drove it to
  200.000, the client's clamp. On screen, 10 vs 30 is clearly closer
  (`cam_user1_dist10.png` vs `cam_user1_dist30.png`). **Open:** 30, 60, 120
  and 200 look alike at this spot, at both headings tried (`cam_user1_dist*.png`,
  `cam2_h270_dist{20,80}.png`). Something between this field and the view
  limits the drawn distance: camera collision near the trees and walls, or a
  per-frame value the field only seeds. The next step is the same A/B on open
  ground, then the camera's per-frame update (the user-camera vtable
  `0x649648`) if that also flattens. Until then, B1206-style clip distance
  can be set exactly up to ~30 here, and the field is readable at any value.

## 4. Proof (b): a scripted walk with timestamps

Macro: `scripts/eqemu/oracle/mq2/macros/walk.mac` (`/macro walk <heading>`).
Log: `review/B1214/proof_b/telnet.log`. Stills: `review/B1214/proof_b/`.

`/macro walk 90` faced east, held FORWARD for 4 s and sampled every 0.5 s.
x went -74.00, -66.21, -54.15, -41.05, -27.69, -14.28, -0.84, 12.59, 26.03
at 0.53-0.59 s intervals, a steady ~24.6 units/s with y fixed at 424.73. On
release it stopped at x=33.04. MQ2MoveUtils `/moveto loc 424.73 -74.00` then
returned it to **0.09 units** of the start. The first run (`/macro walk 180`)
walked into the wall south of the spawn point at y=407.33. The log records
that too: speed stays 100 while the position stops changing.

## 5. Proof (c): the client's particle emitter list, read from memory

The plugin is `scripts/eqemu/oracle/mq2/plugin/MQ2B1214.c`, plain C built
with mingw-w64 i686:

`i686-w64-mingw32-gcc -O1 -shared -static-libgcc -o MQ2B1214.dll MQ2B1214.c -Wl,--insert-timestamp`

The build that ran has sha256
`6a947ca1ac70f161d015dd237e8985a15fb7966ef0dd32580a688eceef44e3d8`. The
built DLL is not committed: build it from the source. MQ2Main's API is
`extern "C"` (the 20051101 source's `MQ2Main.h` defines `EQLIB_API extern "C"
__declspec(dllexport)`), so the plugin resolves `AddCommand` and
`WriteChatColor` by name and needs no MSVC. The loader rejects a plugin whose
PE timestamp is older than `MQ2Main.dll`'s (`MQ2PluginHandler.cpp` `checkme`),
hence `--insert-timestamp`.

**The structures, from our binaries (every address re-derived here, none
taken from MQ2):**

- eqgame holds the particle engine at `*0x99e36c`. The spell-FX create at
  `0x495f02` calls the engine's vtable +0x14 with (EDD index,
  `key = spawn+0x258 << 16 | spell id`, ..., actor, 0, attach track, &out,
  scale, 1) (`review/B1141/ghidra/out/eqgame.exe.c:118594` through
  `review/B1141/ghidra/out/eqgame.exe.c:118598`).
- DX9 vtable +0x14 = `0x1005e1d0` forwards to `0x1005e0a0(engine+0x14, ...)`.
  The environment family uses `engine+0x38` (`0x1005e2c0`,
  `review/B1141/ghidra/out/EQGraphicsDX9.dll.c:66567`). In `0x1005e0a0`
  (`review/B1141/ghidra/out/EQGraphicsDX9.dll.c:66524`), container+0x10 is
  the def count, container+0x14 is the `CEmitterDef*` array and def+4 is the
  enabled byte.
- CreateEmitter `0x1005a430` (`review/B1141/ghidra/out/EQGraphicsDX9.dll.c:63389`)
  pushes each new `CEmitter` (0x8c bytes) on the def's live list. The head is
  def+0x10 and the next link is emitter+0x84
  (`review/B1141/ghidra/out/EQGraphicsDX9.dll.c:63516`).
- The constructor `0x1005a0e0` (`review/B1141/ghidra/out/EQGraphicsDX9.dll.c:63233`)
  lays out the emitter: +0x08 key, +0x0c ring capacity, +0x20 scale,
  +0x24 duration, +0x38 position, +0x68 actor, +0x6c attach point,
  +0x70 track, +0x74/+0x78 ring base/end, +0x7c/+0x80 ring read/write
  (0x80-byte particles).

`/emdump [all]` walks every def of the spell family (and with `all`, the
environment family). For each live emitter it prints the key split into spawn
id and spell id, tagged (ME)/(TARGET), plus capacity, live particle count,
scale, duration, position, actor, point, track and EDD record.

**Run** (`scripts/eqemu/oracle/mq2/macros/emfx.mac`, `/macro emfx 278`:
target self, GM `#castspell 278`, `/emdump` every 0.5 s;
`review/B1214/proof_c/telnet.log`, stills `review/B1214/proof_c/c_278_*.png`):

- Before the cast: the spell family has 2,118 defs and 0 live emitters. The
  environment family had 26 live emitters, all def 50, in pairs ~0.25 apart:
  qeynos2's torch flames, scale 1.818, duration 9999.
- 0.5 s after `#castspell 278` there were 8 live emitters, all key
  `0x01030116` = spawn 259 (ME, equal to MQ2's `${Target.ID}` 259) and
  spell 278: defs 333 and 336 (cap 189, scale 15, dur 5.0), 334 (cap 85,
  scale 10, dur 2.0) and 335 (cap 38, scale 12.5, dur 5.0), two emitters
  each. The live particle counts climb 9 → 39 → 93 → 135 → 177 → 183, then
  drain. The stills show the blue landing on Testcaster.
- **Cross-check against the record:** spell 278 (Spirit of Wolf) has field
  145 = 88 in `/mnt/projects/EverQuestP1999/spells_us.txt`. Record 88's
  Target stage (`refactor/B1164/scripts/b1164_eff.py`) is `(333, min 39,
  anchor 3, pt 3) (334, 24, 3, 2) (335, 0, 3, 2) (336, 39, 3, 1)`. At caster
  level 60 all four slots pass, and the dump has exactly defs 333-336. Their
  heights order by attach point: 333 (pt 3) z≈5.9, 334/335 (pt 2) z≈4.2,
  336 (pt 1) z=3.13, which is the feet (Testcaster's z).
- **Two facts for the FX chain**, measured on this GM `#castspell` path only
  and not investigated further: (1) **every slot made TWO emitters**,
  identical except for their addresses; (2) **+0x6c (point) is 0 and +0x70
  carries a per-attach-point object** (three distinct values, stride 0x54).
  The decompiled call passes a literal 0 in the point position and the
  attach object as the next argument
  (`review/B1141/ghidra/out/eqgame.exe.c:118597`), which agrees with the
  dump.

This is ground truth for what the client draws (emitter set, counts, live
particles, positions) with no pixels involved: the instrument the issue wanted.

## 6. Offsets, and the recommendation

**Offsets match our binary.** Three independent checks:

1. MQ2 refuses a client whose date/time at `__ActualVersionDate`/`Time`
   (0x64D1D4 / 0x64D1E0) differ. Those bytes in our `eqgame.exe` read
   "Oct 31 2005" / "10:33:37".
2. `scripts/eqemu/oracle/mq2/offset_check.py <eqgame.h> <eqgame.exe.c>`
   checks every address in the 20051101 source's `eqgame.h`. Of 262 code
   addresses, 256 are function starts (`// FUNC`) in our decompile. Of 168
   data addresses, 162 are referenced globals. Output:
   `review/B1214/logs/offset_check.txt`. The 12 not matched are the two
   version strings (verified by bytes above), `__EncryptPad0`, `__Guilds`,
   `__HotkeyPage`, `instCRaid` and 6 UI methods Ghidra did not split into
   functions. None is on the paths used here.
3. Live: every `AddDetour` in `review/B1214/logs/DebugSpew.log` reports
   "Detour success." (57 across the session's two client launches, 0 failures). `pinstCharSpawn` 0x905CF8 → +0x258 gave
   SpawnID 259, the same as `${Target.ID}`. `${Me.Y},${Me.X}` = 428,-74
   matched the database row. The plugin's own addresses (0x99e36c, 0x925508,
   0x92550c) were validated by the camera readback (30/82 = the ini) and the
   record-88 match.

Re-deriving an offset for a future plugin is cheap. `eqgame.h` is a flat
`#define NAME 0xADDR` list, and `offset_check.py` plus the decompile turn
each one into a grep. No offset needed re-deriving for Titanium.

**Recommendation: ADOPT for lanes 1-4**, as the oracle's control and readout
layer.

- What it gives: deterministic facing, pitch, camera mode and walking with a
  timestamped log, all without X focus. It also gives emitter-level ground
  truth for every FX comparison, which retires the pixel instrument for
  "which emitters, how many, where, when".
- Cost to productionise, roughly one card each: (1) fold `launch.sh` +
  telnet into `oracle_remote.sh` behind a plan directive (e.g. `# mq2`), with
  plan rows that send MQ2 commands; (2) a `/emdump` stream mode (OnPulse,
  every N ms, to `Logs\b1214.log`) for time series, since a 0.5 s macro tick
  is coarse; (3) the camera-distance follow-up from sec. 3; (4) a decision on
  where the plugin is built. mingw was installed on eq-oracle-1 for this spike
  and purged afterwards. A CI job or a permanent package on the oracle VMs are
  the options.
- Risks under Wine: Wine 9.0 handled the global-hook injection and Detours
  without errors, but a Wine upgrade could change hook-DLL loading, so pin
  the lane's Wine. Enter World still needs a focused X click (step 5). MQ2's
  per-frame hooks add CPU on llvmpipe; not measured here, and FX stills are
  already slow on llvmpipe. The binary is a third-party 2022 build: keep the
  sha256 pins in `install.sh` and the throwaway-prefix rule. **Risk to
  fidelity:** MQ2 detours the client (`HookMemChecker`, display hooks), so an
  MQ2-hosted client is no longer byte-pristine. MQ2 hooks no particle path,
  but an FX leg that must be pristine can still run without `launch.sh`'s MQ2
  step (`NO_MQ2=1`).
- Not proven: lane 5 (NVIDIA Xorg) and a two-client (observer) session with
  MQ2 in each client, which needs a second telnet port per prefix copy.

## 7. Lane restoration

- The lease was taken as `B1214/b1214spike` and renewed every 240 s. Released
  13:02Z. The install.sh re-test took and released `B1214/b1214install` at
  13:03Z. `scripts/eqemu/lease show --lane 1` reads `state=FREE`.
- On eq-oracle-1: eqgame, MQ2, the prefix's wineserver and the spike Xvfb
  were stopped, and `~/eq-oracle/b1214` (the prefix and client copies) was
  deleted. The canonical client and prefix were never written. The mingw
  packages were purged. The shared libraries apt pulled in with them stay
  installed: binutils-common, libmpc3, libisl23.
- On the eqemu VM: the stack was brought up for the spike (it was down) and
  taken down again with `down.sh`. Testcaster's row was restored with the
  world down: pose -74/428/1.26841/heading 0, and the Spirit of Wolf buff row
  deleted (`character_buffs` back to 0 rows). No account or character rows
  were created.

## 8. Files

- `scripts/eqemu/oracle/mq2/install.sh`: install/uninstall on lanes 1-4
  (sha-pinned).
- `scripts/eqemu/oracle/mq2/launch.sh`: Xvfb + MQ2 + client on the VM.
- `scripts/eqemu/oracle/mq2/mqtel.py`: the telnet driver.
- `scripts/eqemu/oracle/mq2/proof_a.sh` and
  `scripts/eqemu/oracle/mq2/proof_cam.sh`: the proof (a) drivers.
- `scripts/eqemu/oracle/mq2/macros/{face_cam,walk,emfx}.mac`: the macros.
- `scripts/eqemu/oracle/mq2/plugin/MQ2B1214.c`: the emitter/camera plugin.
- `scripts/eqemu/oracle/mq2/offset_check.py`: the eqgame.h vs decompile check.
- `review/B1214/`: evidence (`shots/`, `proof_a/`, `proof_b/`, `proof_c/`,
  `logs/`).
