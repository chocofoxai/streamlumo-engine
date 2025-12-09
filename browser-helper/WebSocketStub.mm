#import "WebSocketStub.h"
#import "BrowserManager.h"

#import <dispatch/dispatch.h>
#import <sys/socket.h>
#import <netinet/in.h>
#import <netinet/tcp.h>
#import <arpa/inet.h>
#import <fcntl.h>
#import <unistd.h>
#import <errno.h>
#import <string.h>
#import <limits.h>

@interface WebSocketStub ()
@property (nonatomic, assign) BOOL running;
@property (nonatomic, assign) int listenFd;
@property (nonatomic, strong) dispatch_queue_t serverQueue;
@property (nonatomic, strong) dispatch_source_t acceptSource;
@property (nonatomic, strong) NSMutableSet<dispatch_source_t> *clientSources;
@property (nonatomic, assign) NSUInteger activeConnections;
@property (nonatomic, assign) int activeClientFd;
@end

@implementation WebSocketStub

- (instancetype)init {
    self = [super init];
    if (self) {
        _port = 4777; // default placeholder port
        _running = NO;
        _listenFd = -1;
        _clientSources = [NSMutableSet set];
        _activeConnections = 0;
        _activeClientFd = -1;
        _browserStates = [NSMutableDictionary dictionary];
    }
    return self;
}

- (void)start {
    if (self.running) {
        return;
    }

    self.serverQueue = dispatch_queue_create("com.streamlumo.browser-helper.tcp", DISPATCH_QUEUE_SERIAL);
    self.listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (self.listenFd < 0) {
        NSLog(@"[browser-helper] failed to create socket: %d", errno);
        return;
    }

    // Make the listen socket non-blocking so the accept handler does not stall the queue.
    fcntl(self.listenFd, F_SETFL, O_NONBLOCK);

    int yes = 1;
    setsockopt(self.listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(self.port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(self.listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        NSLog(@"[browser-helper] failed to bind on 127.0.0.1:%hu (errno=%d)", self.port, errno);
        close(self.listenFd);
        self.listenFd = -1;
        return;
    }

    if (listen(self.listenFd, 4) < 0) {
        NSLog(@"[browser-helper] failed to listen (errno=%d)", errno);
        close(self.listenFd);
        self.listenFd = -1;
        return;
    }

    self.acceptSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, self.listenFd, 0, self.serverQueue);
    __weak typeof(self) weakSelf = self;
    dispatch_source_set_event_handler(self.acceptSource, ^{
        [weakSelf handleAccept];
    });
    dispatch_source_set_cancel_handler(self.acceptSource, ^{
        if (weakSelf.listenFd >= 0) {
            close(weakSelf.listenFd);
            weakSelf.listenFd = -1;
        }
    });
    dispatch_resume(self.acceptSource);

    self.running = YES;
    NSLog(@"[browser-helper] TCP JSON-line server listening on 127.0.0.1:%hu", self.port);
}

- (void)handleAccept {
    while (self.acceptSource) {
        struct sockaddr_in clientAddr;
        socklen_t len = sizeof(clientAddr);
        int clientFd = accept(self.listenFd, (struct sockaddr *)&clientAddr, &len);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            NSLog(@"[browser-helper] accept failed (errno=%d)", errno);
            return;
        }

        fcntl(clientFd, F_SETFL, O_NONBLOCK);
        int one = 1;
        setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        if (self.activeConnections >= self.maxConnections && self.maxConnections > 0) {
            // Politely refuse additional connections.
            const char *msg = "{\"type\":\"error\",\"message\":\"too_many_connections\"}\n";
            send(clientFd, msg, strlen(msg), 0);
            close(clientFd);
            continue;
        }

        dispatch_source_t clientSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, clientFd, 0, self.serverQueue);
        if (!clientSource) {
            close(clientFd);
            return;
        }

        NSMutableData *buffer = [NSMutableData data];
        __weak typeof(self) weakSelf = self;
        dispatch_source_set_event_handler(clientSource, ^{
            uint8_t buf[1024];
            ssize_t n = recv(clientFd, buf, sizeof(buf), 0);
            if (n <= 0) {
                [weakSelf closeClientSource:clientSource];
                return;
            }
            if ((NSUInteger)buffer.length + (NSUInteger)n > 64 * 1024) {
                NSLog(@"[browser-helper] dropping connection due to oversized line");
                [weakSelf closeClientSource:clientSource];
                return;
            }
            [buffer appendBytes:buf length:(NSUInteger)n];
            [weakSelf drainBuffer:buffer socket:clientFd source:clientSource];
        });

        dispatch_source_set_cancel_handler(clientSource, ^{
            close(clientFd);
        });

        [self.clientSources addObject:clientSource];
        self.activeConnections += 1;
        self.activeClientFd = clientFd;
        dispatch_resume(clientSource);

        NSLog(@"[browser-helper] accepted connection fd=%d", clientFd);
        [self sendJSON:@{ @"type": @"helper_ready", @"port": @(self.port), @"v": @1 } toSocket:clientFd];
    }
}

- (void)drainBuffer:(NSMutableData *)buffer socket:(int)socketFD source:(dispatch_source_t)source {
    const uint8_t *bytes = (const uint8_t *)buffer.bytes;
    while (buffer.length > 0) {
        const void *newline = memchr(bytes, '\n', buffer.length);
        if (!newline) {
            break;
        }
        size_t lineLen = (const uint8_t *)newline - bytes;
        NSData *lineData = [NSData dataWithBytes:bytes length:lineLen];
        NSString *line = [[NSString alloc] initWithData:lineData encoding:NSUTF8StringEncoding];
        if (line.length > 0) {
            [self handleLine:line socket:socketFD source:source];
        }
        NSRange removeRange = NSMakeRange(0, lineLen + 1); // drop newline
        [buffer replaceBytesInRange:removeRange withBytes:NULL length:0];
        bytes = (const uint8_t *)buffer.bytes;
    }
}

- (void)handleLine:(NSString *)line socket:(int)socketFD source:(dispatch_source_t)source {
    NSData *data = [line dataUsingEncoding:NSUTF8StringEncoding];
    NSError *error = nil;
    id json = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
    if (error || ![json isKindOfClass:[NSDictionary class]]) {
        NSLog(@"[browser-helper] received non-JSON line: %@", line);
        return;
    }

    NSDictionary *dict = (NSDictionary *)json;
    NSString *type = dict[@"type"];
    NSLog(@"[browser-helper] received message: %@", dict);

    if ([type isEqualToString:@"ping"]) {
        NSString *tok = dict[@"token"] ?: @"";
        if (self.token.length > 0 && ![tok isEqualToString:self.token]) {
            [self sendError:@"unauthorized" socket:socketFD];
            [self closeClientSource:source];
            return;
        }
        [self sendJSON:@{ @"type": @"pong", @"from": @"browser-helper", @"v": @1 } toSocket:socketFD];
    } else if ([type isEqualToString:@"handshake"]) {
        NSString *tok = dict[@"token"] ?: @"";
        if (self.token.length > 0 && ![tok isEqualToString:self.token]) {
            [self sendError:@"unauthorized" socket:socketFD];
            [self closeClientSource:source];
            return;
        }
        [self sendJSON:@{ @"type": @"handshake_ack", @"from": @"browser-helper", @"status": @"ok", @"v": @1 } toSocket:socketFD];
    } else if ([type isEqualToString:@"initBrowser"]) {
        NSString *tok = dict[@"token"] ?: @"";
        if (self.token.length > 0 && ![tok isEqualToString:self.token]) {
            [self sendError:@"unauthorized" socket:socketFD];
            [self closeClientSource:source];
            return;
        }
        NSString *browserId = dict[@"id"];
        if (browserId.length == 0) {
            [self sendError:@"missing_id" socket:socketFD];
            return;
        }
        NSNumber *width = dict[@"width"] ?: @(1280);
        NSNumber *height = dict[@"height"] ?: @(720);
        NSDictionary *state = @{ @"id": browserId, @"url": dict[@"url"] ?: @"", @"width": width, @"height": height };
        @synchronized (self.browserStates) {
            self.browserStates[browserId] = state;
        }
        NSLog(@"[browser-helper] initBrowser id=%@ url=%@ %dx%d", browserId, dict[@"url"], width.intValue, height.intValue);
        // CEF: Create off-screen browser
        BrowserManager::Instance().CreateBrowser(
            std::string([browserId UTF8String]),
            std::string([dict[@"url"] UTF8String] ?: ""),
            width.intValue,
            height.intValue);
        [self sendJSON:@{ @"type": @"browserReady", @"id": browserId, @"status": @"ok", @"v": @1 } toSocket:socketFD];
    } else if ([type isEqualToString:@"updateBrowser"]) {
        NSString *browserId = dict[@"id"];
        if (browserId.length == 0) {
            [self sendError:@"missing_id" socket:socketFD];
            return;
        }
        @synchronized (self.browserStates) {
            NSDictionary *existing = self.browserStates[browserId];
            if (!existing) {
                [self sendError:@"unknown_id" socket:socketFD];
                return;
            }
            NSMutableDictionary *next = [existing mutableCopy];
            if (dict[@"url"]) next[@"url"] = dict[@"url"];
            if (dict[@"width"]) next[@"width"] = dict[@"width"];
            if (dict[@"height"]) next[@"height"] = dict[@"height"];
            self.browserStates[browserId] = next;
        }
        NSLog(@"[browser-helper] updateBrowser id=%@ url=%@ width=%@ height=%@", browserId, dict[@"url"], dict[@"width"], dict[@"height"]);
        // CEF: Resize browser
        NSNumber *w = dict[@"width"];
        NSNumber *h = dict[@"height"];
        if (w && h) {
            BrowserManager::Instance().ResizeBrowser(std::string([browserId UTF8String]), w.intValue, h.intValue);
        }
        [self sendJSON:@{ @"type": @"browserUpdated", @"id": browserId, @"status": @"ok", @"v": @1 } toSocket:socketFD];
    } else if ([type isEqualToString:@"disposeBrowser"]) {
        NSString *browserId = dict[@"id"];
        if (browserId.length == 0) {
            [self sendError:@"missing_id" socket:socketFD];
            return;
        }
        @synchronized (self.browserStates) {
            [self.browserStates removeObjectForKey:browserId];
        }
        NSLog(@"[browser-helper] disposeBrowser id=%@", browserId);
        // CEF: Close browser
        BrowserManager::Instance().CloseBrowser(std::string([browserId UTF8String]));
        [self sendJSON:@{ @"type": @"browserDisposed", @"id": browserId, @"status": @"ok", @"v": @1 } toSocket:socketFD];
    } else {
        [self sendError:@"unsupported" socket:socketFD];
    }
}

- (void)sendJSON:(NSDictionary *)dict toSocket:(int)socketFD {
    NSError *error = nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:dict options:0 error:&error];
    if (error || !data) {
        NSLog(@"[browser-helper] failed to encode JSON: %@", error);
        return;
    }

    NSMutableData *line = [NSMutableData dataWithData:data];
    const char newline = '\n';
    [line appendBytes:&newline length:1];

    // Send all data, handling partial sends
    const uint8_t *ptr = (const uint8_t *)line.bytes;
    size_t remaining = line.length;
    while (remaining > 0) {
        ssize_t n = send(socketFD, ptr, remaining, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full, wait a bit and retry
                usleep(1000);
                continue;
            }
            NSLog(@"[browser-helper] send failed (errno=%d)", errno);
            return;
        }
        ptr += n;
        remaining -= (size_t)n;
    }
}

- (void)sendError:(NSString *)message socket:(int)socketFD {
    [self sendJSON:@{ @"type": @"error", @"message": message, @"v": @1 } toSocket:socketFD];
}

- (void)closeClientSource:(dispatch_source_t)source {
    if (!source) {
        return;
    }
    dispatch_source_cancel(source);
    [self.clientSources removeObject:source];
    if (self.activeConnections > 0) {
        self.activeConnections -= 1;
    }
    self.activeClientFd = -1;
}

- (void)sendFrameNotification:(NSString *)browserId width:(int)width height:(int)height buffer:(const void *)buffer {
    if (self.activeClientFd < 0) {
        return;
    }
    size_t dataLen = (size_t)width * (size_t)height * 4;
    NSData *raw = [NSData dataWithBytes:buffer length:dataLen];
    NSString *b64 = [raw base64EncodedStringWithOptions:0];
    NSDictionary *msg = @{
        @"type": @"frameReady",
        @"id": browserId,
        @"width": @(width),
        @"height": @(height),
        @"format": @"bgra",
        @"data": b64,
        @"v": @1
    };
    [self sendJSON:msg toSocket:self.activeClientFd];
}

- (void)stop {
    if (!self.running) {
        return;
    }

    NSLog(@"[browser-helper] stopping TCP JSON-line server");

    for (dispatch_source_t source in self.clientSources) {
        dispatch_source_cancel(source);
    }
    [self.clientSources removeAllObjects];

    if (self.acceptSource) {
        dispatch_source_cancel(self.acceptSource);
        self.acceptSource = nil;
    }

    self.running = NO;
}

- (BOOL)isRunning {
    return self.running;
}

@end
