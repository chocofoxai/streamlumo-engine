#import <Cocoa/Cocoa.h>
#import "AppDelegate.h"

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

int main(int argc, char * argv[]) {
    @autoreleasepool {
        // If this is a CEF subprocess (renderer, GPU, utility), run CEF and exit.
        if (isCefSubprocess(argc, argv)) {
            return RunCefSubprocess(argc, argv);
        }
        
        SetMainArgs(argc, argv);
        NSApplication *app = [NSApplication sharedApplication];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return EXIT_SUCCESS;
}


