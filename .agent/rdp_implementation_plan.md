# RDP Implementation Plan for WillEQ

## Overview

This plan details the implementation of native RDP (Remote Desktop Protocol) support in WillEQ as an **additional display option** alongside the existing Xvfb + x11vnc approach. Users can choose either method based on their needs.

**Display Options After Implementation:**
1. **Direct X11** - Native display with GPU/software rendering (existing)
2. **Xvfb + x11vnc** - VNC-based remote access (existing)
3. **Native RDP** - Direct RDP streaming via FreeRDP (new)

**Benefits of RDP Option:**
- Native Windows Remote Desktop client support (mstsc.exe)
- Alternative to VNC for users who prefer RDP
- Potentially improved compression with RFX/NSCodec codecs
- Better integration for Windows-based users
- Can run alongside X11 (RDP captures frames, X11 still renders)

## Implementation Phases

### Phase 1: CMake and Build System Setup ✅ COMPLETED

**Files modified:**
- `CMakeLists.txt`

**Completed tasks:**
1. ✅ Added FreeRDP library detection using pkg-config (tries freerdp3/winpr3 first, falls back to freerdp2/winpr2)
2. ✅ Created `WITH_RDP` build option (defaults to ON if FreeRDP found, OFF otherwise)
3. ✅ Added `-DWITH_RDP` compile definition when enabled
4. ✅ Added FreeRDP include/library directories via `INCLUDE_DIRECTORIES` and `LINK_DIRECTORIES`
5. ✅ Added FreeRDP libraries to `LINK_LIBS`
6. ✅ Updated configuration summary to show RDP status with clear messages

**Verified:**
- Build succeeds with `WITH_RDP=ON` (FreeRDP 3.5.1 detected and linked)
- Build succeeds with `WITH_RDP=OFF` (no regressions)
- Configuration summary correctly shows RDP status in all cases

**Dependencies to install (Ubuntu 24.04):**
```bash
sudo apt-get install -y freerdp3-dev libwinpr3-dev
```

---

### Phase 2: RDP Server Core Implementation ✅ COMPLETED

**Files created:**

```
include/client/graphics/rdp/
├── rdp_server.h           # RDPServer class header
├── rdp_peer_context.h     # Per-client context structure
└── rdp_input_handler.h    # Scancode translation definitions

src/client/graphics/rdp/
├── rdp_server.cpp         # Main RDP server implementation
├── rdp_peer_context.cpp   # Context initialization/cleanup
└── rdp_input_handler.cpp  # Input event translation
```

**Implemented RDPServer class features:**
1. ✅ FreeRDP listener initialization via `freerdp_listener_new()`
2. ✅ Accepts incoming connections via `PeerAccepted` callback
3. ✅ Per-peer threads for client handling
4. ✅ Frame buffer with mutex-protected double buffering
5. ✅ Keyboard/mouse event routing via callbacks
6. ✅ RFX and NSCodec encoders for frame compression
7. ✅ SURFACE_BITS_COMMAND frame delivery with frame markers

**Input handler features:**
- ✅ Full RDP scancode to Irrlicht EKEY_CODE mapping
- ✅ Character translation for text input (with shift/caps handling)
- ✅ Mouse event translation (move, click, wheel)

**Note:** Certificate configuration is handled automatically by FreeRDP 3
(self-signed cert generation). Custom certificate support may be added later.

---

### Phase 3: Frame Capture and Encoding ✅ COMPLETED

**Integration point:** `IrrlichtRenderer::processFrame()` (src/client/graphics/irrlicht_renderer.cpp:~line 4502)

**Completed tasks:**
1. ✅ Added `captureFrameForRDP()` method that captures framebuffer using `driver_->createScreenShot()`
2. ✅ Passes BGRA pixel data to `RDPServer::updateFrame()` with proper pitch calculation
3. ✅ Frame encoding handled by RDPServer using RFX (RemoteFX) or NSCodec depending on client capabilities
4. ✅ Frames sent via `SURFACE_BITS_COMMAND` with frame markers (already in rdp_server.cpp)
5. ✅ Frame capture integrated into main render loop before `driver_->endScene()`

**Files modified:**
- `include/client/graphics/irrlicht_renderer.h` - Added RDP member, methods, callbacks
- `src/client/graphics/irrlicht_renderer.cpp` - Implemented all RDP methods:
  - `initRDP()` - Initialize RDP server with port and resolution
  - `startRDPServer()` / `stopRDPServer()` - Lifecycle management
  - `isRDPRunning()` / `getRDPClientCount()` - Status queries
  - `captureFrameForRDP()` - Framebuffer capture and streaming
  - `handleRDPKeyboard()` / `handleRDPMouse()` - Input routing to event receiver

**Frame rate considerations:**
- Throttle to ~30fps to balance quality vs bandwidth
- Only send frames when content changes (dirty region tracking as future optimization)
- Track frame_id for each peer context

---

### Phase 4: Input Translation ✅ COMPLETED (merged with Phase 2)

**Note:** Input translation was implemented as part of Phase 2 alongside the RDP server core.

**Completed tasks:**
1. ✅ Created full scancode mapping table in `rdp_input_handler.cpp` (RDP scancode -> Irrlicht EKEY_CODE)
2. ✅ Mapped all common keys: WASD, arrow keys, F1-F12, modifiers, space, enter, escape
3. ✅ Translated RDP mouse events to Irrlicht SEvent:
   - PTR_FLAGS_MOVE -> EMIE_MOUSE_MOVED
   - PTR_FLAGS_BUTTON1 -> EMIE_LMOUSE_PRESSED_DOWN / EMIE_LMOUSE_LEFT_UP
   - PTR_FLAGS_BUTTON2 -> EMIE_RMOUSE_PRESSED_DOWN / EMIE_RMOUSE_LEFT_UP
   - PTR_FLAGS_WHEEL -> EMIE_MOUSE_WHEEL
4. ✅ Added `handleRDPKeyboard()` and `handleRDPMouse()` methods to IrrlichtRenderer (Phase 3)
5. ✅ Events routed through existing `RendererEventReceiver`

**Key mappings implemented:**
- Movement: W/A/S/D, arrow keys, Q/E (strafe)
- Camera: Mouse look (right button drag)
- Actions: Space (jump), ` (auto-attack), Tab (cycle targets)
- Function keys: F1-F8 (targeting), F9 (mode toggle), F12 (screenshot)
- Chat: Enter, / (slash commands)
- UI: I (inventory), G (group), K (skills), U (door interact)

---

### Phase 5: IrrlichtRenderer Integration ✅ COMPLETED (merged with Phase 3)

**Note:** IrrlichtRenderer integration was implemented as part of Phase 3 alongside frame capture.

**Files modified:**
- `include/client/graphics/irrlicht_renderer.h`
- `src/client/graphics/irrlicht_renderer.cpp`

**Methods added to IrrlichtRenderer:**
```cpp
#ifdef WITH_RDP
bool initRDP(uint16_t port = 3389);
bool startRDPServer();
void stopRDPServer();
bool isRDPRunning() const;
size_t getRDPClientCount() const;
private:
    std::unique_ptr<RDPServer> rdpServer_;
    void captureFrameForRDP();
    void handleRDPKeyboard(uint16_t flags, uint8_t scancode);
    void handleRDPMouse(uint16_t flags, uint16_t x, uint16_t y);
#endif
```

**Integration points completed:**
1. ✅ `initRDP()`: Initialize RDP server with port and resolution
2. ✅ `processFrame()` (before `endScene()`): Capture and send frame to RDP clients
3. ✅ `stopRDPServer()`: Stop RDP server and clean up
4. ✅ Input callbacks: RDP input routed to event receiver via `handleRDPKeyboard/Mouse()`

---

### Phase 6: Configuration System ✅ COMPLETED

**Files modified:**
- `src/client/main.cpp` (command line argument parsing and JSON config)

**Command line options implemented:**
```
--rdp, --enable-rdp   Enable native RDP server
--rdp-port <port>     RDP port (default: 3389)
```

**Note:** Certificate options (--rdp-cert, --rdp-key) not implemented as FreeRDP 3
auto-generates self-signed certificates. Custom certificate support can be added later.

**JSON configuration implemented:**
```json
{
    "rdp": {
        "enabled": true,
        "port": 3389
    }
}
```

**Application lifecycle integration:**
1. ✅ RDP server initialized after graphics initialization
2. ✅ RDP server started automatically when enabled
3. ✅ RDP server stopped on application shutdown
4. ✅ Logging for RDP server startup/shutdown events

---

### Phase 7: SSL Certificate Management ✅ NOT NEEDED (FreeRDP 3 handles automatically)

**Note:** FreeRDP 3 automatically generates self-signed certificates, so explicit
certificate management is not required for basic functionality. Custom certificate
support can be added as a future enhancement if needed.

**Optional future enhancements:**
1. Create `scripts/generate_rdp_cert.sh` for custom certificate generation
2. Add `--rdp-cert` and `--rdp-key` command line options
3. Document certificate setup in README for users who need custom certificates

---

### Phase 8: Testing ✅ COMPLETED

**New test files created:**
```
tests/
├── test_rdp_server.cpp           # 20 unit tests for RDP server
├── test_rdp_input_handler.cpp    # 35 input translation tests
```

**Files modified:**
- `tests/CMakeLists.txt` - Added RDP test targets with WITH_RDP conditional

**test_rdp_input_handler.cpp tests (35 total):**
- Scancode translation: Letter keys (QWERTY), number keys, function keys (F1-F12)
- Scancode translation: Special keys (Escape, Tab, Backspace, Enter, Space)
- Scancode translation: Modifier keys (Shift, Ctrl, Alt)
- Scancode translation: Arrow keys, navigation keys
- Scancode translation: Numpad keys with extended flag handling
- Character translation with Shift and CapsLock modifiers
- Mouse event translation (move, left/right click, press/release)
- Wheel delta extraction with positive/negative values

**test_rdp_server.cpp tests (20 total):**
- RDPServerInitTest: Constructor, initialize with default/custom port, resolution setting
- RDPServerLifecycleTest: Start/stop, stop without start, multiple start/stop cycles, client count
- RDPServerCallbackTest: Keyboard/mouse callback registration and invocation
- RDPServerFrameTest: Frame update before/after start, multiple updates, null/zero handling
- RDPServerCertificateTest: Certificate path setting
- RDPServerThreadTest: Rapid frame updates

**Test results:** All 55 RDP tests pass via ctest

**Integration tests (manual - to be performed during final verification):**
1. Connect with Windows mstsc.exe
2. Connect with xfreerdp
3. Verify display output
4. Test keyboard input (movement, hotkeys)
5. Test mouse input (targeting, camera)
6. Verify disconnection handling

---

### Phase 9: Documentation Updates ✅ COMPLETED

**Files updated:**

1. **CLAUDE.md**:
   - Added `freerdp3-dev, libwinpr3-dev` to Dependencies section
   - Added RDP running instructions with command line examples
   - Added Display Options Comparison table (X11 vs VNC vs RDP)
   - Added RDP configuration JSON example

2. **docs/rdp_implementation_guide.md**:
   - Marked as IMPLEMENTED at top
   - Updated overview to describe RDP as additional option (not replacement)
   - Updated file structure to match actual implementation
   - Updated Ubuntu installation for FreeRDP 3.x packages
   - Updated configuration section with actual CLI options
   - Added SSL certificate auto-generation note (FreeRDP 3 feature)
   - Updated usage section with practical examples
   - Added "Running RDP and VNC Simultaneously" section
   - Updated testing section with actual test counts and coverage
   - Expanded troubleshooting with common issues and solutions

**Documentation covered:**
1. ✅ Build instructions with FreeRDP
2. ✅ Certificate generation (and auto-generation note)
3. ✅ Connecting with various clients (mstsc, xfreerdp, Remmina)
4. ✅ Comparison: When to use RDP vs VNC vs direct X11
5. ✅ Troubleshooting guide
6. ✅ Performance tuning options
7. ✅ Running RDP and VNC simultaneously

---

## Implementation Order

1. **Phase 1**: CMake setup (foundation for all other work)
2. **Phase 2**: RDP server core (can be developed in isolation)
3. **Phase 3**: Frame capture (requires Phase 2)
4. **Phase 4**: Input translation (requires Phase 2)
5. **Phase 5**: IrrlichtRenderer integration (requires Phases 2-4)
6. **Phase 6**: Configuration (requires Phase 5)
7. **Phase 7**: SSL certificates (can be done in parallel with Phase 6)
8. **Phase 8**: Testing (requires Phase 5-7)
9. **Phase 9**: Documentation (final step)

---

## Key Files Summary

**New files (8):**
- `include/client/graphics/rdp/rdp_server.h`
- `include/client/graphics/rdp/rdp_peer_context.h`
- `include/client/graphics/rdp/rdp_input_handler.h`
- `src/client/graphics/rdp/rdp_server.cpp`
- `src/client/graphics/rdp/rdp_peer_context.cpp`
- `src/client/graphics/rdp/rdp_input_handler.cpp`
- `tests/test_rdp_server.cpp`
- `scripts/generate_rdp_cert.sh`

**Modified files (4):**
- `CMakeLists.txt`
- `include/client/graphics/irrlicht_renderer.h`
- `src/client/graphics/irrlicht_renderer.cpp`
- `src/client/main.cpp`

---

## Risk Considerations

1. **FreeRDP API stability**: FreeRDP3 API differs from FreeRDP2; ensure correct version
2. **Thread safety**: Frame buffer access requires careful mutex handling
3. **Performance**: Software rendering + RDP encoding may be CPU-intensive
4. **Input latency**: Network round-trip adds delay to input response
5. **Codec compatibility**: Some RDP clients may not support RFX/NSC

---

## Success Criteria

1. WillEQ builds successfully with `WITH_RDP=ON`
2. WillEQ builds successfully with `WITH_RDP=OFF` (no regressions)
3. Existing Xvfb + x11vnc workflow continues to work unchanged
4. RDP server starts on configured port when `--rdp` flag is used
5. Windows mstsc.exe can connect and see game display
6. Keyboard/mouse input works for gameplay via RDP
7. Multiple RDP clients can connect simultaneously
8. RDP and VNC can run concurrently if desired
9. Graceful handling of client disconnect/reconnect
10. No memory leaks or crashes during extended sessions

---

## Future Enhancements (Out of Scope)

1. Audio streaming via RDPSND channel
2. Clipboard sharing
3. Multi-monitor support
4. GPU-accelerated encoding
5. Dirty rectangle optimization
6. Session recording/replay
