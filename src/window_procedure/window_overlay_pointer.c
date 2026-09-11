/**
 * @file window_overlay_pointer.c
 * @brief Capture overlay mouse side buttons; left and right clicks pass through.
 */

#include "window_procedure/window_overlay_pointer.h"

#include "log.h"
#include "window.h"

#include <wchar.h>
#include <windowsx.h>

static HHOOK g_overlayHook = NULL;
static HWND g_overlayHwnd = NULL;
static BOOL g_menuOpen = FALSE;

static BOOL IsPopupMenuWindow(HWND hwnd) {
    wchar_t className[32];

    if (!hwnd) {
        return FALSE;
    }
    className[0] = L'\0';
    return GetClassNameW(hwnd, className, (int)_countof(className)) > 0 &&
           wcscmp(className, L"#32768") == 0;
}

static BOOL IsPointOverPopupMenu(POINT pt) {
    HWND hit = WindowFromPoint(pt);
    if (!hit) {
        return FALSE;
    }
    return IsPopupMenuWindow(hit) || IsPopupMenuWindow(GetAncestor(hit, GA_ROOT));
}

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
    if (g_menuOpen &&
        (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN ||
         msg == WM_XBUTTONDOWN)) {
        if (!IsPointOverPopupMenu(mouse->pt)) {
            EndMenu();
        }
        if (msg == WM_XBUTTONDOWN) {
            return 1;
        }
        return CallNextHookEx(g_overlayHook, code, msg, data);
    }
    if (CLOCK_EDIT_MODE || !IsPointOverOverlay(mouse->pt)) {
        return CallNextHookEx(g_overlayHook, code, msg, data);
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
    g_menuOpen = FALSE;
    if (g_overlayHook) {
        UnhookWindowsHookEx(g_overlayHook);
        g_overlayHook = NULL;
    }
    g_overlayHwnd = NULL;
}

void OverlayPointer_BeginMenu(void) {
    g_menuOpen = TRUE;
}

void OverlayPointer_EndMenu(void) {
    g_menuOpen = FALSE;
}
