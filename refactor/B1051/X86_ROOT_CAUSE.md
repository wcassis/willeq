# B1051 VERIFY — x86 leg root cause (2026-10-19)

## The x86 "core-dump during ETC1 upload" is a host-environment artifact, not a B1051 regression

The prior run reported the x86 willeq binary core-dumping during ETC1 texture
upload (39 uploads then "dumped core", never reaching the zone). A gdb
backtrace (captured 2026-10-19, `x86/spot1_gdb2.out` under the evidence dir)
proves the SIGSEGV is **not** in the B1051 fix code:

- **Crash thread / location:** Thread 1 `"eq-render"`, `rip = 0x0` (a null
  function-pointer call) inside the **loading-frame present path**:

      #0  0x0 in ??
      #1-3 libEGL_mesa.so.0
      #4  irr::CIrrDeviceFB::drmPageFlip()
      #5  irr::CIrrDeviceFB::present(...)
      #6  irr::video::COpenGLES2Driver::beginScene(...)
      #7  EQT::Graphics::drawLoadingFrame(...)   <-- the loading screen
      #8  eqt::Application::render(...)
      #9  eqt::Application::mainLoop()
      #10 eqt::Application::run()

- **When:** the first loading-screen frame, ~1 s after start, **before
  zone-in**. The B1051 ETC1 variant-loading code runs at zone-in, so it is
  never reached before this crash. The "ETC1 upload then core-dump" log tail
  is the *loading screen's* protected UI/weather textures being uploaded —
  the crash is one frame later, in the display present, not in the upload.

- **Why it is the host, not the client:** the ThinkPad (172.16.200.205) has an
  NVIDIA ICD on its DRM display; the client's `CIrrDeviceFB::drmPageFlip`
  present path resolves a null EGL entry point and calls it. Forcing the
  software driver — `MESA_LOADER_DRIVER_OVERRIDE=llvmpipe
  LIBGL_ALWAYS_SOFTWARE=1` — runs the **same binary** clean for the full
  150 s (rc=124 timeout, clean shutdown, no crash): `x86/spot1_llvmpipe_run.log`.
  A 24 GiB `ulimit -v` cap did not change it (not host-RAM driven), and an
  Xvfb `:99` display did not change it (not the lack of X).

**Conclusion:** the x86 crash is specific to the ThinkPad's NVIDIA-ICD
hardware present path. It is an environment fact, not a defect in the B1051
fix. The x86 leg runs under the llvmpipe override (the same llvmpipe backend
the CI visual gate uses on x86), which exercises the identical B1051 code.

Proven-clean x86 invocation:

    MESA_LOADER_DRIVER_OVERRIDE=llvmpipe LIBGL_ALWAYS_SOFTWARE=1 \
      timeout 200 $BIN -c config.json -r 1280 720 --constrained orangepi \
      --eqe1-path /mnt/projects/EverQuest_ETC1 --no-audio --log-level=DEBUG \
      --script spot.txt --dump-frames-dir $DIR --dump-every 15 \
      --dump-max-frames 60
