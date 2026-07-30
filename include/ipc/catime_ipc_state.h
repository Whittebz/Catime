/**
 * @file catime_ipc_state.h
 * @brief Deterministic external timer state machine for the Obsidian bridge.
 */

#ifndef CATIME_IPC_STATE_H
#define CATIME_IPC_STATE_H

#include "ipc/catime_ipc_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char sessionId[CATIME_IPC_MAX_SESSION_ID_BYTES + 1];
    CatimeIpcStatus status;
    CatimeIpcPhase phase;
    uint32_t plannedSeconds;
    uint32_t focusedSeconds;
    uint32_t remainingSeconds;
    int64_t startedAt;
    int64_t updatedAt;
    int64_t deadlineAt;
    int64_t endedAt;
    uint64_t revision;
    CatimeIpcCause cause;
} CatimeIpcSnapshot;

typedef struct {
    CatimeIpcSnapshot snapshot;
    uint32_t focusedAtResume;
    uint64_t acknowledgedRevision;
    bool hasSession;
} CatimeIpcState;

void CatimeIpcState_Init(CatimeIpcState* state);
CatimeIpcError CatimeIpcState_Start(CatimeIpcState* state,
                                    const char* sessionId,
                                    uint32_t durationSeconds,
                                    CatimeIpcPhase phase,
                                    int64_t nowMs,
                                    CatimeIpcSnapshot* output);
CatimeIpcError CatimeIpcState_Pause(CatimeIpcState* state,
                                    const char* sessionId,
                                    int64_t nowMs,
                                    CatimeIpcCause cause,
                                    CatimeIpcSnapshot* output);
CatimeIpcError CatimeIpcState_Resume(CatimeIpcState* state,
                                     const char* sessionId,
                                     int64_t nowMs,
                                     CatimeIpcCause cause,
                                     CatimeIpcSnapshot* output);
CatimeIpcError CatimeIpcState_Finish(CatimeIpcState* state,
                                     const char* sessionId,
                                     int64_t nowMs,
                                     CatimeIpcCause cause,
                                     CatimeIpcSnapshot* output);
CatimeIpcError CatimeIpcState_Cancel(CatimeIpcState* state,
                                     const char* sessionId,
                                     int64_t nowMs,
                                     CatimeIpcCause cause,
                                     CatimeIpcSnapshot* output);
bool CatimeIpcState_Tick(CatimeIpcState* state,
                         int64_t nowMs,
                         CatimeIpcSnapshot* output);
bool CatimeIpcState_Get(const CatimeIpcState* state,
                        CatimeIpcSnapshot* output);
bool CatimeIpcState_GetAt(const CatimeIpcState* state,
                          int64_t nowMs,
                          CatimeIpcSnapshot* output);
CatimeIpcError CatimeIpcState_Acknowledge(CatimeIpcState* state,
                                          const char* sessionId,
                                          uint64_t revision);

#endif /* CATIME_IPC_STATE_H */
