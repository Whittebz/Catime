/**
 * @file catime_ipc_lock.h
 * @brief Portable lock used by platform-independent IPC modules.
 *
 * Added by the Week Planner Calendar fork. Uses Win32 critical sections on
 * Windows and pthread mutexes elsewhere so the pure-C tests can run on any
 * host while the shipped build stays Win32-only.
 */

#ifndef CATIME_IPC_LOCK_H
#define CATIME_IPC_LOCK_H

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION IpcLock;
#define IPC_LOCK_INIT(lock) InitializeCriticalSection(lock)
#define IPC_LOCK_DESTROY(lock) DeleteCriticalSection(lock)
#define IPC_LOCK_ENTER(lock) EnterCriticalSection(lock)
#define IPC_LOCK_LEAVE(lock) LeaveCriticalSection(lock)
#else
#include <pthread.h>
typedef pthread_mutex_t IpcLock;
#define IPC_LOCK_INIT(lock) pthread_mutex_init(lock, NULL)
#define IPC_LOCK_DESTROY(lock) pthread_mutex_destroy(lock)
#define IPC_LOCK_ENTER(lock) pthread_mutex_lock(lock)
#define IPC_LOCK_LEAVE(lock) pthread_mutex_unlock(lock)
#endif

#endif /* CATIME_IPC_LOCK_H */
