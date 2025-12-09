// streamlumo-engine/src/frontend-stubs.cpp
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo
//
// Headless frontend API stubs for obs-websocket plugin support

#include "frontend-stubs.h"
#include "logging.h"

#include <obs.h>
#include <util/config-file.h>

#include <vector>
#include <string>
#include <cstring>

namespace streamlumo {

// Static instance
static HeadlessFrontend* g_frontend = nullptr;

HeadlessFrontend::HeadlessFrontend() {
    // Initialize profile config with some defaults
    m_profileConfig = config_create("streamlumo-profile");
    m_appConfig = config_create("streamlumo-app");
    m_userConfig = config_create("streamlumo-user");
}

HeadlessFrontend::~HeadlessFrontend() {
    if (m_profileConfig) config_close(m_profileConfig);
    if (m_appConfig) config_close(m_appConfig);
    if (m_userConfig) config_close(m_userConfig);
    
    if (m_currentScene) obs_source_release(m_currentScene);
    if (m_currentTransition) obs_source_release(m_currentTransition);
    if (m_streamingService) obs_service_release(m_streamingService);
}

void HeadlessFrontend::install() {
    if (!g_frontend) {
        g_frontend = new HeadlessFrontend();
        obs_frontend_set_callbacks_internal(g_frontend);
        log_info("Headless frontend callbacks installed");
    }
}

void HeadlessFrontend::uninstall() {
    if (g_frontend) {
        obs_frontend_set_callbacks_internal(nullptr);
        delete g_frontend;
        g_frontend = nullptr;
        log_info("Headless frontend callbacks uninstalled");
    }
}

HeadlessFrontend* HeadlessFrontend::instance() {
    return g_frontend;
}

void HeadlessFrontend::signalFinishedLoading() {
    log_info("Signaling OBS finished loading event... (%zu registered callbacks)", m_eventCallbacks.size());
    on_event(OBS_FRONTEND_EVENT_FINISHED_LOADING);
    log_info("OBS ready for requests (event dispatched to %zu callbacks)", m_eventCallbacks.size());
}

// GUI-related - return nullptr (no GUI)
void* HeadlessFrontend::obs_frontend_get_main_window() { return nullptr; }
void* HeadlessFrontend::obs_frontend_get_main_window_handle() { return nullptr; }
void* HeadlessFrontend::obs_frontend_get_system_tray() { return nullptr; }

// Scene management
void HeadlessFrontend::obs_frontend_get_scenes(struct obs_frontend_source_list* sources) {
    // Enumerate all scenes
    auto cb = [](void* param, obs_source_t* source) {
        auto list = static_cast<obs_frontend_source_list*>(param);
        if (obs_source_get_type(source) == OBS_SOURCE_TYPE_SCENE) {
            obs_source_get_ref(source);
            da_push_back(list->sources, &source);
        }
        return true;
    };
    obs_enum_scenes(cb, sources);
}

obs_source_t* HeadlessFrontend::obs_frontend_get_current_scene() {
    if (m_currentScene) {
        obs_source_get_ref(m_currentScene);
    }
    return m_currentScene;
}

void HeadlessFrontend::obs_frontend_set_current_scene(obs_source_t* scene) {
    if (m_currentScene) obs_source_release(m_currentScene);
    m_currentScene = scene;
    if (m_currentScene) {
        obs_source_get_ref(m_currentScene);
        obs_set_output_source(0, m_currentScene);
    }
    on_event(OBS_FRONTEND_EVENT_SCENE_CHANGED);
}

// Transitions
void HeadlessFrontend::obs_frontend_get_transitions(struct obs_frontend_source_list* sources) {
    // Return empty for now - transitions need to be enumerated
    (void)sources;
}

obs_source_t* HeadlessFrontend::obs_frontend_get_current_transition() {
    if (m_currentTransition) obs_source_get_ref(m_currentTransition);
    return m_currentTransition;
}

void HeadlessFrontend::obs_frontend_set_current_transition(obs_source_t* transition) {
    if (m_currentTransition) obs_source_release(m_currentTransition);
    m_currentTransition = transition;
    if (m_currentTransition) obs_source_get_ref(m_currentTransition);
    on_event(OBS_FRONTEND_EVENT_TRANSITION_CHANGED);
}

int HeadlessFrontend::obs_frontend_get_transition_duration() { return m_transitionDuration; }
void HeadlessFrontend::obs_frontend_set_transition_duration(int duration) { 
    m_transitionDuration = duration; 
    on_event(OBS_FRONTEND_EVENT_TRANSITION_DURATION_CHANGED);
}

void HeadlessFrontend::obs_frontend_release_tbar() {}
int HeadlessFrontend::obs_frontend_get_tbar_position() { return 0; }
void HeadlessFrontend::obs_frontend_set_tbar_position(int position) { (void)position; }

// Scene collections
void HeadlessFrontend::obs_frontend_get_scene_collections(std::vector<std::string>& strings) {
    strings.clear();
    strings.push_back("Default");
}

char* HeadlessFrontend::obs_frontend_get_current_scene_collection() {
    return bstrdup("Default");
}

void HeadlessFrontend::obs_frontend_set_current_scene_collection(const char* collection) {
    (void)collection;
    on_event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED);
}

bool HeadlessFrontend::obs_frontend_add_scene_collection(const char* name) {
    (void)name;
    return true;
}

// Profiles
void HeadlessFrontend::obs_frontend_get_profiles(std::vector<std::string>& strings) {
    strings.clear();
    strings.push_back("Default");
}

char* HeadlessFrontend::obs_frontend_get_current_profile() {
    return bstrdup("Default");
}

char* HeadlessFrontend::obs_frontend_get_current_profile_path() {
    return bstrdup(m_profilePath.c_str());
}

void HeadlessFrontend::obs_frontend_set_current_profile(const char* profile) {
    (void)profile;
    on_event(OBS_FRONTEND_EVENT_PROFILE_CHANGED);
}

void HeadlessFrontend::obs_frontend_create_profile(const char* name) { (void)name; }
void HeadlessFrontend::obs_frontend_duplicate_profile(const char* name) { (void)name; }
void HeadlessFrontend::obs_frontend_delete_profile(const char* profile) { (void)profile; }

// Streaming
void HeadlessFrontend::obs_frontend_streaming_start() {
    m_streamingActive = true;
    on_event(OBS_FRONTEND_EVENT_STREAMING_STARTING);
    on_event(OBS_FRONTEND_EVENT_STREAMING_STARTED);
}

void HeadlessFrontend::obs_frontend_streaming_stop() {
    on_event(OBS_FRONTEND_EVENT_STREAMING_STOPPING);
    m_streamingActive = false;
    on_event(OBS_FRONTEND_EVENT_STREAMING_STOPPED);
}

bool HeadlessFrontend::obs_frontend_streaming_active() { return m_streamingActive; }

// Recording
void HeadlessFrontend::obs_frontend_recording_start() {
    m_recordingActive = true;
    on_event(OBS_FRONTEND_EVENT_RECORDING_STARTING);
    on_event(OBS_FRONTEND_EVENT_RECORDING_STARTED);
}

void HeadlessFrontend::obs_frontend_recording_stop() {
    on_event(OBS_FRONTEND_EVENT_RECORDING_STOPPING);
    m_recordingActive = false;
    on_event(OBS_FRONTEND_EVENT_RECORDING_STOPPED);
}

bool HeadlessFrontend::obs_frontend_recording_active() { return m_recordingActive; }
void HeadlessFrontend::obs_frontend_recording_pause(bool pause) { m_recordingPaused = pause; }
bool HeadlessFrontend::obs_frontend_recording_paused() { return m_recordingPaused; }
bool HeadlessFrontend::obs_frontend_recording_split_file() { return false; }
bool HeadlessFrontend::obs_frontend_recording_add_chapter(const char* name) { (void)name; return false; }

// Replay buffer
void HeadlessFrontend::obs_frontend_replay_buffer_start() {
    m_replayBufferActive = true;
    on_event(OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING);
    on_event(OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED);
}

void HeadlessFrontend::obs_frontend_replay_buffer_save() {
    on_event(OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED);
}

void HeadlessFrontend::obs_frontend_replay_buffer_stop() {
    on_event(OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPING);
    m_replayBufferActive = false;
    on_event(OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED);
}

bool HeadlessFrontend::obs_frontend_replay_buffer_active() { return m_replayBufferActive; }

// Tools menu - no-op in headless
void* HeadlessFrontend::obs_frontend_add_tools_menu_qaction(const char* name) { 
    (void)name; 
    return nullptr; 
}

void HeadlessFrontend::obs_frontend_add_tools_menu_item(const char* name, obs_frontend_cb callback, void* private_data) {
    (void)name; (void)callback; (void)private_data;
}

// Docks - no-op in headless
bool HeadlessFrontend::obs_frontend_add_dock_by_id(const char* id, const char* title, void* widget) {
    (void)id; (void)title; (void)widget;
    return false;
}

void HeadlessFrontend::obs_frontend_remove_dock(const char* id) { (void)id; }

bool HeadlessFrontend::obs_frontend_add_custom_qdock(const char* id, void* dock) {
    (void)id; (void)dock;
    return false;
}

// Event callbacks
void HeadlessFrontend::obs_frontend_add_event_callback(obs_frontend_event_cb callback, void* private_data) {
    EventCallback cb = {callback, private_data};
    m_eventCallbacks.push_back(cb);
    log_info("Frontend event callback registered (now %zu callbacks)", m_eventCallbacks.size());
}

void HeadlessFrontend::obs_frontend_remove_event_callback(obs_frontend_event_cb callback, void* private_data) {
    m_eventCallbacks.erase(
        std::remove_if(m_eventCallbacks.begin(), m_eventCallbacks.end(),
            [callback, private_data](const EventCallback& cb) {
                return cb.callback == callback && cb.private_data == private_data;
            }),
        m_eventCallbacks.end()
    );
}

// Outputs - return nullptr for now (need actual output management)
obs_output_t* HeadlessFrontend::obs_frontend_get_streaming_output() { return nullptr; }
obs_output_t* HeadlessFrontend::obs_frontend_get_recording_output() { return nullptr; }
obs_output_t* HeadlessFrontend::obs_frontend_get_replay_buffer_output() { return nullptr; }

// Config
config_t* HeadlessFrontend::obs_frontend_get_profile_config() { return m_profileConfig; }
config_t* HeadlessFrontend::obs_frontend_get_app_config() { return m_appConfig; }
config_t* HeadlessFrontend::obs_frontend_get_user_config() { return m_userConfig; }

// Projector - no-op
void HeadlessFrontend::obs_frontend_open_projector(const char* type, int monitor, const char* geometry, const char* name) {
    (void)type; (void)monitor; (void)geometry; (void)name;
}

// Save
void HeadlessFrontend::obs_frontend_save() {
    obs_data_t* data = obs_data_create();
    on_save(data);
    obs_data_release(data);
}

void HeadlessFrontend::obs_frontend_defer_save_begin() {}
void HeadlessFrontend::obs_frontend_defer_save_end() {}

void HeadlessFrontend::obs_frontend_add_save_callback(obs_frontend_save_cb callback, void* private_data) {
    SaveCallback cb = {callback, private_data};
    m_saveCallbacks.push_back(cb);
}

void HeadlessFrontend::obs_frontend_remove_save_callback(obs_frontend_save_cb callback, void* private_data) {
    m_saveCallbacks.erase(
        std::remove_if(m_saveCallbacks.begin(), m_saveCallbacks.end(),
            [callback, private_data](const SaveCallback& cb) {
                return cb.callback == callback && cb.private_data == private_data;
            }),
        m_saveCallbacks.end()
    );
}

void HeadlessFrontend::obs_frontend_add_preload_callback(obs_frontend_save_cb callback, void* private_data) {
    SaveCallback cb = {callback, private_data};
    m_preloadCallbacks.push_back(cb);
}

void HeadlessFrontend::obs_frontend_remove_preload_callback(obs_frontend_save_cb callback, void* private_data) {
    m_preloadCallbacks.erase(
        std::remove_if(m_preloadCallbacks.begin(), m_preloadCallbacks.end(),
            [callback, private_data](const SaveCallback& cb) {
                return cb.callback == callback && cb.private_data == private_data;
            }),
        m_preloadCallbacks.end()
    );
}

// Translation - no-op
void HeadlessFrontend::obs_frontend_push_ui_translation(obs_frontend_translate_ui_cb translate) { (void)translate; }
void HeadlessFrontend::obs_frontend_pop_ui_translation() {}

// Streaming service
obs_service_t* HeadlessFrontend::obs_frontend_get_streaming_service() { 
    if (m_streamingService) obs_service_get_ref(m_streamingService);
    return m_streamingService; 
}

void HeadlessFrontend::obs_frontend_set_streaming_service(obs_service_t* service) {
    if (m_streamingService) obs_service_release(m_streamingService);
    m_streamingService = service;
    if (m_streamingService) obs_service_get_ref(m_streamingService);
}

void HeadlessFrontend::obs_frontend_save_streaming_service() {}

// Studio mode
bool HeadlessFrontend::obs_frontend_preview_program_mode_active() { return m_studioMode; }
void HeadlessFrontend::obs_frontend_set_preview_program_mode(bool enable) { 
    m_studioMode = enable;
    on_event(enable ? OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED : OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED);
}
void HeadlessFrontend::obs_frontend_preview_program_trigger_transition() {}

bool HeadlessFrontend::obs_frontend_preview_enabled() { return m_previewEnabled; }
void HeadlessFrontend::obs_frontend_set_preview_enabled(bool enable) { m_previewEnabled = enable; }

obs_source_t* HeadlessFrontend::obs_frontend_get_current_preview_scene() {
    if (m_previewScene) obs_source_get_ref(m_previewScene);
    return m_previewScene;
}

void HeadlessFrontend::obs_frontend_set_current_preview_scene(obs_source_t* scene) {
    if (m_previewScene) obs_source_release(m_previewScene);
    m_previewScene = scene;
    if (m_previewScene) obs_source_get_ref(m_previewScene);
    on_event(OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED);
}

// Internal callbacks
void HeadlessFrontend::on_load(obs_data_t* settings) {
    for (auto& cb : m_saveCallbacks) {
        cb.callback(settings, false, cb.private_data);
    }
}

void HeadlessFrontend::on_preload(obs_data_t* settings) {
    for (auto& cb : m_preloadCallbacks) {
        cb.callback(settings, false, cb.private_data);
    }
}

void HeadlessFrontend::on_save(obs_data_t* settings) {
    for (auto& cb : m_saveCallbacks) {
        cb.callback(settings, true, cb.private_data);
    }
}

void HeadlessFrontend::on_event(enum obs_frontend_event event) {
    for (auto& cb : m_eventCallbacks) {
        cb.callback(event, cb.private_data);
    }
}

// Screenshots - no-op
void HeadlessFrontend::obs_frontend_take_screenshot() {}
void HeadlessFrontend::obs_frontend_take_source_screenshot(obs_source_t* source) { (void)source; }

// Virtual cam
obs_output_t* HeadlessFrontend::obs_frontend_get_virtualcam_output() { return nullptr; }
void HeadlessFrontend::obs_frontend_start_virtualcam() {
    m_virtualCamActive = true;
    on_event(OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED);
}
void HeadlessFrontend::obs_frontend_stop_virtualcam() {
    m_virtualCamActive = false;
    on_event(OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED);
}
bool HeadlessFrontend::obs_frontend_virtualcam_active() { return m_virtualCamActive; }

void HeadlessFrontend::obs_frontend_reset_video() {}

// Source windows - no-op
void HeadlessFrontend::obs_frontend_open_source_properties(obs_source_t* source) { (void)source; }
void HeadlessFrontend::obs_frontend_open_source_filters(obs_source_t* source) { (void)source; }
void HeadlessFrontend::obs_frontend_open_source_interaction(obs_source_t* source) { (void)source; }
void HeadlessFrontend::obs_frontend_open_sceneitem_edit_transform(obs_sceneitem_t* item) { (void)item; }

char* HeadlessFrontend::obs_frontend_get_current_record_output_path() {
    return bstrdup(m_recordOutputPath.c_str());
}

const char* HeadlessFrontend::obs_frontend_get_locale_string(const char* string) {
    return string;  // Return as-is (no translation)
}

bool HeadlessFrontend::obs_frontend_is_theme_dark() { return true; }

char* HeadlessFrontend::obs_frontend_get_last_recording() { return bstrdup(""); }
char* HeadlessFrontend::obs_frontend_get_last_screenshot() { return bstrdup(""); }
char* HeadlessFrontend::obs_frontend_get_last_replay() { return bstrdup(""); }

void HeadlessFrontend::obs_frontend_add_undo_redo_action(const char* name, const undo_redo_cb undo,
                                                          const undo_redo_cb redo, const char* undo_data,
                                                          const char* redo_data, bool repeatable) {
    (void)name; (void)undo; (void)redo; (void)undo_data; (void)redo_data; (void)repeatable;
}

// Canvas management
obs_canvas_t* HeadlessFrontend::obs_frontend_add_canvas(const char* name, obs_video_info* ovi, int flags) {
    (void)name; (void)ovi; (void)flags;
    return nullptr;
}

bool HeadlessFrontend::obs_frontend_remove_canvas(obs_canvas_t* canvas) {
    (void)canvas;
    return false;
}

void HeadlessFrontend::obs_frontend_get_canvases(obs_frontend_canvas_list* canvas_list) {
    (void)canvas_list;
}

} // namespace streamlumo
