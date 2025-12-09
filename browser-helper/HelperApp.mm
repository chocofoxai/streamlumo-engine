// HelperApp.mm – CefApp implementation
#include "HelperApp.h"
#include "BrowserManager.h"
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>

void HelperApp::OnBeforeCommandLineProcessing(const CefString& /*process_type*/,
                                               CefRefPtr<CefCommandLine> command_line) {
    // ========== RENDERING CONFIGURATION ==========
    // Use software rendering only - simplest and most stable
    command_line->AppendSwitch("disable-gpu");
    command_line->AppendSwitch("disable-gpu-compositing");
    
    // ========== SECURITY & SANDBOX ==========
    // Disable keychain access to prevent macOS password prompts
    command_line->AppendSwitch("use-mock-keychain");
    
    // ========== NETWORK & STORAGE ==========
    // Keep background networking enabled so cert fetch/OCSP can work.
    command_line->AppendSwitch("disable-sync");
    command_line->AppendSwitch("disable-default-apps");
    command_line->AppendSwitch("no-first-run");
    
    // Ignore certificate errors for development
    command_line->AppendSwitch("ignore-certificate-errors");
    
    // Allow mixed content (some streams use HTTP)
    command_line->AppendSwitch("allow-running-insecure-content");
    
    // Disable web security for cross-origin media (like OBS browser source)
    command_line->AppendSwitch("disable-web-security");
    
    // ========== AUTOPLAY & MEDIA SETTINGS (Critical for YouTube/video playback) ==========
    // Enable autoplay without user gesture - essential for OBS browser sources
    command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
    
    // Allow fake user gestures for media playback
    command_line->AppendSwitch("disable-gesture-requirement-for-media-playback");
    
    // Disable features that block media autoplay (keep minimal list)
    command_line->AppendSwitchWithValue("disable-features",
        "PreloadMediaEngagementData,MediaEngagementBypassAutoplayPolicies");
    
    // ========== DIAGNOSTICS ==========
    // Write Chrome-style net log for SSL/navigation debugging
    command_line->AppendSwitchWithValue("log-net-log", "/tmp/cef_net_log.json");
    command_line->AppendSwitch("enable-logging");
    command_line->AppendSwitchWithValue("v", "1");
    
    // Mute audio by default (audio is handled separately via OBS audio routing)
    // Uncomment if audio should be muted: command_line->AppendSwitch("mute-audio");
}

void HelperApp::OnContextInitialized() {
    NSLog(@"[browser-helper] CEF context initialized");
}

void HelperApp::OnScheduleMessagePumpWork(int64_t delay_ms) {
    // CEF is requesting a message loop pump after delay_ms.
    // Dispatch to main queue to pump CEF.
    static int callCount = 0;
    if (++callCount <= 5 || callCount % 100 == 0) {
        NSLog(@"[browser-helper] OnScheduleMessagePumpWork delay=%lld (call #%d)", delay_ms, callCount);
    }
    
    if (delay_ms < 0)
        delay_ms = 0;
    else if (delay_ms > 16)
        delay_ms = 16;  // Cap near 60 Hz for smoother video

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay_ms * NSEC_PER_MSEC),
                   dispatch_get_main_queue(), ^{
        BrowserManager::Instance().DoMessageLoopWork();
    });
}
