#!/usr/bin/env bash
#
# Build the universal macOS binary of the Swift ecuconnect-tool and package it
# for a GitHub release.
#
# The Homebrew formula cannot build this from source: Package.swift depends on
# Swift-Automotive, which lives in a private repository. The formula therefore
# downloads the artifact this script produces.
#
# Usage: scripts/build-release-binary.sh <version>

set -euo pipefail

VERSION="${1:-}"
if [[ -z "$VERSION" ]]; then
    echo "usage: $0 <version>" >&2
    exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT

cd "$ROOT"
swift build -c release --arch arm64 --arch x86_64 --product ecuconnect-tool
BIN_PATH="$(swift build -c release --arch arm64 --arch x86_64 --product ecuconnect-tool --show-bin-path)/ecuconnect-tool"

if ! lipo -archs "$BIN_PATH" | grep -q x86_64 || ! lipo -archs "$BIN_PATH" | grep -q arm64; then
    echo "error: $BIN_PATH is not universal ($(lipo -archs "$BIN_PATH"))" >&2
    exit 1
fi

# A prebuilt binary must not depend on anything outside the OS. Only the
# indented lines of `otool -L` are dependencies; the rest are architecture
# headers.
FOREIGN="$(otool -L "$BIN_PATH" | grep '^\s' | grep -vE '^[[:space:]]+(/usr/lib|/System)' || true)"
if [[ -n "$FOREIGN" ]]; then
    echo "error: $BIN_PATH links against non-system libraries:" >&2
    echo "$FOREIGN" >&2
    exit 1
fi

install -m 755 "$BIN_PATH" "$STAGING/ecuconnect-tool"
ARTIFACT="$ROOT/ecuconnect-tool-macos-universal-$VERSION.tar.gz"
tar -czf "$ARTIFACT" -C "$STAGING" ecuconnect-tool

echo "artifact: $ARTIFACT"
echo "sha256:   $(shasum -a 256 "$ARTIFACT" | cut -d' ' -f1)"
