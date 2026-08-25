# T241 — delete Application::connectRendererCallbacks() and the dead game-thread key readers (T235 findings)

Slot: slot1. x86. NO eqemu. Class A, reachability pattern #13. Territory:
src/client/application.cpp `connectRendererCallbacks` (+ header),
include/client/graphics/irrlicht_renderer.h / output/graphical_renderer.h
callback setters it targets, src/client/input/graphics_input_handler.cpp
dead consumers, include/client/graphics/renderer_event_receiver* (only the
accessors that lose their last caller), tests. Do NOT touch
input_action_bridge / action_dispatcher hotbar arms (T239 slot3),
eq_action_handler.cpp / combat.* (T240 slot2), renderer_frame.cpp's
processChatInput body (T235 merged — leave it).

## Findings (T235 Review, review/T235/)
1. `Application::connectRendererCallbacks()` is unreachable: nothing
   derives from `output::GraphicalRenderer`, so its `dynamic_cast` always
   fails. Every callback it would set (target, door, chat-submit, …) and
   `IrrlichtRenderer`'s parallel setters have no other caller; live routes
   are bridge intents. Verify each with grep before deleting; delete the
   method, the setters, the callback members, and any `std::function`
   typedefs left orphaned.
2. `enterKeyPressed` / `slashKeyPressed`: the GAME-thread consumers in
   `GraphicsInputHandler` (`OpenChat`/`OpenChatSlash` actions) have no
   downstream consumer; the render thread handles chat opening. Delete the
   game-thread side.
3. `popKeyEvent` / `clearPendingKeyEvents` game-thread callers
   (`graphics_input_handler.cpp` ~242/257) have no production caller.
   Delete; if that leaves the render thread as the ONLY user of
   `pendingKeyEvents_`, say so — it removes one item from T225's vector
   race inventory (deferred.md).
For each deletion record the grep proving zero callers (before) and the
build proving nothing needed it (after).

## Verification
Full ctest; visual gate (renderer headers touched); Review with the
line-count delta; evidence review/T241/. Commit:
`T241: unreachable renderer callback plumbing and dead game-thread key readers deleted`.
