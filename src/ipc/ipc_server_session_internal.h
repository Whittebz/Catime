#ifndef CATIME_IPC_SERVER_SESSION_INTERNAL_H
#define CATIME_IPC_SERVER_SESSION_INTERNAL_H

#include "ipc/catime_ipc_history.h"
#include "ipc/catime_ipc_state.h"
#include "ipc_server_internal.h"

#include <windows.h>

#define IPC_UI_TIMEOUT_MS 5000

typedef struct {
    CatimeIpcRequest request;
    CatimeIpcSnapshot snapshot;
    CatimeIpcError error;
    BOOL hasSnapshot;
} IpcUiCommand;

/* Shared external-session state, owned by ipc_server_session.c and consumed by
 * the UI-thread bridge in ipc_server_ui.c. */
extern HWND g_ipcMainWindow;
extern CRITICAL_SECTION g_ipcStateLock;
extern BOOL g_ipcLockInitialized;
extern BOOL g_ipcApplyingUiCommand;
extern CatimeIpcState g_ipcState;
extern IpcHistory g_ipcHistory;

int64_t IpcState_EpochMilliseconds(void);
BOOL IpcState_GenerateSessionId(char* output, size_t capacity);
void IpcState_SaveWithHistory(void);
void IpcState_RecordTerminal(const CatimeIpcSnapshot* snapshot);

#endif /* CATIME_IPC_SERVER_SESSION_INTERNAL_H */
