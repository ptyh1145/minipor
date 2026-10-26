// mini_port iOS shell. Two builds share this file:
//   MW_RENDERER_METAL  -> iOS 11+, arm64, Metal
//   MW_RENDERER_GLES2  -> iOS 6+, armv7, OpenGL ES 2.0
#import <UIKit/UIKit.h>

#import "Backend.h"
#import "Log.h"

#if defined(MW_RENDERER_GLES2)
id<MWBackend> MWCreateBackend(void) { return [[MWBackendGLES2 alloc] init]; }
static NSString *const kBackendName = @"OpenGL ES 2.0";
#else
id<MWBackend> MWCreateBackend(void) { return [[MWBackendMetal alloc] init]; }
static NSString *const kBackendName = @"Metal";
#endif

@interface ViewController : UIViewController
@property(nonatomic, strong) id<MWBackend> backend;
@end

@implementation ViewController
- (void)viewDidLoad {
    [super viewDidLoad];
    MWLogBegin();
    MWLog(@"viewDidLoad");
    self.view.backgroundColor = [UIColor colorWithWhite:0.05 alpha:1.0];

    self.backend = MWCreateBackend();
    UIView *renderView = [self.backend makeViewWithFrame:self.view.bounds];
    if (renderView) {
        renderView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        [self.view addSubview:renderView];
    }

    UILabel *label = [[UILabel alloc] init];
    label.text = [NSString stringWithFormat:@"mini_port · %@ · log: Documents/mini_port.log", kBackendName];
    label.textColor = [UIColor colorWithWhite:0.85 alpha:1.0];
#if defined(__IPHONE_8_2) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 80200
    label.font = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightRegular];
#else
    label.font = [UIFont fontWithName:@"Courier" size:12];
#endif
    label.textAlignment = NSTextAlignmentCenter;
    label.numberOfLines = 2;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:label];

    NSDictionary *views = @{@"label" : label};
    [self.view addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:|-12-[label]-12-|"
                                                                      options:0
                                                                      metrics:nil
                                                                        views:views]];
    [self.view addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:[label]-12-|"
                                                                      options:0
                                                                      metrics:nil
                                                                        views:views]];
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [self.backend start];
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}
@end

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end

@implementation AppDelegate
- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    self.window.rootViewController = [[ViewController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
