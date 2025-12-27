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
    # Required libraries
    libuv1-dev \
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
    # Graphics dependencies
    libirrlicht-dev \
    libxxf86vm-dev \
    libx11-dev \
    libgl1-mesa-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy source code
COPY . /src

# Build the project
RUN mkdir -p /build && cd /build \
    && cmake /src \
        -DCMAKE_BUILD_TYPE=Release \
        -DEQT_GRAPHICS=ON \
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
    libuv1t64 \
    libssl3t64 \
    zlib1g \
    libfmt9 \
    libjsoncpp25 \
    libboost-graph1.83.0 \
    # Navmesh pathfinding runtime
    librecast1 \
    # Graphics runtime
    libirrlicht1.8 \
    libxxf86vm1 \
    libx11-6 \
    libgl1 \
    libglx-mesa0 \
    # For software rendering fallback
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
