# T345 — Owed live proofs: autorun and /stop (T343), Ultravision zone light (T339)

Measurement only; slot2; eqemu lock (free after T342). Reuse T336's driver
(`review/T336/t336_live.sh`) and T338's config (`maps_path` =
`/mnt/projects/maps/base`). Binary rebuilt from the slot at the current
tip (must contain T343 and T339 — `git log`).

1. **Autorun (B002):** qeynos2, Testchar, daylight, `--eqe1-path`. Press
   Num Lock; capture two frames one second apart — the position (HUD
   `/loc` or the client log's position line) must change with no key
   held. Press W once — autorun stops (two more frames, position steady).
   Then `/moveto` a nearby point and `/stop` mid-way — the position stays
   put on the next tick (T343 found `/stop` used to resume). Log lines +
   PNGs.
2. **Ultravision (B055):** `/say #cast` or `/cast` Ultravision on self
   (the GM kit: `#scribespells`/`#memspell`, see the `gm-kit-out-a-character`
   notes), night time (`/say #set time 22 0`); capture before and after
   the buff lands — the zone light change must be visible; wait for or
   `/say #buff`-remove the fade and capture again. If the spell cannot be
   cast by this character in 10 minutes, record why and leave the row
   owed.
3. `review/T345/README.md`; strike the "owed" notes on B002 and B055
   (and B032's live half if T343 recorded one); restore the character as
   found; `down.sh`; delete the lock. Docs-only commit.

STOP: lock held; Num Lock does not toggle (report the log lines, do not
touch code).
