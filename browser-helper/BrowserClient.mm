// BrowserClient.mm – CefClient / RenderHandler implementation
#include "BrowserClient.h"
#import <Foundation/Foundation.h>

BrowserClient::BrowserClient(const std::string& browserId,
                             int width,
                             int height,
                             FrameCallback onFrame)
    : browserId_(browserId), width_(width), height_(height), onFrame_(onFrame) {
    
    NSLog(@"[browser-helper] ===== BrowserClient constructor START for id=%s (%dx%d) =====", 
          browserId.c_str(), width, height);
    
    // Initialize SHM writer for zero-copy frame transport
    shmWriter_ = std::make_unique<browser_bridge::BrowserShmWriter>(browserId);
    NSLog(@"[browser-helper] Created BrowserShmWriter instance for %s", browserId.c_str());
    
    if (shmWriter_->create(width, height)) {
        useShmTransport_ = true;
        NSLog(@"[browser-helper] ✓ SHM transport ENABLED for %s (%dx%d)", 
              browserId.c_str(), width, height);
    } else {
        useShmTransport_ = false;
        NSLog(@"[browser-helper] ✗ SHM transport FAILED, falling back to IPC for %s", 
              browserId.c_str());
    }
    
    NSLog(@"[browser-helper] ===== BrowserClient constructor END for id=%s (useShmTransport=%d) =====", 
          browserId.c_str(), useShmTransport_);
}

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
    
    // Only log periodically to avoid performance impact
    static int paintCount = 0;
    if (++paintCount % 300 == 1) {
        NSLog(@"[browser-helper] OnPaint id=%s %dx%d (frame #%d) shm=%s", 
              browserId_.c_str(), width, height, paintCount,
              useShmTransport_ ? "yes" : "no");
    }
    
    // Prefer SHM transport (zero-copy) if available
    if (useShmTransport_ && shmWriter_ && shmWriter_->isCreated()) {
        // Write frame directly to shared memory - zero copy to OBS plugin
        if (!shmWriter_->writeFrame(buffer, width, height)) {
            // Frame dropped (consumer is slow) - this is OK, we just skip
        }
        // Don't call IPC callback when using SHM to avoid double processing
        return;
    }
    
    // Fallback: use legacy IPC callback if SHM not available
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
    
    // Recreate SHM if size changed
    if (shmWriter_) {
        shmWriter_->destroy();
        if (shmWriter_->create(w, h)) {
            useShmTransport_ = true;
            NSLog(@"[browser-helper] SHM recreated for resize %dx%d", w, h);
        } else {
            useShmTransport_ = false;
        }
    }
    
    if (browser_) {
        browser_->GetHost()->WasResized();
    }
}

CefRefPtr<CefBrowser> BrowserClient::GetBrowser() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return browser_;
}

void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               int httpStatusCode) {
    if (!frame->IsMain()) {
        return;  // Only handle main frame
    }
    
    std::string url = frame->GetURL().ToString();
    NSLog(@"[browser-helper] OnLoadEnd id=%s status=%d url=%s", 
          browserId_.c_str(), httpStatusCode, url.c_str());
    
    // Inject JavaScript to auto-play videos on YouTube
    if (url.find("youtube.com") != std::string::npos || 
        url.find("youtu.be") != std::string::npos) {
        
        // Comprehensive YouTube auto-play script
        std::string autoplayScript = R"(
            (function() {
                // Try to click play button or start video
                function tryPlayVideo() {
                    // Try HTML5 video element first
                    var videos = document.querySelectorAll('video');
                    for (var i = 0; i < videos.length; i++) {
                        var video = videos[i];
                        if (video.paused) {
                            video.muted = false;
                            video.play().catch(function(e) {
                                console.log('Video play failed:', e);
                            });
                        }
                    }
                    
                    // Try clicking YouTube's play button
                    var playButton = document.querySelector('.ytp-play-button');
                    if (playButton) {
                        var ariaLabel = playButton.getAttribute('aria-label') || '';
                        if (ariaLabel.toLowerCase().includes('play')) {
                            playButton.click();
                        }
                    }
                    
                    // Try clicking the big play button overlay
                    var bigPlayButton = document.querySelector('.ytp-large-play-button');
                    if (bigPlayButton) {
                        bigPlayButton.click();
                    }
                    
                    // Click on the player itself
                    var player = document.querySelector('#movie_player');
                    if (player) {
                        player.click();
                    }
                }
                
                // Try immediately
                tryPlayVideo();
                
                // Retry a few times with delays (for dynamic content)
                setTimeout(tryPlayVideo, 500);
                setTimeout(tryPlayVideo, 1500);
                setTimeout(tryPlayVideo, 3000);
            })();
        )";
        
        frame->ExecuteJavaScript(autoplayScript, url, 0);
        NSLog(@"[browser-helper] Injected YouTube autoplay script for %s", browserId_.c_str());
    }
    
    // Generic video autoplay for other sites
    else {
        std::string genericAutoplayScript = R"(
            (function() {
                var videos = document.querySelectorAll('video');
                for (var i = 0; i < videos.length; i++) {
                    var video = videos[i];
                    if (video.paused) {
                        video.muted = false;
                        video.play().catch(function(e) {
                            console.log('Video play failed:', e);
                        });
                    }
                }
            })();
        )";
        frame->ExecuteJavaScript(genericAutoplayScript, url, 0);
    }
}
