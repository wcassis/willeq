#!/usr/bin/env bash
# ODROID XU4 / Exynos 5422: switch the G3D (Mali-T628) clock mux from the 24 MHz
# crystal to the PLL path. Found 2026-10-13 (willeq play-test box): the Exynos5420
# sub-CMU code (drivers/clk/samsung/clk-exynos5-subcmu.c, exynos5x_g3d_subcmu
# { SRC_TOP5, 0, BIT(16) }) writes mout_user_aclk_g3d = fin_pll as its
# power-down value at boot and never restores it, because the sub-CMU platform
# device stays runtime-suspended while Panfrost runs the GPU. The common clock
# framework still believes the mux selects mout_sw_aclk_g3d, so devfreq/clk
# tools report 177-600 MHz while the GPU cycle counter runs at 24.0 MHz.
# Measured: glmark2 'build' 15 -> 279 fps, willeq qeynos2 1280x720 10 -> 89 fps.
# Register: CMU_TOP 0x10010000 + SRC_TOP5 0x10214, bit 16 (0=fin_pll, 1=sw mux).
set -e
for i in $(seq 1 60); do [ -e /dev/dri/renderD129 ] && break; sleep 0.5; done
python3 -I - <<'PY'
import mmap, os, struct
fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
m = mmap.mmap(fd, 0x20000, offset=0x10010000)
off = 0x10214
v = struct.unpack("<I", m[off:off+4])[0]
if (v >> 16) & 1:
    print("xu4-g3d-clock: SRC_TOP5[16] already 1 (PLL path)")
else:
    m[off:off+4] = struct.pack("<I", v | (1 << 16))
    v2 = struct.unpack("<I", m[off:off+4])[0]
    print(f"xu4-g3d-clock: SRC_TOP5 0x{v:08x} -> 0x{v2:08x} (G3D now on PLL path)")
PY
