#!/bin/bash
# Builds every TLEET plugin on macOS (VST3 + AU, universal arm64 + x86_64),
# runs the tests and signs the bundles ad hoc, so macOS loads them on this
# Mac without a developer certificate.
# Needs: Xcode or the Command Line Tools (xcode-select --install),
# CMake 3.22+ (brew install cmake, or CMake.app from cmake.org) and Git.
# Run from anywhere in a Terminal:
#   bash scripts/build-macos.sh
# Options:
#   --native   build for this Mac's processor only (about twice as fast)
#   --install  copy the plugins into ~/Library/Audio/Plug-Ins and run auval
#              on every Audio Unit
# The finished plugins are collected in dist-mac/VST3 and dist-mac/Components.

set -euo pipefail
cd "$(dirname "$0")/.."

install=0
archs="arm64;x86_64"
for arg in "$@"; do
    case "$arg" in
        --install) install=1 ;;
        --native) archs="$(uname -m)" ;;
        *) echo "Unknown option: $arg" >&2; exit 2 ;;
    esac
done

red() { printf '\033[31m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }

if ! xcode-select -p >/dev/null 2>&1; then
    red "The Command Line Tools are missing. Run: xcode-select --install"
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    if [ -x /Applications/CMake.app/Contents/bin/cmake ]; then
        export PATH="/Applications/CMake.app/Contents/bin:$PATH"
    else
        red "CMake is missing. Install it with: brew install cmake (or CMake.app from cmake.org)"
        exit 1
    fi
fi

generator="Unix Makefiles"
command -v ninja >/dev/null 2>&1 && generator="Ninja"
jobs="$(sysctl -n hw.ncpu)"

# JUCE: a local copy from TLEET_JUCE_DIR, or the 9.0.4 release archive
# (a git clone of JUCE can hang on slow connections).
juce_dir="${TLEET_JUCE_DIR:-$PWD/.deps/JUCE-9.0.4}"
if [ ! -f "$juce_dir/CMakeLists.txt" ]; then
    mkdir -p .deps
    curl -fL --retry 3 -o .deps/juce.tar.gz https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.4.tar.gz
    tar -xzf .deps/juce.tar.gz -C .deps
    rm .deps/juce.tar.gz
    juce_dir="$PWD/.deps/JUCE-9.0.4"
fi

cmake -B build-mac -G "$generator" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$archs" \
    -DTLEET_JUCE_DIR="$juce_dir"
cmake --build build-mac --parallel "$jobs"

(cd build-mac/tests && ./tleet_tests_artefacts/Release/tleet_tests) || {
    red "Tests failed, see build-mac/tests/test-output"
    exit 1
}

rm -rf dist-mac
mkdir -p dist-mac/VST3 dist-mac/Components
for bundle in build-mac/plugins/*/*_artefacts/Release/VST3/*.vst3; do
    ditto "$bundle" "dist-mac/VST3/$(basename "$bundle")"
done
for bundle in build-mac/plugins/*/*_artefacts/Release/AU/*.component; do
    ditto "$bundle" "dist-mac/Components/$(basename "$bundle")"
done
for bundle in dist-mac/VST3/*.vst3 dist-mac/Components/*.component; do
    codesign --force --deep --sign - "$bundle"
    codesign --verify --deep --strict "$bundle"
    green "Built: $bundle ($(lipo -archs "$bundle/Contents/MacOS/"* | tr '\n' ' '))"
done

if [ "$install" = 1 ]; then
    vst3_dir="$HOME/Library/Audio/Plug-Ins/VST3"
    au_dir="$HOME/Library/Audio/Plug-Ins/Components"
    mkdir -p "$vst3_dir" "$au_dir"
    for bundle in dist-mac/VST3/*.vst3; do
        rm -rf "$vst3_dir/$(basename "$bundle")"
        ditto "$bundle" "$vst3_dir/$(basename "$bundle")"
        xattr -dr com.apple.quarantine "$vst3_dir/$(basename "$bundle")" 2>/dev/null || true
    done
    for bundle in dist-mac/Components/*.component; do
        rm -rf "$au_dir/$(basename "$bundle")"
        ditto "$bundle" "$au_dir/$(basename "$bundle")"
        xattr -dr com.apple.quarantine "$au_dir/$(basename "$bundle")" 2>/dev/null || true
    done
    green "Installed into $vst3_dir and $au_dir"

    # Make macOS rescan the Audio Units, then validate each one.
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    failed=0
    for cmake_file in plugins/*/CMakeLists.txt; do
        code="$(sed -n 's/^ *CODE \([A-Za-z0-9]\{4\}\).*/\1/p' "$cmake_file" | head -1)"
        [ -n "$code" ] || continue
        if auval -v aufx "$code" Tlet > "build-mac/auval-$code.log" 2>&1; then
            green "auval passed: $code"
        else
            red "auval FAILED: $code (see build-mac/auval-$code.log)"
            failed=1
        fi
    done
    [ "$failed" = 0 ] || exit 1
fi
