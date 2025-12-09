#import <Cocoa/Cocoa.h>
#import "AppDelegate.h"
#import <signal.h>
#import <dispatch/dispatch.h>

// Expose setter for main args (defined in AppDelegate.mm).
extern void SetMainArgs(int argc, char** argv);

// Check if this is a CEF subprocess by looking for --type= argument.
static BOOL isCefSubprocess(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--type=", 7) == 0) {
            return YES;
        }
    }
    return NO;
}

// Early CEF subprocess execution (defined in BrowserManager.mm).
extern int RunCefSubprocess(int argc, char** argv);

// Signal handler dispatch sources for clean shutdown
static dispatch_source_t sigintSource = nil;
static dispatch_source_t sigtermSource = nil;

static void setupSignalHandlers(void) {
    // Block SIGINT and SIGTERM so we can handle them via dispatch sources
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    
    dispatch_queue_t mainQueue = dispatch_get_main_queue();
    
    // Handle SIGINT (Ctrl+C)
    sigintSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGINT, 0, mainQueue);
    if (sigintSource) {
        dispatch_source_set_event_handler(sigintSource, ^{
            NSLog(@"[browser-helper] received SIGINT, initiating graceful shutdown");
            [[NSApplication sharedApplication] terminate:nil];
        });
        dispatch_resume(sigintSource);
    }
    
    // Handle SIGTERM (kill)
    sigtermSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGTERM, 0, mainQueue);
    if (sigtermSource) {
        dispatch_source_set_event_handler(sigtermSource, ^{
            NSLog(@"[browser-helper] received SIGTERM, initiating graceful shutdown");
            [[NSApplication sharedApplication] terminate:nil];
        });
        dispatch_resume(sigtermSource);
    }
    
    NSLog(@"[browser-helper] signal handlers installed for clean shutdown");
}

static void cleanupSignalHandlers(void) {
    if (sigintSource) {
        dispatch_source_cancel(sigintSource);
        sigintSource = nil;
    }
    if (sigtermSource) {
        dispatch_source_cancel(sigtermSource);
        sigtermSource = nil;
    }
}

int main(int argc, char * argv[]) {
    @autoreleasepool {
        // If this is a CEF subprocess (renderer, GPU, utility), run CEF and exit.
        if (isCefSubprocess(argc, argv)) {
            return RunCefSubprocess(argc, argv);
        }
        
        SetMainArgs(argc, argv);
        
        // Install signal handlers BEFORE starting run loop
        setupSignalHandlers();
        
        NSApplication *app = [NSApplication sharedApplication];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
        
        // Cleanup signal handlers after run loop exits
        cleanupSignalHandlers();
    }
    return EXIT_SUCCESS;
}


