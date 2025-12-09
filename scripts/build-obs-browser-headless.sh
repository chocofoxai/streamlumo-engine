#!/bin/bash
# build-obs-browser-headless.sh
# Builds obs-browser plugin for headless operation (no Qt dependency)
#
# Usage: ./build-obs-browser-headless.sh [--clean]
#
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024 StreamLumo

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE_DIR="$(dirname "$SCRIPT_DIR")"
REPO_ROOT="$(dirname "$ENGINE_DIR")"
OBS_DIR="$REPO_ROOT/obs-studio"
CEF_DIR="$REPO_ROOT/deps/cef_binary_macos_arm64"
BUILD_DIR="$OBS_DIR/build_headless"
DIST_DIR="$ENGINE_DIR/dist/macos-arm64"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."
    
    # Check CMake
    if ! command -v cmake &> /dev/null; then
        log_error "CMake not found. Install with: brew install cmake"
        exit 1
    fi
    
    # Check Ninja
    if ! command -v ninja &> /dev/null; then
        log_error "Ninja not found. Install with: brew install ninja"
        exit 1
    fi
    
    # Check Xcode
    if ! xcode-select -p &> /dev/null; then
        log_error "Xcode Command Line Tools not found. Install with: xcode-select --install"
        exit 1
    fi
    
    # Check CEF
    if [ ! -d "$CEF_DIR" ]; then
        log_error "CEF binary not found at: $CEF_DIR"
        exit 1
    fi
    
    # Check OBS source
    if [ ! -f "$OBS_DIR/CMakeLists.txt" ]; then
        log_error "OBS source not found at: $OBS_DIR"
        exit 1
    fi
    
    log_success "All prerequisites met"
}

# Apply the headless patch
apply_patch() {
    log_info "Applying headless patch..."
    
    PATCH_FILE="$ENGINE_DIR/patches/obs-browser-headless.patch"
    
    if [ ! -f "$PATCH_FILE" ]; then
        log_error "Patch file not found: $PATCH_FILE"
        exit 1
    fi
    
    cd "$OBS_DIR"
    
    # Check if already patched
    if grep -q "STREAMLUMO_HEADLESS" plugins/obs-browser/cmake/os-macos.cmake 2>/dev/null; then
        log_warn "Patch appears to already be applied, skipping..."
        return 0
    fi
    
    # Try to apply patch
    if git apply --check "$PATCH_FILE" 2>/dev/null; then
        git apply "$PATCH_FILE"
        log_success "Patch applied successfully"
    else
        log_warn "Patch may have conflicts, attempting direct modification..."
        apply_manual_modification
    fi
}

# Manual modification if patch fails
apply_manual_modification() {
    log_info "Applying manual modification to os-macos.cmake..."
    
    local CMAKE_FILE="$OBS_DIR/plugins/obs-browser/cmake/os-macos.cmake"
    local BACKUP_FILE="${CMAKE_FILE}.backup"
    
    # Create backup
    cp "$CMAKE_FILE" "$BACKUP_FILE"
    
    # Create modified version
    cat > "$CMAKE_FILE" << 'EOF'
# StreamLumo Headless Mode Support
# When STREAMLUMO_HEADLESS is ON, CEF uses its own message loop thread
# instead of depending on Qt's event loop.

option(STREAMLUMO_HEADLESS "Build obs-browser for headless operation (no Qt dependency)" OFF)

if(STREAMLUMO_HEADLESS)
  message(STATUS "obs-browser: Building for HEADLESS mode (CEF runs own message loop)")
  target_compile_definitions(obs-browser PRIVATE ENABLE_BROWSER_SHARED_TEXTURE)
else()
  find_package(Qt6 REQUIRED Widgets)
  message(STATUS "obs-browser: Building for GUI mode (Qt event loop integration)")
  target_compile_definitions(obs-browser PRIVATE ENABLE_BROWSER_SHARED_TEXTURE ENABLE_BROWSER_QT_LOOP)
endif()

if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 14.0.3)
  target_compile_options(obs-browser PRIVATE -Wno-error=unqualified-std-cast-call)
endif()

if(STREAMLUMO_HEADLESS)
  target_link_libraries(obs-browser PRIVATE CEF::Wrapper "$<LINK_LIBRARY:FRAMEWORK,CoreFoundation.framework>"
                                            "$<LINK_LIBRARY:FRAMEWORK,AppKit.framework>")
else()
  target_link_libraries(obs-browser PRIVATE Qt::Widgets CEF::Wrapper "$<LINK_LIBRARY:FRAMEWORK,CoreFoundation.framework>"
                                            "$<LINK_LIBRARY:FRAMEWORK,AppKit.framework>")
endif()

set(helper_basename browser-helper)
set(helper_output_name "OBS Helper")
set(helper_suffixes "::" " (GPU):_gpu:.gpu" " (Plugin):_plugin:.plugin" " (Renderer):_renderer:.renderer")

foreach(helper IN LISTS helper_suffixes)
  string(REPLACE ":" ";" helper ${helper})
  list(GET helper 0 helper_name)
  list(GET helper 1 helper_target)
  list(GET helper 2 helper_plist)

  set(target_name ${helper_basename}${helper_target})
  set(target_output_name "${helper_output_name}${helper_name}")
  set(EXECUTABLE_NAME "${target_output_name}")
  set(BUNDLE_ID_SUFFIX ${helper_plist})

  configure_file(cmake/macos/Info-helper.plist.in Info-Helper${helper_plist}.plist)

  add_executable(${target_name} MACOSX_BUNDLE EXCLUDE_FROM_ALL)
  add_executable(OBS::${target_name} ALIAS ${target_name})

  target_sources(
    ${target_name} PRIVATE
                           browser-app.cpp browser-app.hpp cef-headers.hpp obs-browser-page/obs-browser-page-main.cpp)

  target_compile_definitions(${target_name} PRIVATE ENABLE_BROWSER_SHARED_TEXTURE)

  if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 14.0.3)
    target_compile_options(${target_name} PRIVATE -Wno-error=unqualified-std-cast-call)
  endif()

  target_include_directories(${target_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/deps"
                                                    "${CMAKE_CURRENT_SOURCE_DIR}/obs-browser-page")

  target_link_libraries(${target_name} PRIVATE CEF::Wrapper nlohmann_json::nlohmann_json)

  set_target_properties(
    ${target_name}
    PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info-Helper${helper_plist}.plist"
               OUTPUT_NAME "${target_output_name}"
               FOLDER plugins/obs-browser/Helpers
               XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.obsproject.obs-studio.helper${helper_plist}
               XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
               "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos/entitlements-helper${helper_plist}.plist")
endforeach()

# Only enable Qt MOC/UIC/RCC when building with Qt
if(NOT STREAMLUMO_HEADLESS)
  set_target_properties(
    obs-browser
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
endif()
EOF
    
    log_success "Manual modification applied (backup at: $BACKUP_FILE)"
}

# Configure the build
configure_build() {
    log_info "Configuring build..."
    
    # Clean if requested
    if [ "$1" == "--clean" ] && [ -d "$BUILD_DIR" ]; then
        log_info "Cleaning previous build..."
        rm -rf "$BUILD_DIR"
    fi
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    cmake "$OBS_DIR" \
        -G Xcode \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
        -DOBS_VERSION_OVERRIDE=31.0.0 \
        -DSTREAMLUMO_HEADLESS=ON \
        -DENABLE_BROWSER=ON \
        -DENABLE_BROWSER_PANELS=OFF \
        -DCEF_ROOT_DIR="$CEF_DIR" \
        -DENABLE_UI=OFF \
        -DENABLE_SCRIPTING=OFF \
        -DENABLE_VIRTUALCAM=OFF \
        -DENABLE_AJA=OFF \
        -DENABLE_NEW_MPEGTS_OUTPUT=OFF \
        -DENABLE_WHATSNEW=OFF
    
    log_success "Build configured"
}

# Build obs-browser
build_plugin() {
    log_info "Building obs-browser plugin..."
    
    cd "$BUILD_DIR"
    
    # Build the browser plugin and helpers with Xcode
    xcodebuild -project obs-studio.xcodeproj \
        -target obs-browser \
        -configuration Release \
        -arch arm64 \
        CODE_SIGN_IDENTITY="-" || {
        log_error "Failed to build obs-browser"
        exit 1
    }
    
    # Build helpers
    for helper in browser-helper browser-helper_gpu browser-helper_plugin browser-helper_renderer; do
        log_info "Building $helper..."
        xcodebuild -project obs-studio.xcodeproj \
            -target "$helper" \
            -configuration Release \
            -arch arm64 \
            CODE_SIGN_IDENTITY="-" 2>&1 | grep -E "(BUILD|error:)" || log_warn "Could not build $helper (may not be needed)"
    done
    
    log_success "Build completed"
}

# Install to engine dist
install_plugin() {
    log_info "Installing to engine distribution..."
    
    local PLUGINS_DIR="$DIST_DIR/PlugIns/obs-plugins"
    local FRAMEWORKS_DIR="$DIST_DIR/Frameworks"
    
    mkdir -p "$PLUGINS_DIR"
    mkdir -p "$FRAMEWORKS_DIR"
    
    # Copy plugin bundle
    if [ -d "$BUILD_DIR/plugins/obs-browser/obs-browser.plugin" ]; then
        log_info "Copying obs-browser.plugin..."
        rm -rf "$PLUGINS_DIR/obs-browser.plugin"
        cp -R "$BUILD_DIR/plugins/obs-browser/obs-browser.plugin" "$PLUGINS_DIR/"
    else
        log_error "obs-browser.plugin not found in build output"
        exit 1
    fi
    
    # Copy helper apps
    for helper_app in "$BUILD_DIR/plugins/obs-browser/"*.app; do
        if [ -d "$helper_app" ]; then
            local helper_name=$(basename "$helper_app")
            log_info "Copying $helper_name..."
            rm -rf "$FRAMEWORKS_DIR/$helper_name"
            cp -R "$helper_app" "$FRAMEWORKS_DIR/"
        fi
    done
    
    log_success "Installation completed"
}

# Sign the plugin (optional, for distribution)
sign_plugin() {
    if [ -z "$CODESIGN_IDENTITY" ]; then
        log_warn "CODESIGN_IDENTITY not set, skipping code signing"
        log_info "To enable signing, set: export CODESIGN_IDENTITY='Developer ID Application: Your Name'"
        return 0
    fi
    
    log_info "Signing plugin with identity: $CODESIGN_IDENTITY"
    
    local PLUGINS_DIR="$DIST_DIR/PlugIns/obs-plugins"
    local FRAMEWORKS_DIR="$DIST_DIR/Frameworks"
    
    # Sign helpers first
    for helper_app in "$FRAMEWORKS_DIR/"*.app; do
        if [ -d "$helper_app" ]; then
            codesign --force --deep --sign "$CODESIGN_IDENTITY" "$helper_app"
        fi
    done
    
    # Sign plugin
    codesign --force --deep --sign "$CODESIGN_IDENTITY" "$PLUGINS_DIR/obs-browser.plugin"
    
    log_success "Code signing completed"
}

# Main execution
main() {
    echo ""
    echo "╔═══════════════════════════════════════════════════════════════╗"
    echo "║   StreamLumo obs-browser Headless Build Script                ║"
    echo "╚═══════════════════════════════════════════════════════════════╝"
    echo ""
    
    check_prerequisites
    apply_patch
    configure_build "$1"
    build_plugin
    install_plugin
    sign_plugin
    
    echo ""
    log_success "═══════════════════════════════════════════════════════════════"
    log_success "obs-browser headless build complete!"
    log_success ""
    log_success "Plugin installed to:"
    log_success "  $DIST_DIR/PlugIns/obs-plugins/obs-browser.plugin"
    log_success ""
    log_success "CEF Helpers installed to:"
    log_success "  $DIST_DIR/Frameworks/OBS Helper*.app"
    log_success "═══════════════════════════════════════════════════════════════"
    echo ""
}

main "$@"
