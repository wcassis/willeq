# B1049 / B1050: DRM input and scanout ordering

The paths below are `docker/irrlicht-drm/CIrrDeviceFB.cpp` and its
`docker/irrlicht-drm-x86/CIrrDeviceFB.cpp` fork. The latter is mirrored
even though its normal x86 route is surfaceless EGL.

| Phase | Before | After |
|---|---|---|
| evdev key value 2 | Updated held state; emitted no `EET_KEY_INPUT_EVENT`. | Emits key-down with translated `Char` and tracked modifiers. |
| pending DRM flip | Wait could time out and clear pending without an event. | Timeout retains pending state and both locked buffers; no new swap/flip. |
| async flip submission | Released the previous, still-scanned-out GBM bo immediately. | Saves it as retired; releases it only after the next successful flip-event wait (kmscube order, #370). |
| synchronous SetCrtc | Released previous bo after submission without checking the result. | Checks SetCrtc success and releases previous bo only after that synchronous operation finishes. |

The new test `DRMKeyTranslation.PressRepeatRepeatRelease` drives evdev
press/repeat/repeat/release without a DRM device. The shift/control and
invalid-event cases exercise the same free function used by both forks.

## Not verified / left open

- Device verification: Will's XU4 play session after merge
  (`ci-arm/by-branch/fable` publish). Scanout tearing and partial windows
  cannot be judged from the off-device unit test, an internal frame dump,
  or a cross-build.
