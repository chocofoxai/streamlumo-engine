// BrowserClient.h – CefClient with render handler for off-screen rendering
#pragma once

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_handler.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

// Callback invoked when a frame is painted (BGRA buffer).
using FrameCallback = std::function<void(const std::string& browserId,
                                          const void* buffer,
                                          int width,
                                          int height)>;

class BrowserClient : public CefClient,
                      public CefLifeSpanHandler,
                      public CefRenderHandler {
public:
    explicit BrowserClient(const std::string& browserId,
                           int width,
                           int height,
                           FrameCallback onFrame);

    // CefClient
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }

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

    IMPLEMENT_REFCOUNTING(BrowserClient);
    DISALLOW_COPY_AND_ASSIGN(BrowserClient);
};
