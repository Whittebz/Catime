/**
 * @file catime_ipc_persistence.h
 * @brief Durable storage for the latest external session and unacked history.
 */

#ifndef CATIME_IPC_PERSISTENCE_H
#define CATIME_IPC_PERSISTENCE_H

#include "ipc/catime_ipc_history.h"
#include "ipc/catime_ipc_state.h"

#include <windows.h>

BOOL CatimeIpcPersistence_Load(CatimeIpcState* state,
                               int64_t nowMs,
                               IpcHistory* history);
BOOL CatimeIpcPersistence_Save(const CatimeIpcState* state,
                               const IpcHistory* history);

#endif /* CATIME_IPC_PERSISTENCE_H */
