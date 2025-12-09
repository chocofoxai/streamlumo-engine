// BrowserManager.mm – CEF lifecycle and browser management
#include "BrowserManager.h"
#include "HelperApp.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_closure_task.h"
#import <Foundation/Foundation.h>

// Early subprocess execution - called from main() for CEF child processes.
extern "C" int RunCefSubprocess(int argc, char** argv) {
    CefMainArgs mainArgs(argc, argv);
    CefRefPtr<HelperApp> app(new HelperApp());
    return CefExecuteProcess(mainArgs, app, nullptr);
}

BrowserManager& BrowserManager::Instance() {
    static BrowserManager instance;
    return instance;
}

bool BrowserManager::InitCef(int argc, char** argv) {
    if (cefInitialized_) {
        return true;
    }
    CefMainArgs mainArgs(argc, argv);
    CefRefPtr<HelperApp> app(new HelperApp());

    // Note: CefExecuteProcess for subprocesses is handled in main() via RunCefSubprocess().

    CefSettings settings;
    settings.windowless_rendering_enabled = true;
    settings.no_sandbox = true;
    settings.external_message_pump = true;  // CEF calls OnScheduleMessagePumpWork
    settings.multi_threaded_message_loop = false;
    // Disable GPU process to avoid sandbox/crash issues in headless helper.
    settings.command_line_args_disabled = false;
    CefString(&settings.log_file).FromASCII("/tmp/cef_helper.log");
    settings.log_severity = LOGSEVERITY_INFO;

    // Locate framework bundle inside the app bundle.
    NSBundle* mainBundle = [NSBundle mainBundle];
    
    // Set browser_subprocess_path to the base helper bundle in Frameworks.
    // CEF will find variants like (Renderer), (GPU) etc. in the same directory.
    NSString* baseHelperPath = [[mainBundle privateFrameworksPath]
        stringByAppendingPathComponent:@"streamlumo-browser-helper.app/Contents/MacOS/streamlumo-browser-helper"];
    CefString(&settings.browser_subprocess_path).FromString([baseHelperPath UTF8String]);
    
    // Set main_bundle_path to the main app bundle
    CefString(&settings.main_bundle_path).FromString([[mainBundle bundlePath] UTF8String]);

    NSString* frameworkPath = [[mainBundle privateFrameworksPath]
        stringByAppendingPathComponent:@"Chromium Embedded Framework.framework"];
    CefString(&settings.framework_dir_path).FromString([frameworkPath UTF8String]);

    NSString* resourcesPath = [frameworkPath stringByAppendingPathComponent:@"Resources"];
    CefString(&settings.resources_dir_path).FromString([resourcesPath UTF8String]);
    CefString(&settings.locales_dir_path).FromString([[resourcesPath stringByAppendingPathComponent:@"locales"] UTF8String]);

    // Use a cache path to satisfy CEF root_cache_path warning.
    NSString* cachePath = [NSTemporaryDirectory() stringByAppendingPathComponent:@"cef_cache"];
    CefString(&settings.root_cache_path).FromString([cachePath UTF8String]);

    if (!CefInitialize(mainArgs, settings, app, nullptr)) {
        NSLog(@"[browser-helper] CefInitialize failed");
        return false;
    }
    cefInitialized_ = true;
    NSLog(@"[browser-helper] CEF initialized successfully");
    return true;
}

void BrowserManager::ShutdownCef() {
    if (!cefInitialized_) {
        return;
    }
    
    NSLog(@"[browser-helper] ShutdownCef - starting CEF shutdown sequence");
    
    // Step 1: Request all browsers to close
    {
        std::lock_guard<std::mutex> lock(mutex_);
        NSLog(@"[browser-helper] ShutdownCef - closing %lu browsers", browsers_.size());
        for (auto& kv : browsers_) {
            if (auto browser = kv.second->GetBrowser()) {
                browser->GetHost()->CloseBrowser(true);  // force=true to speed up
            }
        }
    }
    
    // Step 2: Pump message loop to process close events
    // Give browsers time to close cleanly
    for (int i = 0; i < 20; ++i) {
        CefDoMessageLoopWork();
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
    
    // Step 3: Clear our references
    {
        std::lock_guard<std::mutex> lock(mutex_);
        browsers_.clear();
    }
    
    // Step 4: Final message pump to let CEF finish cleanup
    for (int i = 0; i < 10; ++i) {
        CefDoMessageLoopWork();
    }
    
    // Step 5: Shutdown CEF
    NSLog(@"[browser-helper] ShutdownCef - calling CefShutdown()");
    CefShutdown();
    cefInitialized_ = false;
    NSLog(@"[browser-helper] CEF shutdown complete");
}

void BrowserManager::SetFrameCallback(FrameCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    frameCallback_ = cb;
}

bool BrowserManager::CreateBrowser(const std::string& id,
                                    const std::string& url,
                                    int width,
                                    int height,
                                    int fps) {
    // CEF requires browser creation on the UI thread.
    // Use dispatch_sync to the main queue (which is our CEF UI thread).
    __block bool result = false;
    
    dispatch_block_t createBlock = ^{
        std::lock_guard<std::mutex> lock(mutex_);
        if (browsers_.count(id)) {
            NSLog(@"[browser-helper] browser already exists id=%s", id.c_str());
            result = false;
            return;
        }
        int targetFps = fps > 0 ? fps : 60;
        CefRefPtr<BrowserClient> client(new BrowserClient(id, width, height, frameCallback_));
        CefWindowInfo windowInfo;
        windowInfo.bounds.width = width;
        windowInfo.bounds.height = height;
        windowInfo.windowless_rendering_enabled = true;
        windowInfo.SetAsWindowless(0);
        CefBrowserSettings browserSettings;
        browserSettings.windowless_frame_rate = targetFps;
        // Enable JavaScript (required for video players)
        browserSettings.javascript = STATE_ENABLED;
        // Enable loading images
        browserSettings.image_loading = STATE_ENABLED;
        // Enable local storage (required by many video players)
        browserSettings.local_storage = STATE_ENABLED;
        browserSettings.databases = STATE_ENABLED;
        // Enable WebGL for video rendering
        browserSettings.webgl = STATE_ENABLED;
        // Use synchronous creation like OBS does
        CefRefPtr<CefBrowser> browser = CefBrowserHost::CreateBrowserSync(
            windowInfo, client, url, browserSettings, nullptr, nullptr);
        if (browser) {
            browsers_[id] = client;
            browser->GetHost()->SetWindowlessFrameRate(targetFps);
            // Trigger initial paint like OBS does
            browser->GetHost()->WasResized();
            browser->GetHost()->WasHidden(false);
            browser->GetHost()->Invalidate(PET_VIEW);
            NSLog(@"[browser-helper] CreateBrowserSync id=%s url=%s %dx%d", id.c_str(), url.c_str(), width, height);
            result = true;
        } else {
            NSLog(@"[browser-helper] CreateBrowserSync FAILED id=%s", id.c_str());
            result = false;
        }
    };
    
    if ([NSThread isMainThread]) {
        createBlock();
    } else {
        dispatch_sync(dispatch_get_main_queue(), createBlock);
    }
    return result;
}

bool BrowserManager::NavigateBrowser(const std::string& id, const std::string& url) {
    NSLog(@"[browser-helper] NavigateBrowser called id=%s url=%s", id.c_str(), url.c_str());
    
    // Capture browser reference while holding lock
    CefRefPtr<CefBrowser> browser;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = browsers_.find(id);
        if (it == browsers_.end()) {
            NSLog(@"[browser-helper] NavigateBrowser failed - browser not found id=%s", id.c_str());
            return false;
        }
        browser = it->second->GetBrowser();
    }
    
    if (!browser) {
        NSLog(@"[browser-helper] NavigateBrowser failed - no browser instance for id=%s", id.c_str());
        return false;
    }
    
    // Navigate on main thread (CEF message pump runs on main thread)
    NSString *urlStr = [NSString stringWithUTF8String:url.c_str()];
    NSString *idStr = [NSString stringWithUTF8String:id.c_str()];
    dispatch_async(dispatch_get_main_queue(), ^{
        CefRefPtr<CefFrame> frame = browser->GetMainFrame();
        if (frame) {
            frame->LoadURL([urlStr UTF8String]);
            NSLog(@"[browser-helper] NavigateBrowser completed id=%@ url=%@", idStr, urlStr);
        } else {
            NSLog(@"[browser-helper] NavigateBrowser failed - no main frame id=%@", idStr);
        }
    });
    
    return true;
}

bool BrowserManager::ResizeBrowser(const std::string& id, int width, int height) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = browsers_.find(id);
    if (it == browsers_.end()) {
        return false;
    }
    it->second->SetSize(width, height);
    NSLog(@"[browser-helper] ResizeBrowser id=%s %dx%d", id.c_str(), width, height);
    return true;
}

bool BrowserManager::CloseBrowser(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = browsers_.find(id);
    if (it == browsers_.end()) {
        return false;
    }
    if (auto browser = it->second->GetBrowser()) {
        browser->GetHost()->CloseBrowser(false);
    }
    browsers_.erase(it);
    NSLog(@"[browser-helper] CloseBrowser id=%s", id.c_str());
    return true;
}

void BrowserManager::InvalidateAllBrowsers() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& kv : browsers_) {
        if (auto browser = kv.second->GetBrowser()) {
            browser->GetHost()->Invalidate(PET_VIEW);
        }
    }
}

bool BrowserManager::ExecuteJavaScript(const std::string& id, const std::string& script) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = browsers_.find(id);
    if (it == browsers_.end()) {
        return false;
    }
    if (auto browser = it->second->GetBrowser()) {
        CefRefPtr<CefFrame> frame = browser->GetMainFrame();
        if (frame) {
            frame->ExecuteJavaScript(script, frame->GetURL(), 0);
            NSLog(@"[browser-helper] ExecuteJavaScript id=%s script=%s", id.c_str(), script.substr(0, 50).c_str());
            return true;
        }
    }
    return false;
}

// Send a simulated mouse click to trigger video playback
bool BrowserManager::SendMouseClick(const std::string& id, int x, int y) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = browsers_.find(id);
    if (it == browsers_.end()) {
        return false;
    }
    if (auto browser = it->second->GetBrowser()) {
        CefMouseEvent mouse_event;
        mouse_event.x = x;
        mouse_event.y = y;
        mouse_event.modifiers = 0;
        
        // Send mouse down
        browser->GetHost()->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);
        // Send mouse up
        browser->GetHost()->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);
        
        NSLog(@"[browser-helper] SendMouseClick id=%s at (%d, %d)", id.c_str(), x, y);
        return true;
    }
    return false;
}

void BrowserManager::DoMessageLoopWork() {
    if (cefInitialized_) {
        CefDoMessageLoopWork();
    }
}
