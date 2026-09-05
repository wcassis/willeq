# T379 — login and world sends through PacketSender (B146)

Class B, slot3, no server. Read the B146 board text, T376's §Review
(the census and the STOP that boarded this), `include/client/packet_sender.h`
and `src/client/packet_sender.cpp` (how `SafeQueueZone` feeds the T268
seam and the R01 recorder), and `src/client/login_world/login_world_component.cpp`
(the 12 direct sends: 4 on `m_login_connection`, 8 on `m_world_connection`).

1. Give `PacketSender` per-stream entry points `QueueLogin(...)` and
   `QueueWorld(...)` with the same three-step body as `SafeQueueZone`
   (T268 seam → recorder with `SessionStream::Login`/`World`,
   ClientToServer → the connection's queue), each null-safe (a missing
   connection logs and returns; no dereference). If `PacketSender` does not
   hold the login/world connections, give it what `login_world_component`
   holds (read how the zone connection reaches it — same route).
2. Route the 12 sends; delete wrappers that guard nothing else.
3. Tests, pre-fix failing: (a) each stream's sender with no connection
   returns without crashing; (b) a recorded session of a login handshake
   contains the client's side (`OP_SessionReady`/login request, world
   `OP_SendLoginInfo`/`OP_EnterWorld` — whichever fable sends; name them
   from the code) — T376's `RecordedSession_*` shape.
4. Census pin: extend `tests/test_direct_sender_census.cpp` if T378 has
   landed it on your tip (check `git log fable`), else add one for this TU
   alone — no `->QueuePacket(` outside `PacketSender` in
   `login_world_component.cpp`.
5. Strike B146.

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, the login/world
goldens if any (grep `tests/goldens` for login), file sizes. No graphics.

STOP: the login stream's sends happen before `PacketSender` exists (then
report the construction order and route only the world sends).
