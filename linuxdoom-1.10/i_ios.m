// iOS-only: the parts that need Objective-C. See i_ios.h.

#include "i_ios.h"

#ifdef DOOM_IOS

#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

void I_IOSAudioSession (void)
{
    NSError*	error = nil;
    AVAudioSession*	session = [AVAudioSession sharedInstance];

    // Ambient: mixes with other audio and follows the silent switch.
    if (![session setCategory:AVAudioSessionCategoryAmbient error:&error]
	|| ![session setActive:YES error:&error])
	NSLog (@"AVAudioSession: %@", error);
}

void I_IOSSafeInsets (float* left, float* top, float* right, float* bottom)
{
    *left = *top = *right = *bottom = 0;
    for (UIScene* scene in [UIApplication sharedApplication].connectedScenes)
    {
	if (![scene isKindOfClass:[UIWindowScene class]])
	    continue;
	for (UIWindow* window in ((UIWindowScene*)scene).windows)
	{
	    UIEdgeInsets	insets = window.safeAreaInsets;

	    *left = insets.left;
	    *top = insets.top;
	    *right = insets.right;
	    *bottom = insets.bottom;
	    return;
	}
    }
}

#endif
