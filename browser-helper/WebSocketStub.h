#import <Foundation/Foundation.h>

// Forward declare frame callback block type for ObjC bridging.
typedef void (^FrameReadyBlock)(NSString *browserId, const void *buffer, int width, int height);

@interface WebSocketStub : NSObject
@property (nonatomic, assign) uint16_t port;
@property (nonatomic, copy) NSString *token;
@property (nonatomic, assign) NSUInteger maxConnections;
@property (nonatomic, strong) NSMutableDictionary<NSString *, NSDictionary *> *browserStates;
@property (nonatomic, copy) FrameReadyBlock onFrameReady;
- (void)start;
- (void)stop;
- (BOOL)isRunning;
// Send a frame notification to the connected client (called from frame callback).
- (void)sendFrameNotification:(NSString *)browserId width:(int)width height:(int)height buffer:(const void *)buffer;
@end


