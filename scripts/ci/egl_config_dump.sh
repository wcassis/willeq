#!/usr/bin/env bash
# B1034 (#357): dump the surfaceless-EGL config list the x86 device fork
# consumes (initEGLSurfaceless in docker/irrlicht-drm-x86/CIrrDeviceFB.cpp
# queries PBUFFER + RGBA8888 + the B971 depth, ES2; the KMS/GBM path's
# WINDOW-bit query has no configs on a surfaceless display -- a headless
# x86 box only ever renders through the surfaceless path, and that is the
# "x86 llvmpipe pick" the acceptance row pins as unchanged), as a JSON
# array of {visual, renderableType, depth, stencil} -- the shape
# tests/test_egl_config_select.cpp feeds to the selection decision.
#
# The dump runs ON THE BUILD HOST (the ThinkPad through rbuild, or the
# hygiene tree through the ctest below) with the CI GL pin
# (__EGL_VENDOR_LIBRARY_FILENAMES -> 50_mesa.json + LIBGL_ALWAYS_SOFTWARE,
# exactly rbuild/CI's environment), so the fixture at
# tests/data/b1034/surfaceless_es2.json is llvmpipe's, not the caller's
# vendor's (hermes-ext's surfaceless EGL is NVIDIA's, the B1054 lesson).
#
# Modes:
#   scripts/ci/egl_config_dump.sh dump <out-dir>
#       write <out-dir>/surfaceless_es2.json (prints it)
#   scripts/ci/egl_config_dump.sh verify <fixture-dir>
#       re-dump into a temp dir and diff against <fixture-dir>/surfaceless_es2.json
#       (exit 0 = identical, 1 = drift -- the fixture is stale), so the
#       hygiene suite keeps the fixture honest on every run.
#   (no mode) a no-op that exits 235 (the suite's SKIP_RETURN_CODE), for a
#       manual `bash egl_config_dump.sh <dir>` with WILLEQ_EGL_CONFIG_DUMP
#       unset: the registered test always passes a mode.
set -euo pipefail
# No braces inside the ${1:?...} message: bash's ${...} brace-matching stops
# at the FIRST unbalanced '}', so a braced usage string in the error word
# (as in the pre-fix "usage: $0 {dump <out-dir> | verify <fixture-dir>}")
# silently appended a stray '}' to $1 -- the fixture path arrived as
# 'tests/data/b1034}' and every verify/dump wrote to the wrong file. A
# precomputed string sidesteps the expansion entirely.
USAGE="$0: dump <out-dir> | verify <fixture-dir>"
MODE="${1:-}"
case "$MODE" in
    "")
        [ -n "${WILLEQ_EGL_CONFIG_DUMP:-}" ] || exit 235
        MODE=dump; shift
        ;;
    dump) shift ;;
    verify) shift ;;
    *) echo "usage: $USAGE" >&2; exit 2 ;;
esac
OUT="${1:?usage: $USAGE}"
# The CI GL pin (rbuild's rb_run env; docs/ci.md "No GPU for jobs"): a
# developer's host may have a vendor whose surfaceless EGL differs.
export __EGL_VENDOR_LIBRARY_FILENAMES="${__EGL_VENDOR_LIBRARY_FILENAMES:-/usr/share/glvnd/egl_vendor.d/50_mesa.json}"
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/dump.cpp" <<'CPP'
#include <EGL/egl.h>
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main() {
    // The x86 fork's own query (initEGLSurfaceless): PBUFFER + RGBA8888 +
    // the B971 16-bit depth, no stencil, ES2.
    typedef EGLDisplay (*GetPlatformDisplayEXT)(EGLenum, void*, const EGLint*);
    GetPlatformDisplayEXT getPlatformDisplay =
        (GetPlatformDisplayEXT)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = nullptr;
    if (getPlatformDisplay)
        dpy = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (dpy == EGL_NO_DISPLAY)
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) { fprintf(stderr, "eglGetDisplay failed\n"); return 1; }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) { fprintf(stderr, "eglInitialize failed\n"); return 1; }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) { fprintf(stderr, "bind ES failed\n"); return 1; }
    const EGLint request[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_STENCIL_SIZE, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLint num = 0;
    if (!eglChooseConfig(dpy, request, nullptr, 0, &num) || num < 1) {
        fprintf(stderr, "eglChooseConfig count query failed (num=%d)\n", num);
        return 1;
    }
    fprintf(stderr, "vendor=%s version=%d.%d count=%d\n",
            eglQueryString(dpy, EGL_VENDOR), major, minor, num);
    EGLConfig* all = (EGLConfig*)malloc(sizeof(EGLConfig) * (size_t)num);
    if (!eglChooseConfig(dpy, request, all, num, &num)) {
        fprintf(stderr, "eglChooseConfig full query failed\n");
        return 1;
    }
    printf("[\n");
    for (EGLint i = 0; i < num; ++i) {
        EGLint visual = 0, renderable = 0, depth = 0, stencil = 0;
        eglGetConfigAttrib(dpy, all[i], EGL_NATIVE_VISUAL_ID, &visual);
        eglGetConfigAttrib(dpy, all[i], EGL_RENDERABLE_TYPE, &renderable);
        eglGetConfigAttrib(dpy, all[i], EGL_DEPTH_SIZE, &depth);
        eglGetConfigAttrib(dpy, all[i], EGL_STENCIL_SIZE, &stencil);
        printf("  {\"visual\": %u, \"renderableType\": %d, \"depth\": %d, \"stencil\": %d}%s\n",
               (unsigned)visual, (int)renderable, (int)depth, (int)stencil, i + 1 < num ? "," : "");
    }
    printf("]\n");
    free(all);
    return 0;
}
CPP
g++ -O2 -o $TMP/dump "$TMP/dump.cpp" -lEGL -lGLESv2
case "$MODE" in
    dump)
        mkdir -p "$OUT"
        "$TMP/dump" > "$OUT/surfaceless_es2.json"
        cat "$OUT/surfaceless_es2.json"
        ;;
    verify)
        "$TMP/dump" > "$TMP/surfaceless_es2.json"
        if diff -u "$OUT/surfaceless_es2.json" "$TMP/surfaceless_es2.json" > "$TMP/diff.txt"; then
            echo "OK: $OUT/surfaceless_es2.json matches the live dump"
        else
            echo "DRIFT: $OUT/surfaceless_es2.json != live dump (re-run 'dump' and commit the new fixture):" >&2
            cat "$TMP/diff.txt" >&2
            exit 1
        fi
        ;;
esac
