/**
 * @file ipc_state.c
 * @brief Platform-independent external timer state machine.
 */

#include "ipc/catime_ipc_state.h"

#include <string.h>

static bool CopySessionId(char* destination, const char* source) {
    if (!destination || !source) return false;
    size_t length = strlen(source);
    if (length == 0 || length > CATIME_IPC_MAX_SESSION_ID_BYTES) return false;
    memcpy(destination, source, length + 1);
    return true;
}

static bool SessionMatches(const CatimeIpcState* state,
                           const char* sessionId) {
    return state && state->hasSession && sessionId &&
           strcmp(state->snapshot.sessionId, sessionId) == 0;
}

static void RefreshRunning(CatimeIpcState* state, int64_t nowMs) {
    if (!state || state->snapshot.status != CATIME_IPC_STATUS_RUNNING) return;
    int64_t remainingMs = state->snapshot.deadlineAt - nowMs;
    uint32_t remaining = remainingMs > 0
        ? (uint32_t)((remainingMs + 999) / 1000) : 0;
    if (remaining > state->snapshot.plannedSeconds) {
        remaining = state->snapshot.plannedSeconds;
    }
    state->snapshot.remainingSeconds = remaining;
    state->snapshot.focusedSeconds =
        state->snapshot.plannedSeconds - remaining;
}

static CatimeIpcError RequireMutableSession(CatimeIpcState* state,
                                            const char* sessionId,
                                            CatimeIpcStatus target) {
    if (!SessionMatches(state, sessionId)) {
        return state && state->hasSession
            ? CATIME_IPC_ERROR_SESSION_MISMATCH
            : CATIME_IPC_ERROR_INVALID_STATE;
    }
    return CatimeIpc_CanTransition(state->snapshot.status, target)
        ? CATIME_IPC_ERROR_NONE : CATIME_IPC_ERROR_INVALID_STATE;
}

static void CommitMutation(CatimeIpcState* state,
                           CatimeIpcStatus status,
                           CatimeIpcCause cause,
                           int64_t nowMs) {
    state->snapshot.status = status;
    state->snapshot.cause = cause;
    state->snapshot.updatedAt = nowMs;
    state->snapshot.revision++;
    state->snapshot.deadlineAt = 0;
    if (CatimeIpc_IsTerminalStatus(status)) {
        state->snapshot.endedAt = nowMs;
    }
}

void CatimeIpcState_Init(CatimeIpcState* state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->snapshot.status = CATIME_IPC_STATUS_IDLE;
    state->snapshot.phase = CATIME_IPC_PHASE_FOCUS;
    state->snapshot.cause = CATIME_IPC_CAUSE_CLIENT;
}

CatimeIpcError CatimeIpcState_Start(CatimeIpcState* state,
                                    const char* sessionId,
                                    uint32_t durationSeconds,
                                    CatimeIpcPhase phase,
                                    int64_t nowMs,
                                    CatimeIpcSnapshot* output) {
    if (!state || !output || !CopySessionId(output->sessionId, sessionId)) {
        return CATIME_IPC_ERROR_INVALID_SESSION_ID;
    }
    if (!CatimeIpc_IsValidDuration(durationSeconds)) {
        return CATIME_IPC_ERROR_INVALID_DURATION;
    }
    if (phase < CATIME_IPC_PHASE_FOCUS || phase > CATIME_IPC_PHASE_LONG_BREAK) {
        return CATIME_IPC_ERROR_INVALID_REQUEST;
    }
    if (state->hasSession) {
        if (SessionMatches(state, sessionId) &&
            state->snapshot.plannedSeconds == durationSeconds &&
            state->snapshot.phase == phase) {
            *output = state->snapshot;
            return CATIME_IPC_ERROR_NONE;
        }
        if (!CatimeIpc_IsTerminalStatus(state->snapshot.status)) {
            return CATIME_IPC_ERROR_SESSION_CONFLICT;
        }
    }

    CatimeIpcState_Init(state);
    state->hasSession = true;
    CopySessionId(state->snapshot.sessionId, sessionId);
    state->snapshot.status = CATIME_IPC_STATUS_RUNNING;
    state->snapshot.phase = phase;
    state->snapshot.plannedSeconds = durationSeconds;
    state->snapshot.remainingSeconds = durationSeconds;
    state->snapshot.startedAt = nowMs;
    state->snapshot.updatedAt = nowMs;
    state->snapshot.deadlineAt = nowMs + ((int64_t)durationSeconds * 1000);
    state->snapshot.revision = 1;
    state->snapshot.cause = CATIME_IPC_CAUSE_CLIENT;
    *output = state->snapshot;
    return CATIME_IPC_ERROR_NONE;
}

CatimeIpcError CatimeIpcState_Pause(CatimeIpcState* state,
                                    const char* sessionId,
                                    int64_t nowMs,
                                    CatimeIpcCause cause,
                                    CatimeIpcSnapshot* output) {
    CatimeIpcError error = RequireMutableSession(
        state, sessionId, CATIME_IPC_STATUS_PAUSED);
    if (error != CATIME_IPC_ERROR_NONE || !output) return error;
    RefreshRunning(state, nowMs);
    state->focusedAtResume = state->snapshot.focusedSeconds;
    CommitMutation(state, CATIME_IPC_STATUS_PAUSED, cause, nowMs);
    *output = state->snapshot;
    return CATIME_IPC_ERROR_NONE;
}

CatimeIpcError CatimeIpcState_Resume(CatimeIpcState* state,
                                     const char* sessionId,
                                     int64_t nowMs,
                                     CatimeIpcCause cause,
                                     CatimeIpcSnapshot* output) {
    CatimeIpcError error = RequireMutableSession(
        state, sessionId, CATIME_IPC_STATUS_RUNNING);
    if (error != CATIME_IPC_ERROR_NONE || !output) return error;
    CommitMutation(state, CATIME_IPC_STATUS_RUNNING, cause, nowMs);
    state->snapshot.deadlineAt = nowMs +
        ((int64_t)state->snapshot.remainingSeconds * 1000);
    *output = state->snapshot;
    return CATIME_IPC_ERROR_NONE;
}

CatimeIpcError CatimeIpcState_Finish(CatimeIpcState* state,
                                     const char* sessionId,
                                     int64_t nowMs,
                                     CatimeIpcCause cause,
                                     CatimeIpcSnapshot* output) {
    CatimeIpcError error = RequireMutableSession(
        state, sessionId, CATIME_IPC_STATUS_COMPLETED);
    if (error != CATIME_IPC_ERROR_NONE || !output) return error;
    RefreshRunning(state, nowMs);
    CommitMutation(state, CATIME_IPC_STATUS_COMPLETED, cause, nowMs);
    *output = state->snapshot;
    return CATIME_IPC_ERROR_NONE;
}

CatimeIpcError CatimeIpcState_Cancel(CatimeIpcState* state,
                                     const char* sessionId,
                                     int64_t nowMs,
                                     CatimeIpcCause cause,
                                     CatimeIpcSnapshot* output) {
    CatimeIpcError error = RequireMutableSession(
        state, sessionId, CATIME_IPC_STATUS_CANCELLED);
    if (error != CATIME_IPC_ERROR_NONE || !output) return error;
    RefreshRunning(state, nowMs);
    CommitMutation(state, CATIME_IPC_STATUS_CANCELLED, cause, nowMs);
    *output = state->snapshot;
    return CATIME_IPC_ERROR_NONE;
}

bool CatimeIpcState_Tick(CatimeIpcState* state,
                         int64_t nowMs,
                         CatimeIpcSnapshot* output) {
    if (!state || !output || state->snapshot.status != CATIME_IPC_STATUS_RUNNING) {
        return false;
    }
    RefreshRunning(state, nowMs);
    if (state->snapshot.remainingSeconds > 0) return false;
    state->snapshot.focusedSeconds = state->snapshot.plannedSeconds;
    CommitMutation(state, CATIME_IPC_STATUS_COMPLETED,
                   CATIME_IPC_CAUSE_TIMEOUT, nowMs);
    *output = state->snapshot;
    return true;
}

bool CatimeIpcState_Get(const CatimeIpcState* state,
                        CatimeIpcSnapshot* output) {
    if (!state || !output || !state->hasSession) return false;
    *output = state->snapshot;
    return true;
}

bool CatimeIpcState_GetAt(const CatimeIpcState* state,
                          int64_t nowMs,
                          CatimeIpcSnapshot* output) {
    if (!CatimeIpcState_Get(state, output)) return false;
    if (output->status == CATIME_IPC_STATUS_RUNNING) {
        int64_t remainingMs = output->deadlineAt - nowMs;
        uint32_t remaining = remainingMs > 0
            ? (uint32_t)((remainingMs + 999) / 1000) : 0;
        if (remaining > output->plannedSeconds) {
            remaining = output->plannedSeconds;
        }
        output->remainingSeconds = remaining;
        output->focusedSeconds = output->plannedSeconds - remaining;
    }
    return true;
}

CatimeIpcError CatimeIpcState_Acknowledge(CatimeIpcState* state,
                                          const char* sessionId,
                                          uint64_t revision) {
    if (!SessionMatches(state, sessionId)) {
        return state && state->hasSession
            ? CATIME_IPC_ERROR_SESSION_MISMATCH
            : CATIME_IPC_ERROR_INVALID_STATE;
    }
    if (!CatimeIpc_IsTerminalStatus(state->snapshot.status) ||
        revision != state->snapshot.revision) {
        return CATIME_IPC_ERROR_INVALID_STATE;
    }
    if (revision > state->acknowledgedRevision) {
        state->acknowledgedRevision = revision;
    }
    return CATIME_IPC_ERROR_NONE;
}
