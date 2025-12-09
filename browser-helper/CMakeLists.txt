cmake_minimum_required(VERSION 3.22)

project(streamlumo-browser-helper VERSION 0.1.0
    LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable ARC so we can use weak references safely.
set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS} -fobjc-arc")
set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc")

# =============================================================================
# CEF paths
# =============================================================================
set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../deps/cef_binary_macos_arm64" CACHE PATH "CEF binary distribution root")
set(CEF_FRAMEWORK "${CEF_ROOT}/Chromium Embedded Framework.framework")
set(CEF_WRAPPER_LIB "${CEF_ROOT}/libcef_dll_wrapper/Release/libcef_dll_wrapper.a")

add_executable(streamlumo-browser-helper MACOSX_BUNDLE
    main.m
    AppDelegate.mm
    WebSocketStub.mm
    WebSocketStub.h
    HelperApp.mm
    BrowserClient.mm
    BrowserManager.mm
    BrowserShmWriter.mm
    BrowserShmWriter.h
)

target_include_directories(streamlumo-browser-helper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CEF_ROOT}
    ${CEF_ROOT}/include
)

# Link CEF framework via absolute path (space in name) and wrapper library.
target_link_libraries(streamlumo-browser-helper PRIVATE
    "-framework Cocoa"
    "-framework Foundation"
    "${CEF_FRAMEWORK}/Chromium Embedded Framework"
    ${CEF_WRAPPER_LIB}
)

# Tell linker where to find CEF framework at runtime
set_target_properties(streamlumo-browser-helper PROPERTIES
    BUILD_RPATH "${CEF_FRAMEWORK}/.."
    INSTALL_RPATH "@executable_path/../Frameworks"
)

set_target_properties(streamlumo-browser-helper PROPERTIES
    OUTPUT_NAME "streamlumo-browser-helper"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.streamlumo.browserhelper"
    MACOSX_BUNDLE_BUNDLE_NAME "StreamLumoBrowserHelper"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
)

configure_file(Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)

# =============================================================================
# Copy CEF framework into app bundle for runtime
# =============================================================================
add_custom_command(TARGET streamlumo-browser-helper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:streamlumo-browser-helper>/Contents/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_FRAMEWORK}" "$<TARGET_BUNDLE_DIR:streamlumo-browser-helper>/Contents/Frameworks/Chromium Embedded Framework.framework"
    COMMENT "Copying CEF framework into helper bundle"
)

# =============================================================================
# Create CEF subprocess helper bundles (required for multi-process on macOS)
# These are copies of the main helper with different bundle names/identifiers.
# CEF looks for helpers with specific suffixes: (GPU), (Renderer), (Plugin), (Alerts)
# =============================================================================

# Create a shell script to create helper bundles (avoids CMake escaping issues)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/create_helpers.sh" "#!/bin/bash
set -e
BUNDLE_DIR=\"\$1\"
MAIN_EXE=\"\${BUNDLE_DIR}/Contents/MacOS/streamlumo-browser-helper\"
FRAMEWORKS_DIR=\"\${BUNDLE_DIR}/Contents/Frameworks\"
CEF_FRAMEWORK=\"\${FRAMEWORKS_DIR}/Chromium Embedded Framework.framework\"

# Create base helper bundle (no suffix) - required by CEF
BASE_HELPER_DIR=\"\${FRAMEWORKS_DIR}/streamlumo-browser-helper.app\"
mkdir -p \"\${BASE_HELPER_DIR}/Contents/MacOS\"
mkdir -p \"\${BASE_HELPER_DIR}/Contents/Frameworks\"
cp \"\${MAIN_EXE}\" \"\${BASE_HELPER_DIR}/Contents/MacOS/streamlumo-browser-helper\"
# Symlink CEF framework so subprocess can find it via @executable_path/../Frameworks
# From Frameworks/streamlumo-browser-helper.app/Contents/Frameworks/ we need ../../../ to get back to Frameworks/
ln -sf \"../../../Chromium Embedded Framework.framework\" \"\${BASE_HELPER_DIR}/Contents/Frameworks/Chromium Embedded Framework.framework\"
cat > \"\${BASE_HELPER_DIR}/Contents/Info.plist\" << EOF
<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
    <key>CFBundleIdentifier</key>
    <string>com.streamlumo.browserhelper</string>
    <key>CFBundleName</key>
    <string>streamlumo-browser-helper</string>
    <key>CFBundleExecutable</key>
    <string>streamlumo-browser-helper</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleVersion</key>
    <string>${PROJECT_VERSION}</string>
    <key>LSUIElement</key>
    <true/>
</dict>
</plist>
EOF

# Create helper bundles with suffixes
for SUFFIX in GPU Renderer Plugin Alerts; do
    HELPER_NAME=\"streamlumo-browser-helper (\${SUFFIX})\"
    HELPER_DIR=\"\${FRAMEWORKS_DIR}/\${HELPER_NAME}.app\"
    
    mkdir -p \"\${HELPER_DIR}/Contents/MacOS\"
    mkdir -p \"\${HELPER_DIR}/Contents/Frameworks\"
    cp \"\${MAIN_EXE}\" \"\${HELPER_DIR}/Contents/MacOS/\${HELPER_NAME}\"
    # Symlink CEF framework so subprocess can find it via @executable_path/../Frameworks
    # From Frameworks/streamlumo-browser-helper (Suffix).app/Contents/Frameworks/ we need ../../../ to get back to Frameworks/
    ln -sf \"../../../Chromium Embedded Framework.framework\" \"\${HELPER_DIR}/Contents/Frameworks/Chromium Embedded Framework.framework\"
    
    cat > \"\${HELPER_DIR}/Contents/Info.plist\" << EOF
<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
    <key>CFBundleIdentifier</key>
    <string>com.streamlumo.browserhelper.\${SUFFIX}</string>
    <key>CFBundleName</key>
    <string>\${HELPER_NAME}</string>
    <key>CFBundleExecutable</key>
    <string>\${HELPER_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleVersion</key>
    <string>${PROJECT_VERSION}</string>
    <key>LSUIElement</key>
    <true/>
</dict>
</plist>
EOF
done
echo 'CEF helper bundles created successfully'
")

add_custom_command(TARGET streamlumo-browser-helper POST_BUILD
    COMMAND chmod +x "${CMAKE_CURRENT_BINARY_DIR}/create_helpers.sh"
    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/create_helpers.sh" "$<TARGET_BUNDLE_DIR:streamlumo-browser-helper>"
    COMMENT "Creating CEF helper bundles for subprocess support"
)
