/**
 * @file catime_ipc_persistence.h
 * @brief Durable storage for the latest externally owned timer session.
 */

#ifndef CATIME_IPC_PERSISTENCE_H
#define CATIME_IPC_PERSISTENCE_H

#include "ipc/catime_ipc_state.h"

#include <windows.h>

BOOL CatimeIpcPersistence_Load(CatimeIpcState* state,
                               int64_t nowMs,
                               CatimeIpcSnapshot* completedEvent);
BOOL CatimeIpcPersistence_Save(const CatimeIpcState* state);

#endif /* CATIME_IPC_PERSISTENCE_H */
