// macOS: Mission Control binds Ctrl+Left/Right (and Up/Down) to
// switching Spaces, which fire and turn trigger. A session event
// tap removes the Ctrl flag from arrow key events while the game
// is focused, before the Dock sees them. The game still gets the
// arrow keys, and Ctrl on its own (a flags-changed event) is not
// touched, so fire+turn works. Creating an active tap needs
// Accessibility permission; without it we say so and carry on.
//
// This file includes no raylib header: CoreGraphics and raylib
// clash on a few names.

#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>

#include "i_macos.h"

static volatile int		focused;
static CFMachPortRef	tap;
static CFRunLoopSourceRef	source;

static CGEventRef GuardCallback (CGEventTapProxy proxy, CGEventType type,
				 CGEventRef event, void *refcon)
{
    (void)proxy;
    (void)refcon;

    if (type == kCGEventTapDisabledByTimeout
	|| type == kCGEventTapDisabledByUserInput)
    {
	CGEventTapEnable (tap, true);
	return event;
    }

    if (type != kCGEventKeyDown && type != kCGEventKeyUp)
	return event;

    // Only while the game window has focus, so Spaces switching
    // works everywhere else.
    if (!focused)
	return event;

    switch (CGEventGetIntegerValueField (event, kCGKeyboardEventKeycode))
    {
      case 0x7B:	// left
      case 0x7C:	// right
      case 0x7D:	// down
      case 0x7E:	// up
	CGEventSetFlags (event, CGEventGetFlags (event)
			 & ~kCGEventFlagMaskControl);
	break;
    }
    return event;
}


void MAC_SetSpacesGuard (int on)
{
    if (on)
    {
	if (tap)
	    return;

	tap = CGEventTapCreate (kCGSessionEventTap, kCGHeadInsertEventTap,
				kCGEventTapOptionDefault,
				CGEventMaskBit (kCGEventKeyDown)
				| CGEventMaskBit (kCGEventKeyUp),
				GuardCallback, NULL);
	if (!tap)
	{
	    fprintf (stderr, "Ctrl+arrows may switch Spaces: grant "
		     "Accessibility to this program in System Settings > "
		     "Privacy & Security > Accessibility.\n");
	    return;
	}
	source = CFMachPortCreateRunLoopSource (kCFAllocatorDefault, tap, 0);
	CFRunLoopAddSource (CFRunLoopGetMain (), source,
			    kCFRunLoopCommonModes);
	CGEventTapEnable (tap, true);
    }
    else if (tap)
    {
	CGEventTapEnable (tap, false);
	CFRunLoopRemoveSource (CFRunLoopGetMain (), source,
			       kCFRunLoopCommonModes);
	CFRelease (source);
	CFRelease (tap);
	source = NULL;
	tap = NULL;
    }
}


void MAC_SetFocused (int focus)
{
    focused = focus;
}
