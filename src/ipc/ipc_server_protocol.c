/**
 * @file ipc_server_protocol.c
 * @brief IPC response encoding, replay cache, and frame dispatch.
 */

#include "ipc_server_internal.h"
#include "../../resource/resource.h"

#include <stdio.h>
#include <string.h>

#define CACHED_RESPONSE_CAPACITY 1024

typedef struct {
    char requestId[CATIME_IPC_MAX_REQUEST_ID_BYTES + 1];
    char response[CACHED_RESPONSE_CAPACITY];
} CachedResponse;

typedef struct {
    CachedResponse entries[CATIME_IPC_MAX_RECENT_REQUESTS];
    size_t next;
} ResponseCache;

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

static BOOL Recoverable(CatimeIpcError error) {
    return error == CATIME_IPC_ERROR_INVALID_DURATION ||
           error == CATIME_IPC_ERROR_INVALID_SESSION_ID ||
           error == CATIME_IPC_ERROR_SESSION_CONFLICT ||
           error == CATIME_IPC_ERROR_SESSION_MISMATCH ||
           error == CATIME_IPC_ERROR_INVALID_STATE ||
           error == CATIME_IPC_ERROR_INTERNAL_ERROR;
}

static BOOL SerializeError(const char* requestId, CatimeIpcError error,
                           char* output, size_t capacity) {
    char escaped[CATIME_IPC_MAX_REQUEST_ID_BYTES * 2 + 1] = "";
    EscapeJson(requestId ? requestId : "", escaped, sizeof(escaped));
    int length = snprintf(output, capacity,
        "{\"type\":\"error\",\"requestId\":\"%s\",\"code\":\"%s\","
        "\"message\":\"%s\",\"recoverable\":%s}\n",
        escaped, CatimeIpc_ErrorCode(error), ErrorMessage(error),
        Recoverable(error) ? "true" : "false");
    return length > 0 && (size_t)length < capacity;
}

static BOOL SerializeSnapshot(const CatimeIpcSnapshot* snapshot,
                              const char* requestId, BOOL event,
                              char* output, size_t capacity) {
    char session[CATIME_IPC_MAX_SESSION_ID_BYTES * 2 + 1];
    char request[CATIME_IPC_MAX_REQUEST_ID_BYTES * 2 + 1] = "";
    if (!snapshot || !EscapeJson(snapshot->sessionId, session,
                                 sizeof(session))) return FALSE;
    if (requestId && !EscapeJson(requestId, request, sizeof(request))) return FALSE;
    const char* type = "state";
    if (event) {
        type = snapshot->status == CATIME_IPC_STATUS_COMPLETED ? "completed" :
            snapshot->status == CATIME_IPC_STATUS_CANCELLED ? "cancelled" :
            "stateChanged";
    }
    char requestField[192] = "";
    if (requestId && snprintf(requestField, sizeof(requestField),
        "\"requestId\":\"%s\",", request) < 0) return FALSE;
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
        type, requestField, session, CatimeIpc_StatusName(snapshot->status),
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
    size_t length = strlen(response);
    DWORD written = 0;
    return length < CATIME_IPC_MAX_MESSAGE_BYTES &&
        WriteFile(pipe, response, (DWORD)length, &written, NULL) &&
        written == (DWORD)length;
}

static BOOL SerializeReply(const CatimeIpcRequest* request,
                           const IpcReply* reply,
                           char* output, size_t capacity) {
    char escaped[CATIME_IPC_MAX_REQUEST_ID_BYTES * 2 + 1];
    if (!EscapeJson(request->requestId, escaped, sizeof(escaped))) return FALSE;
    if (reply->type == IPC_REPLY_ERROR) {
        return SerializeError(request->requestId, reply->error, output, capacity);
    }
    if (reply->type == IPC_REPLY_SNAPSHOT) {
        return SerializeSnapshot(&reply->snapshot, request->requestId,
                                 FALSE, output, capacity);
    }
    if (reply->type == IPC_REPLY_HELLO) {
        int length = snprintf(output, capacity,
            "{\"type\":\"helloAck\",\"protocol\":1,\"catimeVersion\":\"%s\","
            "\"distributionVersion\":\"%s\",\"buildCommit\":\"unknown\","
            "\"capabilities\":[\"countdown\",\"pause\",\"resume\",\"cancel\","
            "\"completeEvents\",\"stateRecovery\",\"breakPhases\"],"
            "\"requestId\":\"%s\"}\n",
            CATIME_VERSION, CATIME_DISTRIBUTION_VERSION, escaped);
        return length > 0 && (size_t)length < capacity;
    }
    const char* type = reply->type == IPC_REPLY_PONG ? "pong" : "eventAck";
    int length = snprintf(output, capacity,
        "{\"type\":\"%s\",\"requestId\":\"%s\"}\n", type, escaped);
    return length > 0 && (size_t)length < capacity;
}

void* IpcProtocol_CreateCache(void) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ResponseCache));
}

void IpcProtocol_DestroyCache(void* cache) {
    if (cache) HeapFree(GetProcessHeap(), 0, cache);
}

static const char* FindCached(ResponseCache* cache, const char* requestId) {
    for (size_t index = 0; index < CATIME_IPC_MAX_RECENT_REQUESTS; index++) {
        if (cache->entries[index].requestId[0] &&
            strcmp(cache->entries[index].requestId, requestId) == 0) {
            return cache->entries[index].response;
        }
    }
    return NULL;
}

static void StoreCached(ResponseCache* cache, const char* requestId,
                        const char* response) {
    CachedResponse* entry = &cache->entries[cache->next];
    strncpy_s(entry->requestId, sizeof(entry->requestId), requestId, _TRUNCATE);
    strncpy_s(entry->response, sizeof(entry->response), response, _TRUNCATE);
    cache->next = (cache->next + 1) % CATIME_IPC_MAX_RECENT_REQUESTS;
}

void IpcProtocol_HandleFrame(HANDLE pipe, const char* frame,
                             size_t length, BOOL* handshaken,
                             void* responseCache) {
    ResponseCache* cache = (ResponseCache*)responseCache;
    CatimeIpcRequest request;
    ZeroMemory(&request, sizeof(request));
    CatimeIpcError error = length >= CATIME_IPC_MAX_MESSAGE_BYTES
        ? CATIME_IPC_ERROR_MESSAGE_TOO_LARGE
        : CatimeIpcRequest_Parse(frame, length, &request);
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
    IpcReply reply;
    ZeroMemory(&reply, sizeof(reply));
    if (!IpcSession_Execute(&request, handshaken, &reply) ||
        !SerializeReply(&request, &reply, response, sizeof(response))) {
        SerializeError(request.requestId, CATIME_IPC_ERROR_INTERNAL_ERROR,
                       response, sizeof(response));
    }
    StoreCached(cache, request.requestId, response);
    WriteResponse(pipe, response);
}

BOOL IpcProtocol_WriteEvent(HANDLE pipe,
                            const CatimeIpcSnapshot* snapshot) {
    char response[IPC_RESPONSE_CAPACITY];
    return SerializeSnapshot(snapshot, NULL, TRUE, response, sizeof(response)) &&
           WriteResponse(pipe, response);
}
