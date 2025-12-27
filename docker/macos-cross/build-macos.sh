#!/bin/bash
# Build script for cross-compiling WillEQ to macOS x86_64
set -e

SOURCE_DIR="${SOURCE_DIR:-/src}"
BUILD_DIR="${BUILD_DIR:-/src/build-macos}"
INSTALL_DIR="${INSTALL_DIR:-/src/dist-macos}"

echo "=== WillEQ macOS Cross-Compile Build ==="
echo "Source:  ${SOURCE_DIR}"
echo "Build:   ${BUILD_DIR}"
echo "Install: ${INSTALL_DIR}"
echo ""

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure with CMake
echo "=== Configuring ==="
cmake "${SOURCE_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${SOURCE_DIR}/cmake/toolchains/macos-x86_64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -DEQT_BUILD_TESTS=OFF \
    -DEQT_GRAPHICS=ON

# Build
echo ""
echo "=== Building ==="
cmake --build . --parallel $(nproc)

# Install
echo ""
echo "=== Installing ==="
cmake --install .

# Create distributable bundle
echo ""
echo "=== Creating Distribution ==="
mkdir -p "${INSTALL_DIR}/WillEQ.app/Contents/MacOS"
mkdir -p "${INSTALL_DIR}/WillEQ.app/Contents/Resources"

# Copy binary
cp "${BUILD_DIR}/bin/willeq" "${INSTALL_DIR}/WillEQ.app/Contents/MacOS/"

# Create Info.plist
cat > "${INSTALL_DIR}/WillEQ.app/Contents/Info.plist" << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>willeq</string>
    <key>CFBundleIdentifier</key>
    <string>com.willeq</string>
    <key>CFBundleName</key>
    <string>WillEQ</string>
    <key>CFBundleVersion</key>
    <string>1.0.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>LSMinimumSystemVersion</key>
    <string>10.15</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key>
    <true/>
</dict>
</plist>
EOF

echo ""
echo "=== Build Complete ==="
echo "Binary: ${BUILD_DIR}/bin/willeq"
echo "App Bundle: ${INSTALL_DIR}/WillEQ.app"
echo ""
echo "To run on macOS:"
echo "  1. Copy WillEQ.app to your Mac"
echo "  2. Right-click -> Open (first time, to bypass Gatekeeper)"
echo "  3. Or run from terminal: ./WillEQ.app/Contents/MacOS/willeq -c config.json"
