# One-command official-client oracle capture (B1048)

Run from a willeq worktree on hermes-ext. The driver pairs `EQEMU_LANE=1..4`
with its official Titanium Wine VM, takes the eqemu lock, starts the stack,
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
```

`install.sh` also writes `~/eq-oracle/uigap-scripts/.oracle-repo`, so the
installed copy can be invoked from outside the checkout until that checkout
is moved or removed; reinstall it after moving the repo.

The four tab-separated columns are `action`, `target`, `hold_ms`, `still_name`.
`login <character>` starts Wine and enters character select; `zone <shortname>`
sets that character's zone **before** login and then enters world. `open`
accepts `hotbar`, `inventory`, `character-create`; `click` and `hold` use
root-screen `x,y`; `hold` uses its duration column in milliseconds. `press`
takes an xdotool keysym, `command` sends a slash command, `random`, `accept`,
`cancel`, `create <name>`, `still <name>`, and `wait <ms>` are available.
The shipped untouched-create plan uses the stock Human Warrior path and does
not change any appearance control. Use a fresh output directory and a unique
creation name. A plan cannot reuse an output directory containing
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
