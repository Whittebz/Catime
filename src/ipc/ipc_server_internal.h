#ifndef CATIME_IPC_SERVER_INTERNAL_H
#define CATIME_IPC_SERVER_INTERNAL_H

#include "ipc/catime_ipc_request.h"
#include "ipc/catime_ipc_state.h"

#include <windows.h>

#define IPC_PIPE_NAME_W L"\\\\.\\pipe\\catime-week-planner-v1"
#define IPC_RESPONSE_CAPACITY CATIME_IPC_MAX_MESSAGE_BYTES

typedef enum {
    IPC_REPLY_ERROR = 0,
    IPC_REPLY_HELLO,
    IPC_REPLY_PONG,
    IPC_REPLY_EVENT_ACK,
    IPC_REPLY_SNAPSHOT
} IpcReplyType;

typedef struct {
    IpcReplyType type;
    CatimeIpcError error;
    CatimeIpcSnapshot snapshot;
} IpcReply;

BOOL IpcSession_Initialize(HWND mainWindow);
void IpcSession_Shutdown(void);
BOOL IpcSession_Execute(const CatimeIpcRequest* request,
                        BOOL* handshaken,
                        IpcReply* reply);
BOOL IpcSession_PeekEvent(CatimeIpcSnapshot* snapshot);
void IpcSession_MarkEventSent(const CatimeIpcSnapshot* snapshot);
void IpcSession_ResetEventDelivery(void);
BOOL IpcEventQueue_Initialize(void);
void IpcEventQueue_Shutdown(void);
void IpcEventQueue_Push(const CatimeIpcSnapshot* snapshot);
BOOL IpcEventQueue_Acknowledge(const char* sessionId, uint64_t revision);

void IpcProtocol_HandleFrame(HANDLE pipe, const char* frame,
                             size_t length, BOOL* handshaken,
                             void* responseCache);
void* IpcProtocol_CreateCache(void);
void IpcProtocol_DestroyCache(void* cache);
BOOL IpcProtocol_WriteEvent(HANDLE pipe,
                            const CatimeIpcSnapshot* snapshot);

#endif /* CATIME_IPC_SERVER_INTERNAL_H */
