/**
 * @file ipc_persistence.c
 * @brief Integration-state INI persistence under the current user's app data.
 */

#include "ipc/catime_ipc_persistence.h"

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

static void WriteNumber(const wchar_t* path, const wchar_t* key,
                        long long value) {
    wchar_t buffer[48];
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%lld", value);
    WritePrivateProfileStringW(L"Integration", key, buffer, path);
}

static int64_t ReadNumber(const wchar_t* path, const wchar_t* key) {
    wchar_t buffer[48];
    GetPrivateProfileStringW(L"Integration", key, L"0", buffer,
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

BOOL CatimeIpcPersistence_Load(CatimeIpcState* state,
                               int64_t nowMs,
                               CatimeIpcSnapshot* completedEvent) {
    if (!state) return FALSE;
    wchar_t path[MAX_PATH];
    if (!StatePath(path, _countof(path))) return FALSE;
    wchar_t sessionId[CATIME_IPC_MAX_SESSION_ID_BYTES + 1] = L"";
    GetPrivateProfileStringW(L"Integration", L"sessionId", L"",
                             sessionId, _countof(sessionId), path);
    if (!sessionId[0]) return FALSE;
    char sessionUtf8[CATIME_IPC_MAX_SESSION_ID_BYTES + 1];
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, sessionId, -1,
            sessionUtf8, sizeof(sessionUtf8), NULL, NULL) <= 0) return FALSE;
    wchar_t statusValue[24];
    wchar_t phaseValue[24];
    GetPrivateProfileStringW(L"Integration", L"status", L"idle",
                             statusValue, _countof(statusValue), path);
    GetPrivateProfileStringW(L"Integration", L"phase", L"focus",
                             phaseValue, _countof(phaseValue), path);
    CatimeIpcStatus status = ParseStatus(statusValue);
    int64_t plannedValue = ReadNumber(path, L"plannedSeconds");
    if (status == CATIME_IPC_STATUS_IDLE || plannedValue < 1 ||
        plannedValue > CATIME_IPC_MAX_DURATION_SECONDS) return FALSE;

    CatimeIpcState_Init(state);
    state->hasSession = true;
    strncpy_s(state->snapshot.sessionId, sizeof(state->snapshot.sessionId),
              sessionUtf8, _TRUNCATE);
    state->snapshot.status = status;
    state->snapshot.phase = ParsePhase(phaseValue);
    state->snapshot.plannedSeconds = (uint32_t)plannedValue;
    int64_t focusedValue = ReadNumber(path, L"focusedSeconds");
    int64_t remainingValue = ReadNumber(path, L"remainingSeconds");
    int64_t revisionValue = ReadNumber(path, L"revision");
    int64_t acknowledgedValue = ReadNumber(path, L"acknowledgedRevision");
    if (focusedValue < 0 || focusedValue > plannedValue ||
        remainingValue < 0 || remainingValue > plannedValue ||
        revisionValue < 1 || acknowledgedValue < 0) {
        CatimeIpcState_Init(state);
        return FALSE;
    }
    state->snapshot.focusedSeconds = (uint32_t)focusedValue;
    state->snapshot.remainingSeconds = (uint32_t)remainingValue;
    state->snapshot.startedAt = ReadNumber(path, L"startedAt");
    state->snapshot.updatedAt = ReadNumber(path, L"updatedAt");
    state->snapshot.deadlineAt = ReadNumber(path, L"deadlineAt");
    state->snapshot.endedAt = ReadNumber(path, L"endedAt");
    state->snapshot.revision = (uint64_t)revisionValue;
    state->acknowledgedRevision = (uint64_t)acknowledgedValue;
    state->snapshot.cause = CATIME_IPC_CAUSE_SHUTDOWN;
    state->focusedAtResume = state->snapshot.focusedSeconds;
    if (completedEvent && CatimeIpcState_Tick(state, nowMs, completedEvent)) {
        CatimeIpcPersistence_Save(state);
    }
    return TRUE;
}

BOOL CatimeIpcPersistence_Save(const CatimeIpcState* state) {
    if (!state) return FALSE;
    wchar_t path[MAX_PATH];
    if (!StatePath(path, _countof(path))) return FALSE;
    if (!state->hasSession) return DeleteFileW(path);
    wchar_t sessionId[CATIME_IPC_MAX_SESSION_ID_BYTES + 1];
    wchar_t status[24];
    wchar_t phase[24];
    if (!ToWide(state->snapshot.sessionId, sessionId, _countof(sessionId)) ||
        !ToWide(CatimeIpc_StatusName(state->snapshot.status), status,
                _countof(status)) ||
        !ToWide(CatimeIpc_PhaseName(state->snapshot.phase), phase,
                _countof(phase))) return FALSE;
    WritePrivateProfileStringW(L"Integration", L"sessionId", sessionId, path);
    WritePrivateProfileStringW(L"Integration", L"status", status, path);
    WritePrivateProfileStringW(L"Integration", L"phase", phase, path);
    WriteNumber(path, L"plannedSeconds", state->snapshot.plannedSeconds);
    WriteNumber(path, L"focusedSeconds", state->snapshot.focusedSeconds);
    WriteNumber(path, L"remainingSeconds", state->snapshot.remainingSeconds);
    WriteNumber(path, L"startedAt", state->snapshot.startedAt);
    WriteNumber(path, L"updatedAt", state->snapshot.updatedAt);
    WriteNumber(path, L"deadlineAt", state->snapshot.deadlineAt);
    WriteNumber(path, L"endedAt", state->snapshot.endedAt);
    WriteNumber(path, L"revision", (long long)state->snapshot.revision);
    WriteNumber(path, L"acknowledgedRevision",
                (long long)state->acknowledgedRevision);
    WritePrivateProfileStringW(NULL, NULL, NULL, path);
    return TRUE;
}
