/**
 * @file catime_ipc_server.h
 * @brief Windows named-pipe bridge for Week Planner Calendar.
 */

#ifndef CATIME_IPC_SERVER_H
#define CATIME_IPC_SERVER_H

#include <windows.h>

#define WM_APP_CATIME_IPC_COMMAND (WM_APP + 320)

BOOL CatimeIpcServer_Start(HWND mainWindow);
void CatimeIpcServer_Stop(void);
LRESULT CatimeIpcServer_HandleUiMessage(HWND window, LPARAM parameter);
void CatimeIpcServer_NotifyTimeout(void);
void CatimeIpcServer_NotifyPauseChanged(BOOL paused);
void CatimeIpcServer_NotifyCancelled(void);

#endif /* CATIME_IPC_SERVER_H */
