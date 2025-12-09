#!/bin/bash
# Run streamlumo-engine with proper library paths
# Usage: ./run-engine.sh [args...]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE_DIR="${SCRIPT_DIR}/build/macos-arm64"
DEPS_DIR="${SCRIPT_DIR}/../deps/obs-deps"
OBS_BUILD="${SCRIPT_DIR}/../obs-studio/build_arm64"
OBS_BUILD_MACOS="${SCRIPT_DIR}/../obs-studio/build_macos"

# Plugin path - where OBS plugins (.plugin bundles) are built
OBS_PLUGINS="${OBS_BUILD_MACOS}/plugins"

# Set up library search paths
export DYLD_FRAMEWORK_PATH="${OBS_BUILD}/libobs/Release:${DYLD_FRAMEWORK_PATH}"
export DYLD_LIBRARY_PATH="${OBS_BUILD}/frontend/api/Release:${OBS_BUILD_MACOS}/libobs-metal/Release:${DEPS_DIR}:${DYLD_LIBRARY_PATH}"

echo "=== StreamLumo Engine Launcher ==="
echo "Engine: ${ENGINE_DIR}/streamlumo-engine"
echo "Plugin path: ${OBS_PLUGINS}"
echo "DYLD_FRAMEWORK_PATH: ${DYLD_FRAMEWORK_PATH}"
echo "DYLD_LIBRARY_PATH: ${DYLD_LIBRARY_PATH}"
echo "=================================="
echo ""

exec "${ENGINE_DIR}/streamlumo-engine" --plugin-path "${OBS_PLUGINS}" "$@"
