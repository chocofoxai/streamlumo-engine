// BrowserManager.h – Manages CEF browser instances keyed by ID
#pragma once

#include "BrowserClient.h"
#include <string>
#include <unordered_map>
#include <mutex>

class BrowserManager {
public:
    static BrowserManager& Instance();

    // Initialize CEF (call from main thread before any browsers).
    bool InitCef(int argc, char** argv);
    // Shut down CEF (call on exit).
    void ShutdownCef();

    // Create a browser with the given id/url/size.
    bool CreateBrowser(const std::string& id, const std::string& url, int width, int height);
    // Resize an existing browser.
    bool ResizeBrowser(const std::string& id, int width, int height);
    // Close and remove a browser.
    bool CloseBrowser(const std::string& id);

    // Set the callback invoked when a frame is painted.
    void SetFrameCallback(FrameCallback cb);

    // Pump CEF message loop (call periodically from run-loop).
    void DoMessageLoopWork();

private:
    BrowserManager() = default;
    ~BrowserManager() = default;

    std::mutex mutex_;
    std::unordered_map<std::string, CefRefPtr<BrowserClient>> browsers_;
    FrameCallback frameCallback_;
    bool cefInitialized_ = false;
};
