#!/bin/bash
# streamlumo-engine/scripts/download-deps.sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024 StreamLumo / Intelli-SAAS
#
# Downloads all required dependencies for building streamlumo-engine

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DEPS_DIR="${PROJECT_DIR}/../deps"

# Dependency versions and URLs
OBS_DEPS_VERSION="2025-08-23"
CEF_VERSION="6533"

OBS_DEPS_URL="https://github.com/obsproject/obs-deps/releases/download/${OBS_DEPS_VERSION}/macos-deps-${OBS_DEPS_VERSION}-universal.tar.xz"
OBS_DEPS_QT_URL="https://github.com/obsproject/obs-deps/releases/download/${OBS_DEPS_VERSION}/macos-deps-qt6-${OBS_DEPS_VERSION}-universal.tar.xz"
CEF_ARM64_URL="https://cdn-fastly.obsproject.com/downloads/cef_binary_${CEF_VERSION}_macos_arm64_v5.tar.xz"
CEF_X64_URL="https://cdn-fastly.obsproject.com/downloads/cef_binary_${CEF_VERSION}_macos_x86_64_v5.tar.xz"

echo "============================================"
echo "StreamLumo Engine - Dependency Downloader"
echo "============================================"
echo ""
echo "Project directory: ${PROJECT_DIR}"
echo "Dependencies directory: ${DEPS_DIR}"
echo ""

# Create deps directory
mkdir -p "${DEPS_DIR}"
cd "${DEPS_DIR}"

# Function to download and extract
download_and_extract() {
    local url="$1"
    local name="$2"
    local extract_dir="$3"
    
    if [ -d "${extract_dir}" ]; then
        echo "✓ ${name} already exists, skipping..."
        return 0
    fi
    
    echo "→ Downloading ${name}..."
    local filename=$(basename "${url}")
    
    if [ ! -f "${filename}" ]; then
        curl -L -o "${filename}" "${url}"
    fi
    
    echo "→ Extracting ${name}..."
    mkdir -p "${extract_dir}"
    tar -xf "${filename}" -C "${extract_dir}" --strip-components=1 2>/dev/null || \
    tar -xf "${filename}" -C "${extract_dir}" 2>/dev/null || \
    tar -xf "${filename}"
    
    echo "✓ ${name} ready"
}

# Download OBS dependencies
echo "=== OBS Dependencies ==="
download_and_extract "${OBS_DEPS_URL}" "OBS Dependencies" "obs-deps"

# Download Qt6 (optional, for any Qt-dependent plugins)
echo ""
echo "=== Qt6 Dependencies ==="
download_and_extract "${OBS_DEPS_QT_URL}" "Qt6" "qt6"

# Download CEF for current architecture
echo ""
echo "=== CEF (Chromium Embedded Framework) ==="

ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    echo "Detected Apple Silicon (ARM64)"
    download_and_extract "${CEF_ARM64_URL}" "CEF ARM64" "cef_binary_macos_arm64"
elif [ "$ARCH" = "x86_64" ]; then
    echo "Detected Intel (x86_64)"
    download_and_extract "${CEF_X64_URL}" "CEF x86_64" "cef_binary_macos_x86_64"
else
    echo "Unknown architecture: ${ARCH}"
    echo "Downloading both CEF versions..."
    download_and_extract "${CEF_ARM64_URL}" "CEF ARM64" "cef_binary_macos_arm64"
    download_and_extract "${CEF_X64_URL}" "CEF x86_64" "cef_binary_macos_x86_64"
fi

echo ""
echo "============================================"
echo "✓ All dependencies downloaded successfully!"
echo "============================================"
echo ""
echo "Dependencies are located in: ${DEPS_DIR}"
echo ""
echo "Next steps:"
echo "  1. Build OBS Studio: ./scripts/build-obs.sh"
echo "  2. Build StreamLumo Engine: ./scripts/build.sh"
echo ""
