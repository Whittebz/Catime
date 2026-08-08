/**
 * @file catime_ipc_persistence_format.h
 * @brief INI snapshot field encoding shared by state and history storage.
 */

#ifndef CATIME_IPC_PERSISTENCE_FORMAT_H
#define CATIME_IPC_PERSISTENCE_FORMAT_H

#include "ipc/catime_ipc_state.h"

#include <windows.h>

int64_t IpcPersistence_ReadNumber(const wchar_t* path,
                                  const wchar_t* section,
                                  const wchar_t* key);
BOOL IpcPersistence_WriteSnapshot(const wchar_t* path,
                                  const wchar_t* section,
                                  const wchar_t* prefix,
                                  const CatimeIpcSnapshot* snapshot);
BOOL IpcPersistence_ReadSnapshot(const wchar_t* path,
                                 const wchar_t* section,
                                 const wchar_t* prefix,
                                 CatimeIpcSnapshot* snapshot);

#endif /* CATIME_IPC_PERSISTENCE_FORMAT_H */
