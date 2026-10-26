#import "Log.h"

#import <stdarg.h>

static NSString *gLogPath = nil;
static FILE *gFile = NULL;

NSString *MWLogPath(void) {
    if (gLogPath) return gLogPath;
    NSArray *dirs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *dir = dirs.count ? dirs[0] : NSTemporaryDirectory();
    gLogPath = [dir stringByAppendingPathComponent:@"mini_port.log"];
    return gLogPath;
}

void MWLogBegin(void) {
    if (gFile) {
        fclose(gFile);
        gFile = NULL;
    }
    gFile = fopen(MWLogPath().UTF8String, "w");
    MWLog(@"=== mini-port %@ ===", [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"]);
    MWLog(@"bundle : %@", [[NSBundle mainBundle] bundlePath]);
    MWLog(@"docs   : %@", [MWLogPath() stringByDeletingLastPathComponent]);
}

void MWLog(NSString *format, ...) {
    va_list args;
    va_start(args, format);
    NSString *message = [[NSString alloc] initWithFormat:format arguments:args];
    va_end(args);

    NSLog(@"[mini-port] %@", message);

    if (!gFile) {
        gFile = fopen(MWLogPath().UTF8String, "a");
    }
    if (gFile) {
        NSString *line = [message stringByAppendingString:@"\n"];
        fputs(line.UTF8String, gFile);
        fflush(gFile);
    }
}
