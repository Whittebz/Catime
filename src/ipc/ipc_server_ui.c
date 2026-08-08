/**
 * @file ipc_server_ui.c
 * @brief UI-thread bridge that applies IPC state changes to the clock.
 *
 * Added by the Week Planner Calendar fork. Commands and local gestures
 * (click-to-start focus, pause, end focus, countdown timeout) mutate the shared
 * external session state under the lock, push the resulting snapshot to every
 * connected client, and drive the on-screen countdown.
 */

#include "ipc/catime_ipc_server.h"
#include "ipc/catime_ipc_history.h"
#include "ipc/catime_ipc_persistence.h"
#include "ipc_server_internal.h"
#include "ipc_server_session_internal.h"
#include "timer/timer.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t s_sessionSequence = 0;

BOOL IpcState_GenerateSessionId(char* output, size_t capacity) {
    if (!output || capacity < 24) return FALSE;
    s_sessionSequence++;
    int length = snprintf(output, capacity,
        "catime-%08llx-%04x-%04llx",
        (unsigned long long)GetTickCount64(),
        (unsigned)GetCurrentProcessId(),
        (unsigned long long)s_sessionSequence);
    return length > 0 && (size_t)length < capacity;
}

LRESULT CatimeIpcServer_HandleUiMessage(HWND window, LPARAM parameter) {
    IpcUiCommand* command = (IpcUiCommand*)parameter;
    if (!command) return 0;
    EnterCriticalSection(&g_ipcStateLock);
    int64_t now = IpcState_EpochMilliseconds();
    switch (command->request.command) {
        case CATIME_IPC_COMMAND_START:
            command->error = CatimeIpcState_Start(&g_ipcState,
                command->request.sessionId, command->request.durationSeconds,
                command->request.phase, now, &command->snapshot); break;
        case CATIME_IPC_COMMAND_PAUSE:
            command->error = CatimeIpcState_Pause(&g_ipcState,
                command->request.sessionId, now, CATIME_IPC_CAUSE_CLIENT,
                &command->snapshot); break;
        case CATIME_IPC_COMMAND_RESUME:
            command->error = CatimeIpcState_Resume(&g_ipcState,
                command->request.sessionId, now, CATIME_IPC_CAUSE_CLIENT,
                &command->snapshot); break;
        case CATIME_IPC_COMMAND_CANCEL:
            command->error = CatimeIpcState_Cancel(&g_ipcState,
                command->request.sessionId, now, CATIME_IPC_CAUSE_CLIENT,
                &command->snapshot); break;
        case CATIME_IPC_COMMAND_FINISH:
            command->error = CatimeIpcState_Finish(&g_ipcState,
                command->request.sessionId, now, CATIME_IPC_CAUSE_CLIENT,
                &command->snapshot); break;
        default: command->error = CATIME_IPC_ERROR_UNKNOWN_COMMAND; break;
    }
    command->hasSnapshot = command->error == CATIME_IPC_ERROR_NONE;
    if (command->hasSnapshot) {
        IpcEventQueue_Push(&command->snapshot);
        IpcState_RecordTerminal(&command->snapshot);
        IpcState_SaveWithHistory();
    }
    LeaveCriticalSection(&g_ipcStateLock);

    if (command->hasSnapshot) {
        g_ipcApplyingUiCommand = TRUE;
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
        g_ipcApplyingUiCommand = FALSE;
        InvalidateRect(window, NULL, TRUE);
    }
    return 0;
}

BOOL CatimeIpcServer_NotifyTimeout(void) {
    if (!g_ipcLockInitialized) return FALSE;
    CatimeIpcSnapshot completed;
    CatimeIpcSnapshot next;
    BOOL advanced = FALSE;
    EnterCriticalSection(&g_ipcStateLock);
    int64_t now = IpcState_EpochMilliseconds() + 1000;
    if (CatimeIpcState_Tick(&g_ipcState, now, &completed)) {
        IpcEventQueue_Push(&completed);
        IpcState_RecordTerminal(&completed);
        if (CatimeIpcState_AdvanceQueued(&g_ipcState, now, &next)) {
            IpcEventQueue_Push(&next);
            advanced = TRUE;
        }
        IpcState_SaveWithHistory();
    }
    LeaveCriticalSection(&g_ipcStateLock);
    if (advanced) {
        g_ipcApplyingUiCommand = TRUE;
        CLOCK_SHOW_CURRENT_TIME = false;
        CLOCK_COUNT_UP = false;
        CLOCK_TOTAL_TIME = (int32_t)next.plannedSeconds;
        ResetTimer();
        g_ipcApplyingUiCommand = FALSE;
    }
    return advanced;
}

void CatimeIpcServer_NotifyPauseChanged(BOOL paused) {
    if (!g_ipcLockInitialized || g_ipcApplyingUiCommand) return;
    CatimeIpcSnapshot current;
    CatimeIpcSnapshot updated;
    EnterCriticalSection(&g_ipcStateLock);
    if (CatimeIpcState_Get(&g_ipcState, &current)) {
        CatimeIpcError error = paused ?
            CatimeIpcState_Pause(&g_ipcState, current.sessionId,
                IpcState_EpochMilliseconds(), CATIME_IPC_CAUSE_TRAY, &updated) :
            CatimeIpcState_Resume(&g_ipcState, current.sessionId,
                IpcState_EpochMilliseconds(), CATIME_IPC_CAUSE_TRAY, &updated);
        if (error == CATIME_IPC_ERROR_NONE) {
            IpcEventQueue_Push(&updated);
            IpcState_SaveWithHistory();
        }
    }
    LeaveCriticalSection(&g_ipcStateLock);
}

void CatimeIpcServer_NotifyCancelled(void) {
    if (!g_ipcLockInitialized || g_ipcApplyingUiCommand) return;
    CatimeIpcSnapshot current;
    CatimeIpcSnapshot updated;
    EnterCriticalSection(&g_ipcStateLock);
    if (CatimeIpcState_Get(&g_ipcState, &current) &&
        !CatimeIpc_IsTerminalStatus(current.status) &&
        CatimeIpcState_Cancel(&g_ipcState, current.sessionId,
            IpcState_EpochMilliseconds(), CATIME_IPC_CAUSE_USER_REPLACED,
            &updated) == CATIME_IPC_ERROR_NONE) {
        IpcEventQueue_Push(&updated);
        IpcState_RecordTerminal(&updated);
        IpcState_SaveWithHistory();
    }
    LeaveCriticalSection(&g_ipcStateLock);
}

BOOL CatimeIpcServer_NotifyStarted(uint32_t durationSeconds) {
    if (!g_ipcLockInitialized || g_ipcApplyingUiCommand) return FALSE;
    char sessionId[CATIME_IPC_MAX_SESSION_ID_BYTES + 1];
    CatimeIpcSnapshot started;
    CatimeIpcError error = CATIME_IPC_ERROR_INTERNAL_ERROR;
    EnterCriticalSection(&g_ipcStateLock);
    if (IpcState_GenerateSessionId(sessionId, sizeof(sessionId))) {
        CatimeIpcSnapshot current;
        if (CatimeIpcState_Get(&g_ipcState, &current) &&
            !CatimeIpc_IsTerminalStatus(current.status)) {
            /* The user explicitly replaces the running/paused session. */
            CatimeIpcSnapshot replaced;
            if (CatimeIpcState_Cancel(&g_ipcState, current.sessionId,
                    IpcState_EpochMilliseconds(), CATIME_IPC_CAUSE_USER_REPLACED,
                    &replaced) == CATIME_IPC_ERROR_NONE) {
                IpcEventQueue_Push(&replaced);
                IpcState_RecordTerminal(&replaced);
            }
        }
        error = CatimeIpcState_Start(&g_ipcState, sessionId, durationSeconds,
                                     CATIME_IPC_PHASE_FOCUS,
                                     IpcState_EpochMilliseconds(), &started);
    }
    if (error == CATIME_IPC_ERROR_NONE) {
        IpcEventQueue_Push(&started);
        IpcState_SaveWithHistory();
    }
    LeaveCriticalSection(&g_ipcStateLock);
    if (error != CATIME_IPC_ERROR_NONE) return FALSE;

    g_ipcApplyingUiCommand = TRUE;
    CLOCK_SHOW_CURRENT_TIME = false;
    CLOCK_COUNT_UP = false;
    CLOCK_TOTAL_TIME = (int32_t)started.plannedSeconds;
    ResetTimer();
    g_ipcApplyingUiCommand = FALSE;
    return TRUE;
}

BOOL CatimeIpcServer_NotifyFinished(void) {
    if (!g_ipcLockInitialized || g_ipcApplyingUiCommand) return FALSE;
    CatimeIpcSnapshot current;
    CatimeIpcSnapshot updated;
    BOOL finished = FALSE;
    EnterCriticalSection(&g_ipcStateLock);
    if (CatimeIpcState_Get(&g_ipcState, &current) &&
        !CatimeIpc_IsTerminalStatus(current.status) &&
        CatimeIpcState_Finish(&g_ipcState, current.sessionId,
            IpcState_EpochMilliseconds(), CATIME_IPC_CAUSE_CLIENT,
            &updated) == CATIME_IPC_ERROR_NONE) {
        IpcEventQueue_Push(&updated);
        IpcState_RecordTerminal(&updated);
        IpcState_SaveWithHistory();
        finished = TRUE;
    }
    LeaveCriticalSection(&g_ipcStateLock);
    if (!finished) return FALSE;

    g_ipcApplyingUiCommand = TRUE;
    CLOCK_IS_PAUSED = true;
    countdown_elapsed_time = (int32_t)updated.focusedSeconds;
    g_ipcApplyingUiCommand = FALSE;
    return TRUE;
}
