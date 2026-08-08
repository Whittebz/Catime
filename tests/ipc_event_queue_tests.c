/**
 * @file ipc_event_queue_tests.c
 * @brief Pure-C coverage for per-client multi-vault event delivery.
 */

#include "ipc/catime_ipc_queue.h"

#include <assert.h>
#include <string.h>

static CatimeIpcSnapshot Snapshot(const char* sessionId,
                                  uint64_t revision,
                                  CatimeIpcStatus status) {
    CatimeIpcSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    strncpy(snapshot.sessionId, sessionId, sizeof(snapshot.sessionId) - 1);
    snapshot.status = status;
    snapshot.phase = CATIME_IPC_PHASE_FOCUS;
    snapshot.plannedSeconds = 1500;
    snapshot.focusedSeconds = 1500;
    snapshot.remainingSeconds = 0;
    snapshot.startedAt = 1000;
    snapshot.updatedAt = 151000;
    snapshot.endedAt = 151000;
    snapshot.revision = revision;
    snapshot.cause = CATIME_IPC_CAUSE_TIMEOUT;
    return snapshot;
}

static void TestSingleClientDelivery(void) {
    assert(IpcEventQueue_Initialize());
    int client = IpcEventQueue_RegisterClient();
    assert(client == 0);

    CatimeIpcSnapshot first = Snapshot("s-1", 1, CATIME_IPC_STATUS_COMPLETED);
    CatimeIpcSnapshot second = Snapshot("s-2", 2, CATIME_IPC_STATUS_CANCELLED);
    IpcEventQueue_Push(&first);
    IpcEventQueue_Push(&second);

    CatimeIpcSnapshot peeked;
    assert(IpcSession_PeekEvent(client, &peeked));
    assert(strcmp(peeked.sessionId, "s-1") == 0);
    IpcSession_MarkEventSent(client, &peeked);
    assert(IpcSession_PeekEvent(client, &peeked));
    assert(strcmp(peeked.sessionId, "s-2") == 0);
    IpcSession_MarkEventSent(client, &peeked);
    assert(!IpcSession_PeekEvent(client, &peeked));

    /* Only one client is connected, so its ack removes the event. */
    assert(IpcEventQueue_Acknowledge(client, "s-1", 1) == 2);
    assert(IpcEventQueue_Acknowledge(client, "s-2", 2) == 2);

    IpcEventQueue_UnregisterClient(client);
    IpcEventQueue_Shutdown();
}

static void TestEventsPersistUntilEveryClientAcks(void) {
    assert(IpcEventQueue_Initialize());
    int alpha = IpcEventQueue_RegisterClient();
    int beta = IpcEventQueue_RegisterClient();
    assert(alpha == 0 && beta == 1);

    CatimeIpcSnapshot completed = Snapshot("s-1", 1,
                                           CATIME_IPC_STATUS_COMPLETED);
    IpcEventQueue_Push(&completed);

    /* Alpha receives but beta has not connected-delivered yet. */
    CatimeIpcSnapshot peeked;
    assert(IpcSession_PeekEvent(alpha, &peeked));
    IpcSession_MarkEventSent(alpha, &peeked);
    assert(IpcEventQueue_Acknowledge(alpha, "s-1", 1) == 1);

    /* Beta still sees the event and its ack completes removal. */
    assert(IpcSession_PeekEvent(beta, &peeked));
    IpcSession_MarkEventSent(beta, &peeked);
    assert(IpcEventQueue_Acknowledge(beta, "s-1", 1) == 2);

    /* A fresh event stays until every remaining connected client acks it. */
    CatimeIpcSnapshot cancelled = Snapshot("s-2", 2,
                                           CATIME_IPC_STATUS_CANCELLED);
    IpcEventQueue_Push(&cancelled);
    assert(IpcSession_PeekEvent(alpha, &peeked));
    IpcSession_MarkEventSent(alpha, &peeked);
    assert(IpcEventQueue_Acknowledge(alpha, "s-2", 2) == 1);
    assert(IpcSession_PeekEvent(beta, &peeked));
    IpcSession_MarkEventSent(beta, &peeked);
    assert(IpcEventQueue_Acknowledge(beta, "s-2", 2) == 2);

    IpcEventQueue_UnregisterClient(alpha);
    IpcEventQueue_UnregisterClient(beta);
    IpcEventQueue_Shutdown();
}

static void TestReconnectRedelivers(void) {
    assert(IpcEventQueue_Initialize());
    int client = IpcEventQueue_RegisterClient();
    CatimeIpcSnapshot event = Snapshot("s-1", 1, CATIME_IPC_STATUS_COMPLETED);
    IpcEventQueue_Push(&event);

    CatimeIpcSnapshot peeked;
    assert(IpcSession_PeekEvent(client, &peeked));
    IpcSession_MarkEventSent(client, &peeked);
    IpcSession_ResetEventDelivery(client);
    assert(IpcSession_PeekEvent(client, &peeked));
    assert(strcmp(peeked.sessionId, "s-1") == 0);

    IpcEventQueue_UnregisterClient(client);
    IpcEventQueue_Shutdown();
}

static void TestNoClientsKeepsEvents(void) {
    assert(IpcEventQueue_Initialize());
    CatimeIpcSnapshot event = Snapshot("s-1", 1, CATIME_IPC_STATUS_COMPLETED);
    IpcEventQueue_Push(&event);

    /* With no clients connected the event must survive for the next connect. */
    int client = IpcEventQueue_RegisterClient();
    CatimeIpcSnapshot peeked;
    assert(IpcSession_PeekEvent(client, &peeked));
    assert(strcmp(peeked.sessionId, "s-1") == 0);
    IpcEventQueue_UnregisterClient(client);
    IpcEventQueue_Shutdown();
}

int main(void) {
    TestSingleClientDelivery();
    TestEventsPersistUntilEveryClientAcks();
    TestReconnectRedelivers();
    TestNoClientsKeepsEvents();
    return 0;
}
