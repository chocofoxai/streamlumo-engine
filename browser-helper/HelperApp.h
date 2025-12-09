// HelperApp.h – CefApp subclass for the browser helper process
#pragma once

#include "include/cef_app.h"
#include "include/cef_command_line.h"

class HelperApp : public CefApp, public CefBrowserProcessHandler {
public:
    HelperApp() = default;

    // CefApp
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }

    void OnBeforeCommandLineProcessing(const CefString& process_type,
                                        CefRefPtr<CefCommandLine> command_line) override;

    // CefBrowserProcessHandler
    void OnContextInitialized() override;
    void OnScheduleMessagePumpWork(int64_t delay_ms) override;

private:
    IMPLEMENT_REFCOUNTING(HelperApp);
    DISALLOW_COPY_AND_ASSIGN(HelperApp);
};
