#!/bin/bash
# B1051 step (a): x86 ETC1 capability probe (llvmpipe surfaceless).
# Replicates the client's exact EGL sequence (eglGetPlatformDisplayEXT +
# eglBindAPI(EGL_OPENGL_ES_API) before eglChooseConfig) and checks whether
# llvmpipe has the ETC1 extension and accepts ETC1/DXT1/RGBA8 uploads.
#
# Build (x86 host only; the client's x86 GLES2 tree uses the same libs):
#   gcc -O2 etalib.c -o etalib -lEGL -lGLESv2
# Run:
#   ./etalib
#
# Expected on the x86 dev VM (Mesa 25.2.8 llvmpipe): ETC1_OES=1 and every
# upload 0x0000 ACCEPTED — proving the x86 driver has working ETC1, so the
# multi-buffer character smear is a client fault (see ../STEP_A_x86_capability.md).
set -euo pipefail
cd "$(dirname "$0")"
OUT="${1:-etalib}"
gcc -O2 etalib.c -o "$OUT" -lEGL -lGLESv2
"$OUT"
