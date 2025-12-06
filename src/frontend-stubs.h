// streamlumo-engine/src/frontend-stubs.h
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo
//
// Headless frontend API stubs for obs-websocket plugin support

#pragma once

#include <obs-frontend-internal.hpp>
#include <vector>
#include <string>

namespace streamlumo {

class HeadlessFrontend : public obs_frontend_callbacks {
public:
    HeadlessFrontend();
    ~HeadlessFrontend() override;
    
    static void install();
    static void uninstall();
    static HeadlessFrontend* instance();
    
    // Signal that OBS has finished loading (call after all initialization is complete)
    void signalFinishedLoading();
    
    // Set profile path for config
    void setProfilePath(const std::string& path) { m_profilePath = path; }
    void setRecordOutputPath(const std::string& path) { m_recordOutputPath = path; }
    
    // GUI-related (return nullptr/no-op for headless)
    void* obs_frontend_get_main_window() override;
    void* obs_frontend_get_main_window_handle() override;
    void* obs_frontend_get_system_tray() override;
    
    // Scene management
    void obs_frontend_get_scenes(struct obs_frontend_source_list* sources) override;
    obs_source_t* obs_frontend_get_current_scene() override;
    void obs_frontend_set_current_scene(obs_source_t* scene) override;
    
    // Transitions
    void obs_frontend_get_transitions(struct obs_frontend_source_list* sources) override;
    obs_source_t* obs_frontend_get_current_transition() override;
    void obs_frontend_set_current_transition(obs_source_t* transition) override;
    int obs_frontend_get_transition_duration() override;
    void obs_frontend_set_transition_duration(int duration) override;
    void obs_frontend_release_tbar() override;
    int obs_frontend_get_tbar_position() override;
    void obs_frontend_set_tbar_position(int position) override;
    
    // Scene collections
    void obs_frontend_get_scene_collections(std::vector<std::string>& strings) override;
    char* obs_frontend_get_current_scene_collection() override;
    void obs_frontend_set_current_scene_collection(const char* collection) override;
    bool obs_frontend_add_scene_collection(const char* name) override;
    
    // Profiles
    void obs_frontend_get_profiles(std::vector<std::string>& strings) override;
    char* obs_frontend_get_current_profile() override;
    char* obs_frontend_get_current_profile_path() override;
    void obs_frontend_set_current_profile(const char* profile) override;
    void obs_frontend_create_profile(const char* name) override;
    void obs_frontend_duplicate_profile(const char* name) override;
    void obs_frontend_delete_profile(const char* profile) override;
    
    // Streaming
    void obs_frontend_streaming_start() override;
    void obs_frontend_streaming_stop() override;
    bool obs_frontend_streaming_active() override;
    
    // Recording
    void obs_frontend_recording_start() override;
    void obs_frontend_recording_stop() override;
    bool obs_frontend_recording_active() override;
    void obs_frontend_recording_pause(bool pause) override;
    bool obs_frontend_recording_paused() override;
    bool obs_frontend_recording_split_file() override;
    bool obs_frontend_recording_add_chapter(const char* name) override;
    
    // Replay buffer
    void obs_frontend_replay_buffer_start() override;
    void obs_frontend_replay_buffer_save() override;
    void obs_frontend_replay_buffer_stop() override;
    bool obs_frontend_replay_buffer_active() override;
    
    // Tools menu
    void* obs_frontend_add_tools_menu_qaction(const char* name) override;
    void obs_frontend_add_tools_menu_item(const char* name, obs_frontend_cb callback, void* private_data) override;
    
    // Docks
    bool obs_frontend_add_dock_by_id(const char* id, const char* title, void* widget) override;
    void obs_frontend_remove_dock(const char* id) override;
    bool obs_frontend_add_custom_qdock(const char* id, void* dock) override;
    
    // Event callbacks
    void obs_frontend_add_event_callback(obs_frontend_event_cb callback, void* private_data) override;
    void obs_frontend_remove_event_callback(obs_frontend_event_cb callback, void* private_data) override;
    
    // Outputs
    obs_output_t* obs_frontend_get_streaming_output() override;
    obs_output_t* obs_frontend_get_recording_output() override;
    obs_output_t* obs_frontend_get_replay_buffer_output() override;
    
    // Config
    config_t* obs_frontend_get_profile_config() override;
    config_t* obs_frontend_get_app_config() override;
    config_t* obs_frontend_get_user_config() override;
    
    // Projector
    void obs_frontend_open_projector(const char* type, int monitor, const char* geometry, const char* name) override;
    
    // Save
    void obs_frontend_save() override;
    void obs_frontend_defer_save_begin() override;
    void obs_frontend_defer_save_end() override;
    void obs_frontend_add_save_callback(obs_frontend_save_cb callback, void* private_data) override;
    void obs_frontend_remove_save_callback(obs_frontend_save_cb callback, void* private_data) override;
    void obs_frontend_add_preload_callback(obs_frontend_save_cb callback, void* private_data) override;
    void obs_frontend_remove_preload_callback(obs_frontend_save_cb callback, void* private_data) override;
    
    // Translation
    void obs_frontend_push_ui_translation(obs_frontend_translate_ui_cb translate) override;
    void obs_frontend_pop_ui_translation() override;
    
    // Streaming service
    obs_service_t* obs_frontend_get_streaming_service() override;
    void obs_frontend_set_streaming_service(obs_service_t* service) override;
    void obs_frontend_save_streaming_service() override;
    
    // Studio mode
    bool obs_frontend_preview_program_mode_active() override;
    void obs_frontend_set_preview_program_mode(bool enable) override;
    void obs_frontend_preview_program_trigger_transition() override;
    bool obs_frontend_preview_enabled() override;
    void obs_frontend_set_preview_enabled(bool enable) override;
    obs_source_t* obs_frontend_get_current_preview_scene() override;
    void obs_frontend_set_current_preview_scene(obs_source_t* scene) override;
    
    // Internal callbacks
    void on_load(obs_data_t* settings) override;
    void on_preload(obs_data_t* settings) override;
    void on_save(obs_data_t* settings) override;
    void on_event(enum obs_frontend_event event) override;
    
    // Screenshots
    void obs_frontend_take_screenshot() override;
    void obs_frontend_take_source_screenshot(obs_source_t* source) override;
    
    // Virtual cam
    obs_output_t* obs_frontend_get_virtualcam_output() override;
    void obs_frontend_start_virtualcam() override;
    void obs_frontend_stop_virtualcam() override;
    bool obs_frontend_virtualcam_active() override;
    
    void obs_frontend_reset_video() override;
    
    // Source windows
    void obs_frontend_open_source_properties(obs_source_t* source) override;
    void obs_frontend_open_source_filters(obs_source_t* source) override;
    void obs_frontend_open_source_interaction(obs_source_t* source) override;
    void obs_frontend_open_sceneitem_edit_transform(obs_sceneitem_t* item) override;
    
    char* obs_frontend_get_current_record_output_path() override;
    const char* obs_frontend_get_locale_string(const char* string) override;
    
    bool obs_frontend_is_theme_dark() override;
    
    char* obs_frontend_get_last_recording() override;
    char* obs_frontend_get_last_screenshot() override;
    char* obs_frontend_get_last_replay() override;
    
    void obs_frontend_add_undo_redo_action(const char* name, const undo_redo_cb undo,
                                           const undo_redo_cb redo, const char* undo_data,
                                           const char* redo_data, bool repeatable) override;
    
    // Canvas management
    obs_canvas_t* obs_frontend_add_canvas(const char* name, obs_video_info* ovi, int flags) override;
    bool obs_frontend_remove_canvas(obs_canvas_t* canvas) override;
    void obs_frontend_get_canvases(obs_frontend_canvas_list* canvas_list) override;
    
private:
    struct EventCallback {
        obs_frontend_event_cb callback;
        void* private_data;
    };
    
    struct SaveCallback {
        obs_frontend_save_cb callback;
        void* private_data;
    };
    
    std::vector<EventCallback> m_eventCallbacks;
    std::vector<SaveCallback> m_saveCallbacks;
    std::vector<SaveCallback> m_preloadCallbacks;
    
    config_t* m_profileConfig = nullptr;
    config_t* m_appConfig = nullptr;
    config_t* m_userConfig = nullptr;
    
    obs_source_t* m_currentScene = nullptr;
    obs_source_t* m_previewScene = nullptr;
    obs_source_t* m_currentTransition = nullptr;
    obs_service_t* m_streamingService = nullptr;
    
    std::string m_profilePath;
    std::string m_recordOutputPath;
    
    int m_transitionDuration = 300;
    bool m_streamingActive = false;
    bool m_recordingActive = false;
    bool m_recordingPaused = false;
    bool m_replayBufferActive = false;
    bool m_studioMode = false;
    bool m_previewEnabled = true;
    bool m_virtualCamActive = false;
};

} // namespace streamlumo
