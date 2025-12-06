// streamlumo-engine/src/platform/platform_common.cpp
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo
//
// Platform-independent implementations

#include "platform.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

namespace streamlumo {
namespace platform {

// =============================================================================
// Path Utilities - Common Implementations
// =============================================================================

std::string joinPath(const std::string& base, const std::string& path) {
    if (base.empty()) return path;
    if (path.empty()) return base;
    
    std::string result = base;
    
    // Remove trailing separator from base
    while (!result.empty() && 
           (result.back() == '/' || result.back() == '\\')) {
        result.pop_back();
    }
    
    // Remove leading separator from path
    size_t start = 0;
    while (start < path.size() && 
           (path[start] == '/' || path[start] == '\\')) {
        start++;
    }
    
    result += PathSeparator;
    result += path.substr(start);
    
    return result;
}

std::string joinPath(const std::vector<std::string>& components) {
    if (components.empty()) return "";
    
    std::string result = components[0];
    for (size_t i = 1; i < components.size(); i++) {
        result = joinPath(result, components[i]);
    }
    return result;
}

std::string normalizePath(const std::string& path) {
    if (path.empty()) return "";
    
    std::string result = path;
    
    // Convert all separators to platform-specific
#ifdef STREAMLUMO_WINDOWS
    std::replace(result.begin(), result.end(), '/', '\\');
#else
    std::replace(result.begin(), result.end(), '\\', '/');
#endif
    
    // Split into components
    std::vector<std::string> components;
    std::stringstream ss(result);
    std::string component;
    
    char sep = PathSeparator;
    while (std::getline(ss, component, sep)) {
        if (component == "." || component.empty()) {
            continue;
        } else if (component == "..") {
            if (!components.empty() && components.back() != "..") {
                components.pop_back();
            } else {
                components.push_back(component);
            }
        } else {
            components.push_back(component);
        }
    }
    
    // Rebuild path
    result.clear();
    
    // Preserve leading separator for absolute paths
#ifdef STREAMLUMO_WINDOWS
    // Check for drive letter (C:\)
    if (path.size() >= 2 && path[1] == ':') {
        result = path.substr(0, 2);
        if (!components.empty()) {
            result += PathSeparator;
        }
    } else if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') {
        // UNC path
        result = "\\\\";
    }
#else
    if (!path.empty() && path[0] == '/') {
        result = "/";
    }
#endif
    
    for (size_t i = 0; i < components.size(); i++) {
        if (i > 0) result += PathSeparator;
        result += components[i];
    }
    
    return result.empty() ? "." : result;
}

std::string getDirectory(const std::string& path) {
    if (path.empty()) return ".";
    
    std::string normalized = normalizePath(path);
    
    // Find last separator
    size_t pos = normalized.find_last_of(PathSeparator);
    
    if (pos == std::string::npos) {
        return ".";
    }
    
    if (pos == 0) {
        return std::string(1, PathSeparator);
    }
    
#ifdef STREAMLUMO_WINDOWS
    // Handle drive root (C:\)
    if (pos == 2 && normalized[1] == ':') {
        return normalized.substr(0, 3);
    }
#endif
    
    return normalized.substr(0, pos);
}

std::string getFilename(const std::string& path) {
    if (path.empty()) return "";
    
    std::string normalized = normalizePath(path);
    
    size_t pos = normalized.find_last_of(PathSeparator);
    
    if (pos == std::string::npos) {
        return normalized;
    }
    
    return normalized.substr(pos + 1);
}

std::string getExtension(const std::string& path) {
    std::string filename = getFilename(path);
    
    size_t pos = filename.rfind('.');
    
    if (pos == std::string::npos || pos == 0) {
        return "";
    }
    
    return filename.substr(pos);
}

// =============================================================================
// Timing - Common Implementations
// =============================================================================

uint64_t getTimestampMicros() {
    return getTimestampNanos() / 1000;
}

uint64_t getTimestampMillis() {
    return getTimestampNanos() / 1000000;
}

void sleepMillis(uint32_t millis) {
    std::this_thread::sleep_for(std::chrono::milliseconds(millis));
}

void sleepMicros(uint32_t micros) {
    std::this_thread::sleep_for(std::chrono::microseconds(micros));
}

void preciseSleep(uint64_t nanos) {
    if (nanos == 0) return;
    
    uint64_t start = getTimestampNanos();
    uint64_t end = start + nanos;
    
    // Sleep for most of the time
    if (nanos > 2000000) { // > 2ms
        std::this_thread::sleep_for(
            std::chrono::nanoseconds(nanos - 1000000)); // Leave 1ms for busy wait
    }
    
    // Busy wait for precision
    while (getTimestampNanos() < end) {
        std::this_thread::yield();
    }
}

} // namespace platform
} // namespace streamlumo
