/**
 * @file catime_ipc_history.h
 * @brief Durable record of terminal external sessions for offline backfill.
 *
 * Added by the Week Planner Calendar fork. Holds the last N terminal snapshots
 * (completed/cancelled) that no client has acknowledged yet, so a plugin that
 * connects after the fact can reconcile the full session history instead of
 * only the most recent state.
 */

#ifndef CATIME_IPC_HISTORY_H
#define CATIME_IPC_HISTORY_H

#include "ipc/catime_ipc_state.h"

#include <stdbool.h>
#include <stdint.h>

#define CATIME_IPC_MAX_HISTORY 64

typedef struct {
    CatimeIpcSnapshot snapshot;
    bool delivered; /* whether the entry has been enqueued for delivery */
} IpcHistoryEntry;

typedef struct {
    IpcHistoryEntry entries[CATIME_IPC_MAX_HISTORY];
    uint32_t count;
} IpcHistory;

void IpcHistory_Init(IpcHistory* history);
void IpcHistory_Add(IpcHistory* history, const CatimeIpcSnapshot* snapshot);
bool IpcHistory_Remove(IpcHistory* history, const char* sessionId,
                       uint64_t revision);
bool IpcHistory_Get(const IpcHistory* history, uint32_t index,
                    const CatimeIpcSnapshot** snapshot);
bool IpcHistory_IsDelivered(const IpcHistory* history, uint32_t index);
uint32_t IpcHistory_Count(const IpcHistory* history);
void IpcHistory_MarkDelivered(IpcHistory* history, uint32_t index);

#endif /* CATIME_IPC_HISTORY_H */
