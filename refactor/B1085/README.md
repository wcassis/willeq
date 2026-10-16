# B1085 (GitLab #405): the chat key latches latch only while unfocused, and reset on every focus transition

Will 2026-10-15 (XU4): "having the chat window in focus with no text in the
input field and then pressing escape sometimes causes a forward slash to
enter the chat text input field rather than removing focus. A second escape
press causes focus to go away as intended."

## Mechanism (deterministic, read from the code at the parent commit)

`RendererEventReceiver::OnEvent()` (renderer_event_receiver.cpp:139-147)
latched `enterKeyPressed_` / `escapeKeyPressed_` / `slashKeyPressed_` on
EVERY key press BEFORE the chat-focused gate (the `:150-153` early return).
The latches are read-and-reset only in FrameComponent's UNFOCUSED branch of
processChatInput() (renderer_frame_input.cpp:665-683 enter/slash open the
chat; :736 escape -> handleEscapeKey()), so every press typed INTO the
focused box left a stale latch for the first unfocused frame:

1. `/who` + Enter: Submit drops focus; the next frame the stale ENTER latch
   re-opened the chat EMPTY (the "focus stayed" state).
2. ESC: Cancel dropped focus; the next frame the stale SLASH latch (the `/`
   from step 1) re-opened the chat with `/` -- Will's symptom, and only
   after a slash command, hence "sometimes".
3. A stale ESCAPE ran handleEscapeKey() -- closing the newest window or
   clearing the target (B905/B460) although the press was meant for the
   chat box.

## Fix

- `RendererEventReceiver::OnEvent()`: the three latches are set ONLY below
  the focused gate (i.e. only while chat is unfocused). The
  pendingKeyEvents_ queue is unchanged -- a press while focused is the chat
  box's, and the box is the only thing that reads the queue.
- `RendererEventReceiver::setChatInputFocused()`: resets all three latches
  in BOTH directions, so a press that changed the focus is consumed by the
  transition and can never replay in the new state.
- `FrameComponent`'s chat policy (renderer_frame_input.cpp) is unchanged in
  intent: it still reads the latches only in the unfocused branch. No
  defensive read had to be deleted -- the two other readers (the B998
  character-select host's hand-spending, char_select_host.cpp:144-146, and
  processOptionsKeyCapture's discard reads,
  renderer_frame_options.cpp:240-242) both run under the receiver's
  chat-focused gate, so with the latches now only set while unfocused they
  are a no-op safety net that keeps their behaviour. The host's
  setChatInputFocused(true)/release()'s setChatInputFocused(false) is the
  SHARED reset for those; behaviour is kept, and the sweep table in
  tests/test_read_and_reset_ownership.cpp (3/3/3 call sites) still counts
  them.

## Nets

- `tests/test_b1085_chat_latches.cpp` (new, device-free, the T235 idiom):
  - Part 1, OnEvent alone: with focus on, Enter/Escape/`/` leave all three
    latches false and fill only the queue; with focus off they latch; a
    focus transition resets every latch in both directions.
  - Part 2 (Will's exact sequence through the production
    processChatInput()): `/` opens the chat, `who` is typed, Enter submits
    and publishes, ESC -- chat closed after ONE Escape, input empty, no
    window closed, no ClearTarget.
  - Part 3: an Enter after a sent line does NOT re-open the chat.
  - Part 4: an ESC typed into the box, with the bank open and a target set,
    never closes the bank or clears the target; a genuinely unfocused ESC
    still closes the bank (CloseBankIntent, T290's server-owned rule) and
    leaves the target (B460).

## Acceptance proof

(A) red at the parent commit / green at the head: the two frame-input
    sequence tests (`WhoThenEnterThenEscapeLeavesTheChatClosedAndEmpty`,
    `AnEnterAfterASentLineDoesNotReopenTheChat`) and
    `PressesWhileChatFocusedDoNotLatchAnything` are red at the parent (the
    latches were set above the gate); green at the head. (run: see the
    build log below)
(B) hygiene + build-test green at the MR head -- the MR pipeline.
(C) character select: tests/test_character_select.cpp green at the head
    (B998's tests still spend/ignore Enter/ESC as before).
(D) no bless-only file touched (no tests/goldens/*, no manifest.json, no
    baseline, no config/constrained_presets.json, no third_party/).

## Build log (local, this host)

/home/user/kb-evidence/kb-b1085-chat-latches/build3.log: cmake configure +
`cmake --build build -j3 --target test_b1085_chat_latches
test_character_select test_hotkey_dispatch_census
test_read_and_reset_ownership test_reply_to_tell_key
test_frame_component`. All six B1085 tests green; the four
neighbour suites (character_select, read_and_reset_ownership,
reply_to_tell_key, frame_component) and the hotkey census green;
`ctest -L hygiene` 38/38 and `check_doc_counts.py` OK.

Not verified / left open:

- No live XU4 run (the card is lane::offline and the box is Will's).
- The third replay (mechanism 3) has no oracle still for it; the test pins
  fable's own stated B905/B460 semantics (an ESC that is the box's press
  must not reach the window stack) rather than a measured oracle line.
