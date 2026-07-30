/**
 * @file ipc_server.c
 * @brief Named-pipe transport lifecycle for the Obsidian bridge.
 */

#include "ipc/catime_ipc_server.h"
#include "ipc_server_internal.h"
#include "log.h"

#include <sddl.h>

#define IPC_POLL_INTERVAL_MS 20

static HANDLE s_thread = NULL;
static HANDLE s_stopEvent = NULL;

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

static BOOL ReadClientFrames(HANDLE pipe) {
    BOOL handshaken = FALSE;
    char frame[CATIME_IPC_MAX_MESSAGE_BYTES];
    size_t frameLength = 0;
    void* cache = IpcProtocol_CreateCache();
    if (!cache) return FALSE;
    IpcSession_ResetEventDelivery();
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
                                                &handshaken, cache);
                    }
                    frameLength = 0;
                } else if (frameLength + 1 < sizeof(frame)) {
                    frame[frameLength++] = chunk[index];
                } else {
                    IpcProtocol_HandleFrame(pipe, "", CATIME_IPC_MAX_MESSAGE_BYTES,
                                            &handshaken, cache);
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
        if (handshaken && IpcSession_PeekEvent(&eventSnapshot) &&
            IpcProtocol_WriteEvent(pipe, &eventSnapshot)) {
            IpcSession_MarkEventSent(&eventSnapshot);
        }
        Sleep(IPC_POLL_INTERVAL_MS);
    }
    IpcProtocol_DestroyCache(cache);
    return TRUE;
}

static DWORD WINAPI IpcThreadProc(LPVOID parameter) {
    (void)parameter;
    while (WaitForSingleObject(s_stopEvent, 0) != WAIT_OBJECT_0) {
        SECURITY_ATTRIBUTES security;
        PSECURITY_DESCRIPTOR descriptor = NULL;
        if (!CreatePipeSecurity(&security, &descriptor)) break;
        HANDLE pipe = CreateNamedPipeW(IPC_PIPE_NAME_W,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                PIPE_REJECT_REMOTE_CLIENTS,
            1, CATIME_IPC_MAX_MESSAGE_BYTES, CATIME_IPC_MAX_MESSAGE_BYTES,
            0, &security);
        LocalFree(descriptor);
        if (pipe == INVALID_HANDLE_VALUE) {
            LOG_WINDOWS_ERROR("Catime IPC pipe creation failed");
            break;
        }
        BOOL connected = ConnectNamedPipe(pipe, NULL)
            ? TRUE : GetLastError() == ERROR_PIPE_CONNECTED;
        if (connected) {
            DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
            SetNamedPipeHandleState(pipe, &mode, NULL, NULL);
            ReadClientFrames(pipe);
            DisconnectNamedPipe(pipe);
        }
        CloseHandle(pipe);
    }
    return 0;
}

BOOL CatimeIpcServer_Start(HWND mainWindow) {
    if (s_thread) return TRUE;
    if (!IpcSession_Initialize(mainWindow)) return FALSE;
    s_stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!s_stopEvent) {
        IpcSession_Shutdown();
        return FALSE;
    }
    s_thread = CreateThread(NULL, 0, IpcThreadProc, NULL, 0, NULL);
    if (!s_thread) {
        CloseHandle(s_stopEvent);
        s_stopEvent = NULL;
        IpcSession_Shutdown();
        return FALSE;
    }
    return TRUE;
}

void CatimeIpcServer_Stop(void) {
    if (!s_thread) return;
    SetEvent(s_stopEvent);
    HANDLE wake = CreateFileW(IPC_PIPE_NAME_W, GENERIC_READ | GENERIC_WRITE,
                              0, NULL, OPEN_EXISTING, 0, NULL);
    if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
    DWORD result = WaitForSingleObject(s_thread, 5000);
    if (result != WAIT_OBJECT_0) {
        CancelSynchronousIo(s_thread);
        result = WaitForSingleObject(s_thread, 1000);
    }
    if (result != WAIT_OBJECT_0) {
        LOG_WARNING("Week Planner IPC worker did not stop cleanly");
        return;
    }
    CloseHandle(s_thread);
    CloseHandle(s_stopEvent);
    s_thread = NULL;
    s_stopEvent = NULL;
    IpcSession_Shutdown();
}
