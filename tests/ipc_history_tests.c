/**
 * @file ipc_history_tests.c
 * @brief Pure-C coverage for the durable terminal-session history.
 */

#include "ipc/catime_ipc_history.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static CatimeIpcSnapshot Snapshot(const char* sessionId,
                                  uint64_t revision) {
    CatimeIpcSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    strncpy(snapshot.sessionId, sessionId, sizeof(snapshot.sessionId) - 1);
    snapshot.status = CATIME_IPC_STATUS_COMPLETED;
    snapshot.phase = CATIME_IPC_PHASE_FOCUS;
    snapshot.plannedSeconds = 1500;
    snapshot.focusedSeconds = 1500;
    snapshot.remainingSeconds = 0;
    snapshot.startedAt = 1000;
    snapshot.updatedAt = 151000;
    snapshot.endedAt = 151000;
    snapshot.revision = revision;
    snapshot.cause = CATIME_IPC_CAUSE_TIMEOUT;
    return snapshot;
}

static void TestLifecycle(void) {
    IpcHistory history;
    IpcHistory_Init(&history);
    assert(IpcHistory_Count(&history) == 0);

    CatimeIpcSnapshot first = Snapshot("s-1", 1);
    CatimeIpcSnapshot second = Snapshot("s-2", 2);
    CatimeIpcSnapshot third = Snapshot("s-3", 3);
    IpcHistory_Add(&history, &first);
    IpcHistory_Add(&history, &second);
    IpcHistory_Add(&history, &third);
    assert(IpcHistory_Count(&history) == 3);

    const CatimeIpcSnapshot* snapshot = NULL;
    assert(IpcHistory_Get(&history, 0, &snapshot));
    assert(strcmp(snapshot->sessionId, "s-1") == 0);
    assert(snapshot->revision == 1);
    assert(!IpcHistory_IsDelivered(&history, 0));
    IpcHistory_MarkDelivered(&history, 0);
    assert(IpcHistory_IsDelivered(&history, 0));
    assert(IpcHistory_Get(&history, 2, &snapshot));
    assert(strcmp(snapshot->sessionId, "s-3") == 0);

    assert(IpcHistory_Remove(&history, "s-2", 2));
    assert(IpcHistory_Count(&history) == 2);
    assert(!IpcHistory_Remove(&history, "s-2", 2));
    assert(IpcHistory_Get(&history, 1, &snapshot));
    assert(strcmp(snapshot->sessionId, "s-3") == 0);
}

static void TestBounded(void) {
    IpcHistory history;
    IpcHistory_Init(&history);
    for (int index = 0; index < CATIME_IPC_MAX_HISTORY + 10; index++) {
        char sessionId[24];
        snprintf(sessionId, sizeof(sessionId), "s-%d", index);
        CatimeIpcSnapshot snapshot =
            Snapshot(sessionId, (uint64_t)index + 1);
        IpcHistory_Add(&history, &snapshot);
    }
    assert(IpcHistory_Count(&history) == CATIME_IPC_MAX_HISTORY);
    const CatimeIpcSnapshot* snapshot = NULL;
    assert(IpcHistory_Get(&history, 0, &snapshot));
    assert(strcmp(snapshot->sessionId, "s-10") == 0);
    assert(IpcHistory_Get(&history, CATIME_IPC_MAX_HISTORY - 1, &snapshot));
    assert(strcmp(snapshot->sessionId, "s-73") == 0);
}

int main(void) {
    TestLifecycle();
    TestBounded();
    return 0;
}
