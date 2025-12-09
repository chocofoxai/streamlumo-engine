// BrowserClient.h – CefClient with render handler for off-screen rendering
#pragma once

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_handler.h"
#include "include/cef_load_handler.h"
#include "BrowserShmWriter.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <memory>

// Callback invoked when a frame is painted (BGRA buffer).
using FrameCallback = std::function<void(const std::string& browserId,
                                          const void* buffer,
                                          int width,
                                          int height)>;

class BrowserClient : public CefClient,
                      public CefLifeSpanHandler,
                      public CefRenderHandler,
                      public CefLoadHandler {
public:
    explicit BrowserClient(const std::string& browserId,
                           int width,
                           int height,
                           FrameCallback onFrame);

    // CefClient
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

    // CefLifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    // CefRenderHandler
    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
    bool GetScreenInfo(CefRefPtr<CefBrowser> browser, CefScreenInfo& screen_info) override;
    void OnPaint(CefRefPtr<CefBrowser> browser,
                 PaintElementType type,
                 const RectList& dirtyRects,
                 const void* buffer,
                 int width,
                 int height) override;
    
    // CefLoadHandler
    void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override;

    // Accessors
    void SetSize(int w, int h);
    CefRefPtr<CefBrowser> GetBrowser() const;
    const std::string& GetBrowserId() const { return browserId_; }

private:
    std::string browserId_;
    int width_;
    int height_;
    FrameCallback onFrame_;
    CefRefPtr<CefBrowser> browser_;
    mutable std::mutex mutex_;
    
    // Shared memory writer for zero-copy frame transport
    std::unique_ptr<browser_bridge::BrowserShmWriter> shmWriter_;
    bool useShmTransport_ = true;

    IMPLEMENT_REFCOUNTING(BrowserClient);
    DISALLOW_COPY_AND_ASSIGN(BrowserClient);
};
