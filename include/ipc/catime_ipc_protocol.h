/**
 * @file catime_ipc_protocol.h
 * @brief Stable protocol contract for the Week Planner Calendar integration.
 *
 * Added by the Week Planner Calendar fork. This header intentionally avoids
 * Win32 types so the wire contract can be tested independently of the pipe
 * transport.
 */

#ifndef CATIME_IPC_PROTOCOL_H
#define CATIME_IPC_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define CATIME_IPC_PROTOCOL_VERSION 1
#define CATIME_IPC_PIPE_NAME "\\\\.\\pipe\\catime-week-planner-v1"
#define CATIME_IPC_MAX_MESSAGE_BYTES 4096
#define CATIME_IPC_MAX_REQUEST_ID_BYTES 64
#define CATIME_IPC_MAX_SESSION_ID_BYTES 64
#define CATIME_IPC_MAX_RECENT_REQUESTS 128
#define CATIME_IPC_MIN_DURATION_SECONDS 1
#define CATIME_IPC_MAX_DURATION_SECONDS 10800

typedef enum {
    CATIME_IPC_STATUS_IDLE = 0,
    CATIME_IPC_STATUS_RUNNING,
    CATIME_IPC_STATUS_PAUSED,
    CATIME_IPC_STATUS_COMPLETED,
    CATIME_IPC_STATUS_CANCELLED
} CatimeIpcStatus;

typedef enum {
    CATIME_IPC_PHASE_FOCUS = 0,
    CATIME_IPC_PHASE_SHORT_BREAK,
    CATIME_IPC_PHASE_LONG_BREAK
} CatimeIpcPhase;

typedef enum {
    CATIME_IPC_CAUSE_CLIENT = 0,
    CATIME_IPC_CAUSE_TRAY,
    CATIME_IPC_CAUSE_HOTKEY,
    CATIME_IPC_CAUSE_TIMEOUT,
    CATIME_IPC_CAUSE_USER_REPLACED,
    CATIME_IPC_CAUSE_SHUTDOWN
} CatimeIpcCause;

typedef enum {
    CATIME_IPC_ERROR_NONE = 0,
    CATIME_IPC_ERROR_INVALID_JSON,
    CATIME_IPC_ERROR_MESSAGE_TOO_LARGE,
    CATIME_IPC_ERROR_UNSUPPORTED_PROTOCOL,
    CATIME_IPC_ERROR_INVALID_REQUEST,
    CATIME_IPC_ERROR_INVALID_DURATION,
    CATIME_IPC_ERROR_INVALID_SESSION_ID,
    CATIME_IPC_ERROR_UNKNOWN_COMMAND,
    CATIME_IPC_ERROR_SESSION_CONFLICT,
    CATIME_IPC_ERROR_SESSION_MISMATCH,
    CATIME_IPC_ERROR_INVALID_STATE,
    CATIME_IPC_ERROR_INTERNAL_ERROR
} CatimeIpcError;

const char* CatimeIpc_StatusName(CatimeIpcStatus status);
const char* CatimeIpc_PhaseName(CatimeIpcPhase phase);
const char* CatimeIpc_CauseName(CatimeIpcCause cause);
const char* CatimeIpc_ErrorCode(CatimeIpcError error);

bool CatimeIpc_IsTerminalStatus(CatimeIpcStatus status);
bool CatimeIpc_CanTransition(CatimeIpcStatus from, CatimeIpcStatus to);
bool CatimeIpc_IsValidDuration(uint32_t durationSeconds);
bool CatimeIpc_IsNewerRevision(uint64_t currentRevision,
                               uint64_t candidateRevision);

#endif /* CATIME_IPC_PROTOCOL_H */
