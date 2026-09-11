/**
 * @file window_overlay_pointer.c
 * @brief Capture right/side-button clicks on the overlay without eating left clicks.
 */

#include "window_procedure/window_overlay_pointer.h"

#include "log.h"
#include "window.h"

#include <windowsx.h>

static HHOOK g_overlayHook = NULL;
static HWND g_overlayHwnd = NULL;

static BOOL IsPointOverOverlay(POINT pt) {
    RECT bounds;

    if (!g_overlayHwnd || !IsWindow(g_overlayHwnd) || !IsWindowVisible(g_overlayHwnd)) {
        return FALSE;
    }
    if (!GetWindowRect(g_overlayHwnd, &bounds)) {
        return FALSE;
    }
    return PtInRect(&bounds, pt);
}

static LRESULT CALLBACK OverlayMouseHookProc(int code, WPARAM msg, LPARAM data) {
    const MSLLHOOKSTRUCT* mouse = (const MSLLHOOKSTRUCT*)data;
    UINT xbutton;

    if (code < 0 || !mouse) {
        return CallNextHookEx(g_overlayHook, code, msg, data);
    }
    if (CLOCK_EDIT_MODE || !IsPointOverOverlay(mouse->pt)) {
        return CallNextHookEx(g_overlayHook, code, msg, data);
    }

    if (msg == WM_RBUTTONUP) {
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
            PostMessageW(g_overlayHwnd, WM_RBUTTONDOWN, MK_CONTROL, 0);
        } else {
            PostMessageW(g_overlayHwnd, WM_CONTEXTMENU, (WPARAM)g_overlayHwnd, 0);
        }
        return 1;
    }
    if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) {
        return 1;
    }
    if (msg == WM_XBUTTONUP) {
        xbutton = HIWORD(mouse->mouseData);
        PostMessageW(g_overlayHwnd, WM_XBUTTONUP, MAKEWPARAM(0, xbutton), 0);
        return 1;
    }
    if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONDBLCLK) {
        return 1;
    }
    return CallNextHookEx(g_overlayHook, code, msg, data);
}

BOOL OverlayPointer_Install(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return FALSE;
    }
    g_overlayHwnd = hwnd;
    if (g_overlayHook) {
        return TRUE;
    }
    g_overlayHook = SetWindowsHookExW(WH_MOUSE_LL, OverlayMouseHookProc,
                                      GetModuleHandleW(NULL), 0);
    if (!g_overlayHook) {
        LOG_WARNING("Failed to install overlay pointer hook (error=%lu)",
                    GetLastError());
        g_overlayHwnd = NULL;
        return FALSE;
    }
    return TRUE;
}

void OverlayPointer_Uninstall(void) {
    if (g_overlayHook) {
        UnhookWindowsHookEx(g_overlayHook);
        g_overlayHook = NULL;
    }
    g_overlayHwnd = NULL;
}
