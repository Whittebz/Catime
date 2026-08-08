/**
 * @file catime_ipc_queue.h
 * @brief Platform-independent delivery queue for external session events.
 *
 * Added by the Week Planner Calendar fork. The queue hands pending snapshots
 * to every connected client and only drops an event once all connected clients
 * have acknowledged it, so a second Obsidian vault never misses a completion.
 */

#ifndef CATIME_IPC_QUEUE_H
#define CATIME_IPC_QUEUE_H

#include "ipc/catime_ipc_state.h"

#include <stdbool.h>
#include <stdint.h>

/* Maximum simultaneous Obsidian plugin connections (one per vault). */
#define IPC_MAX_CLIENTS 8
#define IPC_EVENT_QUEUE_CAPACITY 64

bool IpcEventQueue_Initialize(void);
void IpcEventQueue_Shutdown(void);
int IpcEventQueue_RegisterClient(void);
void IpcEventQueue_UnregisterClient(int clientId);
void IpcEventQueue_Push(const CatimeIpcSnapshot* snapshot);
/* 0 = unknown event, 1 = ack recorded (waiting on other clients), 2 = removed. */
int IpcEventQueue_Acknowledge(int clientId, const char* sessionId,
                              uint64_t revision);

bool IpcSession_PeekEvent(int clientId, CatimeIpcSnapshot* snapshot);
void IpcSession_MarkEventSent(int clientId,
                              const CatimeIpcSnapshot* snapshot);
void IpcSession_ResetEventDelivery(int clientId);

#endif /* CATIME_IPC_QUEUE_H */
