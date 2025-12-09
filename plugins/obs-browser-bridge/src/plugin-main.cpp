// obs-browser-bridge/src/plugin-main.cpp
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo

#include <obs-module.h>
#include "browser-bridge-manager.hpp"
#include "browser-bridge-source.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-browser-bridge", "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
    return "Browser Bridge";
}

MODULE_EXPORT const char *obs_module_description(void)
{
    return "Browser source using external helper process";
}

MODULE_EXPORT bool obs_module_load(void)
{
    blog(LOG_INFO, "[obs-browser-bridge] Loading plugin v%s", "1.0.0");

    // Register the browser source type
    browser_bridge_source_register();

    blog(LOG_INFO, "[obs-browser-bridge] Plugin loaded successfully");
    return true;
}

MODULE_EXPORT void obs_module_unload(void)
{
    blog(LOG_INFO, "[obs-browser-bridge] Unloading plugin");

    // Shutdown the bridge manager (stops helper process)
    browser_bridge::BrowserBridgeManager::instance().shutdown();

    blog(LOG_INFO, "[obs-browser-bridge] Plugin unloaded");
}
