// Tiny on-device logger: NSLog plus a line-buffered file in Documents so a jailbroken
// device always has a trace, even when the app is killed before it can show UI.
#pragma once

#import <Foundation/Foundation.h>

// Truncates the log file (call once at launch).
void MWLogBegin(void);

// Appends one line to Documents/mini_port.log and mirrors it to NSLog.
void MWLog(NSString *format, ...) NS_FORMAT_FUNCTION(1, 2);

// Absolute path of the log file (shown on screen / in the log itself).
NSString *MWLogPath(void);
