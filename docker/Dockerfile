# WillEQ Docker Build
# Multi-stage build for minimal runtime image with X11/VNC support
#
# Build modes:
#   docker build -t willeq .                    # Default build
#   docker build -t willeq --target runtime .   # Runtime only
#   docker build -t willeq --target vnc .       # Runtime with VNC
#
# Run modes:
#   X11 forwarding: docker run -e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix willeq
#   VNC mode:       docker run -p 5900:5900 -e VNC=1 willeq-vnc
#
# GLES2-only (P31, refactor/desktop_gl_elimination_plan.md): desktop-GL
# Irrlicht (system libirrlicht-dev / X11-GLX) was removed in P30 — this image
# now bootstraps the same patched GLES2 Irrlicht fork used by
# scripts/build-gles2-x86.sh and docker/Dockerfile.arm-noble (native x86_64
# build of docker/irrlicht-drm/ + docker/irrlicht-drm-x86/ sources) instead of
# apt-installing libirrlicht-dev. Runs the real GLES2 driver via Mesa EGL
# (surfaceless llvmpipe when no DRM display is present — the same fallback
# H01 relies on for the x86 dev VM).

# =============================================================================
# Stage 1: Builder
# =============================================================================
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    pkg-config \
    wget \
    python3 \
    # Required libraries
    libssl-dev \
    zlib1g-dev \
    libfmt-dev \
    libglm-dev \
    libjsoncpp-dev \
    libcereal-dev \
    # Optional: Boost for waypoint pathfinding
    libboost-graph-dev \
    # Optional: Recast/Detour for navmesh pathfinding
    librecast-dev \
    # Graphics dependencies (GLES2/EGL, no desktop GL / X11-GLX)
    libegl1-mesa-dev \
    libgles2-mesa-dev \
    libgbm-dev \
    libdrm-dev \
    libx11-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy source code
COPY . /src

# --- Patched GLES2 Irrlicht 1.8.5 (native x86_64 static lib) ---
# Same recipe as scripts/build-gles2-x86.sh / docker/Dockerfile.arm-noble's
# Irrlicht stage, adapted to fetch the pristine source via wget (no local
# /mnt/projects/irrlicht-1.8.5 reference checkout inside the image) and build
# natively with the host toolchain (no ARM cross-compiler, no libpng NEON
# object list).
RUN mkdir -p /tmp/irrlicht-build && cd /tmp/irrlicht-build \
    && wget -q https://downloads.sourceforge.net/irrlicht/irrlicht-1.8.5.zip \
    && python3 -c "import zipfile; zipfile.ZipFile('irrlicht-1.8.5.zip').extractall()" \
    && cp /src/docker/irrlicht-drm/COpenGLES2Driver.h        irrlicht-1.8.5/source/Irrlicht/ \
    && cp /src/docker/irrlicht-drm/COpenGLES2Driver.cpp      irrlicht-1.8.5/source/Irrlicht/ \
    && cp /src/docker/irrlicht-drm/COGLES2Texture.h          irrlicht-1.8.5/source/Irrlicht/ \
    && cp /src/docker/irrlicht-drm/COGLES2Texture.cpp        irrlicht-1.8.5/source/Irrlicht/ \
    && cp /src/docker/irrlicht-drm/COGLES2Shaders.h          irrlicht-1.8.5/source/Irrlicht/ \
    && cp /src/docker/irrlicht-drm/COGLES2Shaders.cpp        irrlicht-1.8.5/source/Irrlicht/ \
    && cp /src/docker/irrlicht-drm/COGLES2MaterialRenderer.h   irrlicht-1.8.5/source/Irrlicht/ \
    && cp /src/docker/irrlicht-drm/COGLES2MaterialRenderer.cpp irrlicht-1.8.5/source/Irrlicht/ \
    && cp /src/docker/irrlicht-drm/drm_card_select.h           irrlicht-1.8.5/source/Irrlicht/ \
    && cp /src/docker/irrlicht-drm-x86/CIrrDeviceFB.h        irrlicht-1.8.5/source/Irrlicht/ \
    && cp /src/docker/irrlicht-drm-x86/CIrrDeviceFB.cpp      irrlicht-1.8.5/source/Irrlicht/ \
    && cp /src/docker/irrlicht-drm-x86/CIrrDeviceFB_X11.cpp  irrlicht-1.8.5/source/Irrlicht/ \
    && python3 /src/docker/irrlicht-drm-patch.py irrlicht-1.8.5 \
    && sed -i 's|^#define _IRR_COMPILE_WITH_OPENGL_|//#define _IRR_COMPILE_WITH_OPENGL_|' \
        irrlicht-1.8.5/include/IrrCompileConfig.h \
    && cd irrlicht-1.8.5/source/Irrlicht \
    && sed -i 's|^IRRMESHOBJ = |IRRMESHOBJ = COpenGLES2Driver.o COGLES2Texture.o COGLES2Shaders.o COGLES2MaterialRenderer.o |' Makefile \
    && sed -i 's|^IRROTHEROBJ = |IRROTHEROBJ = CIrrDeviceFB_X11.o |' Makefile \
    && make NDEBUG=1 -j"$(nproc)" \
        CXX=g++ CC=gcc AR=ar \
        CXXINCS="-I../../include -Izlib -Ijpeglib -Ilibpng -I/usr/include/libdrm" \
        staticlib \
    && mkdir -p /opt/irrlicht-gles2/lib \
    && cp ../../lib/Linux/libIrrlicht.a /opt/irrlicht-gles2/lib/ \
    && cp -r ../../include /opt/irrlicht-gles2/include \
    && rm -rf /tmp/irrlicht-build

# Build the project
RUN mkdir -p /build && cd /build \
    && cmake /src \
        -DCMAKE_BUILD_TYPE=Release \
        -DEQT_GRAPHICS=ON \
        -DEQT_GLES2=ON \
        -DEQT_DRM=OFF \
        -DEQT_GLES2_IRRLICHT_DIR=/opt/irrlicht-gles2 \
        -DEQT_BUILD_TESTS=OFF \
    && cmake --build . --parallel $(nproc)

# =============================================================================
# Stage 2: Runtime (minimal, X11 forwarding)
# =============================================================================
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Install minimal runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Runtime libraries
    libssl3t64 \
    zlib1g \
    libfmt9 \
    libjsoncpp25 \
    libboost-graph1.83.0 \
    # Navmesh pathfinding runtime
    librecast1 \
    # Graphics runtime (GLES2/EGL via Mesa — surfaceless llvmpipe fallback
    # when no DRM display is present, same as the x86 dev VM's H01 backend;
    # libx11-6 for the optional --egl-x11 windowed backend, P25)
    libegl1 \
    libgles2 \
    libgbm1 \
    libdrm2 \
    libx11-6 \
    mesa-utils \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user for security
RUN useradd -m -s /bin/bash equser

# Copy built binaries
COPY --from=builder /build/bin/willeq /usr/local/bin/
COPY --from=builder /build/bin/model_viewer /usr/local/bin/
COPY --from=builder /build/bin/s3d_dump /usr/local/bin/

# Copy entrypoint script (ensure executable)
COPY --chmod=755 docker/entrypoint.sh /entrypoint.sh

# Create directories for config and EQ files
RUN mkdir -p /eq /config && chown -R equser:equser /eq /config

WORKDIR /eq

USER equser

# Volume mount points
VOLUME ["/eq", "/config"]

ENTRYPOINT ["/entrypoint.sh"]
CMD ["willeq", "-c", "/config/willeq.json"]

# =============================================================================
# Stage 3: Runtime with VNC support
# =============================================================================
FROM runtime AS vnc

USER root

# Install VNC and virtual display dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    xvfb \
    x11vnc \
    # Optional: window manager for better VNC experience
    openbox \
    # Useful tools
    procps \
    && rm -rf /var/lib/apt/lists/*

# VNC configuration
ENV DISPLAY=:99
ENV VNC_PORT=5900
ENV RESOLUTION=800x600x24
ENV VNC=1

# Expose VNC port
EXPOSE 5900

USER equser

CMD ["willeq", "-c", "/config/willeq.json"]
