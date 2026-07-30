#include "ipc/catime_ipc_state.h"

#include <assert.h>
#include <string.h>

static void TestLifecycle(void) {
    CatimeIpcState state;
    CatimeIpcSnapshot snapshot;
    CatimeIpcState_Init(&state);
    assert(!CatimeIpcState_Get(&state, &snapshot));
    assert(CatimeIpcState_Start(&state, "session-1", 1500,
        CATIME_IPC_PHASE_FOCUS, 1000, &snapshot) == CATIME_IPC_ERROR_NONE);
    assert(snapshot.status == CATIME_IPC_STATUS_RUNNING);
    assert(snapshot.revision == 1);
    assert(CatimeIpcState_GetAt(&state, 6000, &snapshot));
    assert(snapshot.focusedSeconds == 5);
    assert(snapshot.revision == 1);
    assert(CatimeIpcState_Pause(&state, "session-1", 11000,
        CATIME_IPC_CAUSE_CLIENT, &snapshot) == CATIME_IPC_ERROR_NONE);
    assert(snapshot.status == CATIME_IPC_STATUS_PAUSED);
    assert(snapshot.focusedSeconds == 10);
    assert(snapshot.revision == 2);
    assert(CatimeIpcState_Resume(&state, "session-1", 21000,
        CATIME_IPC_CAUSE_CLIENT, &snapshot) == CATIME_IPC_ERROR_NONE);
    assert(snapshot.deadlineAt == 1511000);
    assert(CatimeIpcState_Finish(&state, "session-1", 31000,
        CATIME_IPC_CAUSE_CLIENT, &snapshot) == CATIME_IPC_ERROR_NONE);
    assert(snapshot.status == CATIME_IPC_STATUS_COMPLETED);
    assert(snapshot.focusedSeconds == 20);
    assert(snapshot.revision == 4);
    assert(CatimeIpcState_Acknowledge(&state, "session-1", 4) ==
        CATIME_IPC_ERROR_NONE);
}

static void TestIdempotencyAndConflict(void) {
    CatimeIpcState state;
    CatimeIpcSnapshot first;
    CatimeIpcSnapshot repeated;
    CatimeIpcState_Init(&state);
    assert(CatimeIpcState_Start(&state, "same", 60,
        CATIME_IPC_PHASE_SHORT_BREAK, 1000, &first) == CATIME_IPC_ERROR_NONE);
    assert(CatimeIpcState_Start(&state, "same", 60,
        CATIME_IPC_PHASE_SHORT_BREAK, 9000, &repeated) == CATIME_IPC_ERROR_NONE);
    assert(memcmp(&first, &repeated, sizeof(first)) == 0);
    assert(CatimeIpcState_Start(&state, "other", 60,
        CATIME_IPC_PHASE_FOCUS, 9000, &repeated) ==
        CATIME_IPC_ERROR_SESSION_CONFLICT);
}

static void TestTimeout(void) {
    CatimeIpcState state;
    CatimeIpcSnapshot snapshot;
    CatimeIpcState_Init(&state);
    assert(CatimeIpcState_Start(&state, "timeout", 2,
        CATIME_IPC_PHASE_LONG_BREAK, 1000, &snapshot) == CATIME_IPC_ERROR_NONE);
    assert(!CatimeIpcState_Tick(&state, 2999, &snapshot));
    assert(CatimeIpcState_Tick(&state, 3000, &snapshot));
    assert(snapshot.status == CATIME_IPC_STATUS_COMPLETED);
    assert(snapshot.focusedSeconds == 2);
    assert(snapshot.cause == CATIME_IPC_CAUSE_TIMEOUT);
}

int main(void) {
    TestLifecycle();
    TestIdempotencyAndConflict();
    TestTimeout();
    return 0;
}
