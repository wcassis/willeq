# T375 — `/report` sends what eqemu reads (B023)

Class B, slot2, no server (the oracle is eqemu's source). Read the B023
board text.

`PlayerComponent::SendReport` (`src/client/player/player_misc.cpp`) sends a
pipe-delimited reported/reporter/message body; eqemu's
`Client::Handle_OP_Report` (`/mnt/projects/akk-stack/code/zone/client_packet.cpp`)
reads a different shape, so `AddReport()` is never reached.

1. Read the handler and the struct it casts to (quote both, with the
   size check the handler performs). That is the wire shape.
2. Make `SendReport` build exactly that body. If the official client
   sends fields fable cannot know (a category id), pick the value the
   handler treats as the plain-text report and say why from the handler.
3. Test, pre-fix failing: the queued packet's bytes decode with a
   struct mirroring eqemu's and pass the handler's size check; the
   pre-fix body fails it. A recorded-session assertion through
   `PacketSender` (T360 shape) that `/report` emits one OP_Report.
4. The G08 / self-death goldens are untouched (different opcode); the
   bridge intent table golden if `/report` passes through an intent.
5. Strike B023. Live proof (a `/report` landing in eqemu's `reports`
   table) is OWED to the next lock unit — note it on the board.

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, file sizes.

STOP: the handler's shape needs a field fable has no source for and no
default the handler accepts (report the struct).

## Review

Branch `task/T375-op-report-shape`, cut from `fable` at **`84d7e227`**
(verified at checkout; `fable` has since moved on to `e580ce97` with T374/T376 —
the merge base for this branch is `84d7e227`). No STOP fired.

### 1. The wire shape — and a correction to this plan's premise

**The plan's step 1 asks for "the struct it casts to" and "the size check the
handler performs". There is no struct and there is no size check.** That is the
finding, not a shortfall: `Client::Handle_OP_Report`
(`/mnt/projects/akk-stack/code/zone/client_packet.cpp:13442`) is one of the very
few zone handlers that parses the payload byte by byte. Quoted whole:

```cpp
void Client::Handle_OP_Report(const EQApplicationPacket *app)
{
	if (!CanUseReport)
	{
		MessageString(Chat::System, REPORT_ONCE);
		return;
	}

	uint32 size = app->size;
	uint32 current_point = 0;
	std::string reported, reporter;
	std::string current_string;
	int mode = 0;

	while (current_point < size)
	{
		if (mode < 2)
		{
			if (app->pBuffer[current_point] == '|')
			{
				mode++;
			}
			else
			{
				if (mode == 0)
				{
					reported += app->pBuffer[current_point];
				}
				else
				{
					reporter += app->pBuffer[current_point];
				}
			}
			current_point++;
		}
		else
		{
			if (app->pBuffer[current_point] == 0x0a)
			{
				current_string += '\n';
			}
			else if (app->pBuffer[current_point] == 0x00)
			{
				CanUseReport = false;
				database.AddReport(reporter, reported, current_string);
				return;
			}
			else
			{
				current_string += app->pBuffer[current_point];
			}
			current_point++;
		}
	}

	CanUseReport = false;
	database.AddReport(reporter, reported, current_string);
}
```

`size` is read and used only as the loop bound — there is no
`if (app->size != sizeof(X)) return;` line, and no cast anywhere in the body. So
the "size check" this plan expected does not exist and the body has no fixed
length; it is a three-field, `|`-separated, NUL-terminated byte string:

| field | bytes | mode |
| --- | --- | --- |
| `reported` (the accused) | up to the FIRST `\|` | 0 |
| `reporter` (us) | up to the SECOND `\|` | 1 |
| the report text | the rest; `0x0a` -> `\n`; `0x00` terminates | 2 |

The sink (`/mnt/projects/akk-stack/code/common/database.cpp:1117`):

```cpp
void Database::AddReport(const std::string& who, const std::string& against, const std::string& lines)
{
	auto e = ReportsRepository::NewEntity();

	e.name          = Strings::Escape(who);
	e.reported      = Strings::Escape(against);
	e.reported_text = Strings::Escape(lines);

	ReportsRepository::InsertOne(*this, e);
}
```

called as `AddReport(reporter, reported, current_string)` — so `reports.name` is
the REPORTER, `reports.reported` the accused, `reports.reported_text` the text.

Titanium has no encoder or decoder for this opcode: `grep OP_Report` over
`common/patches` returns nothing (only `BugReport_Struct`, a different opcode),
so the payload fable queues is byte for byte the payload the handler walks.
Opcode `OP_Report=0x7f9d` (`utils/patches/patch_Titanium.conf:251`) already
matches fable's `HC_OP_Report = 0x7f9d` (`include/client/eq_opcodes.h:338`) —
the opcode was never the problem.

**The board's wording was slightly off too**: it says `AddReport()` "is never
reached". It IS reached — via the fall-through call at the bottom of the loop —
with `reporter` empty, `reported` = four NUL bytes glued to the description, and
`reported_text` **empty**. The report text was silently thrown away, which is
worse than not reaching the sink at all, because the row looked like it worked.

### 2. What `SendReport` now sends

Body = `"|" + <our character name> + "|" + <description>` written with
`PutCString` (NUL-terminated; `PutString` does not write one, and the mode-2 loop
ends on `0x00` and nothing else — the same reasoning T150 recorded for
`SendFriendsWho` two functions below).

`reported` is sent **empty**. Justification from the handler, as the plan's step 2
requires: there is no category field anywhere in `Handle_OP_Report` (the old
`PutUInt32(2, 0) // category_id` was pure invention — that field belongs to
`OP_Bug`, see B142), and the only field the handler treats as the plain-text
report is mode 2, which is entered only after two `|`. fable's sole caller is
`/bug <description>` (alias `/report`), which reports a defect and not a player,
so there is no accused character; an empty first field is a payload starting with
`|`, which is exactly what mode 0 expects. The reporter is `ctx_.character`,
which is the field eqemu files the row under.

The sender also moved from `ctx_.zone_connection->QueuePacket(packet)` to
`ctx_.net.SafeQueueZone(packet)` — the T360/B068 change, for the same reason:
a direct `QueuePacket` is invisible to BOTH the T268 ordered-capture seam and the
R01 session recorder, so no test could observe it and no `--record-session`
capture would contain it.

### 3. Tests

New file `tests/test_op_report_shape.cpp` (330 lines), target registered in
`tests/cmake/commands.cmake`. Its oracle is `EqemuParseReport()` — the handler
loop above transcribed statement for statement — run over the bytes the T268 seam
captured, so nothing re-derives the format. Fixture is
`test_self_death_sequence.cpp`'s `DeathHarness` construction (real `EverQuest`,
`FakeUdpTransport` connection, `conn->m_stats.sent_packets = 1` so
`SafeQueueZone`'s zero-stats guard does not eat the send).

Pre-fix runs were taken by exporting the diff and `git apply -R` (never the
stash), rebuilding the target, and reverting the revert.

| test | pre-fix | post-fix |
| --- | --- | --- |
| `OpReportShape.QueuedBodyParsesWithEqemusHandler` | **FAIL** — the seam captured 0 OP_Report packets (the old sender bypassed it) | PASS — reporter `Testchar`, reported ``, text intact |
| `OpReportShape.BodyIsNulTerminatedSoTheHandlerTakesTheTerminatingArm` | **FAIL** — same, no body to inspect | PASS — trailing `0x00`, exactly two `\|`, terminating arm taken |
| `OpReportShape.NoCategoryPrefixPrecedesTheText` | **FAIL** | PASS — `body[0] == '\|'`, not a NUL |
| `OpReportShape.NewlinesInTheDescriptionSurviveTheHandler` | **FAIL** — `r.text` `""` against `"line one\nline two"`, `r.reporter` `""` against `"Testchar"` | PASS |
| `OpReportShape.RecordedSession_SlashBugEmitsExactlyOneOpReport` (the T360-shape recorded-session assertion, driven through `EqActionHandler::bug()` = `/bug`) | **FAIL** — 0 OP_Report records in the `.sleq` | PASS — exactly 1, and it parses |
| `OpReportShape.ThePreT375BodyLosesTheReportEntirely` | PASS | PASS |

`6 tests ran, [ PASSED ] 0` (5 failed) before — the sixth is a fixture test that
reconstructs the OLD body by hand and feeds it to the transcription; it is a net
on the transcription, not on `SendReport`, so it is green both ways and is the
concrete statement of the defect: `reporter == ""`, `text == ""`,
`reported == "\0\0\0\0the boat to Freeport never arrives"`.
After: `[ PASSED ] 6 tests`, exit 0.

Note the pre-fix reds are red for TWO independent reasons at once — the wrong
bytes AND the unobservable sender — which is why the first five all report "0
OP_Report" rather than a byte mismatch. The sixth test is what pins the byte
claim on its own.

### 4. Goldens

Untouched. `self_death_outbound_g07.txt` / G08 are a different opcode, and
`/bug` reaches `EqActionHandler` through `ActionDispatcher`, not through a
renderer intent, so the bridge intent-table golden is not on this path. The full
`ctest -j6` (below) ran every G golden green, including
`command_registration_order_g03` — this branch adds no command registration.

### 5. Verification

| step | result |
| --- | --- |
| `cmake --build build -j24` (GLES2 config tree) | **EXIT=0** |
| `ctest --test-dir build -j6 --output-on-failure` | **3,960 of 3,961**; sole failure `AudioManagerTest.PreloadCommonSounds` (the waived ALSA host quirk). ctest exit 8, per the summary line. |
| `tests/test_op_report_shape.cpp` standalone | 5 FAIL pre-fix / 6 PASS post-fix (table above) |
| `scripts/refactor/check_file_sizes.sh 84d7e227` | 1 file checked, `src/client/player/player_misc.cpp` 825 lines, **1 ok / 0 warn / 0 fail**. The new test file is 330 lines. (Running it against `fable` picks up T374/T376's files too, since `fable` moved past this branch's base; that run is also 8 ok / 0 fail.) |

Steps 3-5 of the standing bar (GLES2 tree, visual gate, ARM) were not run: this
plan's `## Proof` narrows the bar to build + `ctest -j6` + the pre/post test runs
+ file sizes, and nothing here touches a shader, an asset or a pixel.

### 6. Owed, and new

- **LIVE PROOF OWED** (plan step 5): a `/report` landing as a row in eqemu's
  `reports` table has NOT been observed. The eqemu lock was held by T370 for the
  whole of T375, and this unit was dispatched explicitly without server
  ownership. Noted on B023's struck entry for the next lock unit. Two things that
  unit needs to know: `Handle_OP_Report` opens with `if (!CanUseReport)` and
  clears the flag on the first report, so a client gets exactly **one** report
  per zone session (a second prints `REPORT_ONCE` and is dropped); and the row to
  look for has `name` = the character, `reported` = `''`, `reported_text` = the
  description.
- **New board item B142** (C): `/bug` is on the player-report channel, not on
  Titanium's actual `OP_Bug` (0x7ac2, an 8,736-byte struct with a category table,
  UI path, position and a 4,098-byte `system_info`). T375 made `/bug` correct on
  the channel it uses; whether it should move channels, and whether `/report`
  should split off as `/report <player>`, is a product call.
