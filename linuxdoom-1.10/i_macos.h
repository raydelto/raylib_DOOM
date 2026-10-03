// macOS: keep Ctrl+arrows from switching Spaces while playing.
#ifndef __I_MACOS__
#define __I_MACOS__

// Starts or stops the event tap that hides Ctrl from the arrow
// keys while the game is focused. Needs Accessibility permission;
// without it nothing changes.
void MAC_SetSpacesGuard (int on);

// Whether the game window has focus; the tap does nothing otherwise.
void MAC_SetFocused (int focus);

#endif
