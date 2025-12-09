#!/bin/bash
# StreamLumo Engine - Development Environment Setup
# 
# This script sets up the necessary symlinks for running the engine
# directly from the build directory during development.
#
# Usage: ./scripts/setup-dev-env.sh [arch]
#   arch: macos-arm64 (default), macos-x86_64, windows-x64, linux-x64

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE_ROOT="$(dirname "$SCRIPT_DIR")"
WORKSPACE_ROOT="$(dirname "$ENGINE_ROOT")"

ARCH="${1:-macos-arm64}"
BUILD_DIR="$ENGINE_ROOT/build/$ARCH"

echo "========================================"
echo "StreamLumo Engine - Dev Environment Setup"
echo "========================================"
echo "Engine Root: $ENGINE_ROOT"
echo "Build Dir: $BUILD_DIR"
echo "Architecture: $ARCH"
echo ""

# Validate build exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "❌ Build directory not found: $BUILD_DIR"
    echo "   Run 'cmake --preset $ARCH && cmake --build build/$ARCH' first"
    exit 1
fi

# Platform-specific setup
case "$ARCH" in
    macos-*)
        echo "🍎 Setting up macOS development environment..."
        
        # Dependencies
        OBS_BUILD="$WORKSPACE_ROOT/obs-studio/build_macos"
        DEPS_PATH="$WORKSPACE_ROOT/deps/obs-deps"
        QT6_PATH="$WORKSPACE_ROOT/deps/qt6"
        
        # Create directories
        mkdir -p "$BUILD_DIR/Frameworks"
        mkdir -p "$BUILD_DIR/PlugIns/obs-plugins"
        mkdir -p "$BUILD_DIR/Resources/obs-data/obs-plugins"
        
        # Create parent-level symlinks (for @executable_path/../)
        rm -f "$ENGINE_ROOT/build/Frameworks" "$ENGINE_ROOT/build/PlugIns" "$ENGINE_ROOT/build/Resources"
        ln -sf "$ARCH/Frameworks" "$ENGINE_ROOT/build/Frameworks"
        ln -sf "$ARCH/PlugIns" "$ENGINE_ROOT/build/PlugIns"
        ln -sf "$ARCH/Resources" "$ENGINE_ROOT/build/Resources"
        
        echo "  📦 Linking FFmpeg libraries..."
        cd "$BUILD_DIR/Frameworks"
        for lib in "$DEPS_PATH"/*.dylib; do
            name=$(basename "$lib")
            [ ! -L "$name" ] && ln -sf "$lib" .
        done
        
        echo "  📦 Linking OBS libraries..."
        ln -sf "$OBS_BUILD/libobs/Release/libobs.framework" . 2>/dev/null || true
        ln -sf "$OBS_BUILD/libobs-metal/Release/libobs-metal.dylib" . 2>/dev/null || true
        ln -sf "$OBS_BUILD/libobs-opengl/Release/libobs-opengl.dylib" . 2>/dev/null || true
        ln -sf "$OBS_BUILD/frontend/api/Release/obs-frontend-api.dylib" . 2>/dev/null || true
        
        echo "  📦 Linking Qt6 frameworks..."
        for framework in "$QT6_PATH"/Qt*.framework; do
            name=$(basename "$framework")
            [ ! -L "$name" ] && ln -sf "$framework" .
        done
        
        echo "  🔌 Linking obs-websocket plugin..."
        cd "$BUILD_DIR/PlugIns/obs-plugins"
        ln -sf "$OBS_BUILD/plugins/obs-websocket/Release/obs-websocket.plugin" . 2>/dev/null || true
        
        echo ""
        echo "✅ macOS development environment ready!"
        echo ""
        echo "To run the engine:"
        echo "  cd $BUILD_DIR"
        echo "  ./streamlumo-engine --port 4455 --resolution 1920x1080 --fps 30"
        ;;
        
    windows-*)
        echo "🪟 Windows setup not yet implemented"
        echo "   Please ensure all DLLs are in the PATH or same directory"
        ;;
        
    linux-*)
        echo "🐧 Linux setup not yet implemented"
        echo "   Please ensure LD_LIBRARY_PATH is set correctly"
        ;;
        
    *)
        echo "❌ Unknown architecture: $ARCH"
        exit 1
        ;;
esac

echo ""
echo "========================================"
echo "Setup complete!"
echo "========================================"
