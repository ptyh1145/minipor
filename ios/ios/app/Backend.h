// Render backend abstraction: one interface, two implementations.
//   MWBackendMetal  — iOS 11+ / arm64 / Metal
//   MWBackendGLES2  — iOS 6+  / armv7 / OpenGL ES 2.0
#pragma once

#import <UIKit/UIKit.h>

@protocol MWBackend <NSObject>
- (UIView *)makeViewWithFrame:(CGRect)frame;
- (void)start;
@end

@interface MWBackendMetal : NSObject <MWBackend>
@end

@interface MWBackendGLES2 : NSObject <MWBackend>
@end

// Picks the backend that matches how the target was compiled.
id<MWBackend> MWCreateBackend(void);
