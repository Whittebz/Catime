#include "ipc/catime_ipc_request.h"

#include <assert.h>
#include <string.h>

int main(void) {
    CatimeIpcRequest request;
    const char* start = "{\"type\":\"start\",\"requestId\":\"r1\","
        "\"sessionId\":\"s1\",\"durationSeconds\":1500,\"phase\":\"focus\"}";
    assert(CatimeIpcRequest_Parse(start, strlen(start), &request) ==
           CATIME_IPC_ERROR_NONE);
    assert(request.command == CATIME_IPC_COMMAND_START);
    assert(request.durationSeconds == 1500);
    assert(request.phase == CATIME_IPC_PHASE_FOCUS);

    const char* hello = "{\"type\":\"hello\",\"protocol\":2,\"requestId\":\"r2\"}";
    assert(CatimeIpcRequest_Parse(hello, strlen(hello), &request) ==
           CATIME_IPC_ERROR_UNSUPPORTED_PROTOCOL);

    const char* nested = "{\"type\":\"ping\",\"requestId\":\"r3\",\"bad\":{}}";
    assert(CatimeIpcRequest_Parse(nested, strlen(nested), &request) ==
           CATIME_IPC_ERROR_INVALID_REQUEST);
    return 0;
}
