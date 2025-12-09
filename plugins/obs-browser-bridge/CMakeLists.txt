# obs-browser-bridge - OBS source plugin that bridges to browser-helper process
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024 StreamLumo

cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Plugin sources
add_library(obs-browser-bridge MODULE
    src/plugin-main.cpp
    src/browser-bridge-manager.cpp
    src/browser-bridge-manager.hpp
    src/browser-bridge-source.cpp
    src/browser-bridge-source.hpp
    src/ipc-client.cpp
    src/ipc-client.hpp
    src/frame-decoder.cpp
    src/frame-decoder.hpp
)

# Link against parent project's libobs (uses LIBOBS_LIBRARY and LIBOBS_INCLUDE_DIR from parent)
target_link_libraries(obs-browser-bridge PRIVATE ${LIBOBS_LIBRARY})
target_include_directories(obs-browser-bridge PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LIBOBS_INCLUDE_DIR}
    ${LIBOBS_INCLUDE_DIR}/..  # For obs-module.h etc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../deps/obs-deps  # For simde
    ${OBS_BUILD_DIR}/config  # For obsconfig.h
)

# Platform-specific settings
if(APPLE)
    set_target_properties(obs-browser-bridge PROPERTIES
        PREFIX ""
        SUFFIX ".so"
        MACOSX_BUNDLE FALSE
    )
    target_compile_definitions(obs-browser-bridge PRIVATE
        BROWSER_HELPER_BUNDLE_NAME="streamlumo-browser-helper.app"
    )
elseif(WIN32)
    set_target_properties(obs-browser-bridge PROPERTIES
        PREFIX ""
    )
    target_compile_definitions(obs-browser-bridge PRIVATE
        BROWSER_HELPER_EXE_NAME="streamlumo-browser-helper.exe"
    )
else()
    # Linux
    set_target_properties(obs-browser-bridge PROPERTIES
        PREFIX ""
    )
    target_compile_definitions(obs-browser-bridge PRIVATE
        BROWSER_HELPER_EXE_NAME="streamlumo-browser-helper"
    )
endif()

# Install
install(TARGETS obs-browser-bridge
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/obs-plugins
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/obs-plugins
)
