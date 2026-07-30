/**
 * @file ipc_protocol.c
 * @brief Platform-independent Week Planner Calendar IPC contract helpers.
 *
 * Added by the Week Planner Calendar fork.
 */

#include "ipc/catime_ipc_protocol.h"

#include <stddef.h>

const char* CatimeIpc_StatusName(CatimeIpcStatus status) {
    switch (status) {
        case CATIME_IPC_STATUS_IDLE: return "idle";
        case CATIME_IPC_STATUS_RUNNING: return "running";
        case CATIME_IPC_STATUS_PAUSED: return "paused";
        case CATIME_IPC_STATUS_COMPLETED: return "completed";
        case CATIME_IPC_STATUS_CANCELLED: return "cancelled";
        default: return NULL;
    }
}

const char* CatimeIpc_PhaseName(CatimeIpcPhase phase) {
    switch (phase) {
        case CATIME_IPC_PHASE_FOCUS: return "focus";
        case CATIME_IPC_PHASE_SHORT_BREAK: return "short_break";
        case CATIME_IPC_PHASE_LONG_BREAK: return "long_break";
        default: return NULL;
    }
}

const char* CatimeIpc_CauseName(CatimeIpcCause cause) {
    switch (cause) {
        case CATIME_IPC_CAUSE_CLIENT: return "client";
        case CATIME_IPC_CAUSE_TRAY: return "tray";
        case CATIME_IPC_CAUSE_HOTKEY: return "hotkey";
        case CATIME_IPC_CAUSE_TIMEOUT: return "timeout";
        case CATIME_IPC_CAUSE_USER_REPLACED: return "user_replaced";
        case CATIME_IPC_CAUSE_SHUTDOWN: return "shutdown";
        default: return NULL;
    }
}

const char* CatimeIpc_ErrorCode(CatimeIpcError error) {
    switch (error) {
        case CATIME_IPC_ERROR_NONE: return "none";
        case CATIME_IPC_ERROR_INVALID_JSON: return "invalid_json";
        case CATIME_IPC_ERROR_MESSAGE_TOO_LARGE: return "message_too_large";
        case CATIME_IPC_ERROR_UNSUPPORTED_PROTOCOL:
            return "unsupported_protocol";
        case CATIME_IPC_ERROR_INVALID_REQUEST: return "invalid_request";
        case CATIME_IPC_ERROR_INVALID_DURATION: return "invalid_duration";
        case CATIME_IPC_ERROR_INVALID_SESSION_ID: return "invalid_session_id";
        case CATIME_IPC_ERROR_UNKNOWN_COMMAND: return "unknown_command";
        case CATIME_IPC_ERROR_SESSION_CONFLICT: return "session_conflict";
        case CATIME_IPC_ERROR_SESSION_MISMATCH: return "session_mismatch";
        case CATIME_IPC_ERROR_INVALID_STATE: return "invalid_state";
        case CATIME_IPC_ERROR_INTERNAL_ERROR: return "internal_error";
        default: return NULL;
    }
}

bool CatimeIpc_IsTerminalStatus(CatimeIpcStatus status) {
    return status == CATIME_IPC_STATUS_COMPLETED ||
           status == CATIME_IPC_STATUS_CANCELLED;
}

bool CatimeIpc_CanTransition(CatimeIpcStatus from, CatimeIpcStatus to) {
    switch (from) {
        case CATIME_IPC_STATUS_IDLE:
            return to == CATIME_IPC_STATUS_RUNNING;
        case CATIME_IPC_STATUS_RUNNING:
            return to == CATIME_IPC_STATUS_PAUSED ||
                   to == CATIME_IPC_STATUS_COMPLETED ||
                   to == CATIME_IPC_STATUS_CANCELLED;
        case CATIME_IPC_STATUS_PAUSED:
            return to == CATIME_IPC_STATUS_RUNNING ||
                   to == CATIME_IPC_STATUS_COMPLETED ||
                   to == CATIME_IPC_STATUS_CANCELLED;
        case CATIME_IPC_STATUS_COMPLETED:
        case CATIME_IPC_STATUS_CANCELLED:
            return to == CATIME_IPC_STATUS_IDLE;
        default:
            return false;
    }
}

bool CatimeIpc_IsValidDuration(uint32_t durationSeconds) {
    return durationSeconds >= CATIME_IPC_MIN_DURATION_SECONDS &&
           durationSeconds <= CATIME_IPC_MAX_DURATION_SECONDS;
}

bool CatimeIpc_IsNewerRevision(uint64_t currentRevision,
                               uint64_t candidateRevision) {
    return candidateRevision > currentRevision;
}
