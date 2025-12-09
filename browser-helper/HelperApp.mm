// HelperApp.mm – CefApp implementation
#include "HelperApp.h"
#include "BrowserManager.h"
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>

void HelperApp::OnBeforeCommandLineProcessing(const CefString& /*process_type*/,
                                               CefRefPtr<CefCommandLine> command_line) {
    // Disable GPU acceleration to run in headless / sandboxless helper.
    command_line->AppendSwitch("disable-gpu");
    command_line->AppendSwitch("disable-gpu-compositing");
    command_line->AppendSwitch("disable-software-rasterizer");
    command_line->AppendSwitch("in-process-gpu");
    
    // Disable keychain access (prevents macOS password prompts).
    command_line->AppendSwitch("use-mock-keychain");
    
    // Disable features that require persistent storage or network config.
    command_line->AppendSwitch("disable-background-networking");
    command_line->AppendSwitch("disable-sync");
    command_line->AppendSwitch("disable-default-apps");
    command_line->AppendSwitch("no-first-run");
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
    else if (delay_ms > 33)
        delay_ms = 33;  // Cap at ~30 Hz for responsiveness

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay_ms * NSEC_PER_MSEC),
                   dispatch_get_main_queue(), ^{
        BrowserManager::Instance().DoMessageLoopWork();
    });
}
