# StreamLumo Engine - Headless OBS Server
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024 StreamLumo
#
# This file is part of streamlumo-engine.
# streamlumo-engine is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.

cmake_minimum_required(VERSION 3.22)

project(streamlumo-engine VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Options
# =============================================================================

option(ENABLE_BROWSER "Enable browser source support (CEF)" ON)
option(ENABLE_WEBSOCKET "Enable WebSocket server (obs-websocket)" ON)
option(ENABLE_BROWSER_HELPER "Build standalone macOS CEF helper app (experimental)" OFF)

# =============================================================================
# Find OBS Libraries
# =============================================================================

# These paths should be set via CMake presets or command line
# OBS_SOURCE_DIR: Path to obs-studio source
# OBS_BUILD_DIR: Path to obs-studio build output

if(NOT DEFINED OBS_SOURCE_DIR)
    set(OBS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../obs-studio" CACHE PATH "Path to OBS Studio source")
endif()

if(NOT DEFINED OBS_BUILD_DIR)
    set(OBS_BUILD_DIR "${OBS_SOURCE_DIR}/build" CACHE PATH "Path to OBS Studio build")
endif()

message(STATUS "OBS Source: ${OBS_SOURCE_DIR}")
message(STATUS "OBS Build: ${OBS_BUILD_DIR}")

# Find libobs
find_library(LIBOBS_LIBRARY
    NAMES obs libobs
    PATHS 
        "${OBS_BUILD_DIR}/libobs"
        "${OBS_BUILD_DIR}/libobs/Release"
        "${OBS_BUILD_DIR}/libobs/Debug"
    NO_DEFAULT_PATH
)

find_path(LIBOBS_INCLUDE_DIR
    NAMES obs.h
    PATHS "${OBS_SOURCE_DIR}/libobs"
    NO_DEFAULT_PATH
)

if(NOT LIBOBS_LIBRARY OR NOT LIBOBS_INCLUDE_DIR)
    message(FATAL_ERROR "Could not find libobs. Please build OBS Studio first or set OBS_SOURCE_DIR and OBS_BUILD_DIR")
endif()

message(STATUS "Found libobs: ${LIBOBS_LIBRARY}")
message(STATUS "Found libobs headers: ${LIBOBS_INCLUDE_DIR}")

# Find obs-frontend-api (required for headless frontend stubs)
find_library(OBS_FRONTEND_API_LIBRARY
    NAMES obs-frontend-api obs-frontend-api.dylib libobs-frontend-api.dylib
    PATHS 
        "${OBS_BUILD_DIR}/frontend/api/Release"
        "${OBS_BUILD_DIR}/frontend/api"
        "${OBS_BUILD_DIR}/frontend"
        "${OBS_BUILD_DIR}/frontend/Release"
    PATH_SUFFIXES Release Debug
    NO_DEFAULT_PATH
)

find_path(OBS_FRONTEND_API_INCLUDE_DIR
    NAMES obs-frontend-api.h
    PATHS 
        "${OBS_SOURCE_DIR}/frontend/api"
        "${OBS_SOURCE_DIR}/frontend"
    NO_DEFAULT_PATH
)

if(OBS_FRONTEND_API_LIBRARY)
    message(STATUS "Found obs-frontend-api: ${OBS_FRONTEND_API_LIBRARY}")
    if(OBS_FRONTEND_API_INCLUDE_DIR)
        message(STATUS "Found obs-frontend-api headers: ${OBS_FRONTEND_API_INCLUDE_DIR}")
    endif()
else()
    message(WARNING "obs-frontend-api not found - headless frontend stubs may not compile")
endif()

# =============================================================================
# Platform-Specific Sources
# =============================================================================

# Common platform sources
set(PLATFORM_SOURCES
    src/platform/platform.h
    src/platform/platform_common.cpp
)

# Platform-specific implementations
if(APPLE)
    list(APPEND PLATFORM_SOURCES src/platform/platform_macos.cpp)
elseif(WIN32)
    list(APPEND PLATFORM_SOURCES src/platform/platform_windows.cpp)
elseif(UNIX)
    list(APPEND PLATFORM_SOURCES src/platform/platform_linux.cpp)
endif()

# =============================================================================
# StreamLumo Engine Executable
# =============================================================================

add_executable(streamlumo-engine
    src/main.cpp
    src/engine.cpp
    src/engine.h
    src/browser_helper_launcher.cpp
    src/browser_helper_launcher.h
    src/browser_helper_client.cpp
    src/browser_helper_client.h
    src/config.cpp
    src/config.h
    src/logging.cpp
    src/logging.h
    src/frontend-stubs.cpp
    src/frontend-stubs.h
    ${PLATFORM_SOURCES}
)

target_include_directories(streamlumo-engine PRIVATE
    ${LIBOBS_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../deps/obs-deps
    ${OBS_BUILD_DIR}/config
    ${OBS_FRONTEND_API_INCLUDE_DIR}
)

target_link_libraries(streamlumo-engine PRIVATE
    ${LIBOBS_LIBRARY}
)

if(ENABLE_BROWSER_HELPER AND APPLE)
    target_compile_definitions(streamlumo-engine PRIVATE STREAMLUMO_ENABLE_BROWSER_HELPER=1)
endif()

# Link obs-frontend-api for headless frontend stubs
if(OBS_FRONTEND_API_LIBRARY)
    target_link_libraries(streamlumo-engine PRIVATE ${OBS_FRONTEND_API_LIBRARY})
    target_compile_definitions(streamlumo-engine PRIVATE HAS_FRONTEND_API)
    message(STATUS "Linking with obs-frontend-api for headless frontend support")
endif()

# =============================================================================
# macOS Specific Configuration
# =============================================================================

if(APPLE)
    # Set RPATH for finding frameworks at runtime
    set_target_properties(streamlumo-engine PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks"
        BUILD_WITH_INSTALL_RPATH TRUE
        MACOSX_RPATH TRUE
    )
    
    # Link against macOS frameworks
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    find_library(IOKIT_FRAMEWORK IOKit)
    
    target_link_libraries(streamlumo-engine PRIVATE
        ${COCOA_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
    )
    
    # Code signing configuration
    set(ENTITLEMENTS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/entitlements/streamlumo-engine.entitlements")
    
    if(EXISTS ${ENTITLEMENTS_FILE})
        set_target_properties(streamlumo-engine PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${ENTITLEMENTS_FILE}"
        )
    endif()
endif()

# =============================================================================
# Windows Specific Configuration
# =============================================================================

if(WIN32)
    # Windows subsystem (console for now, can be changed to WIN32 for GUI)
    set_target_properties(streamlumo-engine PROPERTIES
        WIN32_EXECUTABLE FALSE
    )
    
    # Link Windows system libraries
    target_link_libraries(streamlumo-engine PRIVATE
        Shlwapi
        Shell32
        Advapi32
        User32
        Version
    )
    
    # MSVC-specific settings
    if(MSVC)
        target_compile_options(streamlumo-engine PRIVATE
            /W4
            /MP
            /utf-8
            /Zc:__cplusplus
        )
        
        # Use static runtime for easier distribution
        set_property(TARGET streamlumo-engine PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()
    
    # MinGW-specific settings
    if(MINGW)
        target_link_options(streamlumo-engine PRIVATE
            -static-libgcc
            -static-libstdc++
        )
    endif()
endif()

# =============================================================================
# Linux Specific Configuration
# =============================================================================

if(UNIX AND NOT APPLE)
    # Position-independent code for shared library compatibility
    set_target_properties(streamlumo-engine PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
    
    # Link Linux system libraries
    target_link_libraries(streamlumo-engine PRIVATE
        pthread
        dl
        ${CMAKE_DL_LIBS}
    )
    
    # Set RPATH for finding shared libraries
    set_target_properties(streamlumo-engine PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib:$ORIGIN"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
    
    # Compiler warnings and optimizations
    target_compile_options(streamlumo-engine PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# =============================================================================
# Installation (Cross-Platform)
# =============================================================================

# OBS Plugins build directory (Release configuration)
if(NOT DEFINED OBS_PLUGINS_DIR)
    set(OBS_PLUGINS_DIR "${OBS_BUILD_DIR}/../build_macos/plugins" CACHE PATH "Path to built OBS plugins")
endif()

message(STATUS "OBS Plugins Dir: ${OBS_PLUGINS_DIR}")

# List of required OBS plugins for full source type support
# These are built from OBS source and provide all input/output capabilities
set(REQUIRED_OBS_PLUGINS
    # Source plugins (input types)
    image-source          # Image, slideshow, color sources
    mac-capture           # Screen/window/display capture on macOS  
    mac-avcapture         # Camera/video capture on macOS
    obs-ffmpeg            # FFmpeg-based media sources and outputs
    text-freetype2        # Text rendering with FreeType
    
    # Filter plugins
    obs-filters           # Video/audio filters (chroma key, color correction, etc.)
    
    # Output plugins
    obs-outputs           # Streaming/recording outputs
    obs-x264              # H.264 software encoder
    mac-videotoolbox      # Hardware H.264/HEVC encoding on macOS
    coreaudio-encoder     # AAC audio encoding on macOS
    rtmp-services         # RTMP streaming services configuration
    
    # Transition plugins  
    obs-transitions       # Scene transitions (fade, swipe, etc.)
)

if(APPLE)
    # macOS: Install to bundle structure
    install(TARGETS streamlumo-engine
        RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/MacOS"
        BUNDLE DESTINATION "${CMAKE_INSTALL_PREFIX}"
    )
    
    # Install entitlements for signing
    install(FILES
        entitlements/streamlumo-engine.entitlements
        entitlements/streamlumo-engine-helper.entitlements
        DESTINATION "${CMAKE_INSTALL_PREFIX}/entitlements"
    )
    
    # Install OBS Plugins
    message(STATUS "Installing OBS plugins from: ${OBS_PLUGINS_DIR}")
    foreach(PLUGIN_NAME ${REQUIRED_OBS_PLUGINS})
        set(PLUGIN_PATH "${OBS_PLUGINS_DIR}/${PLUGIN_NAME}/Release/${PLUGIN_NAME}.plugin")
        if(EXISTS "${PLUGIN_PATH}")
            message(STATUS "  → ${PLUGIN_NAME}")
            install(DIRECTORY "${PLUGIN_PATH}"
                DESTINATION "${CMAKE_INSTALL_PREFIX}/PlugIns/obs-plugins"
                USE_SOURCE_PERMISSIONS
            )
        else()
            message(WARNING "  ⚠ Plugin not found: ${PLUGIN_NAME} (expected at ${PLUGIN_PATH})")
        endif()
    endforeach()
    
    # Also install our headless obs-websocket plugin
    # This is built separately with OBS_WEBSOCKET_HEADLESS=ON
    set(WEBSOCKET_PLUGIN_PATH "${OBS_PLUGINS_DIR}/obs-websocket/Release/obs-websocket.plugin")
    if(EXISTS "${WEBSOCKET_PLUGIN_PATH}")
        message(STATUS "  → obs-websocket (headless)")
        install(DIRECTORY "${WEBSOCKET_PLUGIN_PATH}"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/PlugIns/obs-plugins"
            USE_SOURCE_PERMISSIONS
        )
    else()
        message(WARNING "  ⚠ obs-websocket plugin not found at ${WEBSOCKET_PLUGIN_PATH}")
    endif()
    
    # Install libobs framework
    set(LIBOBS_FRAMEWORK "${OBS_BUILD_DIR}/../build_macos/libobs/Release/libobs.framework")
    if(EXISTS "${LIBOBS_FRAMEWORK}")
        message(STATUS "Installing libobs framework")
        install(DIRECTORY "${LIBOBS_FRAMEWORK}"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/Frameworks"
            USE_SOURCE_PERMISSIONS
        )
    endif()
    
    # Install obs-frontend-api framework (if available)
    set(FRONTEND_FRAMEWORK "${OBS_BUILD_DIR}/../build_macos/frontend/api/Release/obs-frontend-api.framework")
    if(EXISTS "${FRONTEND_FRAMEWORK}")
        message(STATUS "Installing obs-frontend-api framework")
        install(DIRECTORY "${FRONTEND_FRAMEWORK}"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/Frameworks"
            USE_SOURCE_PERMISSIONS
        )
    endif()
    
elseif(WIN32)
    # Windows: Standard installation
    install(TARGETS streamlumo-engine
        RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
    )
    
    # Install OBS runtime DLLs alongside executable
    # These would typically come from OBS_BUILD_DIR
    install(DIRECTORY "${OBS_BUILD_DIR}/bin/Release/"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
        FILES_MATCHING PATTERN "*.dll"
        OPTIONAL
    )
elseif(UNIX)
    # Linux: Standard FHS installation
    include(GNUInstallDirs)
    
    install(TARGETS streamlumo-engine
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    
    # Install shared libraries
    install(DIRECTORY "${OBS_BUILD_DIR}/lib/"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/streamlumo"
        FILES_MATCHING PATTERN "*.so*"
        OPTIONAL
    )
endif()

# =============================================================================
# CEF Helper Apps (if browser sources enabled)
# =============================================================================

if(ENABLE_BROWSER AND APPLE)
    include(cmake/cef-helpers.cmake OPTIONAL)
endif()

# =============================================================================
# Standalone CEF Helper app (experimental)
# =============================================================================

if(ENABLE_BROWSER_HELPER AND APPLE)
    add_subdirectory(browser-helper)
    add_dependencies(streamlumo-engine streamlumo-browser-helper)
    # Copy helper bundle next to the engine binary (../Helpers/) so the launcher can find it.
    add_custom_command(TARGET streamlumo-engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:streamlumo-engine>/../Helpers"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_FILE_DIR:streamlumo-browser-helper>/../.." "$<TARGET_FILE_DIR:streamlumo-engine>/../Helpers/streamlumo-browser-helper.app"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:streamlumo-engine>/Helpers"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_FILE_DIR:streamlumo-browser-helper>/../.." "$<TARGET_FILE_DIR:streamlumo-engine>/Helpers/streamlumo-browser-helper.app"
        COMMENT "Copying browser helper bundle next to engine"
    )
    
    # Build browser bridge plugin when helper is enabled
    add_subdirectory(plugins/obs-browser-bridge)
    add_dependencies(streamlumo-engine obs-browser-bridge)
    
    # Copy plugin next to engine
    add_custom_command(TARGET streamlumo-engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:obs-browser-bridge>" "$<TARGET_FILE_DIR:streamlumo-engine>/../PlugIns/$<TARGET_FILE_NAME:obs-browser-bridge>"
        COMMENT "Copying obs-browser-bridge plugin"
    )
endif()

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "=== StreamLumo Engine Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Browser Sources: ${ENABLE_BROWSER}")
message(STATUS "WebSocket Server: ${ENABLE_WEBSOCKET}")
message(STATUS "Browser Helper (macOS): ${ENABLE_BROWSER_HELPER}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "========================================")
message(STATUS "")
