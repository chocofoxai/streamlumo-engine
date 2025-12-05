#!/bin/bash
# streamlumo-engine/scripts/build-obs.sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024 StreamLumo / Intelli-SAAS
#
# Builds OBS Studio in headless mode (no UI) for use with streamlumo-engine

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OBS_DIR="${PROJECT_DIR}/../obs-studio"
DEPS_DIR="${PROJECT_DIR}/../deps"

# Default values
BUILD_TYPE="Release"
ARCH="arm64"
CLEAN=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --arch)
            ARCH="$2"
            shift 2
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --debug       Build debug version"
            echo "  --arch ARCH   Target architecture: arm64, x86_64, universal"
            echo "  --clean       Clean build directory first"
            echo "  --help, -h    Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "============================================"
echo "OBS Studio Headless Build"
echo "============================================"
echo ""
echo "OBS Source: ${OBS_DIR}"
echo "Build Type: ${BUILD_TYPE}"
echo "Architecture: ${ARCH}"
echo ""

# Check if OBS source exists
if [ ! -d "${OBS_DIR}" ]; then
    echo "Error: OBS Studio source not found at: ${OBS_DIR}"
    echo ""
    echo "Please ensure obs-studio is cloned as a submodule:"
    echo "  git submodule update --init --recursive"
    exit 1
fi

# Check if dependencies exist
if [ ! -d "${DEPS_DIR}/obs-deps" ]; then
    echo "Error: Dependencies not found"
    echo "Please run: ./scripts/download-deps.sh"
    exit 1
fi

# Determine CEF path based on architecture
if [ "$ARCH" = "arm64" ]; then
    CEF_DIR="${DEPS_DIR}/cef_binary_macos_arm64"
    OSX_ARCH="arm64"
elif [ "$ARCH" = "x86_64" ]; then
    CEF_DIR="${DEPS_DIR}/cef_binary_macos_x86_64"
    OSX_ARCH="x86_64"
else
    CEF_DIR="${DEPS_DIR}/cef_binary_macos_arm64"  # Use ARM for universal, lipo later
    OSX_ARCH="arm64;x86_64"
fi

# Build CEF Wrapper if not exists
if [ ! -f "${CEF_DIR}/build/libcef_dll_wrapper/libcef_dll_wrapper.a" ] && [ ! -f "${CEF_DIR}/build/libcef_dll_wrapper/Release/libcef_dll_wrapper.a" ]; then
    echo "→ Building CEF Wrapper..."
    mkdir -p "${CEF_DIR}/build"
    pushd "${CEF_DIR}/build" > /dev/null
    cmake .. \
        -G "Ninja" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES="${OSX_ARCH}" \
        -DPROJECT_ARCH="${OSX_ARCH}"
    cmake --build . --config Release
    mkdir -p libcef_dll_wrapper
    mv libcef_dll_wrapper.a libcef_dll_wrapper/
    popd > /dev/null
    echo "✓ CEF Wrapper built"
fi

cd "${OBS_DIR}"

BUILD_DIR="build_${ARCH}"

# Clean if requested
if [ "$CLEAN" = true ] && [ -d "${BUILD_DIR}" ]; then
    echo "→ Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
fi

# Configure OBS with headless settings
echo "→ Configuring OBS Studio (headless mode)..."

cmake -S . -B "${BUILD_DIR}" \
    -G "Xcode" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_OSX_ARCHITECTURES="${OSX_ARCH}" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="11.0" \
    -DCMAKE_PREFIX_PATH="${DEPS_DIR}/obs-deps;${DEPS_DIR}/qt6" \
    -DOBS_VERSION_OVERRIDE="30.0.0" \
    -DENABLE_UI=OFF \
    -DENABLE_BROWSER=ON \
    -DENABLE_WEBSOCKET=ON \
    -DENABLE_SCRIPTING=OFF \
    -DENABLE_VLC=OFF \
    -DENABLE_AJA=OFF \
    -DENABLE_NEW_MPEGTS_OUTPUT=OFF \
    -DENABLE_VIRTUALCAM=ON \
    -DENABLE_PLUGINS=ON \
    -DCEF_ROOT_DIR="${CEF_DIR}"

# Build
echo ""
echo "→ Building OBS Studio..."
cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" -j$(sysctl -n hw.ncpu)

echo ""
echo "============================================"
echo "✓ OBS Studio build complete!"
echo "============================================"
echo ""
echo "Build output: ${OBS_DIR}/${BUILD_DIR}"
echo ""
echo "Key outputs:"
echo "  - libobs: ${BUILD_DIR}/libobs/"
echo "  - Plugins: ${BUILD_DIR}/plugins/"
echo ""
echo "Next steps:"
echo "  1. Build streamlumo-engine: cd ../streamlumo-engine && ./scripts/build.sh"
echo ""
