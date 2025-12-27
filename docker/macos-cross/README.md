# macOS Cross-Compilation Build

This directory contains Docker-based tooling for cross-compiling WillEQ to macOS x86_64 from Linux.

## Prerequisites

- Docker installed and running
- macOS 10.15 SDK (see below)
- ~10GB disk space for the Docker image

## Obtaining the macOS SDK

Apple does not distribute the SDK separately. You need access to Xcode:

### Option 1: Extract from Xcode (Recommended)

1. Download Xcode 11.x from [Apple Developer](https://developer.apple.com/download/all/) (requires Apple ID)
2. Extract the SDK:
   ```bash
   # On macOS with Xcode installed:
   cd /opt/osxcross
   ./tools/gen_sdk_package.sh
   # Creates MacOSX10.15.sdk.tar.xz

   # Or from Xcode.xip on Linux:
   ./tools/gen_sdk_package_pbzx.sh /path/to/Xcode_11.7.xip
   ```

### Option 2: Use an existing SDK

If you have a macOS machine, you can package the SDK:
```bash
# On macOS:
tar -cJf MacOSX10.15.sdk.tar.xz \
    -C /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs \
    MacOSX10.15.sdk
```

### Option 3: Third-party SDK packages

Some projects provide pre-packaged SDKs. Search for "MacOSX10.15.sdk.tar.xz" but verify authenticity.

## Building

1. Place `MacOSX10.15.sdk.tar.xz` in this directory

2. Build the Docker image:
   ```bash
   cd docker/macos-cross
   docker build -t willeq-macos-builder .
   ```
   This takes 15-30 minutes the first time (downloads and compiles dependencies).

3. Run the build:
   ```bash
   # From project root
   docker run --rm -v $(pwd):/src willeq-macos-builder
   ```

4. Output will be in:
   - `build-macos/bin/willeq` - The binary
   - `dist-macos/WillEQ.app` - macOS app bundle

## Build Options

### Custom build directory
```bash
docker run --rm \
    -v $(pwd):/src \
    -e BUILD_DIR=/src/my-build \
    willeq-macos-builder
```

### Interactive shell (for debugging)
```bash
docker run --rm -it -v $(pwd):/src willeq-macos-builder /bin/bash
```

### Headless build (no graphics)
```bash
docker run --rm -v $(pwd):/src willeq-macos-builder \
    cmake /src -DEQT_GRAPHICS=OFF && make
```

## Troubleshooting

### "MacOSX10.15.sdk.tar.xz not found"
Place the SDK tarball in this directory before building the Docker image.

### Build fails on Irrlicht
Irrlicht cross-compilation can be tricky. Try building without graphics:
```bash
docker run --rm -v $(pwd):/src eqt-macos-builder \
    bash -c "cmake /src -DEQT_GRAPHICS=OFF && make"
```

### Binary crashes on macOS
- Ensure you're running on macOS 10.15 or later
- Check that all dylibs are included (use `otool -L` to inspect)
- For static builds, this shouldn't be an issue

## Architecture

This setup uses:
- **osxcross**: Cross-compilation toolchain for macOS
- **macports**: Dependency management within osxcross
- **clang**: Apple's preferred compiler

The toolchain targets:
- macOS 10.15 (Catalina) minimum
- x86_64 architecture (Intel Macs)
- Darwin 19 ABI

## Notes

- ARM64 (Apple Silicon) builds would require a different SDK (11.0+) and toolchain configuration
- Code signing is not performed; users will need to bypass Gatekeeper
- For distribution, consider notarization with an Apple Developer account
