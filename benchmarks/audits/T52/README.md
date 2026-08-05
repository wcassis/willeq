# T52 evidence

Captured at `-d 6` (TRACE + packet dumps), which produced ~200k-line,
~26MB transcripts per run — ~121MB in total, against ~350KB for
comparable prior units (T49, T50). Orchestrator repacked them at merge:

- `*_excerpt.txt` — focused ±25-line windows around every zone-change,
  zone-connection, PlayerProfile, spawn-position and `/loc` event. **This
  is the wire evidence the Review cites**; read these first.
- `*.txt.gz` — the complete original transcripts, unmodified, gzipped.
- `run_step1_*.sh`, `*.driver` — the probe drivers, unchanged and
  re-runnable.

Nothing was discarded. The decisive lines for the "not ours" verdict are
in `transcript_goto_primary_excerpt.txt`: the server requests the
correct destination `pos=(200.0, 100.0, 5.0)`, the client faithfully asks
for it, and the server's own approval comes back with Testchar's
DB-persisted `pos=(21.9, -77.9, 3.0)`.

Future units: `-d 5` is enough unless you specifically need packet dumps,
and if you do need them, capture a narrow window rather than a whole
session.
