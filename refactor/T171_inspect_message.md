# T171 — A local inspect message, so answering an inspect stops destroying data

**Slot: slot1. x86 only. NO eqemu, NO lock, NO Pi (offline).** Orchestrator
executed directly, at Will's instruction ("build it but skip the one-time
warning").

**Territory: the inspect-message stack —
`include/client/inspect_answer.h`, `src/client/inspect_answer.cpp`,
`include/client/eq.h`, `src/client/eq_bridge.cpp` (config persistence),
`src/client/application.cpp` (one load call), the action layer
(`action_dispatcher.{h,cpp}`, `eq_action_handler.{h,cpp}`,
`command_processor.{h,cpp}`), `tests/action_test_fakes.h`,
`tests/test_inspect_answer.cpp`, `tests/test_command_processor_senders.cpp`,
`refactor/`.**

## The problem T170 left behind

T170 made this client answer inspect requests, which it must (eqemu only
builds the answer server-side for SoF+ clients and bots). But every answer's
`text` field is memcpy'd over the character's stored inspect message and
`Save()`d unconditionally, and T170 had nothing to put there, so **it sent
empty and wiped the message on every inspection**.

**What a local store can and cannot fix — established before building, not
assumed:**

- **The first clobber is unfixable by any client-side means.** There is *no*
  server→client route for the value: no PlayerProfile field carries it,
  `OP_InspectMessageUpdate` is inbound-only in eqemu (a `ConnectedOpcodes`
  entry with no send site anywhere) **and is not mapped for Titanium at all**,
  and the answer path only ever carries *other* players' text. The only way to
  not clobber is to not answer.
- **Everything after it is fixed.** The user sets a message, we echo it on
  every answer, the server's copy converges to ours and stays.

The real gain is not damage reduction, it is that **we stop being anomalous**.
The protocol's design is that the client owns this text — the real Titanium
client also only pushes it inside its own inspect answers, so between any two
clients it is already last-writer-wins. Before T171 we were a client that
always sent empty and therefore always destroyed. Now we are an ordinary
participant, and the residual "loss" is the same race the real client already
has with itself across two machines.

## What was built

**`/inspectmessage [text|-clear]`**, alias `/inspectmsg`, category Social, on
the full action stack (CommandProcessor → ActionDispatcher →
IActionHandler → EqActionHandler → EverQuest).

**No argument SHOWS, it does not clear.** Clearing needs the explicit
`-clear`, so the destructive form is never the accidental one — the server
never sends the message back, so a wrongly-cleared message is unrecoverable.
There is a test whose failure message says exactly that.

The text is taken **raw**, not tokenised and rejoined: it is prose, and the
user's internal spacing is theirs. Only the leading separator whitespace is
dropped, and a whitespace-only argument is treated as "show", so a stray
trailing space cannot be read as a message.

Persistence is per character in the existing config file (`m_config_path`),
alongside the hotbar, loaded at zone-connect — the first moment anyone could
inspect us.

**Deliberately not zone-gated**, unlike every other dispatcher entry point:
this changes local state and writes a file, and there is no packet to gate.
The value reaches the server on its own schedule.

## The bug fixed on the way, and it was mine

`MakeInspectAnswer` (T170) truncated at 287 bytes — the size of the wire
field. **The bound that matters is the server's:** `Handle_OP_InspectAnswer`
memcpy's exactly `sizeof(InspectMessage_Struct)` == **256** bytes out of that
288-byte field into a 256-byte struct. A 256+ character message would be
stored with **no terminator inside the bytes eqemu keeps**, and eqemu later
`strcpy`s that stored text into a 288-byte buffer (`client.cpp:6628`) — an
overread on their side. It was unreachable while the message was always empty;
letting a user set one makes it reachable, which is why it is fixed here and
not deferred. The limit is now a named constant,
`EQT::INSPECT_MESSAGE_MAX_LEN`, applied **on set** so the user is shown what
will actually reach the server.

**Measured, not argued:** restoring the old 287-byte bound fails
`AnOverlongMessageIsTruncatedToTheServersLimitNotTheFields` and nothing else.

## One extraction and one deliberate behaviour change

`SaveHotbarConfig`/`LoadHotbarConfig` each hand-rolled the "find the
per-client node in this config" logic, in two subtly different forms. Rather
than add a third and fourth copy — duplicated-then-drifted being this
codebase's second most common defect class — the two forms were extracted
**verbatim** into `ResolveClientConfigForWrite` / `ResolveClientConfigForRead`
and all four call sites now share them. They stay two functions on purpose:
the read side accepts a bare top-level object as the client config and never
rewrites the file, and merging them would change one of the two behaviours.

**The one intentional change:** a NULL root — which is what
`JsonConfigFile::Load` returns for a missing *or* unparseable file — now
initialises the structure instead of bailing. The previous behaviour meant a
first save against a not-yet-existing config **silently wrote nothing**, which
is a bug the hotbar shared. The only case this touches is the one that
previously did nothing.

## Verification

- **16 new tests** (9 in `test_inspect_answer.cpp`, 7 command-layer). Beyond
  the cap: the config round trip, a save that must not discard unrelated keys
  in the same file, a load from a config *without* the key that must not clear
  what is already set, and the handler's show path proving it never writes.
- Suite **2643/2644**, sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`.
- T168's `check_sender_reachability` gate passes.
- Build clean; `build-gles2-x86.sh` clean; **visual gate PASS**, goldens
  untouched; `build-arm-noble.sh` clean; `check_file_sizes.sh` **0 FAIL**.

## Not done, by instruction

**The one-time "answering an inspect will replace your stored message"
warning was explicitly skipped at Will's request.** The consequence stands
unannounced to the user: the first inspection of a WillEQ session still
replaces whatever the server held, silently. It is documented at
`SendInspectAnswer()` for anyone reading the code.

## Not verified

Still not wire-verified — this needs the two-client probe T170 already owes:
`Testmelee` and `Oraclerogue` in one zone, `/inspectmessage` set on one,
`/inspect` from the other, and `character_inspect_messages` read back to
confirm the stored value is now the one we set rather than empty.
