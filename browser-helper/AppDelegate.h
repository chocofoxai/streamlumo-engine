#import <Cocoa/Cocoa.h>

@class WebSocketStub;

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) WebSocketStub *wsStub;
@end

