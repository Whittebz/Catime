/**
 * @file ipc_history.c
 * @brief Bounded, in-memory terminal-session history for offline backfill.
 *
 * Added by the Week Planner Calendar fork. This module is intentionally
 * platform-independent so the lifecycle can be covered by the pure-C tests.
 */

#include "ipc/catime_ipc_history.h"

#include <string.h>

void IpcHistory_Init(IpcHistory* history) {
    if (!history) return;
    memset(history, 0, sizeof(*history));
}

void IpcHistory_Add(IpcHistory* history, const CatimeIpcSnapshot* snapshot) {
    if (!history || !snapshot) return;
    if (history->count == CATIME_IPC_MAX_HISTORY) {
        memmove(&history->entries[0], &history->entries[1],
                sizeof(history->entries[0]) *
                    (CATIME_IPC_MAX_HISTORY - 1));
        history->count--;
    }
    history->entries[history->count].snapshot = *snapshot;
    history->entries[history->count].delivered = false;
    history->count++;
}

bool IpcHistory_Remove(IpcHistory* history, const char* sessionId,
                       uint64_t revision) {
    if (!history || !sessionId) return false;
    for (uint32_t index = 0; index < history->count; index++) {
        const IpcHistoryEntry* entry = &history->entries[index];
        if (entry->snapshot.revision == revision &&
            strcmp(entry->snapshot.sessionId, sessionId) == 0) {
            if (index + 1 < history->count) {
                memmove(&history->entries[index],
                        &history->entries[index + 1],
                        sizeof(history->entries[0]) *
                            (history->count - index - 1));
            }
            history->count--;
            return true;
        }
    }
    return false;
}

bool IpcHistory_Get(const IpcHistory* history, uint32_t index,
                    const CatimeIpcSnapshot** snapshot) {
    if (!history || !snapshot || index >= history->count) return false;
    *snapshot = &history->entries[index].snapshot;
    return true;
}

bool IpcHistory_IsDelivered(const IpcHistory* history, uint32_t index) {
    return history && index < history->count &&
           history->entries[index].delivered;
}

uint32_t IpcHistory_Count(const IpcHistory* history) {
    return history ? history->count : 0;
}

void IpcHistory_MarkDelivered(IpcHistory* history, uint32_t index) {
    if (history && index < history->count) {
        history->entries[index].delivered = true;
    }
}
