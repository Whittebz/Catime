/**
 * @file ipc_persistence.c
 * @brief Integration-state INI persistence under the current user's app data.
 *
 * Added by the Week Planner Calendar fork. Besides the latest session state it
 * also stores a bounded history of terminal sessions that no client has
 * acknowledged yet, so a plugin that connects later can backfill everything.
 */

#include "ipc/catime_ipc_persistence.h"
#include "ipc/catime_ipc_history.h"

#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static BOOL StatePath(wchar_t* output, size_t capacity) {
    if (!output || capacity < MAX_PATH) return FALSE;
    wchar_t localAppData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL,
                                SHGFP_TYPE_CURRENT, localAppData))) {
        return FALSE;
    }
    wchar_t directory[MAX_PATH];
    if (_snwprintf_s(directory, _countof(directory), _TRUNCATE,
                     L"%s\\Catime", localAppData) < 0) return FALSE;
    if (!CreateDirectoryW(directory, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
    return _snwprintf_s(output, capacity, _TRUNCATE,
                        L"%s\\integration-state.ini", directory) >= 0;
}

static BOOL ToWide(const char* input, wchar_t* output, size_t capacity) {
    return input && output && capacity > 0 &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1,
                            output, (int)capacity) > 0;
}

static void WriteNumber(const wchar_t* path, const wchar_t* section,
                        const wchar_t* key, long long value) {
    wchar_t buffer[48];
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%lld", value);
    WritePrivateProfileStringW(section, key, buffer, path);
}

static int64_t ReadNumber(const wchar_t* path, const wchar_t* section,
                          const wchar_t* key) {
    wchar_t buffer[48];
    GetPrivateProfileStringW(section, key, L"0", buffer,
                             _countof(buffer), path);
    return _wtoi64(buffer);
}

static CatimeIpcStatus ParseStatus(const wchar_t* value) {
    if (wcscmp(value, L"running") == 0) return CATIME_IPC_STATUS_RUNNING;
    if (wcscmp(value, L"paused") == 0) return CATIME_IPC_STATUS_PAUSED;
    if (wcscmp(value, L"completed") == 0) return CATIME_IPC_STATUS_COMPLETED;
    if (wcscmp(value, L"cancelled") == 0) return CATIME_IPC_STATUS_CANCELLED;
    return CATIME_IPC_STATUS_IDLE;
}

static CatimeIpcPhase ParsePhase(const wchar_t* value) {
    if (wcscmp(value, L"short_break") == 0) return CATIME_IPC_PHASE_SHORT_BREAK;
    if (wcscmp(value, L"long_break") == 0) return CATIME_IPC_PHASE_LONG_BREAK;
    return CATIME_IPC_PHASE_FOCUS;
}

static CatimeIpcCause ParseCause(const wchar_t* value) {
    if (wcscmp(value, L"tray") == 0) return CATIME_IPC_CAUSE_TRAY;
    if (wcscmp(value, L"hotkey") == 0) return CATIME_IPC_CAUSE_HOTKEY;
    if (wcscmp(value, L"timeout") == 0) return CATIME_IPC_CAUSE_TIMEOUT;
    if (wcscmp(value, L"user_replaced") == 0)
        return CATIME_IPC_CAUSE_USER_REPLACED;
    if (wcscmp(value, L"shutdown") == 0) return CATIME_IPC_CAUSE_SHUTDOWN;
    return CATIME_IPC_CAUSE_CLIENT;
}

static void WriteString(const wchar_t* path, const wchar_t* section,
                        const wchar_t* key, const char* value) {
    wchar_t wide[CATIME_IPC_MAX_SESSION_ID_BYTES + 1];
    if (!ToWide(value, wide, _countof(wide))) return;
    WritePrivateProfileStringW(section, key, wide, path);
}

static BOOL ReadString(const wchar_t* path, const wchar_t* section,
                       const wchar_t* key, char* output, size_t capacity) {
    wchar_t wide[CATIME_IPC_MAX_SESSION_ID_BYTES + 1] = L"";
    GetPrivateProfileStringW(section, key, L"", wide,
                             _countof(wide), path);
    return wide[0] &&
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                            output, (int)capacity, NULL, NULL) > 0;
}

static const wchar_t* PrefixKey(wchar_t* buffer, size_t capacity,
                                const wchar_t* prefix,
                                const wchar_t* suffix) {
    _snwprintf_s(buffer, capacity, _TRUNCATE, L"%s%s", prefix, suffix);
    return buffer;
}

static void WriteSnapshot(const wchar_t* path, const wchar_t* section,
                          const wchar_t* prefix,
                          const CatimeIpcSnapshot* snapshot) {
    wchar_t key[64];
    PrefixKey(key, _countof(key), prefix, L"sessionId");
    WriteString(path, section, key, snapshot->sessionId);
    PrefixKey(key, _countof(key), prefix, L"status");
    WritePrivateProfileStringW(section, key,
        CatimeIpc_StatusName(snapshot->status), path);
    PrefixKey(key, _countof(key), prefix, L"phase");
    WritePrivateProfileStringW(section, key,
        CatimeIpc_PhaseName(snapshot->phase), path);
    PrefixKey(key, _countof(key), prefix, L"plannedSeconds");
    WriteNumber(path, section, key, snapshot->plannedSeconds);
    PrefixKey(key, _countof(key), prefix, L"focusedSeconds");
    WriteNumber(path, section, key, snapshot->focusedSeconds);
    PrefixKey(key, _countof(key), prefix, L"remainingSeconds");
    WriteNumber(path, section, key, snapshot->remainingSeconds);
    PrefixKey(key, _countof(key), prefix, L"startedAt");
    WriteNumber(path, section, key, snapshot->startedAt);
    PrefixKey(key, _countof(key), prefix, L"updatedAt");
    WriteNumber(path, section, key, snapshot->updatedAt);
    PrefixKey(key, _countof(key), prefix, L"deadlineAt");
    WriteNumber(path, section, key, snapshot->deadlineAt);
    PrefixKey(key, _countof(key), prefix, L"endedAt");
    WriteNumber(path, section, key, snapshot->endedAt);
    PrefixKey(key, _countof(key), prefix, L"revision");
    WriteNumber(path, section, key, (long long)snapshot->revision);
    PrefixKey(key, _countof(key), prefix, L"cause");
    WritePrivateProfileStringW(section, key,
        CatimeIpc_CauseName(snapshot->cause), path);
}

static BOOL ReadSnapshot(const wchar_t* path, const wchar_t* section,
                         const wchar_t* prefix, CatimeIpcSnapshot* snapshot) {
    char sessionId[CATIME_IPC_MAX_SESSION_ID_BYTES + 1];
    wchar_t key[64];
    wchar_t statusText[24];
    wchar_t phaseText[24];
    wchar_t causeText[24];
    wchar_t key[64];
    PrefixKey(key, _countof(key), prefix, L"sessionId");
    if (!ReadString(path, section, key, sessionId, sizeof(sessionId))) {
        return FALSE;
    }
    PrefixKey(key, _countof(key), prefix, L"status");
    GetPrivateProfileStringW(section, key, L"idle", statusText,
                             _countof(statusText), path);
    CatimeIpcStatus status = ParseStatus(statusText);
    if (status == CATIME_IPC_STATUS_IDLE) return FALSE;
    PrefixKey(key, _countof(key), prefix, L"phase");
    GetPrivateProfileStringW(section, key, L"focus", phaseText,
                             _countof(phaseText), path);
    PrefixKey(key, _countof(key), prefix, L"cause");
    GetPrivateProfileStringW(section, key, L"client", causeText,
                             _countof(causeText), path);
    PrefixKey(key, _countof(key), prefix, L"plannedSeconds");
    int64_t planned = ReadNumber(path, section, key);
    if (planned < 1 || planned > CATIME_IPC_MAX_DURATION_SECONDS) return FALSE;
    PrefixKey(key, _countof(key), prefix, L"focusedSeconds");
    int64_t focused = ReadNumber(path, section, key);
    PrefixKey(key, _countof(key), prefix, L"remainingSeconds");
    int64_t remaining = ReadNumber(path, section, key);
    PrefixKey(key, _countof(key), prefix, L"revision");
    int64_t revision = ReadNumber(path, section, key);
    if (focused < 0 || focused > planned ||
        remaining < 0 || remaining > planned || revision < 1) return FALSE;
    memset(snapshot, 0, sizeof(*snapshot));
    strncpy_s(snapshot->sessionId, sizeof(snapshot->sessionId),
              sessionId, _TRUNCATE);
    snapshot->status = status;
    snapshot->phase = ParsePhase(phaseText);
    snapshot->plannedSeconds = (uint32_t)planned;
    snapshot->focusedSeconds = (uint32_t)focused;
    snapshot->remainingSeconds = (uint32_t)remaining;
    PrefixKey(key, _countof(key), prefix, L"startedAt");
    snapshot->startedAt = ReadNumber(path, section, key);
    PrefixKey(key, _countof(key), prefix, L"updatedAt");
    snapshot->updatedAt = ReadNumber(path, section, key);
    PrefixKey(key, _countof(key), prefix, L"deadlineAt");
    snapshot->deadlineAt = ReadNumber(path, section, key);
    PrefixKey(key, _countof(key), prefix, L"endedAt");
    snapshot->endedAt = ReadNumber(path, section, key);
    snapshot->revision = (uint64_t)revision;
    snapshot->cause = ParseCause(causeText);
    return TRUE;
}

BOOL CatimeIpcPersistence_Load(CatimeIpcState* state,
                               int64_t nowMs,
                               IpcHistory* history) {
    if (!state || !history) return FALSE;
    wchar_t path[MAX_PATH];
    if (!StatePath(path, _countof(path))) return FALSE;
    IpcHistory_Init(history);

    int64_t historyCount = ReadNumber(path, L"History", L"count");
    if (historyCount < 0 || historyCount > CATIME_IPC_MAX_HISTORY) {
        historyCount = 0;
    }
    for (int64_t index = 0; index < historyCount; index++) {
        wchar_t prefix[16];
        CatimeIpcSnapshot snapshot;
        _snwprintf_s(prefix, _countof(prefix), _TRUNCATE, L"%lld_", index);
        if (ReadSnapshot(path, L"History", prefix, &snapshot) &&
            CatimeIpc_IsTerminalStatus(snapshot.status)) {
            IpcHistory_Add(history, &snapshot);
        }
    }

    wchar_t sessionId[CATIME_IPC_MAX_SESSION_ID_BYTES + 1] = L"";
    GetPrivateProfileStringW(L"Integration", L"sessionId", L"",
                             sessionId, _countof(sessionId), path);
    BOOL hasState = sessionId[0] != 0;
    CatimeIpcState_Init(state);
    if (!hasState) return IpcHistory_Count(history) > 0;

    char sessionUtf8[CATIME_IPC_MAX_SESSION_ID_BYTES + 1];
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, sessionId, -1,
            sessionUtf8, sizeof(sessionUtf8), NULL, NULL) <= 0) {
        return IpcHistory_Count(history) > 0;
    }
    CatimeIpcSnapshot snapshot;
    if (!ReadSnapshot(path, L"Integration", L"", &snapshot)) {
        return IpcHistory_Count(history) > 0;
    }
    state->hasSession = true;
    strncpy_s(state->snapshot.sessionId, sizeof(state->snapshot.sessionId),
              sessionUtf8, _TRUNCATE);
    state->snapshot = snapshot;
    state->acknowledgedRevision = ReadNumber(path, L"Integration",
                                             L"acknowledgedRevision");
    state->focusedAtResume = state->snapshot.focusedSeconds;
    int64_t queueCount = ReadNumber(path, L"Plan", L"queuedPhaseCount");
    int64_t queueNext = ReadNumber(path, L"Plan", L"nextQueuedPhase");
    if (queueCount < 0 || queueCount > CATIME_IPC_MAX_QUEUED_PHASES ||
        queueNext < 0 || queueNext > queueCount) {
        queueCount = 0;
        queueNext = 0;
    }
    for (int64_t index = 0; index < queueCount; index++) {
        wchar_t key[48];
        wchar_t queuedId[CATIME_IPC_MAX_SESSION_ID_BYTES + 1];
        _snwprintf_s(key, _countof(key), _TRUNCATE,
                     L"phase%lldSessionId", index);
        GetPrivateProfileStringW(L"Plan", key, L"", queuedId,
                                 _countof(queuedId), path);
        _snwprintf_s(key, _countof(key), _TRUNCATE,
                     L"phase%lldDuration", index);
        int64_t duration = ReadNumber(path, L"Plan", key);
        _snwprintf_s(key, _countof(key), _TRUNCATE,
                     L"phase%lldType", index);
        wchar_t queuedPhase[24];
        GetPrivateProfileStringW(L"Plan", key, L"focus", queuedPhase,
                                 _countof(queuedPhase), path);
        CatimeIpcPlanStep* step = &state->queuedPhases[index];
        if (!queuedId[0] || duration < 1 ||
            duration > CATIME_IPC_MAX_DURATION_SECONDS ||
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, queuedId, -1,
                step->sessionId, sizeof(step->sessionId), NULL, NULL) <= 0) {
            continue;
        }
        step->durationSeconds = (uint32_t)duration;
        step->phase = ParsePhase(queuedPhase);
    }
    state->queuedPhaseCount = (uint32_t)queueCount;
    state->nextQueuedPhase = (uint32_t)queueNext;
    CatimeIpcSnapshot completed;
    if (CatimeIpcState_Tick(state, nowMs, &completed)) {
        IpcHistory_Add(history, &completed);
        CatimeIpcPersistence_Save(state, history);
    }
    return TRUE;
}

BOOL CatimeIpcPersistence_Save(const CatimeIpcState* state,
                               const IpcHistory* history) {
    if (!state) return FALSE;
    wchar_t path[MAX_PATH];
    if (!StatePath(path, _countof(path))) return FALSE;
    if (!state->hasSession && IpcHistory_Count(history) == 0) {
        return DeleteFileW(path);
    }
    if (state->hasSession) {
        WriteSnapshot(path, L"Integration", L"", &state->snapshot);
        WriteNumber(path, L"Integration", L"acknowledgedRevision",
                    (long long)state->acknowledgedRevision);
        WriteNumber(path, L"Plan", L"queuedPhaseCount",
                    state->queuedPhaseCount);
        WriteNumber(path, L"Plan", L"nextQueuedPhase",
                    state->nextQueuedPhase);
        for (uint32_t index = 0; index < state->queuedPhaseCount; index++) {
            const CatimeIpcPlanStep* step = &state->queuedPhases[index];
            wchar_t key[48];
            wchar_t queuedId[CATIME_IPC_MAX_SESSION_ID_BYTES + 1];
            wchar_t queuedPhase[24];
            if (!ToWide(step->sessionId, queuedId, _countof(queuedId)) ||
                !ToWide(CatimeIpc_PhaseName(step->phase), queuedPhase,
                        _countof(queuedPhase))) return FALSE;
            _snwprintf_s(key, _countof(key), _TRUNCATE,
                         L"phase%luSessionId", (unsigned long)index);
            WritePrivateProfileStringW(L"Plan", key, queuedId, path);
            _snwprintf_s(key, _countof(key), _TRUNCATE,
                         L"phase%luDuration", (unsigned long)index);
            WriteNumber(path, L"Plan", key, step->durationSeconds);
            _snwprintf_s(key, _countof(key), _TRUNCATE,
                         L"phase%luType", (unsigned long)index);
            WritePrivateProfileStringW(L"Plan", key, queuedPhase, path);
        }
    }
    uint32_t historyCount = IpcHistory_Count(history);
    WriteNumber(path, L"History", L"count", (long long)historyCount);
    for (uint32_t index = 0; index < historyCount; index++) {
        const CatimeIpcSnapshot* snapshot = NULL;
        wchar_t prefix[16];
        if (!IpcHistory_Get(history, index, &snapshot)) continue;
        _snwprintf_s(prefix, _countof(prefix), _TRUNCATE, L"%lu_",
                     (unsigned long)index);
        WriteSnapshot(path, L"History", prefix, snapshot);
    }
    WritePrivateProfileStringW(NULL, NULL, NULL, path);
    return TRUE;
}
