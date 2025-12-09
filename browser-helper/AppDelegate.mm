#import "AppDelegate.h"
#import "WebSocketStub.h"
#import "BrowserManager.h"
#import <dispatch/dispatch.h>

// Store argc/argv for CEF init since we can only access them reliably from main().
static int s_argc = 0;
static char** s_argv = nullptr;

extern "C" void SetMainArgs(int argc, char** argv) {
    s_argc = argc;
    s_argv = argv;
}

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    NSLog(@"[browser-helper] launching helper event loop (TCP JSON-line + CEF)");

    // Initialize CEF (handles subprocess execution internally).
    if (!BrowserManager::Instance().InitCef(s_argc, s_argv)) {
        NSLog(@"[browser-helper] CEF init failed");
    }

    self.wsStub = [[WebSocketStub alloc] init];

    // Parse port from command line args (--port=XXXX) or environment.
    uint16_t port = 4777;
    const char *portEnv = getenv("BROWSER_HELPER_PORT");
    const char *tokenEnv = getenv("BROWSER_HELPER_TOKEN");
    
    // Check command-line arguments for --port=XXXX
    for (int i = 1; i < s_argc; i++) {
        if (strncmp(s_argv[i], "--port=", 7) == 0) {
            int p = atoi(s_argv[i] + 7);
            if (p > 0 && p < 65536) {
                port = (uint16_t)p;
            }
            break;
        }
    }
    
    // Environment variable overrides command line if set
    if (portEnv) {
        int p = atoi(portEnv);
        if (p > 0 && p < 65536) {
            port = (uint16_t)p;
        }
    }
    self.wsStub.port = port;
    self.wsStub.token = tokenEnv ? [NSString stringWithUTF8String:tokenEnv] : @"";
    // Allow multiple connections: engine + obs-browser-bridge plugin
    self.wsStub.maxConnections = 5;

    // Wire up frame callback from BrowserManager → WebSocketStub.
    __weak WebSocketStub *weakStub = self.wsStub;
    BrowserManager::Instance().SetFrameCallback([weakStub](const std::string& browserId,
                                                            const void* buffer,
                                                            int width,
                                                            int height) {
        WebSocketStub *stub = weakStub;
        if (stub) {
            [stub sendFrameNotification:[NSString stringWithUTF8String:browserId.c_str()]
                                  width:width
                                 height:height
                                 buffer:buffer];
        }
    });

    [self.wsStub start];
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;
    [self.wsStub stop];
    self.wsStub = nil;
    BrowserManager::Instance().ShutdownCef();
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

@end
