# T338 — The owed Ctrl+M map-overlay live pair (B054 close-out proof)

Measurement only; slot1; eqemu lock. T336's driver (`review/T336/t336_live.sh`)
with the key changed from Ctrl+N to Ctrl+M, at the current tip (must
contain T335 + T335b — check `git log`). OFF and ON captures, same
character/spot/daylight, `--eqe1-path`; the ON frame must show the map
overlay lines AND the 2D UI. Optionally the same for Ctrl+V (frustum) if
the binding exists (`hotkey_manager.cpp`). `review/T338/README.md` + PNGs;
strike the "owed" note on B054; restore the character as found; `down.sh`;
delete the lock. STOP: lock held; the toggle does not toggle (report the
log line, do not touch code).
