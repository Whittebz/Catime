/**
 * @file catime_ipc_request.h
 * @brief Strict flat-JSON request decoder for the local IPC protocol.
 */

#ifndef CATIME_IPC_REQUEST_H
#define CATIME_IPC_REQUEST_H

#include "ipc/catime_ipc_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CATIME_IPC_COMMAND_INVALID = 0,
    CATIME_IPC_COMMAND_HELLO,
    CATIME_IPC_COMMAND_START,
    CATIME_IPC_COMMAND_PAUSE,
    CATIME_IPC_COMMAND_RESUME,
    CATIME_IPC_COMMAND_CANCEL,
    CATIME_IPC_COMMAND_FINISH,
    CATIME_IPC_COMMAND_GET_STATE,
    CATIME_IPC_COMMAND_ACK_EVENT,
    CATIME_IPC_COMMAND_QUEUE_PHASE,
    CATIME_IPC_COMMAND_PING
} CatimeIpcCommand;

typedef struct {
    CatimeIpcCommand command;
    char requestId[CATIME_IPC_MAX_REQUEST_ID_BYTES + 1];
    char sessionId[CATIME_IPC_MAX_SESSION_ID_BYTES + 1];
    CatimeIpcPhase phase;
    uint32_t durationSeconds;
    uint64_t revision;
    uint32_t protocol;
    bool hasSessionId;
    bool hasDuration;
    bool hasPhase;
    bool hasRevision;
    bool hasProtocol;
} CatimeIpcRequest;

CatimeIpcError CatimeIpcRequest_Parse(const char* json,
                                      size_t length,
                                      CatimeIpcRequest* output);

#endif /* CATIME_IPC_REQUEST_H */
