// BrowserManager.mm – CEF lifecycle and browser management
#include "BrowserManager.h"
#include "HelperApp.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/wrapper/cef_helpers.h"
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
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& kv : browsers_) {
            if (auto browser = kv.second->GetBrowser()) {
                browser->GetHost()->CloseBrowser(true);
            }
        }
        browsers_.clear();
    }
    CefShutdown();
    cefInitialized_ = false;
    NSLog(@"[browser-helper] CEF shut down");
}

void BrowserManager::SetFrameCallback(FrameCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    frameCallback_ = cb;
}

bool BrowserManager::CreateBrowser(const std::string& id,
                                    const std::string& url,
                                    int width,
                                    int height) {
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
        CefRefPtr<BrowserClient> client(new BrowserClient(id, width, height, frameCallback_));
        CefWindowInfo windowInfo;
        windowInfo.bounds.width = width;
        windowInfo.bounds.height = height;
        windowInfo.windowless_rendering_enabled = true;
        windowInfo.SetAsWindowless(0);
        CefBrowserSettings browserSettings;
        browserSettings.windowless_frame_rate = 60;
        // Use synchronous creation like OBS does
        CefRefPtr<CefBrowser> browser = CefBrowserHost::CreateBrowserSync(
            windowInfo, client, url, browserSettings, nullptr, nullptr);
        if (browser) {
            browsers_[id] = client;
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

void BrowserManager::DoMessageLoopWork() {
    if (cefInitialized_) {
        CefDoMessageLoopWork();
    }
}
