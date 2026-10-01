#!/bin/bash
# B970 (#288): the vendored-archive store for the ARM image build.
#
# docker/Dockerfile.arm-noble used to wget irrlicht-1.8.5.zip and libpng-1.6.37.tar.xz from
# downloads.sourceforge.net inside a Docker layer; a SourceForge outage (HTTP 522, 2026-10-01)
# failed every build-arm job whose docker/irrlicht-drm/* change invalidated that layer.
# Now the archives live in a store on NFS (outside git), pinned by SHA256 in
# docker/vendor.sha256, fetched ONCE, and handed to `docker build` as a named build context.
#
# Usage:
#   scripts/ci/vendor_fetch.sh --ensure    verify the store against the pins; download what is
#                                          missing or corrupt; print the store dir on stdout
#   scripts/ci/vendor_fetch.sh --check     verify only; exit 1 if anything is missing/wrong
#
# Env: VENDOR_STORE (default /mnt/projects/fable/ci-arm/vendor)
# Refreshing a pin: put the new file in the store, update docker/vendor.sha256, commit the pin.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PIN_FILE="$PROJECT_DIR/docker/vendor.sha256"
STORE="${VENDOR_STORE:-/mnt/projects/fable/ci-arm/vendor}"

# Ordered sources per archive; the first that yields the pinned bytes wins.
sources_for() {
    case "$1" in
        irrlicht-1.8.5.zip)
            echo "https://downloads.sourceforge.net/irrlicht/irrlicht-1.8.5.zip"
            echo "https://sources.buildroot.net/irrlicht/irrlicht-1.8.5.zip" ;;
        libpng-1.6.37.tar.xz)
            echo "https://downloads.sourceforge.net/libpng/libpng-1.6.37.tar.xz"
            echo "https://sources.buildroot.net/libpng/libpng-1.6.37.tar.xz"
            echo "https://ftp.osuosl.org/pub/blfs/conglomeration/libpng/libpng-1.6.37.tar.xz" ;;
    esac
}

say() { printf '[vendor_fetch] %s\n' "$*" >&2; }

MODE=""
case "${1:-}" in
    --ensure) MODE=ensure ;;
    --check) MODE=check ;;
    *) echo "usage: $0 --ensure|--check" >&2; exit 64 ;;
esac

pins=$(grep -v '^[[:space:]]*#' "$PIN_FILE" | grep -v '^[[:space:]]*$') || { say "FAIL: no pins in $PIN_FILE"; exit 1; }
mkdir -p "$STORE"
bad=0
while read -r sha name; do
    f="$STORE/$name"
    if [ -f "$f" ] && [ "$(sha256sum "$f" | cut -d' ' -f1)" = "$sha" ]; then
        say "ok      $name (store)"
        continue
    fi
    if [ "$MODE" = check ]; then
        say "FAIL: $name missing or wrong sha256 in $STORE (pin $sha)"
        bad=1; continue
    fi
    [ -f "$f" ] && say "store copy of $name does not match its pin; refetching"
    got=""
    while read -r url; do
        tmp="$STORE/.$name.tmp.$$"
        say "fetch   $name <- $url"
        if curl -fsSL --retry 2 --connect-timeout 20 --max-time 600 -o "$tmp" "$url" \
           && [ "$(sha256sum "$tmp" | cut -d' ' -f1)" = "$sha" ]; then
            mv -f "$tmp" "$f"; got=1; break
        fi
        say "        failed or wrong sha256 from $url"
        rm -f "$tmp"
    done < <(sources_for "$name")
    if [ -z "$got" ]; then
        say "FAIL: pin '$name' ($sha) is not in the store $STORE and no source delivered it."
        say "      Put the file in the store by hand, or fix the sources in $0."
        bad=1
    fi
done <<< "$pins"
[ "$bad" = 0 ] || exit 1
echo "$STORE"
