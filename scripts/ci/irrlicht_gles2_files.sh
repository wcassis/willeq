#!/bin/bash
# Shared source-copy manifest for the x86 and Darwin patched Irrlicht builds.
DRM_GLES2_FILES=(
    COpenGLES2Driver.h COpenGLES2Driver.cpp
    COGLES2Texture.h COGLES2Texture.cpp
    COGLES2Shaders.h COGLES2Shaders.cpp
    COGLES2MaterialRenderer.h COGLES2MaterialRenderer.cpp
    gl_call_counters.h gpu_sync_probe.h t122_texprobe.h
)
FB_GLES2_FILES=(
    CIrrDeviceFB.h CIrrDeviceFB.cpp CIrrDeviceFB_X11.cpp
    CIrrDeviceFB_Darwin.cpp x11_keysym_map.h
)
copy_irrlicht_gles2_files() {
    local f
    for f in "${DRM_GLES2_FILES[@]}"; do
        cp "$DRM_SRC/$f" "$DEST_SRC/$f"
    done
    for f in "${FB_GLES2_FILES[@]}"; do
        cp "$DRM_X86_SRC/$f" "$DEST_SRC/$f"
    done
}
