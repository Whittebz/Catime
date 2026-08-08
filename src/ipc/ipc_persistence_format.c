/**
 * @file ipc_persistence_format.c
 * @brief INI encoding for one session snapshot (state section or history).
 *
 * Added by the Week Planner Calendar fork. Keeps the persistence key format in
 * one place so the single-session state and the offline history stay
 * compatible with existing integration-state.ini files.
 */

#include "ipc/catime_ipc_persistence_format.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static BOOL ToWide(const char* input, wchar_t* output, size_t capacity) {
    return input && output && capacity > 0 &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1,
                            output, (int)capacity) > 0;
}

int64_t IpcPersistence_ReadNumber(const wchar_t* path,
                                  const wchar_t* section,
                                  const wchar_t* key) {
    wchar_t buffer[48];
    GetPrivateProfileStringW(section, key, L"0", buffer,
                             _countof(buffer), path);
    return _wtoi64(buffer);
}

static void WriteNumber(const wchar_t* path, const wchar_t* section,
                        const wchar_t* key, long long value) {
    wchar_t buffer[48];
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%lld", value);
    WritePrivateProfileStringW(section, key, buffer, path);
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

BOOL IpcPersistence_WriteSnapshot(const wchar_t* path,
                                  const wchar_t* section,
                                  const wchar_t* prefix,
                                  const CatimeIpcSnapshot* snapshot) {
    wchar_t key[64];
    if (!snapshot) return FALSE;
    PrefixKey(key, _countof(key), prefix, L"sessionId");
    WriteString(path, section, key, snapshot->sessionId);
    PrefixKey(key, _countof(key), prefix, L"status");
    WriteString(path, section, key, CatimeIpc_StatusName(snapshot->status));
    PrefixKey(key, _countof(key), prefix, L"phase");
    WriteString(path, section, key, CatimeIpc_PhaseName(snapshot->phase));
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
    WriteString(path, section, key, CatimeIpc_CauseName(snapshot->cause));
    return TRUE;
}

BOOL IpcPersistence_ReadSnapshot(const wchar_t* path,
                                 const wchar_t* section,
                                 const wchar_t* prefix,
                                 CatimeIpcSnapshot* snapshot) {
    char sessionId[CATIME_IPC_MAX_SESSION_ID_BYTES + 1];
    wchar_t key[64];
    wchar_t statusText[24];
    wchar_t phaseText[24];
    wchar_t causeText[24];
    if (!snapshot) return FALSE;
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
    int64_t planned = IpcPersistence_ReadNumber(path, section, key);
    if (planned < 1 || planned > CATIME_IPC_MAX_DURATION_SECONDS) return FALSE;
    PrefixKey(key, _countof(key), prefix, L"focusedSeconds");
    int64_t focused = IpcPersistence_ReadNumber(path, section, key);
    PrefixKey(key, _countof(key), prefix, L"remainingSeconds");
    int64_t remaining = IpcPersistence_ReadNumber(path, section, key);
    PrefixKey(key, _countof(key), prefix, L"revision");
    int64_t revision = IpcPersistence_ReadNumber(path, section, key);
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
    snapshot->startedAt = IpcPersistence_ReadNumber(path, section, key);
    PrefixKey(key, _countof(key), prefix, L"updatedAt");
    snapshot->updatedAt = IpcPersistence_ReadNumber(path, section, key);
    PrefixKey(key, _countof(key), prefix, L"deadlineAt");
    snapshot->deadlineAt = IpcPersistence_ReadNumber(path, section, key);
    PrefixKey(key, _countof(key), prefix, L"endedAt");
    snapshot->endedAt = IpcPersistence_ReadNumber(path, section, key);
    snapshot->revision = (uint64_t)revision;
    snapshot->cause = ParseCause(causeText);
    return TRUE;
}
