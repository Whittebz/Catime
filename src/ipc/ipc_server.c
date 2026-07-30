/**
 * @file ipc_server.c
 * @brief Current-user named-pipe server and Catime UI-thread bridge.
 */

#include "ipc/catime_ipc_server.h"
#include "ipc/catime_ipc_request.h"
#include "ipc/catime_ipc_state.h"
#include "ipc/catime_ipc_persistence.h"
#include "timer/timer.h"
#include "log.h"
#include "../../resource/resource.h"

#include <sddl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IPC_PIPE_NAME_W L"\\\\.\\pipe\\catime-week-planner-v1"
#define IPC_POLL_INTERVAL_MS 20
#define IPC_UI_TIMEOUT_MS 5000
#define IPC_RESPONSE_CAPACITY CATIME_IPC_MAX_MESSAGE_BYTES
#define IPC_CACHED_RESPONSE_CAPACITY 1024

typedef struct {
    CatimeIpcRequest request;
    CatimeIpcSnapshot snapshot;
    CatimeIpcError error;
    BOOL hasSnapshot;
} IpcUiCommand;

typedef struct {
    char requestId[CATIME_IPC_MAX_REQUEST_ID_BYTES + 1];
    char response[IPC_CACHED_RESPONSE_CAPACITY];
} CachedResponse;

static HWND s_mainWindow = NULL;
static HANDLE s_thread = NULL;
static HANDLE s_stopEvent = NULL;
static CRITICAL_SECTION s_stateLock;
static BOOL s_lockInitialized = FALSE;
static CatimeIpcState s_state;
static CatimeIpcSnapshot s_pendingEvent;
static BOOL s_hasPendingEvent = FALSE;
static BOOL s_applyingUiCommand = FALSE;

static int64_t EpochMilliseconds(void) {
    FILETIME fileTime;
    ULARGE_INTEGER value;
    GetSystemTimeAsFileTime(&fileTime);
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return (int64_t)(value.QuadPart / 10000ULL) - 11644473600000LL;
}

static const char* ErrorMessage(CatimeIpcError error) {
    switch (error) {
        case CATIME_IPC_ERROR_INVALID_JSON: return "The request is not valid JSON.";
        case CATIME_IPC_ERROR_MESSAGE_TOO_LARGE: return "The request exceeds 4096 bytes.";
        case CATIME_IPC_ERROR_UNSUPPORTED_PROTOCOL: return "The protocol version is unsupported.";
        case CATIME_IPC_ERROR_INVALID_REQUEST: return "The request fields are invalid.";
        case CATIME_IPC_ERROR_INVALID_DURATION: return "The timer duration is invalid.";
        case CATIME_IPC_ERROR_INVALID_SESSION_ID: return "The session identifier is invalid.";
        case CATIME_IPC_ERROR_UNKNOWN_COMMAND: return "The command is unknown.";
        case CATIME_IPC_ERROR_SESSION_CONFLICT: return "Another external session is active.";
        case CATIME_IPC_ERROR_SESSION_MISMATCH: return "The active session does not match.";
        case CATIME_IPC_ERROR_INVALID_STATE: return "The timer state does not allow this command.";
        default: return "Catime could not complete the request.";
    }
}

static BOOL ErrorRecoverable(CatimeIpcError error) {
    return error == CATIME_IPC_ERROR_INVALID_DURATION ||
           error == CATIME_IPC_ERROR_INVALID_SESSION_ID ||
           error == CATIME_IPC_ERROR_SESSION_CONFLICT ||
           error == CATIME_IPC_ERROR_SESSION_MISMATCH ||
           error == CATIME_IPC_ERROR_INVALID_STATE ||
           error == CATIME_IPC_ERROR_INTERNAL_ERROR;
}

static size_t EscapeJson(const char* input, char* output, size_t capacity) {
    size_t written = 0;
    if (!input || !output || capacity == 0) return 0;
    while (*input) {
        unsigned char value = (unsigned char)*input++;
        const char* escaped = NULL;
        switch (value) {
            case '"': escaped = "\\\""; break;
            case '\\': escaped = "\\\\"; break;
            case '\b': escaped = "\\b"; break;
            case '\f': escaped = "\\f"; break;
            case '\n': escaped = "\\n"; break;
            case '\r': escaped = "\\r"; break;
            case '\t': escaped = "\\t"; break;
            default: break;
        }
        if (escaped) {
            if (written + 2 >= capacity) return 0;
            output[written++] = escaped[0];
            output[written++] = escaped[1];
        } else {
            if (value < 0x20 || written + 1 >= capacity) return 0;
            output[written++] = (char)value;
        }
    }
    output[written] = '\0';
    return written;
}

static BOOL SerializeError(const char* requestId,
                           CatimeIpcError error,
                           char* output,
                           size_t capacity) {
    char escapedId[CATIME_IPC_MAX_REQUEST_ID_BYTES * 2 + 1];
    if (!EscapeJson(requestId ? requestId : "", escapedId, sizeof(escapedId))) {
        escapedId[0] = '\0';
    }
    int length = snprintf(output, capacity,
        "{\"type\":\"error\",\"requestId\":\"%s\",\"code\":\"%s\","
        "\"message\":\"%s\",\"recoverable\":%s}\n",
        escapedId, CatimeIpc_ErrorCode(error), ErrorMessage(error),
        ErrorRecoverable(error) ? "true" : "false");
    return length > 0 && (size_t)length < capacity;
}

static BOOL SerializeSnapshot(const CatimeIpcSnapshot* snapshot,
                              const char* requestId,
                              BOOL event,
                              char* output,
                              size_t capacity) {
    char escapedSession[CATIME_IPC_MAX_SESSION_ID_BYTES * 2 + 1];
    char escapedRequest[CATIME_IPC_MAX_REQUEST_ID_BYTES * 2 + 1];
    if (!snapshot ||
        !EscapeJson(snapshot->sessionId, escapedSession, sizeof(escapedSession))) {
        return FALSE;
    }
    escapedRequest[0] = '\0';
    if (requestId && !EscapeJson(requestId, escapedRequest, sizeof(escapedRequest))) {
        return FALSE;
    }
    const char* type = "state";
    if (event) {
        if (snapshot->status == CATIME_IPC_STATUS_COMPLETED) type = "completed";
        else if (snapshot->status == CATIME_IPC_STATUS_CANCELLED) type = "cancelled";
        else type = "stateChanged";
    }
    char requestField[192] = "";
    if (requestId) {
        int requestLength = snprintf(requestField, sizeof(requestField),
                                     "\"requestId\":\"%s\",", escapedRequest);
        if (requestLength < 0 || (size_t)requestLength >= sizeof(requestField)) {
            return FALSE;
        }
    }
    char deadline[64] = "null";
    char ended[80] = "";
    if (snapshot->deadlineAt > 0) {
        snprintf(deadline, sizeof(deadline), "%lld",
                 (long long)snapshot->deadlineAt);
    }
    if (snapshot->endedAt > 0) {
        snprintf(ended, sizeof(ended), ",\"endedAt\":%lld",
                 (long long)snapshot->endedAt);
    }
    int length = snprintf(output, capacity,
        "{\"type\":\"%s\",%s\"sessionId\":\"%s\",\"status\":\"%s\","
        "\"phase\":\"%s\",\"plannedSeconds\":%lu,\"focusedSeconds\":%lu,"
        "\"remainingSeconds\":%lu,\"startedAt\":%lld,\"updatedAt\":%lld,"
        "\"deadlineAt\":%s%s,\"revision\":%llu,\"cause\":\"%s\"}\n",
        type, requestField, escapedSession, CatimeIpc_StatusName(snapshot->status),
        CatimeIpc_PhaseName(snapshot->phase),
        (unsigned long)snapshot->plannedSeconds,
        (unsigned long)snapshot->focusedSeconds,
        (unsigned long)snapshot->remainingSeconds,
        (long long)snapshot->startedAt, (long long)snapshot->updatedAt,
        deadline, ended, (unsigned long long)snapshot->revision,
        CatimeIpc_CauseName(snapshot->cause));
    return length > 0 && (size_t)length < capacity;
}

static BOOL WriteResponse(HANDLE pipe, const char* response) {
    DWORD written = 0;
    size_t length = strlen(response);
    return length < CATIME_IPC_MAX_MESSAGE_BYTES &&
           WriteFile(pipe, response, (DWORD)length, &written, NULL) &&
           written == (DWORD)length;
}

static void QueueEventLocked(const CatimeIpcSnapshot* snapshot) {
    if (!snapshot) return;
    s_pendingEvent = *snapshot;
    s_hasPendingEvent = TRUE;
}

static BOOL DispatchUiCommand(const CatimeIpcRequest* request,
                              CatimeIpcSnapshot* snapshot,
                              CatimeIpcError* error) {
    IpcUiCommand command;
    ZeroMemory(&command, sizeof(command));
    command.request = *request;
    DWORD_PTR messageResult = 0;
    if (!SendMessageTimeoutW(s_mainWindow, WM_APP_CATIME_IPC_COMMAND,
            0, (LPARAM)&command, SMTO_ABORTIFHUNG | SMTO_BLOCK,
            IPC_UI_TIMEOUT_MS, &messageResult)) {
        *error = CATIME_IPC_ERROR_INTERNAL_ERROR;
        return FALSE;
    }
    *error = command.error;
    if (command.hasSnapshot) *snapshot = command.snapshot;
    return command.hasSnapshot;
}

static BOOL ProcessRequest(const CatimeIpcRequest* request,
                           BOOL* handshaken,
                           char* response,
                           size_t capacity) {
    char escapedRequest[CATIME_IPC_MAX_REQUEST_ID_BYTES * 2 + 1];
    if (!EscapeJson(request->requestId, escapedRequest,
                    sizeof(escapedRequest))) {
        return SerializeError("", CATIME_IPC_ERROR_INVALID_REQUEST,
                              response, capacity);
    }
    if (request->command == CATIME_IPC_COMMAND_HELLO) {
        *handshaken = TRUE;
        int length = snprintf(response, capacity,
            "{\"type\":\"helloAck\",\"protocol\":1,\"catimeVersion\":\"%s\","
            "\"distributionVersion\":\"%s\",\"buildCommit\":\"unknown\","
            "\"capabilities\":[\"countdown\",\"pause\",\"resume\",\"cancel\","
            "\"completeEvents\",\"stateRecovery\",\"breakPhases\"],"
            "\"requestId\":\"%s\"}\n",
            CATIME_VERSION, CATIME_DISTRIBUTION_VERSION, escapedRequest);
        return length > 0 && (size_t)length < capacity;
    }
    if (!*handshaken) {
        return SerializeError(request->requestId,
                              CATIME_IPC_ERROR_UNSUPPORTED_PROTOCOL,
                              response, capacity);
    }
    if (request->command == CATIME_IPC_COMMAND_PING) {
        int length = snprintf(response, capacity,
            "{\"type\":\"pong\",\"requestId\":\"%s\"}\n",
            escapedRequest);
        return length > 0 && (size_t)length < capacity;
    }
    if (request->command == CATIME_IPC_COMMAND_GET_STATE) {
        CatimeIpcSnapshot snapshot;
        BOOL hasSnapshot;
        EnterCriticalSection(&s_stateLock);
        hasSnapshot = CatimeIpcState_GetAt(&s_state, EpochMilliseconds(), &snapshot);
        LeaveCriticalSection(&s_stateLock);
        if (!hasSnapshot) {
            return SerializeError(request->requestId,
                                  CATIME_IPC_ERROR_SESSION_MISMATCH,
                                  response, capacity);
        }
        return SerializeSnapshot(&snapshot, request->requestId,
                                 FALSE, response, capacity);
    }
    if (request->command == CATIME_IPC_COMMAND_ACK_EVENT) {
        CatimeIpcError error;
        EnterCriticalSection(&s_stateLock);
        error = CatimeIpcState_Acknowledge(&s_state, request->sessionId,
                                           request->revision);
        if (error == CATIME_IPC_ERROR_NONE) {
            CatimeIpcPersistence_Save(&s_state);
        }
        LeaveCriticalSection(&s_stateLock);
        if (error != CATIME_IPC_ERROR_NONE) {
            return SerializeError(request->requestId, error, response, capacity);
        }
        int length = snprintf(response, capacity,
            "{\"type\":\"eventAck\",\"requestId\":\"%s\"}\n",
            escapedRequest);
        return length > 0 && (size_t)length < capacity;
    }
    CatimeIpcSnapshot snapshot;
    CatimeIpcError error = CATIME_IPC_ERROR_NONE;
    if (!DispatchUiCommand(request, &snapshot, &error)) {
        return SerializeError(request->requestId, error, response, capacity);
    }
    return SerializeSnapshot(&snapshot, request->requestId,
                             FALSE, response, capacity);
}

static const char* FindCached(CachedResponse* cache, const char* requestId) {
    for (size_t index = 0; index < CATIME_IPC_MAX_RECENT_REQUESTS; index++) {
        if (cache[index].requestId[0] &&
            strcmp(cache[index].requestId, requestId) == 0) {
            return cache[index].response;
        }
    }
    return NULL;
}

static void StoreCached(CachedResponse* cache, size_t* next,
                        const char* requestId, const char* response) {
    CachedResponse* entry = &cache[*next];
    strncpy_s(entry->requestId, sizeof(entry->requestId), requestId, _TRUNCATE);
    strncpy_s(entry->response, sizeof(entry->response), response, _TRUNCATE);
    *next = (*next + 1) % CATIME_IPC_MAX_RECENT_REQUESTS;
}

static void HandleFrame(HANDLE pipe, const char* frame, size_t length,
                        BOOL* handshaken, CachedResponse* cache,
                        size_t* cacheNext) {
    CatimeIpcRequest request;
    ZeroMemory(&request, sizeof(request));
    CatimeIpcError error = CatimeIpcRequest_Parse(frame, length, &request);
    char response[IPC_RESPONSE_CAPACITY];
    if (error != CATIME_IPC_ERROR_NONE) {
        SerializeError(request.requestId, error, response, sizeof(response));
        WriteResponse(pipe, response);
        return;
    }
    const char* cached = FindCached(cache, request.requestId);
    if (cached) {
        WriteResponse(pipe, cached);
        return;
    }
    if (!ProcessRequest(&request, handshaken, response, sizeof(response))) {
        SerializeError(request.requestId, CATIME_IPC_ERROR_INTERNAL_ERROR,
                       response, sizeof(response));
    }
    StoreCached(cache, cacheNext, request.requestId, response);
    WriteResponse(pipe, response);
}

static BOOL CreatePipeSecurity(SECURITY_ATTRIBUTES* attributes,
                               PSECURITY_DESCRIPTOR* descriptor) {
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;OW)", SDDL_REVISION_1,
            descriptor, NULL)) {
        return FALSE;
    }
    attributes->nLength = sizeof(*attributes);
    attributes->lpSecurityDescriptor = *descriptor;
    attributes->bInheritHandle = FALSE;
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
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, CATIME_IPC_MAX_MESSAGE_BYTES, CATIME_IPC_MAX_MESSAGE_BYTES, 0, &security);
        LocalFree(descriptor);
        if (pipe == INVALID_HANDLE_VALUE) {
            LOG_WINDOWS_ERROR("Catime IPC pipe creation failed");
            break;
        }
        BOOL connected = ConnectNamedPipe(pipe, NULL)
            ? TRUE : GetLastError() == ERROR_PIPE_CONNECTED;
        if (!connected) {
            CloseHandle(pipe);
            continue;
        }
        DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
        SetNamedPipeHandleState(pipe, &mode, NULL, NULL);
        BOOL handshaken = FALSE;
        char frame[CATIME_IPC_MAX_MESSAGE_BYTES];
        size_t frameLength = 0;
        CachedResponse cache[CATIME_IPC_MAX_RECENT_REQUESTS];
        ZeroMemory(cache, sizeof(cache));
        size_t cacheNext = 0;
        while (WaitForSingleObject(s_stopEvent, 0) != WAIT_OBJECT_0) {
            char chunk[512];
            DWORD read = 0;
            BOOL readOk = ReadFile(pipe, chunk, sizeof(chunk), &read, NULL);
            if (readOk && read > 0) {
                for (DWORD index = 0; index < read; index++) {
                    if (chunk[index] == '\n') {
                        if (frameLength > 0) {
                            if (frame[frameLength - 1] == '\r') frameLength--;
                            HandleFrame(pipe, frame, frameLength, &handshaken,
                                        cache, &cacheNext);
                        }
                        frameLength = 0;
                    } else if (frameLength + 1 < sizeof(frame)) {
                        frame[frameLength++] = chunk[index];
                    } else {
                        char errorResponse[IPC_RESPONSE_CAPACITY];
                        SerializeError("", CATIME_IPC_ERROR_MESSAGE_TOO_LARGE,
                                       errorResponse, sizeof(errorResponse));
                        WriteResponse(pipe, errorResponse);
                        frameLength = 0;
                        DisconnectNamedPipe(pipe);
                        break;
                    }
                }
            } else if (!readOk) {
                DWORD readError = GetLastError();
                if (readError == ERROR_BROKEN_PIPE || readError == ERROR_PIPE_NOT_CONNECTED) {
                    break;
                }
            }
            CatimeIpcSnapshot eventSnapshot;
            BOOL hasEvent = FALSE;
            EnterCriticalSection(&s_stateLock);
            if (s_hasPendingEvent) {
                eventSnapshot = s_pendingEvent;
                s_hasPendingEvent = FALSE;
                hasEvent = TRUE;
            }
            LeaveCriticalSection(&s_stateLock);
            if (hasEvent) {
                char eventResponse[IPC_RESPONSE_CAPACITY];
                if (SerializeSnapshot(&eventSnapshot, NULL, TRUE,
                                      eventResponse, sizeof(eventResponse))) {
                    WriteResponse(pipe, eventResponse);
                }
            }
            Sleep(IPC_POLL_INTERVAL_MS);
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    return 0;
}

BOOL CatimeIpcServer_Start(HWND mainWindow) {
    if (s_thread) return TRUE;
    if (!mainWindow || !IsWindow(mainWindow)) return FALSE;
    InitializeCriticalSection(&s_stateLock);
    s_lockInitialized = TRUE;
    CatimeIpcState_Init(&s_state);
    CatimeIpcSnapshot recoveredCompletion;
    ZeroMemory(&recoveredCompletion, sizeof(recoveredCompletion));
    if (CatimeIpcPersistence_Load(&s_state, EpochMilliseconds(),
                                  &recoveredCompletion) &&
        recoveredCompletion.status == CATIME_IPC_STATUS_COMPLETED) {
        QueueEventLocked(&recoveredCompletion);
    }
    CatimeIpcSnapshot recoveredState;
    if (CatimeIpcState_GetAt(&s_state, EpochMilliseconds(), &recoveredState) &&
        (recoveredState.status == CATIME_IPC_STATUS_RUNNING ||
         recoveredState.status == CATIME_IPC_STATUS_PAUSED)) {
        s_applyingUiCommand = TRUE;
        CLOCK_SHOW_CURRENT_TIME = false;
        CLOCK_COUNT_UP = false;
        CLOCK_TOTAL_TIME = (int32_t)recoveredState.remainingSeconds;
        ResetTimer();
        if (recoveredState.status == CATIME_IPC_STATUS_PAUSED) TogglePauseTimer();
        s_applyingUiCommand = FALSE;
    }
    s_mainWindow = mainWindow;
    s_stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!s_stopEvent) {
        DeleteCriticalSection(&s_stateLock);
        s_lockInitialized = FALSE;
        return FALSE;
    }
    s_thread = CreateThread(NULL, 0, IpcThreadProc, NULL, 0, NULL);
    if (!s_thread) {
        CloseHandle(s_stopEvent);
        s_stopEvent = NULL;
        DeleteCriticalSection(&s_stateLock);
        s_lockInitialized = FALSE;
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
    DWORD waitResult = WaitForSingleObject(s_thread, 5000);
    if (waitResult != WAIT_OBJECT_0) {
        CancelSynchronousIo(s_thread);
        waitResult = WaitForSingleObject(s_thread, 1000);
    }
    if (waitResult != WAIT_OBJECT_0) {
        LOG_WARNING("Week Planner IPC worker did not stop cleanly");
        return;
    }
    CloseHandle(s_thread);
    CloseHandle(s_stopEvent);
    s_thread = NULL;
    s_stopEvent = NULL;
    s_mainWindow = NULL;
    if (s_lockInitialized) {
        DeleteCriticalSection(&s_stateLock);
        s_lockInitialized = FALSE;
    }
}

LRESULT CatimeIpcServer_HandleUiMessage(HWND window, LPARAM parameter) {
    IpcUiCommand* command = (IpcUiCommand*)parameter;
    if (!command) return 0;
    EnterCriticalSection(&s_stateLock);
    int64_t nowMs = EpochMilliseconds();
    switch (command->request.command) {
        case CATIME_IPC_COMMAND_START:
            command->error = CatimeIpcState_Start(&s_state,
                command->request.sessionId, command->request.durationSeconds,
                command->request.phase, nowMs, &command->snapshot);
            break;
        case CATIME_IPC_COMMAND_PAUSE:
            command->error = CatimeIpcState_Pause(&s_state,
                command->request.sessionId, nowMs, CATIME_IPC_CAUSE_CLIENT,
                &command->snapshot);
            break;
        case CATIME_IPC_COMMAND_RESUME:
            command->error = CatimeIpcState_Resume(&s_state,
                command->request.sessionId, nowMs, CATIME_IPC_CAUSE_CLIENT,
                &command->snapshot);
            break;
        case CATIME_IPC_COMMAND_CANCEL:
            command->error = CatimeIpcState_Cancel(&s_state,
                command->request.sessionId, nowMs, CATIME_IPC_CAUSE_CLIENT,
                &command->snapshot);
            break;
        case CATIME_IPC_COMMAND_FINISH:
            command->error = CatimeIpcState_Finish(&s_state,
                command->request.sessionId, nowMs, CATIME_IPC_CAUSE_CLIENT,
                &command->snapshot);
            break;
        default:
            command->error = CATIME_IPC_ERROR_UNKNOWN_COMMAND;
            break;
    }
    command->hasSnapshot = command->error == CATIME_IPC_ERROR_NONE;
    if (command->hasSnapshot) CatimeIpcPersistence_Save(&s_state);
    LeaveCriticalSection(&s_stateLock);

    if (command->hasSnapshot) {
        s_applyingUiCommand = TRUE;
        if (command->request.command == CATIME_IPC_COMMAND_START) {
            CLOCK_SHOW_CURRENT_TIME = false;
            CLOCK_COUNT_UP = false;
            CLOCK_TOTAL_TIME = (int32_t)command->snapshot.plannedSeconds;
            ResetTimer();
        } else if (command->request.command == CATIME_IPC_COMMAND_PAUSE &&
                   !CLOCK_IS_PAUSED) {
            TogglePauseTimer();
        } else if (command->request.command == CATIME_IPC_COMMAND_RESUME &&
                   CLOCK_IS_PAUSED) {
            TogglePauseTimer();
        } else if (command->request.command == CATIME_IPC_COMMAND_CANCEL ||
                   command->request.command == CATIME_IPC_COMMAND_FINISH) {
            CLOCK_IS_PAUSED = true;
            countdown_elapsed_time = (int32_t)command->snapshot.focusedSeconds;
        }
        s_applyingUiCommand = FALSE;
        InvalidateRect(window, NULL, TRUE);
    }
    return 0;
}

void CatimeIpcServer_NotifyTimeout(void) {
    if (!s_lockInitialized) return;
    CatimeIpcSnapshot snapshot;
    EnterCriticalSection(&s_stateLock);
    if (CatimeIpcState_Tick(&s_state, EpochMilliseconds() + 1000, &snapshot)) {
        QueueEventLocked(&snapshot);
        CatimeIpcPersistence_Save(&s_state);
    }
    LeaveCriticalSection(&s_stateLock);
}

void CatimeIpcServer_NotifyPauseChanged(BOOL paused) {
    if (!s_lockInitialized || s_applyingUiCommand) return;
    CatimeIpcSnapshot current;
    CatimeIpcSnapshot updated;
    EnterCriticalSection(&s_stateLock);
    if (CatimeIpcState_Get(&s_state, &current)) {
        CatimeIpcError error = paused
            ? CatimeIpcState_Pause(&s_state, current.sessionId,
                EpochMilliseconds(), CATIME_IPC_CAUSE_TRAY, &updated)
            : CatimeIpcState_Resume(&s_state, current.sessionId,
                EpochMilliseconds(), CATIME_IPC_CAUSE_TRAY, &updated);
        if (error == CATIME_IPC_ERROR_NONE) {
            QueueEventLocked(&updated);
            CatimeIpcPersistence_Save(&s_state);
        }
    }
    LeaveCriticalSection(&s_stateLock);
}

void CatimeIpcServer_NotifyCancelled(void) {
    if (!s_lockInitialized || s_applyingUiCommand) return;
    CatimeIpcSnapshot current;
    CatimeIpcSnapshot updated;
    EnterCriticalSection(&s_stateLock);
    if (CatimeIpcState_Get(&s_state, &current) &&
        !CatimeIpc_IsTerminalStatus(current.status) &&
        CatimeIpcState_Cancel(&s_state, current.sessionId,
            EpochMilliseconds(), CATIME_IPC_CAUSE_USER_REPLACED,
            &updated) == CATIME_IPC_ERROR_NONE) {
        QueueEventLocked(&updated);
        CatimeIpcPersistence_Save(&s_state);
    }
    LeaveCriticalSection(&s_stateLock);
}
