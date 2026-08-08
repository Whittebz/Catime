/**
 * @file ipc_persistence.c
 * @brief Integration-state INI persistence under the current user's app data.
 *
 * Added by the Week Planner Calendar fork. Besides the latest session state it
 * also stores a bounded history of terminal sessions that no client has
 * acknowledged yet, so a plugin that connects later can backfill everything.
 */

#include "ipc/catime_ipc_history.h"
#include "ipc/catime_ipc_persistence.h"
#include "ipc/catime_ipc_persistence_format.h"

#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

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

static CatimeIpcPhase ParsePhase(const wchar_t* value) {
    if (wcscmp(value, L"short_break") == 0) return CATIME_IPC_PHASE_SHORT_BREAK;
    if (wcscmp(value, L"long_break") == 0) return CATIME_IPC_PHASE_LONG_BREAK;
    return CATIME_IPC_PHASE_FOCUS;
}

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

BOOL CatimeIpcPersistence_Load(CatimeIpcState* state,
                               int64_t nowMs,
                               IpcHistory* history) {
    if (!state || !history) return FALSE;
    wchar_t path[MAX_PATH];
    if (!StatePath(path, _countof(path))) return FALSE;
    IpcHistory_Init(history);

    int64_t historyCount = IpcPersistence_ReadNumber(path, L"History",
                                                     L"count");
    if (historyCount < 0 || historyCount > CATIME_IPC_MAX_HISTORY) {
        historyCount = 0;
    }
    for (int64_t index = 0; index < historyCount; index++) {
        wchar_t prefix[16];
        CatimeIpcSnapshot snapshot;
        _snwprintf_s(prefix, _countof(prefix), _TRUNCATE, L"%lld_", index);
        if (IpcPersistence_ReadSnapshot(path, L"History", prefix, &snapshot) &&
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
    if (!IpcPersistence_ReadSnapshot(path, L"Integration", L"", &snapshot)) {
        return IpcHistory_Count(history) > 0;
    }
    state->hasSession = true;
    strncpy_s(state->snapshot.sessionId, sizeof(state->snapshot.sessionId),
              sessionUtf8, _TRUNCATE);
    state->snapshot = snapshot;
    state->acknowledgedRevision = IpcPersistence_ReadNumber(
        path, L"Integration", L"acknowledgedRevision");
    state->focusedAtResume = state->snapshot.focusedSeconds;
    int64_t queueCount = IpcPersistence_ReadNumber(path, L"Plan",
                                                   L"queuedPhaseCount");
    int64_t queueNext = IpcPersistence_ReadNumber(path, L"Plan",
                                                  L"nextQueuedPhase");
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
        int64_t duration = IpcPersistence_ReadNumber(path, L"Plan", key);
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
        IpcPersistence_WriteSnapshot(path, L"Integration", L"",
                                     &state->snapshot);
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
        IpcPersistence_WriteSnapshot(path, L"History", prefix, snapshot);
    }
    WritePrivateProfileStringW(NULL, NULL, NULL, path);
    return TRUE;
}
