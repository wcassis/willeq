# T432 — per-character persistence of what the user can change (B275)

Class B, no server. Read B275 and Will's direction (docs/decisions.md
2026-09-07): persist per character the user-changeable state — the 15
window open/closed states (`isOpen` in the static panels + the four
toggles T368/T415 added), the camera mode; keybinds stay global.
Positions are static (XML-derived) and are NOT persisted.

1. The official client writes `UI_<char>_<server>.ini` next to its
   executable, `[<Window>] Show=0/1` per window (T368 quoted
   `defaults.ini`; T382's oracle screenshots). Match the shape: fable
   writes `<cache dir>/ui/UI_<char>_<server>.ini` (T402's cache-path
   rule — outside the repo) with the same section/key names for the
   windows the oracle has, plus a `[Camera] Mode=` key; read it at
   zone-in, write it on every change and at camp.
2. Tests pre-fix failing: toggle a window, camp, log in → the state
   holds; a fresh character starts from `defaults.ini`'s rule (shown
   unless `Show=0` there — T368); the file lands in the cache dir, not
   the repo (`git status` clean).
3. Strike B275; annotate B350 if the oracle's ini carries any look
   setting fable now honours.

## Proof

Build (background), `ctest -j6`, pre-fix/post-fix, file sizes; no gate.

STOP: none expected.
