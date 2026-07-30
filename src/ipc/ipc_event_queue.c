/** @file ipc_event_queue.c Thread-safe delivery queue for state events. */

#include "ipc_server_internal.h"

#include <string.h>

#define IPC_EVENT_QUEUE_CAPACITY 64

typedef struct {
    CatimeIpcSnapshot snapshot;
    BOOL sent;
} IpcQueuedEvent;

static CRITICAL_SECTION s_eventLock;
static BOOL s_initialized = FALSE;
static IpcQueuedEvent s_events[IPC_EVENT_QUEUE_CAPACITY];
static uint32_t s_eventCount = 0;

BOOL IpcEventQueue_Initialize(void) {
    if (s_initialized) return TRUE;
    InitializeCriticalSection(&s_eventLock);
    s_initialized = TRUE;
    s_eventCount = 0;
    return TRUE;
}

void IpcEventQueue_Shutdown(void) {
    if (!s_initialized) return;
    DeleteCriticalSection(&s_eventLock);
    s_initialized = FALSE;
    s_eventCount = 0;
}

void IpcEventQueue_Push(const CatimeIpcSnapshot* snapshot) {
    if (!snapshot || !s_initialized) return;
    EnterCriticalSection(&s_eventLock);
    if (s_eventCount == IPC_EVENT_QUEUE_CAPACITY) {
        memmove(&s_events[0], &s_events[1],
                sizeof(s_events[0]) * (IPC_EVENT_QUEUE_CAPACITY - 1));
        s_eventCount--;
    }
    s_events[s_eventCount].snapshot = *snapshot;
    s_events[s_eventCount].sent = FALSE;
    s_eventCount++;
    LeaveCriticalSection(&s_eventLock);
}

BOOL IpcEventQueue_Acknowledge(const char* sessionId, uint64_t revision) {
    if (!sessionId || !s_initialized) return FALSE;
    BOOL removed = FALSE;
    EnterCriticalSection(&s_eventLock);
    for (uint32_t index = 0; index < s_eventCount; index++) {
        if (s_events[index].snapshot.revision == revision &&
            strcmp(s_events[index].snapshot.sessionId, sessionId) == 0) {
            if (index + 1 < s_eventCount) {
                memmove(&s_events[index], &s_events[index + 1],
                        sizeof(s_events[0]) * (s_eventCount - index - 1));
            }
            s_eventCount--;
            removed = TRUE;
            break;
        }
    }
    LeaveCriticalSection(&s_eventLock);
    return removed;
}

BOOL IpcSession_PeekEvent(CatimeIpcSnapshot* snapshot) {
    if (!snapshot || !s_initialized) return FALSE;
    BOOL found = FALSE;
    EnterCriticalSection(&s_eventLock);
    for (uint32_t index = 0; index < s_eventCount; index++) {
        if (!s_events[index].sent) {
            *snapshot = s_events[index].snapshot;
            found = TRUE;
            break;
        }
    }
    LeaveCriticalSection(&s_eventLock);
    return found;
}

void IpcSession_MarkEventSent(const CatimeIpcSnapshot* snapshot) {
    if (!snapshot || !s_initialized) return;
    EnterCriticalSection(&s_eventLock);
    for (uint32_t index = 0; index < s_eventCount; index++) {
        if (s_events[index].snapshot.revision == snapshot->revision &&
            strcmp(s_events[index].snapshot.sessionId,
                   snapshot->sessionId) == 0) {
            s_events[index].sent = TRUE;
            break;
        }
    }
    LeaveCriticalSection(&s_eventLock);
}

void IpcSession_ResetEventDelivery(void) {
    if (!s_initialized) return;
    EnterCriticalSection(&s_eventLock);
    for (uint32_t index = 0; index < s_eventCount; index++) {
        s_events[index].sent = FALSE;
    }
    LeaveCriticalSection(&s_eventLock);
}
