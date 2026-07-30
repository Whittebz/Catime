/**
 * @file ipc_request.c
 * @brief Bounded JSON object tokenizer for IPC requests.
 */

#include "ipc/catime_ipc_request.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* cursor;
    const char* end;
} JsonCursor;

static void SkipWhitespace(JsonCursor* json) {
    while (json->cursor < json->end &&
           isspace((unsigned char)*json->cursor)) {
        json->cursor++;
    }
}

static bool Consume(JsonCursor* json, char expected) {
    SkipWhitespace(json);
    if (json->cursor >= json->end || *json->cursor != expected) return false;
    json->cursor++;
    return true;
}

static bool HexDigit(char value) {
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static bool ParseString(JsonCursor* json, char* output, size_t outputSize) {
    if (!output || outputSize == 0 || !Consume(json, '"')) return false;
    size_t written = 0;
    while (json->cursor < json->end) {
        unsigned char value = (unsigned char)*json->cursor++;
        if (value == '"') {
            output[written] = '\0';
            return true;
        }
        if (value < 0x20) return false;
        if (value == '\\') {
            if (json->cursor >= json->end) return false;
            char escaped = *json->cursor++;
            switch (escaped) {
                case '"': value = '"'; break;
                case '\\': value = '\\'; break;
                case '/': value = '/'; break;
                case 'b': value = '\b'; break;
                case 'f': value = '\f'; break;
                case 'n': value = '\n'; break;
                case 'r': value = '\r'; break;
                case 't': value = '\t'; break;
                case 'u':
                    if ((json->end - json->cursor) < 4 ||
                        !HexDigit(json->cursor[0]) || !HexDigit(json->cursor[1]) ||
                        !HexDigit(json->cursor[2]) || !HexDigit(json->cursor[3])) {
                        return false;
                    }
                    /* Identifiers emitted by Week Planner are ASCII UUIDs. */
                    return false;
                default: return false;
            }
        }
        if (written + 1 >= outputSize) return false;
        output[written++] = (char)value;
    }
    return false;
}

static bool ParseUnsigned(JsonCursor* json, uint64_t* output) {
    SkipWhitespace(json);
    if (!output || json->cursor >= json->end ||
        !isdigit((unsigned char)*json->cursor)) return false;
    uint64_t value = 0;
    do {
        unsigned int digit = (unsigned int)(*json->cursor - '0');
        if (value > (UINT64_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
        json->cursor++;
    } while (json->cursor < json->end &&
             isdigit((unsigned char)*json->cursor));
    *output = value;
    return true;
}

static bool SkipLiteral(JsonCursor* json) {
    SkipWhitespace(json);
    const char* start = json->cursor;
    while (json->cursor < json->end &&
           *json->cursor != ',' && *json->cursor != '}') {
        if (isspace((unsigned char)*json->cursor)) break;
        json->cursor++;
    }
    size_t length = (size_t)(json->cursor - start);
    return (length == 4 && memcmp(start, "true", 4) == 0) ||
           (length == 5 && memcmp(start, "false", 5) == 0) ||
           (length == 4 && memcmp(start, "null", 4) == 0);
}

static bool SkipValue(JsonCursor* json) {
    SkipWhitespace(json);
    if (json->cursor >= json->end) return false;
    if (*json->cursor == '"') {
        char ignored[256];
        return ParseString(json, ignored, sizeof(ignored));
    }
    if (isdigit((unsigned char)*json->cursor)) {
        uint64_t ignored;
        return ParseUnsigned(json, &ignored);
    }
    return SkipLiteral(json);
}

static CatimeIpcCommand ParseCommand(const char* value) {
    if (strcmp(value, "hello") == 0) return CATIME_IPC_COMMAND_HELLO;
    if (strcmp(value, "start") == 0) return CATIME_IPC_COMMAND_START;
    if (strcmp(value, "pause") == 0) return CATIME_IPC_COMMAND_PAUSE;
    if (strcmp(value, "resume") == 0) return CATIME_IPC_COMMAND_RESUME;
    if (strcmp(value, "cancel") == 0) return CATIME_IPC_COMMAND_CANCEL;
    if (strcmp(value, "finish") == 0) return CATIME_IPC_COMMAND_FINISH;
    if (strcmp(value, "getState") == 0) return CATIME_IPC_COMMAND_GET_STATE;
    if (strcmp(value, "ackEvent") == 0) return CATIME_IPC_COMMAND_ACK_EVENT;
    if (strcmp(value, "queuePhase") == 0) return CATIME_IPC_COMMAND_QUEUE_PHASE;
    if (strcmp(value, "ping") == 0) return CATIME_IPC_COMMAND_PING;
    return CATIME_IPC_COMMAND_INVALID;
}

static bool AssignStringField(CatimeIpcRequest* request,
                              const char* key,
                              const char* value) {
    if (strcmp(key, "type") == 0) {
        request->command = ParseCommand(value);
    } else if (strcmp(key, "requestId") == 0) {
        size_t length = strlen(value);
        if (length == 0 || length > CATIME_IPC_MAX_REQUEST_ID_BYTES) return false;
        memcpy(request->requestId, value, length + 1);
    } else if (strcmp(key, "sessionId") == 0) {
        size_t length = strlen(value);
        if (length == 0 || length > CATIME_IPC_MAX_SESSION_ID_BYTES) return false;
        memcpy(request->sessionId, value, length + 1);
        request->hasSessionId = true;
    } else if (strcmp(key, "phase") == 0) {
        if (strcmp(value, "focus") == 0) request->phase = CATIME_IPC_PHASE_FOCUS;
        else if (strcmp(value, "short_break") == 0) request->phase = CATIME_IPC_PHASE_SHORT_BREAK;
        else if (strcmp(value, "long_break") == 0) request->phase = CATIME_IPC_PHASE_LONG_BREAK;
        else return false;
        request->hasPhase = true;
    }
    return true;
}

static bool AssignNumberField(CatimeIpcRequest* request,
                              const char* key,
                              uint64_t value) {
    if (strcmp(key, "protocol") == 0) {
        if (value > UINT32_MAX) return false;
        request->protocol = (uint32_t)value;
        request->hasProtocol = true;
    } else if (strcmp(key, "durationSeconds") == 0) {
        if (value > UINT32_MAX) return false;
        request->durationSeconds = (uint32_t)value;
        request->hasDuration = true;
    } else if (strcmp(key, "revision") == 0) {
        request->revision = value;
        request->hasRevision = true;
    }
    return true;
}

static CatimeIpcError ValidateRequest(const CatimeIpcRequest* request) {
    if (request->requestId[0] == '\0') return CATIME_IPC_ERROR_INVALID_REQUEST;
    if (request->command == CATIME_IPC_COMMAND_INVALID) {
        return CATIME_IPC_ERROR_UNKNOWN_COMMAND;
    }
    if (request->command == CATIME_IPC_COMMAND_HELLO) {
        if (!request->hasProtocol) return CATIME_IPC_ERROR_INVALID_REQUEST;
        return request->protocol == CATIME_IPC_PROTOCOL_VERSION
            ? CATIME_IPC_ERROR_NONE : CATIME_IPC_ERROR_UNSUPPORTED_PROTOCOL;
    }
    if (request->command == CATIME_IPC_COMMAND_START ||
        request->command == CATIME_IPC_COMMAND_QUEUE_PHASE) {
        if (!request->hasSessionId || !request->hasDuration || !request->hasPhase) {
            return CATIME_IPC_ERROR_INVALID_REQUEST;
        }
        if (!CatimeIpc_IsValidDuration(request->durationSeconds)) {
            return CATIME_IPC_ERROR_INVALID_DURATION;
        }
    }
    if (request->command == CATIME_IPC_COMMAND_PAUSE ||
        request->command == CATIME_IPC_COMMAND_RESUME ||
        request->command == CATIME_IPC_COMMAND_CANCEL ||
        request->command == CATIME_IPC_COMMAND_FINISH) {
        if (!request->hasSessionId) return CATIME_IPC_ERROR_INVALID_REQUEST;
    }
    if (request->command == CATIME_IPC_COMMAND_ACK_EVENT &&
        (!request->hasSessionId || !request->hasRevision)) {
        return CATIME_IPC_ERROR_INVALID_REQUEST;
    }
    return CATIME_IPC_ERROR_NONE;
}

CatimeIpcError CatimeIpcRequest_Parse(const char* json,
                                      size_t length,
                                      CatimeIpcRequest* output) {
    if (!json || !output || length == 0 ||
        length >= CATIME_IPC_MAX_MESSAGE_BYTES) {
        return CATIME_IPC_ERROR_INVALID_JSON;
    }
    memset(output, 0, sizeof(*output));
    JsonCursor cursor = {json, json + length};
    if (!Consume(&cursor, '{')) return CATIME_IPC_ERROR_INVALID_JSON;
    SkipWhitespace(&cursor);
    if (cursor.cursor < cursor.end && *cursor.cursor == '}') {
        return CATIME_IPC_ERROR_INVALID_REQUEST;
    }
    for (;;) {
        char key[48];
        if (!ParseString(&cursor, key, sizeof(key)) || !Consume(&cursor, ':')) {
            return CATIME_IPC_ERROR_INVALID_JSON;
        }
        SkipWhitespace(&cursor);
        bool assigned = true;
        if (cursor.cursor < cursor.end && *cursor.cursor == '"') {
            char value[256];
            assigned = ParseString(&cursor, value, sizeof(value)) &&
                       AssignStringField(output, key, value);
        } else if (cursor.cursor < cursor.end &&
                   isdigit((unsigned char)*cursor.cursor)) {
            uint64_t value = 0;
            assigned = ParseUnsigned(&cursor, &value) &&
                       AssignNumberField(output, key, value);
        } else {
            assigned = SkipValue(&cursor);
        }
        if (!assigned) return CATIME_IPC_ERROR_INVALID_REQUEST;
        SkipWhitespace(&cursor);
        if (cursor.cursor < cursor.end && *cursor.cursor == '}') {
            cursor.cursor++;
            break;
        }
        if (!Consume(&cursor, ',')) return CATIME_IPC_ERROR_INVALID_JSON;
    }
    SkipWhitespace(&cursor);
    if (cursor.cursor != cursor.end) return CATIME_IPC_ERROR_INVALID_JSON;
    return ValidateRequest(output);
}
