/** @file ipc_server_session.c UI-thread bridge and external session state. */

#include "ipc/catime_ipc_server.h"
#include "ipc/catime_ipc_persistence.h"
#include "ipc_server_internal.h"
#include "timer/timer.h"

#include <stdint.h>

#define IPC_UI_TIMEOUT_MS 5000
typedef struct {
    CatimeIpcRequest request;
    CatimeIpcSnapshot snapshot;
    CatimeIpcError error;
    BOOL hasSnapshot;
} IpcUiCommand;

static HWND s_mainWindow = NULL;
static CRITICAL_SECTION s_stateLock;
static BOOL s_lockInitialized = FALSE;
static BOOL s_applyingUiCommand = FALSE;
static CatimeIpcState s_state;

static int64_t EpochMilliseconds(void) {
    FILETIME fileTime;
    ULARGE_INTEGER value;
    GetSystemTimeAsFileTime(&fileTime);
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return (int64_t)(value.QuadPart / 10000ULL) - 11644473600000LL;
}

static BOOL DispatchUiCommand(const CatimeIpcRequest* request,
                              IpcReply* reply) {
    IpcUiCommand command;
    ZeroMemory(&command, sizeof(command));
    command.request = *request;
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(s_mainWindow, WM_APP_CATIME_IPC_COMMAND,
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
                        IpcReply* reply) {
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
        EnterCriticalSection(&s_stateLock);
        BOOL found = CatimeIpcState_GetAt(
            &s_state, EpochMilliseconds(), &reply->snapshot);
        LeaveCriticalSection(&s_stateLock);
        reply->type = found ? IPC_REPLY_SNAPSHOT : IPC_REPLY_ERROR;
        reply->error = found ? CATIME_IPC_ERROR_NONE :
            CATIME_IPC_ERROR_SESSION_MISMATCH;
        return TRUE;
    }
    if (request->command == CATIME_IPC_COMMAND_ACK_EVENT) {
        EnterCriticalSection(&s_stateLock);
        BOOL removed = IpcEventQueue_Acknowledge(request->sessionId,
                                                 request->revision);
        CatimeIpcError error = removed ? CATIME_IPC_ERROR_NONE :
            CatimeIpcState_Acknowledge(&s_state, request->sessionId,
                                       request->revision);
        if (error == CATIME_IPC_ERROR_NONE) {
            CatimeIpcPersistence_Save(&s_state);
        }
        LeaveCriticalSection(&s_stateLock);
        reply->type = error == CATIME_IPC_ERROR_NONE ?
            IPC_REPLY_EVENT_ACK : IPC_REPLY_ERROR;
        reply->error = error;
        return TRUE;
    }
    if (request->command == CATIME_IPC_COMMAND_QUEUE_PHASE) {
        EnterCriticalSection(&s_stateLock);
        CatimeIpcError error = CatimeIpcState_QueuePhase(&s_state,
            request->sessionId, request->durationSeconds, request->phase);
        if (error == CATIME_IPC_ERROR_NONE) {
            CatimeIpcPersistence_Save(&s_state);
        }
        LeaveCriticalSection(&s_stateLock);
        reply->type = error == CATIME_IPC_ERROR_NONE ?
            IPC_REPLY_EVENT_ACK : IPC_REPLY_ERROR;
        reply->error = error;
        return TRUE;
    }
    return DispatchUiCommand(request, reply);
}

BOOL IpcSession_Initialize(HWND mainWindow) {
    if (!mainWindow || !IsWindow(mainWindow)) return FALSE;
    InitializeCriticalSection(&s_stateLock);
    s_lockInitialized = TRUE;
    IpcEventQueue_Initialize();
    CatimeIpcState_Init(&s_state);
    CatimeIpcSnapshot completed;
    ZeroMemory(&completed, sizeof(completed));
    int64_t now = EpochMilliseconds();
    if (CatimeIpcPersistence_Load(&s_state, now, &completed) &&
        completed.status == CATIME_IPC_STATUS_COMPLETED) {
        IpcEventQueue_Push(&completed);
        CatimeIpcSnapshot next;
        if (CatimeIpcState_AdvanceQueued(&s_state, now, &next)) {
            IpcEventQueue_Push(&next);
            CatimeIpcPersistence_Save(&s_state);
        }
    }
    CatimeIpcSnapshot recovered;
    if (CatimeIpcState_GetAt(&s_state, EpochMilliseconds(), &recovered) &&
        (recovered.status == CATIME_IPC_STATUS_RUNNING ||
         recovered.status == CATIME_IPC_STATUS_PAUSED)) {
        s_applyingUiCommand = TRUE;
        CLOCK_SHOW_CURRENT_TIME = false;
        CLOCK_COUNT_UP = false;
        CLOCK_TOTAL_TIME = (int32_t)recovered.remainingSeconds;
        ResetTimer();
        if (recovered.status == CATIME_IPC_STATUS_PAUSED) TogglePauseTimer();
        s_applyingUiCommand = FALSE;
    }
    s_mainWindow = mainWindow;
    return TRUE;
}

void IpcSession_Shutdown(void) {
    s_mainWindow = NULL;
    IpcEventQueue_Shutdown();
    if (s_lockInitialized) {
        DeleteCriticalSection(&s_stateLock);
        s_lockInitialized = FALSE;
    }
}

LRESULT CatimeIpcServer_HandleUiMessage(HWND window, LPARAM parameter) {
    IpcUiCommand* command = (IpcUiCommand*)parameter;
    if (!command) return 0;
    EnterCriticalSection(&s_stateLock);
    int64_t now = EpochMilliseconds();
    switch (command->request.command) {
        case CATIME_IPC_COMMAND_START:
            command->error = CatimeIpcState_Start(&s_state,
                command->request.sessionId, command->request.durationSeconds,
                command->request.phase, now, &command->snapshot); break;
        case CATIME_IPC_COMMAND_PAUSE:
            command->error = CatimeIpcState_Pause(&s_state,
                command->request.sessionId, now, CATIME_IPC_CAUSE_CLIENT,
                &command->snapshot); break;
        case CATIME_IPC_COMMAND_RESUME:
            command->error = CatimeIpcState_Resume(&s_state,
                command->request.sessionId, now, CATIME_IPC_CAUSE_CLIENT,
                &command->snapshot); break;
        case CATIME_IPC_COMMAND_CANCEL:
            command->error = CatimeIpcState_Cancel(&s_state,
                command->request.sessionId, now, CATIME_IPC_CAUSE_CLIENT,
                &command->snapshot); break;
        case CATIME_IPC_COMMAND_FINISH:
            command->error = CatimeIpcState_Finish(&s_state,
                command->request.sessionId, now, CATIME_IPC_CAUSE_CLIENT,
                &command->snapshot); break;
        default: command->error = CATIME_IPC_ERROR_UNKNOWN_COMMAND; break;
    }
    command->hasSnapshot = command->error == CATIME_IPC_ERROR_NONE;
    if (command->hasSnapshot) CatimeIpcPersistence_Save(&s_state);
    LeaveCriticalSection(&s_stateLock);

    if (command->hasSnapshot) {
        s_applyingUiCommand = TRUE;
        if (command->request.command == CATIME_IPC_COMMAND_START) {
            CLOCK_SHOW_CURRENT_TIME = false;
            CLOCK_COUNT_UP = false;
            CLOCK_TOTAL_TIME = (int32_t)command->snapshot.plannedSeconds;
            ResetTimer();
        } else if (command->request.command == CATIME_IPC_COMMAND_PAUSE &&
                   !CLOCK_IS_PAUSED) TogglePauseTimer();
        else if (command->request.command == CATIME_IPC_COMMAND_RESUME &&
                 CLOCK_IS_PAUSED) TogglePauseTimer();
        else if (command->request.command == CATIME_IPC_COMMAND_CANCEL ||
                 command->request.command == CATIME_IPC_COMMAND_FINISH) {
            CLOCK_IS_PAUSED = true;
            countdown_elapsed_time = (int32_t)command->snapshot.focusedSeconds;
        }
        s_applyingUiCommand = FALSE;
        InvalidateRect(window, NULL, TRUE);
    }
    return 0;
}

BOOL CatimeIpcServer_NotifyTimeout(void) {
    if (!s_lockInitialized) return FALSE;
    CatimeIpcSnapshot completed;
    CatimeIpcSnapshot next;
    BOOL advanced = FALSE;
    EnterCriticalSection(&s_stateLock);
    int64_t now = EpochMilliseconds() + 1000;
    if (CatimeIpcState_Tick(&s_state, now, &completed)) {
        IpcEventQueue_Push(&completed);
        if (CatimeIpcState_AdvanceQueued(&s_state, now, &next)) {
            IpcEventQueue_Push(&next);
            advanced = TRUE;
        }
        CatimeIpcPersistence_Save(&s_state);
    }
    LeaveCriticalSection(&s_stateLock);
    if (advanced) {
        s_applyingUiCommand = TRUE;
        CLOCK_SHOW_CURRENT_TIME = false;
        CLOCK_COUNT_UP = false;
        CLOCK_TOTAL_TIME = (int32_t)next.plannedSeconds;
        ResetTimer();
        s_applyingUiCommand = FALSE;
    }
    return advanced;
}

void CatimeIpcServer_NotifyPauseChanged(BOOL paused) {
    if (!s_lockInitialized || s_applyingUiCommand) return;
    CatimeIpcSnapshot current;
    CatimeIpcSnapshot updated;
    EnterCriticalSection(&s_stateLock);
    if (CatimeIpcState_Get(&s_state, &current)) {
        CatimeIpcError error = paused ?
            CatimeIpcState_Pause(&s_state, current.sessionId,
                EpochMilliseconds(), CATIME_IPC_CAUSE_TRAY, &updated) :
            CatimeIpcState_Resume(&s_state, current.sessionId,
                EpochMilliseconds(), CATIME_IPC_CAUSE_TRAY, &updated);
        if (error == CATIME_IPC_ERROR_NONE) {
            IpcEventQueue_Push(&updated);
            CatimeIpcPersistence_Save(&s_state);
        }
    }
    LeaveCriticalSection(&s_stateLock);
}

void CatimeIpcServer_NotifyCancelled(void) {
    if (!s_lockInitialized || s_applyingUiCommand) return;
    CatimeIpcSnapshot current;
    CatimeIpcSnapshot updated;
    EnterCriticalSection(&s_stateLock);
    if (CatimeIpcState_Get(&s_state, &current) &&
        !CatimeIpc_IsTerminalStatus(current.status) &&
        CatimeIpcState_Cancel(&s_state, current.sessionId,
            EpochMilliseconds(), CATIME_IPC_CAUSE_USER_REPLACED,
            &updated) == CATIME_IPC_ERROR_NONE) {
        IpcEventQueue_Push(&updated);
        CatimeIpcPersistence_Save(&s_state);
    }
    LeaveCriticalSection(&s_stateLock);
}
