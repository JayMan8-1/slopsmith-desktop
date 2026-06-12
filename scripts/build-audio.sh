#!/bin/bash

# Build the JUCE audio engine as a Node.js native addon
# Usage: ./scripts/build-audio.sh [debug|release]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

BUILD_TYPE="${1:-Release}"

cd "$PROJECT_DIR"

# Ensure JUCE submodule is available
if [ ! -f "JUCE/CMakeLists.txt" ]; then
    echo "Initializing JUCE submodule..."
    git submodule update --init --recursive
fi

# Ensure node_modules exist (for node-addon-api headers)
if [ ! -d "node_modules" ]; then
    echo "Installing npm dependencies..."
    npm install
fi

# Detect architecture
ARCH=$(uname -m)
case "$ARCH" in
    x86_64)
        CMAKE_ARCH="x64"
        ;;
    aarch64|arm64)
        CMAKE_ARCH="arm64"
        ;;
    *)
        CMAKE_ARCH="$ARCH"
        ;;
esac

# Linux: NeuralAmpModelerCore's A2 (slimmable) sources use
# std::atomic<std::shared_ptr<...>>, a C++20 library feature that libstdc++ only
# implements from GCC 12 on. ubuntu-22.04's default g++ is 11, where the primary
# std::atomic template fires a "trivially copyable" static_assert and the build
# fails. Prefer a g++ >= 12 when the default is older. No-op on macOS/Windows and
# on hosts whose default compiler is already new enough, and respects a CXX the
# caller already set.
if [ "$(uname -s)" = "Linux" ] && [ -z "${CXX:-}" ]; then
    default_major="$(g++ -dumpversion 2>/dev/null | cut -d. -f1)"
    if [ -n "$default_major" ] && [ "$default_major" -lt 12 ] 2>/dev/null; then
        for v in 14 13 12; do
            if command -v "g++-$v" >/dev/null 2>&1; then
                export CC="gcc-$v" CXX="g++-$v"
                echo "Default g++ is $default_major (<12, lacks std::atomic<shared_ptr>); using g++-$v for the NAM A2 sources"
                # A build/ configured earlier with the default g++ has that
                # compiler cached in CMakeCache.txt; cmake-js would reuse it and
                # ignore CC/CXX, so the A2 sources would still compile with the
                # old g++ and hit the static_assert. Drop a stale cache (one that
                # isn't already on the selected compiler) so cmake reconfigures.
                if [ -f build/CMakeCache.txt ] && ! grep -q "CMAKE_CXX_COMPILER:.*g++-$v" build/CMakeCache.txt; then
                    echo "Removing stale build/ (configured with a different compiler) so cmake reconfigures"
                    rm -rf build
                fi
                break
            fi
        done
        if [ -z "${CXX:-}" ]; then
            echo "Warning: default g++ is $default_major (<12) and no g++-12+ was found." >&2
            echo "         The NAM A2 sources need std::atomic<shared_ptr> (GCC 12+ libstdc++); the build will likely fail." >&2
        fi
    fi
fi

# Get the Electron version directly from the installed Electron package.
# Native addons MUST be built against the exact Electron ABI that ships
# with the app — guessing a fallback (the prior `|| echo 35.7.5`) can
# produce a .node that loads but crashes at runtime when the actual
# Electron version differs.
echo "Detecting Electron version..."
ELECTRON_PKG="node_modules/electron/package.json"
if [[ ! -f "$ELECTRON_PKG" ]]; then
    echo "Error: $ELECTRON_PKG not found. Run \`npm install\` before building native addons." >&2
    exit 1
fi
ELECTRON_VERSION=$(node -p "require('./$ELECTRON_PKG').version" 2>/dev/null | tr -d '\r\n')
if [[ -z "$ELECTRON_VERSION" ]]; then
    echo "Error: failed to read Electron version from $ELECTRON_PKG." >&2
    exit 1
fi
echo "  Electron version: $ELECTRON_VERSION"

# Set environment variables for cmake-js
# CROSS-PLATFORM NOTE: cmake-js looks for these CMAKE_JS_* variables internally
export CMAKE_JS_RUNTIME="electron"
export CMAKE_JS_RUNTIME_VERSION="$ELECTRON_VERSION"
export CMAKE_JS_ARCH="$CMAKE_ARCH"

# Also set npm_config variables for compatibility
# CROSS-PLATFORM NOTE: These are needed because cmake-js falls back to node-gyp
# which expects npm_config_* variables. Both sets are required for reliable
# cross-platform builds, especially on Windows where environment handling differs.
export npm_config_runtime="electron"
export npm_config_target="$ELECTRON_VERSION"
export npm_config_arch="$CMAKE_ARCH"
export npm_config_target_arch="$CMAKE_ARCH"

# Optional: clear cmake-js cache on Windows (where this matters most)
# CROSS-PLATFORM NOTE: Only clear cache in CI environments by default to avoid
# permission issues on local Windows machines and preserve incremental builds.
# On Windows, cmake-js downloads headers to a different location
# (C:\Users\...\.cmake-js) than on Unix systems.
# To force cache clearing locally, set CLEAN_CMAKE_JS=1
if [ "${CLEAN_CMAKE_JS:-}" = "1" ] || { [ -n "$CI" ] && [ -d "$HOME/.cmake-js" ]; }; then
  echo "Clearing cmake-js cache..."
  rm -rf "$HOME/.cmake-js"
fi

echo ""
echo "Building audio engine..."
echo "  Platform: $(uname -s)"
echo "  Arch: $CMAKE_ARCH"
echo "  Electron: $ELECTRON_VERSION"
echo "  Build type: $BUILD_TYPE"
echo ""

# Debug: show what cmake-js will see
echo "Environment for cmake-js:"
echo "  CMAKE_JS_RUNTIME=$CMAKE_JS_RUNTIME"
echo "  CMAKE_JS_RUNTIME_VERSION=$CMAKE_JS_RUNTIME_VERSION"
echo "  CMAKE_JS_ARCH=$CMAKE_JS_ARCH"
echo "  npm_config_runtime=$npm_config_runtime"
echo "  npm_config_target=$npm_config_target"
echo ""

npx cmake-js build \
    --runtime electron \
    --runtime-version "$ELECTRON_VERSION" \
    --arch "$CMAKE_ARCH" \
    --CDCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo ""
echo "Build complete!"
if [ -f "build/Release/slopsmith_audio.node" ]; then
    echo "Output: build/Release/slopsmith_audio.node"
    ls -lh "build/Release/slopsmith_audio.node"
else
    echo "Warning: slopsmith_audio.node not found in expected location"
    find build -name "*.node" 2>/dev/null
fi
