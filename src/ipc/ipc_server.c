/**
 * @file ipc_server.c
 * @brief Named-pipe transport lifecycle for the Obsidian bridge.
 *
 * Added by the Week Planner Calendar fork. Multiple pipe instances allow one
 * connection per Obsidian vault; events are delivered per client (see
 * ipc_event_queue.c) so every connected vault mirrors the same timer.
 */

#include "ipc/catime_ipc_server.h"
#include "ipc_server_internal.h"
#include "log.h"

#include <sddl.h>
#include <stdint.h>

#define IPC_POLL_INTERVAL_MS 20

static HANDLE s_threads[IPC_MAX_CLIENTS] = {0};
static HANDLE s_pipes[IPC_MAX_CLIENTS] = {0};
static HANDLE s_stopEvent = NULL;
static DWORD s_threadCount = 0;

static BOOL CreatePipeSecurity(SECURITY_ATTRIBUTES* attributes,
                               PSECURITY_DESCRIPTOR* descriptor) {
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;OW)", SDDL_REVISION_1,
            descriptor, NULL)) return FALSE;
    attributes->nLength = sizeof(*attributes);
    attributes->lpSecurityDescriptor = *descriptor;
    attributes->bInheritHandle = FALSE;
    return TRUE;
}

static BOOL ReadClientFrames(HANDLE pipe, int clientId) {
    BOOL handshaken = FALSE;
    char frame[CATIME_IPC_MAX_MESSAGE_BYTES];
    size_t frameLength = 0;
    void* cache = IpcProtocol_CreateCache();
    if (!cache) return FALSE;
    while (WaitForSingleObject(s_stopEvent, 0) != WAIT_OBJECT_0) {
        char chunk[512];
        DWORD read = 0;
        BOOL readOk = ReadFile(pipe, chunk, sizeof(chunk), &read, NULL);
        if (readOk && read > 0) {
            for (DWORD index = 0; index < read; index++) {
                if (chunk[index] == '\n') {
                    if (frameLength > 0) {
                        if (frame[frameLength - 1] == '\r') frameLength--;
                        IpcProtocol_HandleFrame(pipe, frame, frameLength,
                                                &handshaken, cache, clientId);
                    }
                    frameLength = 0;
                } else if (frameLength + 1 < sizeof(frame)) {
                    frame[frameLength++] = chunk[index];
                } else {
                    IpcProtocol_HandleFrame(pipe, "", CATIME_IPC_MAX_MESSAGE_BYTES,
                                            &handshaken, cache, clientId);
                    IpcProtocol_DestroyCache(cache);
                    return FALSE;
                }
            }
        } else if (!readOk) {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                break;
            }
        }
        CatimeIpcSnapshot eventSnapshot;
        if (handshaken && IpcSession_PeekEvent(clientId, &eventSnapshot) &&
            IpcProtocol_WriteEvent(pipe, &eventSnapshot)) {
            IpcSession_MarkEventSent(clientId, &eventSnapshot);
        }
        Sleep(IPC_POLL_INTERVAL_MS);
    }
    IpcProtocol_DestroyCache(cache);
    return TRUE;
}

static DWORD WINAPI IpcPipeThreadProc(LPVOID parameter) {
    intptr_t pipeIndex = (intptr_t)parameter;
    BOOL firstInstance = pipeIndex == 0;
    HANDLE pipe = s_pipes[pipeIndex];
    while (WaitForSingleObject(s_stopEvent, 0) != WAIT_OBJECT_0) {
        if (pipe == INVALID_HANDLE_VALUE) {
            SECURITY_ATTRIBUTES security;
            PSECURITY_DESCRIPTOR descriptor = NULL;
            if (!CreatePipeSecurity(&security, &descriptor)) break;
            pipe = CreateNamedPipeW(IPC_PIPE_NAME_W,
                PIPE_ACCESS_DUPLEX |
                    (firstInstance ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                    PIPE_REJECT_REMOTE_CLIENTS,
                IPC_MAX_CLIENTS, CATIME_IPC_MAX_MESSAGE_BYTES,
                CATIME_IPC_MAX_MESSAGE_BYTES, 0, &security);
            LocalFree(descriptor);
            if (pipe == INVALID_HANDLE_VALUE) {
                if (firstInstance) {
                    LOG_WINDOWS_ERROR("Catime IPC pipe creation failed");
                    break;
                }
                Sleep(IPC_POLL_INTERVAL_MS);
                continue;
            }
            s_pipes[pipeIndex] = pipe;
        }
        BOOL connected = ConnectNamedPipe(pipe, NULL)
            ? TRUE : GetLastError() == ERROR_PIPE_CONNECTED;
        if (connected) {
            DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
            SetNamedPipeHandleState(pipe, &mode, NULL, NULL);
            int clientId = IpcEventQueue_RegisterClient();
            if (clientId >= 0) {
                IpcSession_ResetEventDelivery(clientId);
                ReadClientFrames(pipe, clientId);
                IpcEventQueue_UnregisterClient(clientId);
            }
            DisconnectNamedPipe(pipe);
        }
        CloseHandle(pipe);
        s_pipes[pipeIndex] = INVALID_HANDLE_VALUE;
        pipe = INVALID_HANDLE_VALUE;
    }
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
    return 0;
}

BOOL CatimeIpcServer_Start(HWND mainWindow) {
    if (s_threadCount > 0) return TRUE;
    if (!IpcSession_Initialize(mainWindow)) return FALSE;
    s_stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!s_stopEvent) {
        IpcSession_Shutdown();
        return FALSE;
    }
    /* Create every pipe instance up front so the first-instance flag is
     * applied exactly once and in order, before any server thread races. */
    BOOL createdAny = FALSE;
    for (int index = 0; index < IPC_MAX_CLIENTS; index++) {
        SECURITY_ATTRIBUTES security;
        PSECURITY_DESCRIPTOR descriptor = NULL;
        HANDLE pipe = INVALID_HANDLE_VALUE;
        if (CreatePipeSecurity(&security, &descriptor)) {
            pipe = CreateNamedPipeW(IPC_PIPE_NAME_W,
                PIPE_ACCESS_DUPLEX |
                    (index == 0 ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                    PIPE_REJECT_REMOTE_CLIENTS,
                IPC_MAX_CLIENTS, CATIME_IPC_MAX_MESSAGE_BYTES,
                CATIME_IPC_MAX_MESSAGE_BYTES, 0, &security);
            LocalFree(descriptor);
        }
        s_pipes[index] = pipe;
        if (pipe != INVALID_HANDLE_VALUE) createdAny = TRUE;
    }
    if (!createdAny) {
        CloseHandle(s_stopEvent);
        s_stopEvent = NULL;
        IpcSession_Shutdown();
        return FALSE;
    }
    for (int index = 0; index < IPC_MAX_CLIENTS; index++) {
        HANDLE thread = CreateThread(NULL, 0, IpcPipeThreadProc,
                                     (LPVOID)(intptr_t)index, 0, NULL);
        if (!thread) break;
        s_threads[index] = thread;
        s_threadCount++;
    }
    if (s_threadCount == 0) {
        for (int index = 0; index < IPC_MAX_CLIENTS; index++) {
            if (s_pipes[index] != INVALID_HANDLE_VALUE) CloseHandle(s_pipes[index]);
            s_pipes[index] = INVALID_HANDLE_VALUE;
        }
        CloseHandle(s_stopEvent);
        s_stopEvent = NULL;
        IpcSession_Shutdown();
        return FALSE;
    }
    return TRUE;
}

void CatimeIpcServer_Stop(void) {
    if (s_threadCount == 0) return;
    SetEvent(s_stopEvent);
    for (DWORD index = 0; index < s_threadCount; index++) {
        HANDLE wake = CreateFileW(IPC_PIPE_NAME_W, GENERIC_READ | GENERIC_WRITE,
                                  0, NULL, OPEN_EXISTING, 0, NULL);
        if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
    }
    DWORD result = WaitForMultipleObjects(s_threadCount, s_threads,
                                          TRUE, 5000);
    if (result == WAIT_FAILED || result == WAIT_TIMEOUT) {
        for (DWORD index = 0; index < s_threadCount; index++) {
            CancelSynchronousIo(s_threads[index]);
        }
        result = WaitForMultipleObjects(s_threadCount, s_threads,
                                        TRUE, 1000);
    }
    if (result != WAIT_OBJECT_0) {
        LOG_WARNING("Week Planner IPC worker did not stop cleanly");
        return;
    }
    for (DWORD index = 0; index < s_threadCount; index++) {
        CloseHandle(s_threads[index]);
        s_threads[index] = NULL;
    }
    for (int index = 0; index < IPC_MAX_CLIENTS; index++) {
        if (s_pipes[index] != INVALID_HANDLE_VALUE) CloseHandle(s_pipes[index]);
        s_pipes[index] = INVALID_HANDLE_VALUE;
    }
    s_threadCount = 0;
    CloseHandle(s_stopEvent);
    s_stopEvent = NULL;
    IpcSession_Shutdown();
}
