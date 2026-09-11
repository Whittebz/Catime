/**
 * @file window_overlay_pointer.h
 * @brief Overlay pointer capture for right-click and mouse side buttons.
 */

#ifndef WINDOW_OVERLAY_POINTER_H
#define WINDOW_OVERLAY_POINTER_H

#include <windows.h>

BOOL OverlayPointer_Install(HWND hwnd);
void OverlayPointer_Uninstall(void);
void OverlayPointer_BeginMenu(void);
void OverlayPointer_EndMenu(void);

#endif /* WINDOW_OVERLAY_POINTER_H */
