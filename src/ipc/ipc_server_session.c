/** @file ipc_server_session.c UI-thread bridge and external session state. */

#include "ipc/catime_ipc_server.h"
#include "ipc/catime_ipc_history.h"
#include "ipc/catime_ipc_persistence.h"
#include "ipc_server_internal.h"
#include "ipc_server_session_internal.h"
#include "timer/timer.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

HWND g_ipcMainWindow = NULL;
CRITICAL_SECTION g_ipcStateLock;
BOOL g_ipcLockInitialized = FALSE;
BOOL g_ipcApplyingUiCommand = FALSE;
CatimeIpcState g_ipcState;
IpcHistory g_ipcHistory;

int64_t IpcState_EpochMilliseconds(void) {
    FILETIME fileTime;
    ULARGE_INTEGER value;
    GetSystemTimeAsFileTime(&fileTime);
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return (int64_t)(value.QuadPart / 10000ULL) - 11644473600000LL;
}

void IpcState_SaveWithHistory(void) {
    CatimeIpcPersistence_Save(&g_ipcState, &g_ipcHistory);
}

void IpcState_RecordTerminal(const CatimeIpcSnapshot* snapshot) {
    if (!CatimeIpc_IsTerminalStatus(snapshot->status)) return;
    IpcHistory_Add(&g_ipcHistory, snapshot);
}

static BOOL DispatchUiCommand(const CatimeIpcRequest* request,
                              IpcReply* reply) {
    IpcUiCommand command;
    ZeroMemory(&command, sizeof(command));
    command.request = *request;
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(g_ipcMainWindow, WM_APP_CATIME_IPC_COMMAND,
            0, (LPARAM)&command, SMTO_ABORTIFHUNG | SMTO_BLOCK,
            IPC_UI_TIMEOUT_MS, &result)) {
        reply->type = IPC_REPLY_ERROR;
        reply->error = CATIME_IPC_ERROR_INTERNAL_ERROR;
        return TRUE;
    }
    if (!command.hasSnapshot) {
        reply->type = IPC_REPLY_ERROR;
        reply->error = command.error;
    } else {
        reply->type = IPC_REPLY_SNAPSHOT;
        reply->snapshot = command.snapshot;
    }
    return TRUE;
}

BOOL IpcSession_Execute(const CatimeIpcRequest* request,
                        BOOL* handshaken,
                        IpcReply* reply,
                        int clientId) {
    if (!request || !handshaken || !reply) return FALSE;
    if (request->command == CATIME_IPC_COMMAND_HELLO) {
        *handshaken = TRUE;
        reply->type = IPC_REPLY_HELLO;
        return TRUE;
    }
    if (!*handshaken) {
        reply->type = IPC_REPLY_ERROR;
        reply->error = CATIME_IPC_ERROR_UNSUPPORTED_PROTOCOL;
        return TRUE;
    }
    if (request->command == CATIME_IPC_COMMAND_PING) {
        reply->type = IPC_REPLY_PONG;
        return TRUE;
    }
    if (request->command == CATIME_IPC_COMMAND_GET_STATE) {
        EnterCriticalSection(&g_ipcStateLock);
        BOOL found = CatimeIpcState_GetAt(
            &g_ipcState, IpcState_EpochMilliseconds(), &reply->snapshot);
        LeaveCriticalSection(&g_ipcStateLock);
        reply->type = found ? IPC_REPLY_SNAPSHOT : IPC_REPLY_ERROR;
        reply->error = found ? CATIME_IPC_ERROR_NONE :
            CATIME_IPC_ERROR_SESSION_MISMATCH;
        return TRUE;
    }
    if (request->command == CATIME_IPC_COMMAND_ACK_EVENT) {
        EnterCriticalSection(&g_ipcStateLock);
        int ackState = IpcEventQueue_Acknowledge(
            clientId, request->sessionId, request->revision);
        CatimeIpcError error = ackState > 0 ? CATIME_IPC_ERROR_NONE :
            CatimeIpcState_Acknowledge(&g_ipcState, request->sessionId,
                                       request->revision);
        if (error == CATIME_IPC_ERROR_NONE &&
            IpcHistory_Remove(&g_ipcHistory, request->sessionId,
                              request->revision)) {
            IpcState_SaveWithHistory();
        }
        LeaveCriticalSection(&g_ipcStateLock);
        reply->type = error == CATIME_IPC_ERROR_NONE ?
            IPC_REPLY_EVENT_ACK : IPC_REPLY_ERROR;
        reply->error = error;
        return TRUE;
    }
    if (request->command == CATIME_IPC_COMMAND_QUEUE_PHASE) {
        EnterCriticalSection(&g_ipcStateLock);
        CatimeIpcError error = CatimeIpcState_QueuePhase(&g_ipcState,
            request->sessionId, request->durationSeconds, request->phase);
        if (error == CATIME_IPC_ERROR_NONE) IpcState_SaveWithHistory();
        LeaveCriticalSection(&g_ipcStateLock);
        reply->type = error == CATIME_IPC_ERROR_NONE ?
            IPC_REPLY_EVENT_ACK : IPC_REPLY_ERROR;
        reply->error = error;
        return TRUE;
    }
    return DispatchUiCommand(request, reply);
}

static void EnqueueHistoryReplay(void) {
    const CatimeIpcSnapshot* snapshot = NULL;
    uint32_t count = IpcHistory_Count(&g_ipcHistory);
    for (uint32_t index = 0; index < count; index++) {
        if (IpcHistory_Get(&g_ipcHistory, index, &snapshot)) {
            IpcEventQueue_Push(snapshot);
            IpcHistory_MarkDelivered(&g_ipcHistory, index);
        }
    }
}

BOOL IpcSession_Initialize(HWND mainWindow) {
    if (!mainWindow || !IsWindow(mainWindow)) return FALSE;
    InitializeCriticalSection(&g_ipcStateLock);
    g_ipcLockInitialized = TRUE;
    IpcEventQueue_Initialize();
    CatimeIpcState_Init(&g_ipcState);
    IpcHistory_Init(&g_ipcHistory);
    int64_t now = IpcState_EpochMilliseconds();
    if (CatimeIpcPersistence_Load(&g_ipcState, now, &g_ipcHistory)) {
        EnqueueHistoryReplay();
        CatimeIpcSnapshot next;
        if (CatimeIpc_IsTerminalStatus(g_ipcState.snapshot.status) &&
            CatimeIpcState_AdvanceQueued(&g_ipcState, now, &next)) {
            IpcEventQueue_Push(&next);
            IpcState_SaveWithHistory();
        }
    }
    CatimeIpcSnapshot recovered;
    if (CatimeIpcState_GetAt(&g_ipcState, IpcState_EpochMilliseconds(),
                             &recovered) &&
        (recovered.status == CATIME_IPC_STATUS_RUNNING ||
         recovered.status == CATIME_IPC_STATUS_PAUSED)) {
        g_ipcApplyingUiCommand = TRUE;
        CLOCK_SHOW_CURRENT_TIME = false;
        CLOCK_COUNT_UP = false;
        CLOCK_TOTAL_TIME = (int32_t)recovered.remainingSeconds;
        ResetTimer();
        if (recovered.status == CATIME_IPC_STATUS_PAUSED) TogglePauseTimer();
        g_ipcApplyingUiCommand = FALSE;
    }
    g_ipcMainWindow = mainWindow;
    return TRUE;
}

void IpcSession_Shutdown(void) {
    g_ipcMainWindow = NULL;
    IpcEventQueue_Shutdown();
    IpcHistory_Init(&g_ipcHistory);
    if (g_ipcLockInitialized) {
        DeleteCriticalSection(&g_ipcStateLock);
        g_ipcLockInitialized = FALSE;
    }
}
