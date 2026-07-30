#include "ipc/catime_ipc_protocol.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void Expect(const char* name, bool value) {
    if (!value) {
        fprintf(stderr, "%s\n", name);
        g_failures++;
    }
}

static void ExpectString(const char* name, const char* actual,
                         const char* expected) {
    if (!actual || strcmp(actual, expected) != 0) {
        fprintf(stderr, "%s: expected %s, got %s\n", name, expected,
                actual ? actual : "(null)");
        g_failures++;
    }
}

static void TestStableNames(void) {
    ExpectString("running status",
                 CatimeIpc_StatusName(CATIME_IPC_STATUS_RUNNING), "running");
    ExpectString("short break phase",
                 CatimeIpc_PhaseName(CATIME_IPC_PHASE_SHORT_BREAK),
                 "short_break");
    ExpectString("timeout cause",
                 CatimeIpc_CauseName(CATIME_IPC_CAUSE_TIMEOUT), "timeout");
    ExpectString("session conflict error",
                 CatimeIpc_ErrorCode(CATIME_IPC_ERROR_SESSION_CONFLICT),
                 "session_conflict");
    Expect("unknown status rejected", CatimeIpc_StatusName((CatimeIpcStatus)99) == NULL);
    Expect("unknown error rejected", CatimeIpc_ErrorCode((CatimeIpcError)99) == NULL);
}

static void TestStateTransitions(void) {
    Expect("idle starts running",
           CatimeIpc_CanTransition(CATIME_IPC_STATUS_IDLE,
                                   CATIME_IPC_STATUS_RUNNING));
    Expect("running pauses",
           CatimeIpc_CanTransition(CATIME_IPC_STATUS_RUNNING,
                                   CATIME_IPC_STATUS_PAUSED));
    Expect("paused resumes",
           CatimeIpc_CanTransition(CATIME_IPC_STATUS_PAUSED,
                                   CATIME_IPC_STATUS_RUNNING));
    Expect("running completes",
           CatimeIpc_CanTransition(CATIME_IPC_STATUS_RUNNING,
                                   CATIME_IPC_STATUS_COMPLETED));
    Expect("paused can finish",
           CatimeIpc_CanTransition(CATIME_IPC_STATUS_PAUSED,
                                   CATIME_IPC_STATUS_COMPLETED));
    Expect("terminal resets to idle",
           CatimeIpc_CanTransition(CATIME_IPC_STATUS_COMPLETED,
                                   CATIME_IPC_STATUS_IDLE));
    Expect("idle cannot pause",
           !CatimeIpc_CanTransition(CATIME_IPC_STATUS_IDLE,
                                    CATIME_IPC_STATUS_PAUSED));
    Expect("completed cannot resume",
           !CatimeIpc_CanTransition(CATIME_IPC_STATUS_COMPLETED,
                                    CATIME_IPC_STATUS_RUNNING));
    Expect("same-state is not a mutation",
           !CatimeIpc_CanTransition(CATIME_IPC_STATUS_RUNNING,
                                    CATIME_IPC_STATUS_RUNNING));
}

static void TestBoundsAndOrdering(void) {
    Expect("minimum duration accepted", CatimeIpc_IsValidDuration(1));
    Expect("maximum duration accepted", CatimeIpc_IsValidDuration(10800));
    Expect("zero duration rejected", !CatimeIpc_IsValidDuration(0));
    Expect("oversized duration rejected", !CatimeIpc_IsValidDuration(10801));
    Expect("larger revision accepted", CatimeIpc_IsNewerRevision(4, 5));
    Expect("duplicate revision rejected", !CatimeIpc_IsNewerRevision(5, 5));
    Expect("older revision rejected", !CatimeIpc_IsNewerRevision(5, 4));
}

int main(void) {
    TestStableNames();
    TestStateTransitions();
    TestBoundsAndOrdering();

    if (g_failures != 0) {
        fprintf(stderr, "%d IPC protocol test(s) failed\n", g_failures);
        return 1;
    }
    return 0;
}
