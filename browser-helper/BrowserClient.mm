// BrowserClient.mm – CefClient / RenderHandler implementation
#include "BrowserClient.h"
#import <Foundation/Foundation.h>

BrowserClient::BrowserClient(const std::string& browserId,
                             int width,
                             int height,
                             FrameCallback onFrame)
    : browserId_(browserId), width_(width), height_(height), onFrame_(onFrame) {}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        browser_ = browser;
    }
    NSLog(@"[browser-helper] browser created id=%s", browserId_.c_str());
    // Trigger initial paint by notifying about size.
    browser->GetHost()->WasResized();
    browser->GetHost()->Invalidate(PET_VIEW);
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    std::lock_guard<std::mutex> lock(mutex_);
    browser_ = nullptr;
    NSLog(@"[browser-helper] browser closed id=%s", browserId_.c_str());
}

void BrowserClient::GetViewRect(CefRefPtr<CefBrowser> /*browser*/, CefRect& rect) {
    std::lock_guard<std::mutex> lock(mutex_);
    rect.Set(0, 0, width_, height_);
    static int callCount = 0;
    if (++callCount <= 5) {
        NSLog(@"[browser-helper] GetViewRect id=%s returning %dx%d", browserId_.c_str(), width_, height_);
    }
}

bool BrowserClient::GetScreenInfo(CefRefPtr<CefBrowser> /*browser*/, CefScreenInfo& screen_info) {
    std::lock_guard<std::mutex> lock(mutex_);
    screen_info.device_scale_factor = 1.0f;
    screen_info.depth = 32;
    screen_info.depth_per_component = 8;
    screen_info.is_monochrome = false;
    screen_info.rect = CefRect(0, 0, width_, height_);
    screen_info.available_rect = screen_info.rect;
    return true;
}

void BrowserClient::OnPaint(CefRefPtr<CefBrowser> /*browser*/,
                            PaintElementType type,
                            const RectList& /*dirtyRects*/,
                            const void* buffer,
                            int width,
                            int height) {
    if (type != PET_VIEW) {
        return;
    }
    NSLog(@"[browser-helper] OnPaint id=%s %dx%d hasCallback=%d", browserId_.c_str(), width, height, (onFrame_ != nullptr));
    if (onFrame_) {
        onFrame_(browserId_, buffer, width, height);
    }
}

void BrowserClient::SetSize(int w, int h) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        width_ = w;
        height_ = h;
    }
    if (browser_) {
        browser_->GetHost()->WasResized();
    }
}

CefRefPtr<CefBrowser> BrowserClient::GetBrowser() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return browser_;
}
