# Catime Week Planner IPC v1

## Status

- Contract status: frozen for Phase 1 implementation
- Protocol version: `1`
- Catime distribution version: `1.5.0-wpc.1`
- Transport: local Windows named pipe
- Encoding: UTF-8 NDJSON

This document is the canonical wire contract between the Catime fork and Week
Planner Calendar. The C server and TypeScript client must use the same golden
vectors in `docs/obsidian-ipc-v1-test-vectors.json`.

## Transport

The server listens on:

```text
\\.\pipe\catime-week-planner-v1
```

The pipe is duplex and restricted to the current Windows user. One JSON object
is sent per line. A message, including its trailing newline, must not exceed
4096 bytes. Invalid UTF-8, embedded newlines in a frame, excessive nesting, and
messages over the limit are rejected before command dispatch.

The pipe server runs outside the UI thread. It must marshal commands to the
Catime main window with `PostMessage`; it must never mutate timer globals from
the pipe thread.

## Handshake

The client sends `hello` before any other command:

```json
{"type":"hello","protocol":1,"client":"week-planner-calendar","clientVersion":"1.0.1","requestId":"r1"}
```

The server responds:

```json
{"type":"helloAck","protocol":1,"catimeVersion":"1.5.0","distributionVersion":"1.5.0-wpc.1","buildCommit":"COMMIT_SHA","capabilities":["countdown","pause","resume","cancel","completeEvents","stateRecovery","breakPhases","phaseQueue"],"requestId":"r1"}
```

An unsupported protocol returns `unsupported_protocol`. The integration must
not silently fall back to Catime's CLI because CLI control cannot reconcile
state.

## Commands

| Type | Required fields | Meaning |
| --- | --- | --- |
| `start` | `requestId`, `sessionId`, `durationSeconds`, `phase` | Start one externally owned countdown |
| `queuePhase` | `requestId`, `sessionId`, `durationSeconds`, `phase` | Append one phase to the active plan |
| `pause` | `requestId`, `sessionId` | Pause the matching running session |
| `resume` | `requestId`, `sessionId` | Resume the matching paused session |
| `cancel` | `requestId`, `sessionId` | Cancel without recording a completed focus interval |
| `finish` | `requestId`, `sessionId` | Complete immediately with elapsed focused time |
| `getState` | `requestId` | Return the active or most recent recoverable state |
| `ackEvent` | `requestId`, `sessionId`, `revision` | Confirm durable client processing of a terminal event |
| `ping` | `requestId` | Confirm pipe liveness without reading timer state |

`requestId` and `sessionId` are non-empty UTF-8 strings up to 64 bytes. Week
Planner Calendar uses UUIDs for session IDs. `phase` accepts `focus`,
`short_break`, and `long_break`. At most 23 phases may be queued after the
active phase. Queued phases are persisted locally and advance after natural
timeouts even when Obsidian is closed.

`durationSeconds` is an integer from 1 through 10800 inclusive.

## Idempotency

The server retains the 128 most recent request IDs for the active client.
Repeating an accepted request ID returns its original response without applying
the command again. Repeating `start` with a new request ID but the same session
ID and identical parameters returns the current snapshot. A different active
session produces `session_conflict` and is never replaced implicitly.

Commands targeting a session other than the active or recoverable session
produce `session_mismatch`.

Repeating an identical `queuePhase` is idempotent. Reusing its session ID with
different phase parameters returns `session_conflict`. Starting the next queued
session explicitly consumes that queue entry, which lets the client advance a
phase early without leaving a duplicate behind.

## State snapshot

Every successful state-changing command returns a complete state object:

```json
{"type":"state","requestId":"r2","sessionId":"018f47d2-8d1b-7d35-9e14-7ccaa208d493","status":"running","phase":"focus","plannedSeconds":1500,"focusedSeconds":0,"remainingSeconds":1500,"startedAt":1785480000000,"updatedAt":1785480000000,"deadlineAt":1785481500000,"revision":1,"cause":"client"}
```

Time values are Unix epoch milliseconds. `deadlineAt` is non-null only while
running. `endedAt` is present only for terminal states. The server clamps
focused time to planned time for a natural timeout, but `finish` may complete
with a smaller focused value.

Revision zero means no external session. Starting a session creates revision
one. Every accepted state mutation increments revision exactly once. Clients
ignore snapshots and events whose revision is not greater than their stored
revision, except when answering the request that established the same state.

## Events

State changes originating from timeout, tray controls, global hotkeys,
shutdown, or replacement are sent without `requestId`. Event `type` is the new
terminal status for terminal events and `stateChanged` for non-terminal events.

Causes are:

- `client`
- `tray`
- `hotkey`
- `timeout`
- `user_replaced`
- `shutdown`

Catime stores the most recent externally owned session snapshot under
`%LOCALAPPDATA%\Catime\integration-state.ini`. A reconnecting client calls
`getState`, persists any newer revision, and sends `ackEvent` for a terminal
event. The persisted integration state contains identifiers and timing only;
task titles and vault paths are never sent to Catime.

## State machine

```text
idle -> running
running -> paused | completed | cancelled
paused -> running | completed | cancelled
completed -> idle
cancelled -> idle
```

A same-state idempotent reply does not mutate state and does not increment the
revision. Any transition absent from the diagram returns `invalid_state`.

## Stable error codes

| Code | Meaning | Recoverable |
| --- | --- | --- |
| `invalid_json` | The frame is not a valid protocol object | No for that frame |
| `message_too_large` | The 4096-byte limit was exceeded | Reconnect required |
| `unsupported_protocol` | Client and server protocol versions differ | No |
| `invalid_request` | A required field is missing or has the wrong type | No for that request |
| `invalid_duration` | Duration is outside the accepted range | Yes |
| `invalid_session_id` | Session ID is empty or too long | Yes |
| `unknown_command` | Command type is not part of protocol v1 | No |
| `session_conflict` | Another external session is active | Yes after reconciliation |
| `session_mismatch` | The command targets a different session | Yes after `getState` |
| `invalid_state` | The requested state transition is not allowed | Yes after `getState` |
| `internal_error` | Catime could not complete an accepted operation | Maybe |

Errors use this envelope:

```json
{"type":"error","requestId":"r5","code":"session_mismatch","message":"The active session does not match.","recoverable":true}
```

Only `code` is stable API. Clients must not branch on `message`.

The complete v1 code set is `invalid_json`, `message_too_large`,
`unsupported_protocol`, `invalid_request`, `invalid_duration`,
`invalid_session_id`, `unknown_command`, `session_conflict`,
`session_mismatch`, `invalid_state`, and `internal_error`.

## Phase 1 acceptance contract

- C and TypeScript implementations consume the golden vectors without changing
  their meaning.
- All stable names come from `catime_ipc_protocol.h` on the C side and a single
  matching TypeScript module on the client side.
- Duplicate commands cannot double-apply elapsed time or terminal events.
- Disconnect and reconnect can recover the latest terminal revision.
- No task title, note content, vault path, network endpoint, or telemetry enters
  the protocol.
