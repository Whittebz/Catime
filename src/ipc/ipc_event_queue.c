/**
 * @file ipc_event_queue.c
 * @brief Thread-safe delivery queue for state events with per-client acks.
 *
 * Added by the Week Planner Calendar fork. The queue keeps unacknowledged
 * terminal/live snapshots and hands each pending event to every connected
 * client. An event is only removed once every currently connected client has
 * acknowledged it, so a second Obsidian vault that connects mid-session never
 * misses a completion. The lock is portable so the delivery rules are covered
 * by the pure-C tests on any host.
 */

#include "ipc/catime_ipc_lock.h"
#include "ipc/catime_ipc_queue.h"

#include <string.h>

typedef struct {
    CatimeIpcSnapshot snapshot;
    uint32_t deliveredMask;
    uint32_t ackedMask;
} IpcQueuedEvent;

static IpcLock s_eventLock;
static bool s_initialized = false;
static IpcQueuedEvent s_events[IPC_EVENT_QUEUE_CAPACITY];
static uint32_t s_eventCount = 0;
static uint32_t s_clientMask = 0;

static uint32_t ClientBit(int clientId) {
    return clientId >= 0 && clientId < IPC_MAX_CLIENTS
        ? (1u << clientId) : 0u;
}

static bool EventFullyAcked(const IpcQueuedEvent* event) {
    return s_clientMask != 0 && (event->ackedMask & s_clientMask) == s_clientMask;
}

/* Remove every event that all connected clients have acknowledged. */
static void CompactEvents(void) {
    uint32_t write = 0;
    for (uint32_t read = 0; read < s_eventCount; read++) {
        if (EventFullyAcked(&s_events[read])) continue;
        if (write != read) s_events[write] = s_events[read];
        write++;
    }
    s_eventCount = write;
}

bool IpcEventQueue_Initialize(void) {
    if (s_initialized) return true;
    IPC_LOCK_INIT(&s_eventLock);
    s_initialized = true;
    s_eventCount = 0;
    s_clientMask = 0;
    return true;
}

void IpcEventQueue_Shutdown(void) {
    if (!s_initialized) return;
    IPC_LOCK_DESTROY(&s_eventLock);
    s_initialized = false;
    s_eventCount = 0;
    s_clientMask = 0;
}

int IpcEventQueue_RegisterClient(void) {
    if (!s_initialized) return -1;
    IPC_LOCK_ENTER(&s_eventLock);
    for (int clientId = 0; clientId < IPC_MAX_CLIENTS; clientId++) {
        if (!(s_clientMask & (1u << clientId))) {
            s_clientMask |= (1u << clientId);
            IPC_LOCK_LEAVE(&s_eventLock);
            return clientId;
        }
    }
    IPC_LOCK_LEAVE(&s_eventLock);
    return -1;
}

void IpcEventQueue_UnregisterClient(int clientId) {
    uint32_t bit = ClientBit(clientId);
    if (!s_initialized || bit == 0) return;
    IPC_LOCK_ENTER(&s_eventLock);
    s_clientMask &= ~bit;
    for (uint32_t index = 0; index < s_eventCount; index++) {
        s_events[index].deliveredMask &= ~bit;
        s_events[index].ackedMask &= ~bit;
    }
    CompactEvents();
    IPC_LOCK_LEAVE(&s_eventLock);
}

void IpcEventQueue_Push(const CatimeIpcSnapshot* snapshot) {
    if (!snapshot || !s_initialized) return;
    IPC_LOCK_ENTER(&s_eventLock);
    if (s_eventCount == IPC_EVENT_QUEUE_CAPACITY) {
        memmove(&s_events[0], &s_events[1],
                sizeof(s_events[0]) * (IPC_EVENT_QUEUE_CAPACITY - 1));
        s_eventCount--;
    }
    s_events[s_eventCount].snapshot = *snapshot;
    s_events[s_eventCount].deliveredMask = 0;
    s_events[s_eventCount].ackedMask = 0;
    s_eventCount++;
    IPC_LOCK_LEAVE(&s_eventLock);
}

bool IpcSession_PeekEvent(int clientId, CatimeIpcSnapshot* snapshot) {
    uint32_t bit = ClientBit(clientId);
    if (!s_initialized || snapshot == NULL || bit == 0) return false;
    bool found = false;
    IPC_LOCK_ENTER(&s_eventLock);
    for (uint32_t index = 0; index < s_eventCount; index++) {
        if ((s_events[index].deliveredMask & bit) == 0) {
            *snapshot = s_events[index].snapshot;
            found = true;
            break;
        }
    }
    IPC_LOCK_LEAVE(&s_eventLock);
    return found;
}

void IpcSession_MarkEventSent(int clientId, const CatimeIpcSnapshot* snapshot) {
    uint32_t bit = ClientBit(clientId);
    if (!s_initialized || snapshot == NULL || bit == 0) return;
    IPC_LOCK_ENTER(&s_eventLock);
    for (uint32_t index = 0; index < s_eventCount; index++) {
        if (s_events[index].snapshot.revision == snapshot->revision &&
            strcmp(s_events[index].snapshot.sessionId,
                   snapshot->sessionId) == 0) {
            s_events[index].deliveredMask |= bit;
            break;
        }
    }
    IPC_LOCK_LEAVE(&s_eventLock);
}

void IpcSession_ResetEventDelivery(int clientId) {
    uint32_t bit = ClientBit(clientId);
    if (!s_initialized || bit == 0) return;
    IPC_LOCK_ENTER(&s_eventLock);
    for (uint32_t index = 0; index < s_eventCount; index++) {
        s_events[index].deliveredMask &= ~bit;
    }
    IPC_LOCK_LEAVE(&s_eventLock);
}

int IpcEventQueue_Acknowledge(int clientId, const char* sessionId,
                              uint64_t revision) {
    uint32_t bit = ClientBit(clientId);
    if (!s_initialized || sessionId == NULL || bit == 0) return 0;
    int result = 0;
    IPC_LOCK_ENTER(&s_eventLock);
    for (uint32_t index = 0; index < s_eventCount; index++) {
        IpcQueuedEvent* event = &s_events[index];
        if (event->snapshot.revision == revision &&
            strcmp(event->snapshot.sessionId, sessionId) == 0) {
            event->ackedMask |= bit;
            if (EventFullyAcked(event)) {
                /* Remove this single event now; others are swept below. */
                if (index + 1 < s_eventCount) {
                    memmove(&s_events[index], &s_events[index + 1],
                            sizeof(s_events[0]) * (s_eventCount - index - 1));
                }
                s_eventCount--;
                result = 2;
            } else {
                result = 1;
            }
            break;
        }
    }
    if (result == 2) CompactEvents();
    IPC_LOCK_LEAVE(&s_eventLock);
    return result;
}
