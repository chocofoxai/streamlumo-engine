#!/bin/bash
# streamlumo-engine/scripts/sign.sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024 StreamLumo
#
# Code signing script for streamlumo-engine and CEF helpers
#
# Usage:
#   ./scripts/sign.sh <path-to-app-bundle> [--identity "Developer ID"]
#
# Example:
#   ./scripts/sign.sh ../apps/desktop/dist/mac-arm64/StreamLumo.app

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Default signing identity (ad-hoc for development)
IDENTITY="-"

# Entitlements paths
ENTITLEMENTS_MAIN="${PROJECT_DIR}/entitlements/streamlumo-engine.entitlements"
ENTITLEMENTS_HELPER="${PROJECT_DIR}/entitlements/streamlumo-engine-helper.entitlements"

# Parse arguments
APP_PATH=""
while [[ $# -gt 0 ]]; do
    case $1 in
        --identity|-i)
            IDENTITY="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 <app-bundle-path> [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --identity, -i <ID>  Code signing identity"
            echo "                       Default: '-' (ad-hoc signing)"
            echo "                       Use 'Developer ID Application: Name (TEAMID)' for distribution"
            echo "  --help, -h           Show this help"
            echo ""
            echo "Example:"
            echo "  $0 StreamLumo.app"
            echo "  $0 StreamLumo.app --identity 'Developer ID Application: MyCompany (ABC123)'"
            exit 0
            ;;
        *)
            APP_PATH="$1"
            shift
            ;;
    esac
done

if [ -z "${APP_PATH}" ]; then
    echo "Error: App bundle path required"
    echo "Usage: $0 <app-bundle-path> [--identity <signing-identity>]"
    exit 1
fi

if [ ! -d "${APP_PATH}" ]; then
    echo "Error: App bundle not found: ${APP_PATH}"
    exit 1
fi

echo "============================================"
echo "StreamLumo Engine - Code Signing"
echo "============================================"
echo ""
echo "App Bundle: ${APP_PATH}"
echo "Identity: ${IDENTITY}"
echo ""

# Signing options
SIGN_OPTS="--force --timestamp --options runtime"

# Function to sign a file/bundle
sign_item() {
    local path="$1"
    local entitlements="$2"
    local name=$(basename "$path")
    
    if [ ! -e "$path" ]; then
        echo "  ⚠️  Not found: ${name}"
        return 0
    fi
    
    echo "  → Signing: ${name}"
    
    if [ -n "$entitlements" ] && [ -f "$entitlements" ]; then
        codesign ${SIGN_OPTS} --sign "${IDENTITY}" --entitlements "$entitlements" "$path"
    else
        codesign ${SIGN_OPTS} --sign "${IDENTITY}" "$path"
    fi
}

# 1. Sign CEF Framework (deepest first)
echo "=== Signing CEF Framework ==="
CEF_FRAMEWORK="${APP_PATH}/Contents/Frameworks/Chromium Embedded Framework.framework"
if [ -d "${CEF_FRAMEWORK}" ]; then
    sign_item "${CEF_FRAMEWORK}" ""
else
    echo "  ⚠️  CEF Framework not found (browser sources won't work)"
fi

# 2. Sign CEF Helper Apps
echo ""
echo "=== Signing CEF Helper Apps ==="
for helper_name in \
    "streamlumo-engine Helper" \
    "streamlumo-engine Helper (GPU)" \
    "streamlumo-engine Helper (Plugin)" \
    "streamlumo-engine Helper (Renderer)"; do
    
    helper_path="${APP_PATH}/Contents/Frameworks/${helper_name}.app"
    sign_item "${helper_path}" "${ENTITLEMENTS_HELPER}"
done

# 3. Sign libobs framework
echo ""
echo "=== Signing libobs Framework ==="
sign_item "${APP_PATH}/Contents/Frameworks/libobs.framework" ""
sign_item "${APP_PATH}/Contents/Frameworks/libobs.0.dylib" ""

# 4. Sign other frameworks
echo ""
echo "=== Signing Other Frameworks ==="
for framework in "${APP_PATH}/Contents/Frameworks"/*.framework; do
    if [ -d "$framework" ]; then
        name=$(basename "$framework")
        if [[ "$name" != "Chromium Embedded Framework.framework" ]] && \
           [[ "$name" != "libobs.framework" ]] && \
           [[ "$name" != "Electron Framework.framework" ]]; then
            sign_item "$framework" ""
        fi
    fi
done

# 5. Sign dylibs
echo ""
echo "=== Signing Dynamic Libraries ==="
for dylib in "${APP_PATH}/Contents/Frameworks"/*.dylib; do
    if [ -f "$dylib" ]; then
        sign_item "$dylib" ""
    fi
done

# 6. Sign OBS plugins
echo ""
echo "=== Signing OBS Plugins ==="
PLUGINS_DIR="${APP_PATH}/Contents/PlugIns/obs-plugins"
if [ -d "${PLUGINS_DIR}" ]; then
    for plugin in "${PLUGINS_DIR}"/*.so "${PLUGINS_DIR}"/*.dylib; do
        if [ -f "$plugin" ]; then
            sign_item "$plugin" ""
        fi
    done
else
    echo "  ⚠️  OBS plugins directory not found"
fi

# 7. Sign streamlumo-engine executable
echo ""
echo "=== Signing StreamLumo Engine ==="
sign_item "${APP_PATH}/Contents/MacOS/streamlumo-engine" "${ENTITLEMENTS_MAIN}"

# 8. Verify signatures
echo ""
echo "=== Verifying Signatures ==="

echo "  → Verifying streamlumo-engine..."
codesign --verify --deep --strict "${APP_PATH}/Contents/MacOS/streamlumo-engine" 2>&1 || {
    echo "  ⚠️  Verification failed for streamlumo-engine"
}

if [ -d "${CEF_FRAMEWORK}" ]; then
    echo "  → Verifying CEF Framework..."
    codesign --verify --deep --strict "${CEF_FRAMEWORK}" 2>&1 || {
        echo "  ⚠️  Verification failed for CEF Framework"
    }
fi

echo ""
echo "============================================"
echo "✓ Code signing complete!"
echo "============================================"
echo ""
echo "To verify the complete app bundle:"
echo "  codesign --verify --deep --strict --verbose=2 '${APP_PATH}'"
echo ""
echo "To check entitlements:"
echo "  codesign -d --entitlements - '${APP_PATH}/Contents/MacOS/streamlumo-engine'"
echo ""

if [ "${IDENTITY}" = "-" ]; then
    echo "⚠️  Note: Ad-hoc signing used (development only)"
    echo "   For distribution, use a Developer ID certificate:"
    echo "   $0 '${APP_PATH}' --identity 'Developer ID Application: Your Name (TEAMID)'"
    echo ""
fi
