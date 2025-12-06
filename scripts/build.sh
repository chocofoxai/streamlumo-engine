#!/bin/bash
# streamlumo-engine/scripts/build.sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024 StreamLumo
#
# Build script for streamlumo-engine

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Default values
BUILD_TYPE="Release"
PRESET="macos-arm64"
CLEAN=false
VERBOSE=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            PRESET="macos-arm64-debug"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --preset)
            PRESET="$2"
            shift 2
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --debug          Build debug version"
            echo "  --release        Build release version (default)"
            echo "  --preset NAME    Use specific CMake preset"
            echo "  --clean          Clean build directory first"
            echo "  --verbose, -v    Verbose output"
            echo "  --help, -h       Show this help"
            echo ""
            echo "Available presets:"
            echo "  macos-arm64      Apple Silicon (M1/M2/M3)"
            echo "  macos-x86_64     Intel Macs"
            echo "  macos-universal  Universal binary"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "============================================"
echo "StreamLumo Engine - Build Script"
echo "============================================"
echo ""
echo "Project: ${PROJECT_DIR}"
echo "Preset: ${PRESET}"
echo "Build Type: ${BUILD_TYPE}"
echo ""

cd "${PROJECT_DIR}"

BUILD_DIR="build/${PRESET}"

# Clean if requested
if [ "$CLEAN" = true ] && [ -d "${BUILD_DIR}" ]; then
    echo "→ Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
fi

# Check if OBS has been built
OBS_BUILD_DIR="${PROJECT_DIR}/../obs-studio/build_arm64"
if [ ! -d "${OBS_BUILD_DIR}" ]; then
    echo "⚠️  OBS Studio build not found at: ${OBS_BUILD_DIR}"
    echo "   Please build OBS Studio first:"
    echo "   cd ../obs-studio && cmake --preset macos-arm64 && cmake --build build_arm64"
    echo ""
    echo "   Or run: ./scripts/build-obs.sh"
    exit 1
fi

# Configure
echo "→ Configuring with preset: ${PRESET}..."
CMAKE_ARGS=""
if [ "$VERBOSE" = true ]; then
    CMAKE_ARGS="--log-level=DEBUG"
fi

cmake --preset "${PRESET}" ${CMAKE_ARGS}

# Build
echo ""
echo "→ Building..."

BUILD_ARGS=""
if [ "$VERBOSE" = true ]; then
    BUILD_ARGS="--verbose"
fi

cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" ${BUILD_ARGS}

# Install
echo ""
echo "→ Installing to dist/${PRESET}..."
cmake --install "${BUILD_DIR}" --prefix "dist/${PRESET}"

echo ""
echo "============================================"
echo "✓ Build complete!"
echo "============================================"
echo ""
echo "Output: ${PROJECT_DIR}/dist/${PRESET}"
echo ""
echo "To test:"
echo "  ./dist/${PRESET}/MacOS/streamlumo-engine --help"
echo ""
echo "Next steps:"
echo "  1. Sign the binary: ./scripts/sign.sh"
echo "  2. Integrate with StreamLumo desktop app"
echo ""
