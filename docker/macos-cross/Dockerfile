# Docker image for cross-compiling WillEQ to macOS
# Usage:
#   1. Obtain MacOSX10.15.sdk.tar.xz (see README for instructions)
#   2. Place it in this directory
#   3. docker build -t willeq-macos-builder .
#   4. docker run -v $(pwd):/src willeq-macos-builder

FROM ubuntu:22.04

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    clang \
    cmake \
    git \
    libbz2-dev \
    libffi-dev \
    liblzma-dev \
    libncurses5-dev \
    libreadline-dev \
    libsqlite3-dev \
    libssl-dev \
    libxml2-dev \
    libxslt1-dev \
    llvm \
    make \
    patch \
    pkg-config \
    python3 \
    python3-pip \
    wget \
    xz-utils \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# Install osxcross
WORKDIR /opt
RUN git clone https://github.com/tpoechtrager/osxcross.git

# Copy the macOS SDK (must be provided by user)
# To create: download Xcode, extract, run:
#   ./osxcross/tools/gen_sdk_package_pbzx.sh /path/to/Xcode.app
COPY MacOSX10.15.sdk.tar.xz /opt/osxcross/tarballs/

# Build osxcross
WORKDIR /opt/osxcross
ENV UNATTENDED=1
RUN ./build.sh

# Set up environment
ENV OSXCROSS_ROOT=/opt/osxcross
ENV PATH="/opt/osxcross/target/bin:${PATH}"
ENV MACOSX_DEPLOYMENT_TARGET=10.15

# Install macports for dependencies
RUN ./tools/osxcross-macports install \
    libuv \
    openssl3 \
    zlib \
    libfmt \
    glm \
    jsoncpp \
    boost

# Build Irrlicht for macOS (not available via macports)
WORKDIR /tmp
RUN git clone --depth 1 --branch irrlicht-1.8.5 https://github.com/minetest/irrlicht.git irrlicht-src

# Patch Irrlicht for macOS cross-compilation
WORKDIR /tmp/irrlicht-src
RUN mkdir -p build && cd build && \
    cmake .. \
        -DCMAKE_TOOLCHAIN_FILE=/opt/osxcross/toolchain.cmake \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
        -DCMAKE_INSTALL_PREFIX=/opt/osxcross/target/macports/pkgs/opt/local \
        -DBUILD_SHARED_LIBS=OFF \
        -DIRRLICHT_BUILD_EXAMPLES=OFF \
        -DIRRLICHT_BUILD_TOOLS=OFF && \
    cmake --build . --parallel $(nproc) && \
    cmake --install .

# Create workspace
WORKDIR /src

# Copy build script
COPY build-macos.sh /usr/local/bin/
RUN chmod +x /usr/local/bin/build-macos.sh

# Default command
CMD ["/usr/local/bin/build-macos.sh"]
