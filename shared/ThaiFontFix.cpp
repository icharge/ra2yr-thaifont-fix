#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>

// x86 system-DLL proxies and in-memory Thai bitmap-font shaping hooks.
// The game executable and FNT file are never edited on disk.

static HMODULE g_selfModule;
#if defined(THAI_FONT_PROXY_DDRAW)
static HMODULE g_customDdraw;
static HMODULE g_systemDdraw;
static volatile LONG g_ddrawTargetInit;
static FARPROC g_ddrawForwarders[22];
static char g_customDdrawSetting[MAX_PATH] = "ddraw-custom.dll";
#elif defined(THAI_FONT_PROXY_WINMM)
static HMODULE g_systemWinmm;
static volatile LONG g_winmmInit;
static FARPROC g_winmmForwarders[5];
#elif defined(THAI_FONT_FIX_INTEGRATED)
// The integrated TS-DDraw build has no proxy state.
#endif

struct FontFixConfig {
    BOOL enabled;
    BOOL centerMarks;
    BOOL debugLog;
    int toneMarkRaise;
    int maxExtraSpacing;
};

static FontFixConfig g_config = { TRUE, TRUE, FALSE, 2, 8 };
static HANDLE g_debugLogInitMutex;
static volatile LONG g_runtimeInit;

static CRITICAL_SECTION g_fontLock;
static BOOL g_runtimeReady;
static BOOL g_hooksInstalled;
static void* g_renderTrampoline;
static void* g_layoutTrampoline;
static void* g_textTrampoline;
static volatile LONG g_loggedLayoutHook;
static volatile LONG g_loggedRenderHook;
static volatile LONG g_loggedTextHook;
static volatile LONG g_loggedFontObject;
static volatile LONG g_loggedFontData;
static volatile LONG g_loggedFontDimensions;
static volatile LONG g_loggedFontStorage;
static volatile LONG g_loggedMapUnreadable;
static volatile LONG g_loggedRecordsUnreadable;
static volatile LONG g_loggedRecordsQueryMismatch;
static volatile LONG g_loggedBadGlyphWidth;
static volatile LONG g_loggedBadMarkMap;
static volatile LONG g_loggedNoThaiMarks;
static volatile LONG g_loggedGlyphAlias;
static volatile LONG g_loggedFontAccepted;
static volatile LONG g_loggedFntFallbackFailure;
static volatile LONG g_loggedFntFallbackSuccess;
static volatile LONG g_loggedNoFontState;
static volatile LONG g_loggedMetricsRejected;
static volatile LONG g_loggedMetricsApplied;
static volatile LONG g_loggedNoLayoutContext;
static volatile LONG g_loggedMarkHandled;
static volatile LONG g_loggedSaraAmSplit;
static volatile LONG g_loggedToneAboveSaraAm;

#if defined(THAI_FONT_PROXY_DDRAW)
static const char* const kDdrawExportNames[22] = {
    "AcquireDDThreadLock",
    "CompleteCreateSysmemSurface",
    "D3DParseUnknownCommand",
    "DDGetAttachedSurfaceLcl",
    "DDInternalLock",
    "DDInternalUnlock",
    "DSoundHelp",
    "DirectDrawCreate",
    "DirectDrawCreateClipper",
    "DirectDrawCreateEx",
    "DirectDrawEnumerateA",
    "DirectDrawEnumerateExA",
    "DirectDrawEnumerateExW",
    "DirectDrawEnumerateW",
    "DllCanUnloadNow",
    "DllGetClassObject",
    "GetDDSurfaceLocal",
    "GetOLEThunkData",
    "GetSurfaceFromDC",
    "RegisterSpecialCase",
    "ReleaseDDThreadLock",
    "SetAppCompatData",
};
#elif defined(THAI_FONT_PROXY_WINMM)
static const char* const kWinmmExportNames[5] = {
    "timeBeginPeriod",
    "timeEndPeriod",
    "timeGetTime",
    "timeKillEvent",
    "timeSetEvent",
};
#endif

struct FontState {
    FontState* next;
    BYTE* data;
    WORD* map;
    BYTE* records;
    BYTE* originalWidths;
    BYTE* markedRecords;
    DWORD count;
    DWORD stride;
    BYTE safeSpacing;
    BYTE markCount;
    int saraAmGlyphIndex;
    BYTE saraAmTopMask[16][3];
    BOOL saraAmSplitAvailable;
};

struct LayoutContext {
    LayoutContext* previous;
    void* font;
    FontState* state;
    int spacing;
    int currentY;
    int baseX;
    int baseWidth;
    BOOL hasY;
    BOOL hasBase;
    BOOL hasAboveMark;
    BOOL metricsApplied;
    BOOL hasPendingTone;
    void* pendingToneFont;
    FontState* pendingToneState;
    int pendingToneGlyphIndex;
    unsigned short pendingToneCode;
    int pendingToneX;
    int pendingToneY;
    int pendingToneColor;
};

static FontState* g_fontStates;
// Access is serialized by g_fontLock; recursive text draws use the context chain.
static LayoutContext* g_currentContext;

static BOOL InstallFontHooks(void);
static BOOL IsOtherThaiFontFixActive(void);

typedef int (__thiscall* RenderFunction)(void*, unsigned short, int, int, int);
typedef int (__stdcall* LayoutFunction)(void*, void*, short*, int, int, int, int, int, int, int);
typedef int (__thiscall* TextFunction)(void*, short*, int, int, int, int);

static BOOL IsReadableRange(const void* pointer, SIZE_T size) {
    uintptr_t cursor = (uintptr_t)pointer;
    uintptr_t end = cursor + size;
    if (pointer == NULL || end < cursor) return FALSE;

    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery((const void*)cursor, &info, sizeof(info)) != sizeof(info)) return FALSE;
        if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return FALSE;
        DWORD protection = info.Protect & 0xff;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY) return FALSE;
        uintptr_t regionEnd = (uintptr_t)info.BaseAddress + info.RegionSize;
        if (regionEnd <= cursor) return FALSE;
        cursor = regionEnd < end ? regionEnd : end;
    }
    return TRUE;
}

static BOOL IsWritableRange(const void* pointer, SIZE_T size) {
    uintptr_t cursor = (uintptr_t)pointer;
    uintptr_t end = cursor + size;
    if (pointer == NULL || end < cursor) return FALSE;

    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery((const void*)cursor, &info, sizeof(info)) != sizeof(info)) return FALSE;
        if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return FALSE;
        DWORD protection = info.Protect & 0xff;
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return FALSE;
        uintptr_t regionEnd = (uintptr_t)info.BaseAddress + info.RegionSize;
        if (regionEnd <= cursor) return FALSE;
        cursor = regionEnd < end ? regionEnd : end;
    }
    return TRUE;
}

static BOOL CopyMemorySafely(void* destination, const void* source, SIZE_T size) {
    __try {
        CopyMemory(destination, source, size);
        return TRUE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

static BOOL AppendText(char* destination, SIZE_T capacity, SIZE_T* length, const char* text) {
    for (SIZE_T i = 0; text[i] != 0; ++i) {
        if (*length + 1 >= capacity) return FALSE;
        destination[(*length)++] = text[i];
    }
    destination[*length] = 0;
    return TRUE;
}

static BOOL GetProxyDirectory(char* path, SIZE_T capacity) {
    if (path == NULL || capacity == 0 || g_selfModule == NULL) return FALSE;
    DWORD length = GetModuleFileNameA(g_selfModule, path, (DWORD)capacity);
    if (length == 0 || length >= capacity) return FALSE;
    while (length != 0 && path[length - 1] != '\\' && path[length - 1] != '/') --length;
    if (length == 0 || length >= capacity) return FALSE;
    path[length] = 0;
    return TRUE;
}

static void LogPath(const char* label, const char* value);
static void WriteDebugDiagnostic(const char* message);
static BOOL GetProxyConfigurationPath(char* path, SIZE_T capacity);

static BOOL GetDebugLogPath(char* path, SIZE_T capacity) {
    if (!GetProxyConfigurationPath(path, capacity)) return FALSE;
    SIZE_T length = (SIZE_T)lstrlenA(path);
    if (length < 4) return FALSE;
    path[length - 3] = 'l';
    path[length - 2] = 'o';
    path[length - 1] = 'g';
    return TRUE;
}

static void ClearDebugLogOnce(void) {
    if (g_debugLogInitMutex != NULL) return;

    char path[MAX_PATH];
    if (!GetDebugLogPath(path, MAX_PATH)) return;

    DWORD pathHash = 2166136261u;
    for (const unsigned char* cursor = (const unsigned char*)path; *cursor != 0; ++cursor) {
        unsigned char value = *cursor;
        if (value >= 'A' && value <= 'Z') value = (unsigned char)(value + ('a' - 'A'));
        pathHash = (pathHash ^ value) * 16777619u;
    }

    char mutexName[96];
    wsprintfA(mutexName, "Local\\ThaiFontFixLogInit-%lu-%08lX",
        (unsigned long)GetCurrentProcessId(), (unsigned long)pathHash);
    HANDLE mutex = CreateMutexA(NULL, FALSE, mutexName);
    if (mutex != NULL) {
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(mutex);
            return;
        }
        // Keep the named object alive so other integrations in this process
        // do not clear the same log again during their own initialization.
        g_debugLogInitMutex = mutex;
    }

    HANDLE logFile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (logFile != INVALID_HANDLE_VALUE) CloseHandle(logFile);
}

static BOOL TryLoadMatchingGameFntRecords(WORD* runtimeMap, DWORD height, DWORD stride,
    DWORD count, BYTE* destination, SIZE_T recordsSize) {
    char path[MAX_PATH];
    if (!GetProxyDirectory(path, MAX_PATH)) return FALSE;

    SIZE_T pathLength = (SIZE_T)lstrlenA(path);
    if (!AppendText(path, MAX_PATH, &pathLength, "game.fnt")) return FALSE;

    HANDLE file = CreateFileA(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        LogPath("ThaiFontFix: could not open game.fnt fallback: ", path);
        return FALSE;
    }

    LARGE_INTEGER fileSize;
    BYTE header[28];
    WORD diskMap[0x100];
    LARGE_INTEGER offset;
    DWORD bytesRead = 0;
    DWORD diskHeight = 0;
    DWORD diskCount = 0;
    DWORD diskStride = 0;
    DWORD copiedCount = 0;
    SIZE_T copiedBytes = 0;
    unsigned mismatchedCode = 0x100;
    const char* failureReason = "file size/header read failed";
    BOOL valid = GetFileSizeEx(file, &fileSize) &&
        fileSize.QuadPart >= (LONGLONG)(sizeof(header) + 0x20000);

    offset.QuadPart = 0;
    if (valid) {
        valid = SetFilePointerEx(file, offset, NULL, FILE_BEGIN) &&
            ReadFile(file, header, sizeof(header), &bytesRead, NULL) && bytesRead == sizeof(header);
    }
    if (valid) {
        diskHeight = *(DWORD*)(header + 12);
        diskCount = *(DWORD*)(header + 20);
        diskStride = *(DWORD*)(header + 24);
        valid = header[0] == 'f' && header[1] == 'o' && header[2] == 'n' && header[3] == 'T' &&
            diskHeight == height && diskStride == stride && diskCount != 0 && diskCount <= 0x10000;
        if (!valid) failureReason = "fonT dimensions or record count differ";
    }
    if (valid) {
        copiedCount = diskCount < count ? diskCount : count;
        copiedBytes = (SIZE_T)copiedCount * stride;
        valid = fileSize.QuadPart >= (LONGLONG)(sizeof(header) + 0x20000 + copiedBytes);
        if (!valid) failureReason = "glyph records are truncated";
    }

    offset.QuadPart = sizeof(header);
    if (valid) {
        valid = SetFilePointerEx(file, offset, NULL, FILE_BEGIN) &&
            ReadFile(file, diskMap, sizeof(diskMap), &bytesRead, NULL) && bytesRead == sizeof(diskMap);
        if (!valid) failureReason = "CP874 glyph map read failed";
    }
    if (valid) {
        for (unsigned code = 0; code <= 0xff; ++code) {
            if (diskMap[code] != runtimeMap[code]) {
                valid = FALSE;
                mismatchedCode = code;
                failureReason = "CP874 glyph map differs";
                break;
            }
            if (runtimeMap[code] > copiedCount) {
                valid = FALSE;
                mismatchedCode = code;
                failureReason = "CP874 glyph index is outside copied records";
                break;
            }
        }
    }

    if (valid) {
        ZeroMemory(destination, recordsSize);
        offset.QuadPart = sizeof(header) + 0x20000;
        valid = SetFilePointerEx(file, offset, NULL, FILE_BEGIN) &&
            ReadFile(file, destination, (DWORD)copiedBytes, &bytesRead, NULL) &&
            bytesRead == (DWORD)copiedBytes;
        if (!valid) failureReason = "glyph record read failed";
    }

    CloseHandle(file);
    if (!valid) {
        if (InterlockedCompareExchange(&g_loggedFntFallbackFailure, 1, 0) == 0 && g_config.debugLog) {
            char message[MAX_PATH + 240];
            wsprintfA(message,
                "ThaiFontFix: game.fnt fallback rejected (%s; runtime height=%lu stride=%lu records=%lu, file height=%lu stride=%lu records=%lu, CP874=0x%02X runtime=%u file=%u): %s",
                failureReason, (unsigned long)height, (unsigned long)stride, (unsigned long)count,
                (unsigned long)diskHeight, (unsigned long)diskStride, (unsigned long)diskCount,
                mismatchedCode,
                mismatchedCode < 0x100 ? (unsigned)runtimeMap[mismatchedCode] : 0,
                mismatchedCode < 0x100 ? (unsigned)diskMap[mismatchedCode] : 0,
                path);
            WriteDebugDiagnostic(message);
        }
        return FALSE;
    }

    if (InterlockedCompareExchange(&g_loggedFntFallbackSuccess, 1, 0) == 0 && g_config.debugLog) {
        char message[MAX_PATH + 192];
        wsprintfA(message,
            "ThaiFontFix: loaded matching game.fnt records file=%lu runtime=%lu copied=%lu zero-filled=%lu: %s",
            (unsigned long)diskCount, (unsigned long)count, (unsigned long)copiedCount,
            (unsigned long)(count - copiedCount), path);
        WriteDebugDiagnostic(message);
    }
    return TRUE;
}

static BOOL GetProxyConfigurationPath(char* path, SIZE_T capacity) {
    if (path == NULL || capacity == 0) return FALSE;
    path[0] = 0;
    if (!GetProxyDirectory(path, capacity)) return FALSE;
    SIZE_T length = (SIZE_T)lstrlenA(path);
    return AppendText(path, capacity, &length, "thaifontfix.ini");
}

static void WriteDebugDiagnostic(const char* message) {
    if (!g_config.debugLog || message == NULL) return;

    char path[MAX_PATH];
    if (!GetDebugLogPath(path, MAX_PATH)) return;

    HANDLE logFile = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (logFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(logFile, message, (DWORD)lstrlenA(message), &written, NULL);
        WriteFile(logFile, "\r\n", 2, &written, NULL);
        CloseHandle(logFile);
    }
    OutputDebugStringA(message);
}

static void LogOnce(volatile LONG* logged, const char* message) {
    if (InterlockedCompareExchange(logged, 1, 0) == 0) WriteDebugDiagnostic(message);
}

static void LogFontDimensions(const char* prefix, DWORD height, DWORD stride, DWORD count) {
    if (!g_config.debugLog) return;
    char message[160];
    wsprintfA(message, "ThaiFontFix: %s height=%lu stride=%lu glyph-count=%lu",
        prefix, (unsigned long)height, (unsigned long)stride, (unsigned long)count);
    WriteDebugDiagnostic(message);
}

static void LogUnreadableRange(const char* label, const void* pointer, SIZE_T size) {
    if (!g_config.debugLog) return;
    uintptr_t cursor = (uintptr_t)pointer;
    uintptr_t end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery((const void*)cursor, &info, sizeof(info)) != sizeof(info)) {
            char message[160];
            wsprintfA(message, "ThaiFontFix: %s VirtualQuery failed at=%p bytes=%lu",
                label, (void*)cursor, (unsigned long)size);
            WriteDebugDiagnostic(message);
            return;
        }
        DWORD protection = info.Protect & 0xff;
        BOOL readable = info.State == MEM_COMMIT &&
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
            (protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
             protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
             protection == PAGE_EXECUTE_WRITECOPY);
        uintptr_t regionEnd = (uintptr_t)info.BaseAddress + info.RegionSize;
        if (!readable || regionEnd <= cursor) {
            char message[192];
            wsprintfA(message, "ThaiFontFix: %s unreadable ptr=%p bytes=%lu first-bad=%p state=0x%08lX protect=0x%08lX",
                label, pointer, (unsigned long)size, (void*)cursor,
                (unsigned long)info.State, (unsigned long)info.Protect);
            WriteDebugDiagnostic(message);
            return;
        }
        cursor = regionEnd < end ? regionEnd : end;
    }
}

static void LogMainImagePath(void) {
    if (!g_config.debugLog) return;
    char imagePath[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, imagePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return;
    char message[MAX_PATH + 32];
    SIZE_T messageLength = 0;
    message[0] = 0;
    if (!AppendText(message, sizeof(message), &messageLength, "ThaiFontFix: main executable: ") ||
        !AppendText(message, sizeof(message), &messageLength, imagePath)) return;
    WriteDebugDiagnostic(message);
}

static void LogPath(const char* label, const char* value) {
    if (!g_config.debugLog || label == NULL || value == NULL) return;
    char message[MAX_PATH + 64];
    SIZE_T length = 0;
    message[0] = 0;
    if (!AppendText(message, sizeof(message), &length, label) ||
        !AppendText(message, sizeof(message), &length, value)) return;
    WriteDebugDiagnostic(message);
}

static void EnsureConfigurationValue(const char* section, const char* key,
    const char* defaultValue, const char* path) {
    char existingValue[64];
    existingValue[0] = 0;
    if (GetPrivateProfileStringA(section, key, NULL, existingValue,
        sizeof(existingValue), path) == 0) {
        WritePrivateProfileStringA(section, key, defaultValue, path);
    }
}

static void LoadConfiguration(void) {
    char path[MAX_PATH];
    path[0] = 0;
    if (!GetProxyConfigurationPath(path, MAX_PATH)) {
        WriteDebugDiagnostic("ThaiFontFix: unable to construct the proxy-specific configuration path");
        return;
    }

    EnsureConfigurationValue("FontFix", "Enabled", "1", path);
    EnsureConfigurationValue("FontFix", "CenterMarks", "1", path);
    EnsureConfigurationValue("FontFix", "ToneMarkRaise", "2", path);
    EnsureConfigurationValue("FontFix", "MaxExtraSpacing", "8", path);
    EnsureConfigurationValue("FontFix", "DebugLog", "0", path);
#if defined(THAI_FONT_PROXY_DDRAW)
    EnsureConfigurationValue("Proxy", "CustomDll", "ddraw-custom.dll", path);
#endif

    g_config.enabled = GetPrivateProfileIntA("FontFix", "Enabled", 1, path) != 0;
    g_config.centerMarks = GetPrivateProfileIntA("FontFix", "CenterMarks", 1, path) != 0;
    g_config.debugLog = GetPrivateProfileIntA("FontFix", "DebugLog", 0, path) != 0;
    g_config.toneMarkRaise = (int)GetPrivateProfileIntA("FontFix", "ToneMarkRaise", 2, path);
    g_config.maxExtraSpacing = (int)GetPrivateProfileIntA("FontFix", "MaxExtraSpacing", 8, path);
    if (g_config.toneMarkRaise < 0) g_config.toneMarkRaise = 0;
    if (g_config.toneMarkRaise > 16) g_config.toneMarkRaise = 16;
    if (g_config.maxExtraSpacing < 0) g_config.maxExtraSpacing = 0;
    if (g_config.maxExtraSpacing > 8) g_config.maxExtraSpacing = 8;

#if defined(THAI_FONT_PROXY_DDRAW)
    GetPrivateProfileStringA("Proxy", "CustomDll", "ddraw-custom.dll",
        g_customDdrawSetting, MAX_PATH, path);
#endif

    if (g_config.debugLog) {
        ClearDebugLogOnce();
        char proxyPath[MAX_PATH];
        DWORD proxyLength = GetModuleFileNameA(g_selfModule, proxyPath, MAX_PATH);
        if (proxyLength != 0 && proxyLength < MAX_PATH) LogPath("ThaiFontFix: proxy module: ", proxyPath);
        LogPath("ThaiFontFix: configuration file: ", path);
        WriteDebugDiagnostic(g_config.enabled
            ? "ThaiFontFix: configuration loaded; font fix enabled"
            : "ThaiFontFix: configuration loaded; font fix disabled");
    }
}

static void EnsureRuntimeInitialized(void) {
    LONG state = InterlockedCompareExchange(&g_runtimeInit, 1, 0);
    if (state == 0) {
        LoadConfiguration();
        if (!g_config.enabled) {
            WriteDebugDiagnostic("ThaiFontFix: font hooks disabled by configuration");
            g_runtimeReady = TRUE;
        } else if (IsOtherThaiFontFixActive()) {
            g_hooksInstalled = TRUE;
            g_runtimeReady = TRUE;
            WriteDebugDiagnostic("ThaiFontFix: another loader already installed the font hooks; duplicate installation skipped");
        } else if (InitializeCriticalSectionAndSpinCount(&g_fontLock, 2000)) {
            if (g_config.enabled) {
                g_hooksInstalled = InstallFontHooks();
                WriteDebugDiagnostic(g_hooksInstalled
                    ? "ThaiFontFix: font hooks installed"
                    : "ThaiFontFix: font hooks were not installed");
            }
            g_runtimeReady = TRUE;
        } else {
            WriteDebugDiagnostic("ThaiFontFix: failed to initialize the font lock");
        }
        InterlockedExchange(&g_runtimeInit, 2);
        return;
    }
    while (InterlockedCompareExchange(&g_runtimeInit, 2, 2) != 2) Sleep(0);
}

static BOOL IsOtherThaiFontFixActive(void) {
    static const char* const moduleNames[] = {
        "ddraw.dll",
        "binkw32.dll",
        "ThaiFontFix-Syringe-RA2.dll",
        "ThaiFontFix-Syringe-YR.dll",
    };
    typedef BOOL (WINAPI* IsActiveFunction)(void);

    for (unsigned i = 0; i < sizeof(moduleNames) / sizeof(moduleNames[0]); ++i) {
        HMODULE module = GetModuleHandleA(moduleNames[i]);
        if (module == NULL || module == g_selfModule) continue;
        IsActiveFunction isActive = (IsActiveFunction)GetProcAddress(module, "ThaiFontFixIsActive");
        if (isActive != NULL && isActive()) return TRUE;
    }
    return FALSE;
}

extern "C" BOOL WINAPI ThaiFontFixIsActive(void) {
    return g_hooksInstalled;
}

#if defined(THAI_FONT_FIX_INTEGRATED)
extern "C" void __cdecl ThaiFontFixInitialize(void) {
    if (g_selfModule == NULL) {
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&ThaiFontFixInitialize),
                           &g_selfModule);
    }
    EnsureRuntimeInitialized();
}
#endif

static BOOL IsThaiMarkCode(unsigned short code) {
    return code == 0x00d1 || (code >= 0x00d4 && code <= 0x00da) ||
           (code >= 0x00e7 && code <= 0x00ee);
}

static BOOL IsThaiUpperMark(unsigned short code) {
    return code == 0x00d1 || (code >= 0x00d4 && code <= 0x00d7) ||
           (code >= 0x00e7 && code <= 0x00ee);
}

static BOOL IsThaiToneMark(unsigned short code) {
    return code >= 0x00e8 && code <= 0x00eb;
}

static BOOL IsGlyphPixelSet(const BYTE* record, unsigned x, unsigned y) {
    const BYTE* row = record + 1 + y * 3;
    return (row[x >> 3] & (BYTE)(0x80u >> (x & 7))) != 0;
}

static BOOL BuildSaraAmTopMask(FontState* state) {
    state->saraAmGlyphIndex = -1;
    if (state->map == NULL) return FALSE;
    WORD entry = state->map[0x00d3];
    if (entry == 0 || entry > state->count) return FALSE;

    unsigned index = entry - 1;
    BYTE* record = state->records + (SIZE_T)index * state->stride;
    unsigned width = record[0];
    if (width == 0 || width > 16) return FALSE;
    state->saraAmGlyphIndex = (int)index;

    // Find the lower Sara Aa's right-side vertical stroke. The row just above
    // it is the cap of the lower vowel; rows before that form the nikhahit.
    unsigned rightRunStart = 16;
    unsigned rightRunLength = 0;
    unsigned runStart = 0;
    unsigned runLength = 0;
    for (unsigned y = 0; y <= 16; ++y) {
        BOOL ink = y < 16 && IsGlyphPixelSet(record, width - 1, y);
        if (ink) {
            if (runLength == 0) runStart = y;
            ++runLength;
        } else {
            if (runLength > rightRunLength) {
                rightRunStart = runStart;
                rightRunLength = runLength;
            }
            runLength = 0;
        }
    }
    if (rightRunLength < 3 || rightRunStart < 2 || rightRunStart >= 16) return FALSE;
    unsigned splitRow = rightRunStart - 1;
    if (splitRow == 0 || splitRow >= 16) return FALSE;

    unsigned topCount = 0;
    unsigned otherCount = 0;
    unsigned topMaxX = 0;
    for (unsigned y = 0; y < 16; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            if (!IsGlyphPixelSet(record, x, y)) continue;
            if (y < splitRow) {
                ++topCount;
                if (x > topMaxX) topMaxX = x;
                state->saraAmTopMask[y][x >> 3] |= (BYTE)(0x80u >> (x & 7));
            } else {
                ++otherCount;
            }
        }
    }

    // Split only when the compact upper dot is left of the lower vowel body.
    // Otherwise leave custom artwork untouched.
    if (topCount < 2 || otherCount < 3 || topCount >= otherCount || topMaxX >= width - 1) {
        ZeroMemory(state->saraAmTopMask, sizeof(state->saraAmTopMask));
        return FALSE;
    }
    state->saraAmSplitAvailable = TRUE;
    return TRUE;
}

static int FindGlyphIndex(const FontState* state, unsigned short code) {
    // The game widens CP874 bytes to 16-bit values before drawing them.
    if (state == NULL || code > 0xff) return -1;
    WORD entry = state->map[code];
    if (entry == 0 || entry > state->count) return -1;
    return (int)entry - 1;
}

static FontState* FindFontState(BYTE* data) {
    for (FontState* state = g_fontStates; state != NULL; state = state->next) {
        if (state->data == data) return state;
    }
    return NULL;
}

static FontState* CreateFontState(void* font) {
    BYTE* fontBytes = (BYTE*)font;
    if (!IsReadableRange(fontBytes, 0x30) ||
        !IsWritableRange(fontBytes + 4, sizeof(BYTE*)) ||
        !IsWritableRange(fontBytes + 0x2c, sizeof(int))) {
        LogOnce(&g_loggedFontObject, "ThaiFontFix: font state rejected; font object is not readable/writable");
        return NULL;
    }

    BYTE* data = *(BYTE**)(fontBytes + 4);
    if (!IsReadableRange(data, 0x24)) {
        LogOnce(&g_loggedFontData, "ThaiFontFix: font state rejected; parsed font header is not readable");
        return NULL;
    }
    DWORD height = *(DWORD*)(data + 8);
    DWORD stride = *(DWORD*)(data + 0x14);
    WORD* map = *(WORD**)(data + 0x18);
    BYTE* sourceRecords = *(BYTE**)(data + 0x1c);
    DWORD count = *(DWORD*)(data + 0x20);

    if (height != 16 || stride < 49 || stride > 128 || count == 0 || count > 0x10000 ||
        map == NULL || sourceRecords == NULL) {
        LogOnce(&g_loggedFontDimensions, "ThaiFontFix: font state rejected; unexpected bitmap-font dimensions or pointers");
        LogFontDimensions("rejected font", height, stride, count);
        return NULL;
    }
    SIZE_T recordsSize = (SIZE_T)stride * count;
    BOOL mapReadable = IsReadableRange(map, 0x100 * sizeof(WORD));
    if (!mapReadable) {
        LogOnce(&g_loggedMapUnreadable, "ThaiFontFix: font state rejected; CP874 glyph map is not readable");
    }
    if (!mapReadable) return NULL;

    SIZE_T allocationSize = sizeof(FontState) + (SIZE_T)count * 2;
    FontState* state = (FontState*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, allocationSize);
    BYTE* clonedData = (BYTE*)HeapAlloc(GetProcessHeap(), 0, 0x24);
    BYTE* records = (BYTE*)HeapAlloc(GetProcessHeap(), 0, recordsSize);
    if (state == NULL || clonedData == NULL || records == NULL) {
        if (state != NULL) HeapFree(GetProcessHeap(), 0, state);
        if (clonedData != NULL) HeapFree(GetProcessHeap(), 0, clonedData);
        if (records != NULL) HeapFree(GetProcessHeap(), 0, records);
        LogOnce(&g_loggedFontStorage, "ThaiFontFix: font state rejected; unable to allocate private font data");
        return NULL;
    }
    CopyMemory(clonedData, data, 0x24);
    BOOL recordsRangeReadable = IsReadableRange(sourceRecords, recordsSize);
    if (!CopyMemorySafely(records, sourceRecords, recordsSize)) {
        if (InterlockedCompareExchange(&g_loggedRecordsUnreadable, 1, 0) == 0) {
            if (!recordsRangeReadable) LogUnreadableRange("bitmap records", sourceRecords, recordsSize);
            else WriteDebugDiagnostic("ThaiFontFix: bitmap records copy faulted although VirtualQuery marked the range readable");
        }
        if (!TryLoadMatchingGameFntRecords(map, height, stride, count, records, recordsSize)) {
            HeapFree(GetProcessHeap(), 0, state);
            HeapFree(GetProcessHeap(), 0, clonedData);
            HeapFree(GetProcessHeap(), 0, records);
            return NULL;
        }
    }
    if (!recordsRangeReadable) {
        LogOnce(&g_loggedRecordsQueryMismatch, "ThaiFontFix: bitmap records copied safely despite VirtualQuery protection classification");
    }
    *(BYTE**)(clonedData + 0x1c) = records;

    state->data = clonedData;
    state->map = map;
    state->records = records;
    state->count = count;
    state->stride = stride;
    state->originalWidths = (BYTE*)(state + 1);
    state->markedRecords = state->originalWidths + count;
    state->safeSpacing = 8;

    for (DWORD i = 0; i < count; ++i) {
        BYTE* record = records + (SIZE_T)i * stride;
        BYTE width = record[0];
        if (width > 16 || 1 + 16 * 3 > stride) {
            LogOnce(&g_loggedBadGlyphWidth, "ThaiFontFix: font state rejected; glyph record width/stride is invalid");
            HeapFree(GetProcessHeap(), 0, state);
            HeapFree(GetProcessHeap(), 0, clonedData);
            HeapFree(GetProcessHeap(), 0, records);
            return NULL;
        }
        state->originalWidths[i] = width;

        for (unsigned row = 0; row < 16; ++row) {
            const BYTE* bits = record + 1 + row * 3;
            for (unsigned x = width; x < (unsigned)width + 8u; ++x) {
                BYTE mask = (BYTE)(0x80u >> (x & 7));
                if ((bits[x >> 3] & mask) != 0) {
                    unsigned paddingColumns = x - width;
                    if (paddingColumns < state->safeSpacing) state->safeSpacing = (BYTE)paddingColumns;
                }
            }
        }
    }

    BuildSaraAmTopMask(state);

    for (unsigned code = 0; code <= 0xff; ++code) {
        if (!IsThaiMarkCode((unsigned short)code)) continue;
        WORD entry = map[code];
        if (entry == 0) continue;
        if (entry > count) {
            LogOnce(&g_loggedBadMarkMap, "ThaiFontFix: font state rejected; Thai mark map entry exceeds glyph count");
            HeapFree(GetProcessHeap(), 0, state);
            HeapFree(GetProcessHeap(), 0, clonedData);
            HeapFree(GetProcessHeap(), 0, records);
            return NULL;
        }
        BYTE* flag = state->markedRecords + (entry - 1);
        if (*flag == 0) {
            *flag = 1;
            ++state->markCount;
        }
    }
    if (state->markCount == 0) {
        LogOnce(&g_loggedNoThaiMarks, "ThaiFontFix: font state rejected; no glyphs found in the configured Thai mark slots");
        HeapFree(GetProcessHeap(), 0, state);
        HeapFree(GetProcessHeap(), 0, clonedData);
        HeapFree(GetProcessHeap(), 0, records);
        return NULL;
    }

    // A zero-advance mark record must not also be used by another CP874 byte.
    for (unsigned code = 0; code <= 0xff; ++code) {
        if (IsThaiMarkCode((unsigned short)code)) continue;
        WORD entry = map[code];
        if (entry != 0 && entry <= count && state->markedRecords[entry - 1] != 0) {
            LogOnce(&g_loggedGlyphAlias, "ThaiFontFix: font state rejected; a Thai mark glyph is shared with a spacing character");
            HeapFree(GetProcessHeap(), 0, state);
            HeapFree(GetProcessHeap(), 0, clonedData);
            HeapFree(GetProcessHeap(), 0, records);
            return NULL;
        }
    }

    *(BYTE**)(fontBytes + 4) = clonedData;
    state->next = g_fontStates;
    g_fontStates = state;
    if (InterlockedCompareExchange(&g_loggedFontAccepted, 1, 0) == 0 && g_config.debugLog) {
        char message[192];
        wsprintfA(message, "ThaiFontFix: FNT accepted with private glyph copy height=%lu stride=%lu glyph-count=%lu Thai-mark-count=%u safe-spacing=%u",
            (unsigned long)height, (unsigned long)stride, (unsigned long)count,
            (unsigned)state->markCount, (unsigned)state->safeSpacing);
        WriteDebugDiagnostic(message);
    }
    return state;
}

static FontState* GetFontState(void* font) {
    if (!IsReadableRange(font, 8)) return NULL;
    BYTE* data = *(BYTE**)((BYTE*)font + 4);
    FontState* state = FindFontState(data);
    return state != NULL ? state : CreateFontState(font);
}

static LayoutContext* FindContextForData(LayoutContext* context, BYTE* data) {
    for (; context != NULL; context = context->previous) {
        if (context->state != NULL && context->state->data == data && context->metricsApplied) return context;
    }
    return NULL;
}

static LayoutContext* FindContextForFont(LayoutContext* context, void* font) {
    for (; context != NULL; context = context->previous) {
        if (context->font == font && context->metricsApplied) return context;
    }
    return NULL;
}

static BOOL CanApplyMetrics(const LayoutContext* context) {
    return g_config.enabled && context->state != NULL && context->spacing >= 0 &&
           context->spacing <= g_config.maxExtraSpacing &&
           context->spacing <= context->state->safeSpacing;
}

static void ApplyMetrics(LayoutContext* context) {
    FontState* state = context->state;
    for (DWORD i = 0; i < state->count; ++i) {
        BYTE width = state->originalWidths[i];
        state->records[(SIZE_T)i * state->stride] = state->markedRecords[i] ? 0 : (BYTE)(width + context->spacing);
    }
    *(int*)((BYTE*)context->font + 0x2c) = 0;
    context->metricsApplied = TRUE;
}

static void RestoreMetrics(LayoutContext* context) {
    if (context == NULL || !context->metricsApplied) return;
    FontState* state = context->state;
    for (DWORD i = 0; i < state->count; ++i) {
        state->records[(SIZE_T)i * state->stride] = state->originalWidths[i];
    }
    *(int*)((BYTE*)context->font + 0x2c) = context->spacing;
    context->metricsApplied = FALSE;
}

static void FlushPendingTone(RenderFunction original, LayoutContext* context, BOOL aboveSaraAm) {
    if (context == NULL || !context->hasPendingTone) return;

    FontState* state = context->pendingToneState;
    int index = context->pendingToneGlyphIndex;
    if (state != NULL && index >= 0 && (DWORD)index < state->count) {
        BYTE* record = state->records + (SIZE_T)index * state->stride;
        BYTE patchedWidth = record[0];
        record[0] = state->originalWidths[index];
        int drawY = context->pendingToneY;
        if (aboveSaraAm) drawY -= g_config.toneMarkRaise;
        original(context->pendingToneFont, context->pendingToneCode,
            context->pendingToneX, drawY, context->pendingToneColor);
        record[0] = patchedWidth;
        if (aboveSaraAm && InterlockedCompareExchange(&g_loggedToneAboveSaraAm, 1, 0) == 0 && g_config.debugLog) {
            char message[128];
            wsprintfA(message, "ThaiFontFix: tone mark stacked above Sara Am code=0x%02X y=%ld",
                (unsigned)context->pendingToneCode, (long)drawY);
            WriteDebugDiagnostic(message);
        }
    }
    context->hasPendingTone = FALSE;
    context->pendingToneFont = NULL;
    context->pendingToneState = NULL;
    context->pendingToneGlyphIndex = -1;
}

static int DrawSaraAm(RenderFunction original, void* font, LayoutContext* context,
    unsigned short code, int x, int y, int color) {
    FontState* state = context->state;
    if (state == NULL || !state->saraAmSplitAvailable || state->stride > 128) {
        return original(font, code, x, y, color);
    }

    BYTE* record = state->records + (SIZE_T)state->saraAmGlyphIndex * state->stride;
    BYTE savedRecord[128];
    CopyMemory(savedRecord, record, state->stride);

    int topMinX = 24;
    int topMaxX = -1;
    for (unsigned row = 0; row < 16; ++row) {
        for (unsigned byteIndex = 0; byteIndex < 3; ++byteIndex) {
            BYTE bits = state->saraAmTopMask[row][byteIndex];
            for (unsigned bit = 0; bit < 8; ++bit) {
                if ((bits & (BYTE)(0x80u >> bit)) == 0) continue;
                int pixelX = (int)(byteIndex * 8 + bit);
                if (pixelX < topMinX) topMinX = pixelX;
                if (pixelX > topMaxX) topMaxX = pixelX;
            }
        }
    }
    if (topMaxX < topMinX) return original(font, code, x, y, color);

    if (context->hasY && context->currentY != y) {
        context->hasBase = FALSE;
        context->hasAboveMark = FALSE;
    }
    context->currentY = y;
    context->hasY = TRUE;

    int markX = x;
    if (g_config.centerMarks && context->hasBase) {
        int markWidth = topMaxX - topMinX + 1;
        markX = context->baseX + (context->baseWidth - markWidth) / 2 - topMinX;
    }
    int markY = y;
    if (context->hasAboveMark && !context->hasPendingTone) markY -= g_config.toneMarkRaise;

    for (unsigned row = 0; row < 16; ++row) {
        for (unsigned byteIndex = 0; byteIndex < 3; ++byteIndex) {
            record[1 + row * 3 + byteIndex] &= state->saraAmTopMask[row][byteIndex];
        }
    }
    original(font, code, markX, markY, color);

    CopyMemory(record, savedRecord, state->stride);
    for (unsigned row = 0; row < 16; ++row) {
        for (unsigned byteIndex = 0; byteIndex < 3; ++byteIndex) {
            record[1 + row * 3 + byteIndex] &= (BYTE)~state->saraAmTopMask[row][byteIndex];
        }
    }
    int result = original(font, code, x, y, color);
    CopyMemory(record, savedRecord, state->stride);

    context->hasAboveMark = TRUE;
    if (InterlockedCompareExchange(&g_loggedSaraAmSplit, 1, 0) == 0 && g_config.debugLog) {
        WriteDebugDiagnostic("ThaiFontFix: Sara Am split; upper nikhahit positioned separately");
    }
    return result;
}

static BOOL IsKnownImage(BYTE* image, DWORD* renderRva, DWORD* layoutRva, DWORD* textRva) {
    if (!IsReadableRange(image, sizeof(IMAGE_DOS_HEADER))) return FALSE;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x100000) return FALSE;
    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(image + dos->e_lfanew);
    if (!IsReadableRange(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.ImageBase != 0x400000) return FALSE;

    DWORD timestamp = nt->FileHeader.TimeDateStamp;
    DWORD imageSize = nt->OptionalHeader.SizeOfImage;
    if (timestamp == 0x3b1ebbed && imageSize == 0x743000) {
        *renderRva = 0x318e0;
        *layoutRva = 0x32490;
        *textRva = 0x31cc0;
    } else if (timestamp == 0x3bdf544e && imageSize == 0x793000) {
        *renderRva = 0x34120;
        *layoutRva = 0x34cd0;
        *textRva = 0x34500;
    } else {
        return FALSE;
    }
    return *renderRva <= nt->OptionalHeader.SizeOfImage - 12 &&
           *layoutRva <= nt->OptionalHeader.SizeOfImage - 12 &&
           *textRva <= nt->OptionalHeader.SizeOfImage - 12;
}

static BOOL MatchesBytes(const BYTE* address, const BYTE* expected, SIZE_T count) {
    for (SIZE_T i = 0; i < count; ++i) if (address[i] != expected[i]) return FALSE;
    return TRUE;
}

static void* MakeTrampoline(BYTE* target, SIZE_T instructionSpan) {
    if (instructionSpan < 5 || instructionSpan > 32) return NULL;
    SIZE_T trampolineSize = instructionSpan + 5;
    BYTE* trampoline = (BYTE*)VirtualAlloc(NULL, trampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (trampoline == NULL) return NULL;
    for (SIZE_T i = 0; i < instructionSpan; ++i) trampoline[i] = target[i];
    trampoline[instructionSpan] = 0xe9;
    *(LONG*)(trampoline + instructionSpan + 1) =
        (LONG)((uintptr_t)(target + instructionSpan) - (uintptr_t)(trampoline + trampolineSize));
    FlushInstructionCache(GetCurrentProcess(), trampoline, trampolineSize);
    return trampoline;
}

static BOOL WriteJump(BYTE* target, const void* destination) {
    DWORD oldProtection;
    if (!VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &oldProtection)) return FALSE;
    target[0] = 0xe9;
    *(LONG*)(target + 1) = (LONG)((uintptr_t)destination - (uintptr_t)(target + 5));
    FlushInstructionCache(GetCurrentProcess(), target, 5);
    DWORD ignored;
    VirtualProtect(target, 5, oldProtection, &ignored);
    return TRUE;
}

static void RestoreBytes(BYTE* target, const BYTE* bytes) {
    DWORD oldProtection;
    if (!VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &oldProtection)) return;
    for (unsigned i = 0; i < 5; ++i) target[i] = bytes[i];
    FlushInstructionCache(GetCurrentProcess(), target, 5);
    DWORD ignored;
    VirtualProtect(target, 5, oldProtection, &ignored);
}

extern "C" int __stdcall ThaiLayoutHook(void* font, void* target, short* text,
    int x, int y, int maxWidth, int maxHeight, int flags, int param9, int param10) {
    LogOnce(&g_loggedLayoutHook, "ThaiFontFix: layout hook reached");
    LayoutFunction original = (LayoutFunction)g_layoutTrampoline;
    if (!g_runtimeReady || original == NULL) return original != NULL ? original(font, target, text, x, y, maxWidth, maxHeight, flags, param9, param10) : 0;

    EnterCriticalSection(&g_fontLock);
    LayoutContext context;
    context.previous = g_currentContext;
    context.font = font;
    context.state = GetFontState(font);
    if (context.state == NULL) LogOnce(&g_loggedNoFontState, "ThaiFontFix: layout hook reached, but this font has no usable Thai mark state");
    context.spacing = 0;
    context.currentY = 0;
    context.baseX = 0;
    context.baseWidth = 0;
    context.hasY = FALSE;
    context.hasBase = FALSE;
    context.hasAboveMark = FALSE;
    context.metricsApplied = FALSE;
    context.hasPendingTone = FALSE;
    context.pendingToneFont = NULL;
    context.pendingToneState = NULL;
    context.pendingToneGlyphIndex = -1;
    context.pendingToneCode = 0;
    context.pendingToneX = 0;
    context.pendingToneY = 0;
    context.pendingToneColor = 0;

    LayoutContext* parentMetrics = context.state != NULL ? FindContextForData(context.previous, context.state->data) : NULL;
    LayoutContext* parentFont = FindContextForFont(context.previous, font);
    if (parentFont != NULL) context.spacing = parentFont->spacing;
    else if (IsReadableRange((BYTE*)font + 0x2c, sizeof(int))) context.spacing = *(int*)((BYTE*)font + 0x2c);

    if (parentMetrics != NULL) RestoreMetrics(parentMetrics);

    BOOL patch = CanApplyMetrics(&context);
    if (patch) {
        ApplyMetrics(&context);
        if (InterlockedCompareExchange(&g_loggedMetricsApplied, 1, 0) == 0 && g_config.debugLog) {
            char message[128];
            wsprintfA(message, "ThaiFontFix: metrics applied spacing=%ld safe-spacing=%u Thai-mark-count=%u",
                (long)context.spacing, (unsigned)context.state->safeSpacing,
                (unsigned)context.state->markCount);
            WriteDebugDiagnostic(message);
        }
    } else if (context.state != NULL) {
        LogOnce(&g_loggedMetricsRejected, "ThaiFontFix: usable Thai font found, but spacing/safe-spacing checks prevented metrics adjustment");
    }
    g_currentContext = &context;

    int result = original(font, target, text, x, y, maxWidth, maxHeight, flags, param9, param10);

    FlushPendingTone((RenderFunction)g_renderTrampoline, &context, FALSE);
    g_currentContext = context.previous;
    RestoreMetrics(&context);
    if (parentMetrics != NULL) ApplyMetrics(parentMetrics);
    LeaveCriticalSection(&g_fontLock);
    return result;
}

extern "C" int __fastcall ThaiTextHook(void* font, void*, short* text,
    int x, int y, int maxWidth, int maxHeight) {
    LogOnce(&g_loggedTextHook, "ThaiFontFix: direct text hook reached");
    TextFunction original = (TextFunction)g_textTrampoline;
    if (!g_runtimeReady || original == NULL) {
        return original != NULL ? original(font, text, x, y, maxWidth, maxHeight) : 0;
    }

    EnterCriticalSection(&g_fontLock);
    LayoutContext context;
    context.previous = g_currentContext;
    context.font = font;
    context.state = GetFontState(font);
    context.spacing = 0;
    context.currentY = 0;
    context.baseX = 0;
    context.baseWidth = 0;
    context.hasY = FALSE;
    context.hasBase = FALSE;
    context.hasAboveMark = FALSE;
    context.metricsApplied = FALSE;
    context.hasPendingTone = FALSE;
    context.pendingToneFont = NULL;
    context.pendingToneState = NULL;
    context.pendingToneGlyphIndex = -1;
    context.pendingToneCode = 0;
    context.pendingToneX = 0;
    context.pendingToneY = 0;
    context.pendingToneColor = 0;

    if (context.state == NULL) {
        LogOnce(&g_loggedNoFontState,
            "ThaiFontFix: direct text hook reached, but this font has no usable Thai mark state");
    }
    LayoutContext* parentMetrics = context.state != NULL ?
        FindContextForData(context.previous, context.state->data) : NULL;
    LayoutContext* parentFont = FindContextForFont(context.previous, font);
    if (parentFont != NULL) context.spacing = parentFont->spacing;
    else if (IsReadableRange((BYTE*)font + 0x2c, sizeof(int))) {
        context.spacing = *(int*)((BYTE*)font + 0x2c);
    }

    if (parentMetrics != NULL) RestoreMetrics(parentMetrics);
    if (CanApplyMetrics(&context) && IsWritableRange((BYTE*)font + 0x2c, sizeof(int))) {
        ApplyMetrics(&context);
        if (InterlockedCompareExchange(&g_loggedMetricsApplied, 1, 0) == 0 && g_config.debugLog) {
            char message[128];
            wsprintfA(message, "ThaiFontFix: metrics applied spacing=%ld safe-spacing=%u Thai-mark-count=%u",
                (long)context.spacing, (unsigned)context.state->safeSpacing,
                (unsigned)context.state->markCount);
            WriteDebugDiagnostic(message);
        }
    } else if (context.state != NULL) {
        LogOnce(&g_loggedMetricsRejected,
            "ThaiFontFix: usable Thai font found, but spacing/safe-spacing checks prevented metrics adjustment");
    }
    g_currentContext = &context;

    int result = original(font, text, x, y, maxWidth, maxHeight);

    FlushPendingTone((RenderFunction)g_renderTrampoline, &context, FALSE);
    g_currentContext = context.previous;
    RestoreMetrics(&context);
    if (parentMetrics != NULL) ApplyMetrics(parentMetrics);
    LeaveCriticalSection(&g_fontLock);
    return result;
}

extern "C" int __fastcall ThaiRenderHook(void* font, void*, unsigned short code,
    int x, int y, int color) {
    LogOnce(&g_loggedRenderHook, "ThaiFontFix: glyph render hook reached");
    RenderFunction original = (RenderFunction)g_renderTrampoline;
    if (original == NULL) return x;
    if (!g_runtimeReady) return original(font, code, x, y, color);

    EnterCriticalSection(&g_fontLock);
    LayoutContext* context = FindContextForFont(g_currentContext, font);
    BYTE* renderData = NULL;
    if (context == NULL && font != NULL && IsReadableRange((BYTE*)font + 4, sizeof(BYTE*))) {
        renderData = *(BYTE**)((BYTE*)font + 4);
        context = FindContextForData(g_currentContext, renderData);
    }
    if (context == NULL || context->state == NULL) {
        if (IsThaiMarkCode(code) &&
            InterlockedCompareExchange(&g_loggedNoLayoutContext, 1, 0) == 0 && g_config.debugLog) {
            LayoutContext* top = g_currentContext;
            unsigned depth = 0;
            for (LayoutContext* item = top; item != NULL; item = item->previous) ++depth;
            if (renderData == NULL && font != NULL && IsReadableRange((BYTE*)font + 4, sizeof(BYTE*))) {
                renderData = *(BYTE**)((BYTE*)font + 4);
            }
            char message[256];
            wsprintfA(message,
                "ThaiFontFix: no matching adjusted context for Thai mark=0x%02X render-font=%p render-data=%p top-font=%p top-data=%p top-applied=%u depth=%u",
                (unsigned)code, font, renderData, top != NULL ? top->font : NULL,
                top != NULL && top->state != NULL ? top->state->data : NULL,
                top != NULL ? (unsigned)top->metricsApplied : 0,
                depth);
            WriteDebugDiagnostic(message);
        }
        int result = original(font, code, x, y, color);
        LeaveCriticalSection(&g_fontLock);
        return result;
    }

    FontState* state = context->state;
    int index = FindGlyphIndex(state, code);
    BOOL isSaraAm = code == 0x00d3 && state->saraAmSplitAvailable &&
        index == state->saraAmGlyphIndex;
    if (context->hasY && context->currentY != y) {
        FlushPendingTone(original, context, FALSE);
        context->hasBase = FALSE;
        context->hasAboveMark = FALSE;
    }
    BOOL toneWillSitAboveSaraAm = isSaraAm && context->hasBase &&
        context->hasPendingTone && context->pendingToneState == state &&
        context->pendingToneY == y;
    if (context->hasPendingTone && !toneWillSitAboveSaraAm) {
        FlushPendingTone(original, context, FALSE);
    }

    if (code == 9) {
        context->hasBase = FALSE;
        context->hasAboveMark = FALSE;
        int result = original(font, code, x, y, color);
        LeaveCriticalSection(&g_fontLock);
        return result;
    }

    if (isSaraAm) {
        if (context->hasBase) {
            int result = DrawSaraAm(original, font, context, code, x, y, color);
            FlushPendingTone(original, context, TRUE);
            LeaveCriticalSection(&g_fontLock);
            return result;
        }
    }

    if (IsThaiMarkCode(code) && index >= 0 && state->markedRecords[index] != 0) {
        BYTE* record = state->records + (SIZE_T)index * state->stride;
        BYTE patchedWidth = record[0];
        BYTE glyphWidth = state->originalWidths[index];
        int drawX = x;
        int drawY = y;
        context->currentY = y;
        context->hasY = TRUE;
        if (context->hasBase) {
            if (g_config.centerMarks) {
                drawX = context->baseX + (context->baseWidth - (int)glyphWidth) / 2;
            }
            if (IsThaiToneMark(code) && context->hasAboveMark) drawY -= g_config.toneMarkRaise;
        }

        if (IsThaiToneMark(code) && state->saraAmSplitAvailable && context->hasBase) {
            context->hasPendingTone = TRUE;
            context->pendingToneFont = font;
            context->pendingToneState = state;
            context->pendingToneGlyphIndex = index;
            context->pendingToneCode = code;
            context->pendingToneX = drawX;
            context->pendingToneY = drawY;
            context->pendingToneColor = color;
            context->hasAboveMark = TRUE;
            LeaveCriticalSection(&g_fontLock);
            return x;
        }

        record[0] = glyphWidth;
        if (InterlockedCompareExchange(&g_loggedMarkHandled, 1, 0) == 0 && g_config.debugLog) {
            char message[128];
            wsprintfA(message, "ThaiFontFix: Thai mark handled code=0x%02X x=%ld y=%ld glyph-width=%u",
                (unsigned)code, (long)drawX, (long)drawY, (unsigned)glyphWidth);
            WriteDebugDiagnostic(message);
        }
        original(font, code, drawX, drawY, color);
        record[0] = patchedWidth;
        if (IsThaiUpperMark(code)) context->hasAboveMark = TRUE;
        LeaveCriticalSection(&g_fontLock);
        return x;
    }

    context->currentY = y;
    context->hasY = TRUE;
    context->hasBase = TRUE;
    context->hasAboveMark = FALSE;
    context->baseX = x;
    context->baseWidth = index >= 0 ? state->originalWidths[index] : 0;
    int result = original(font, code, x, y, color);
    LeaveCriticalSection(&g_fontLock);
    return result;
}

static BOOL InstallFontHooks(void) {
    static const BYTE renderSignature[12] = {0x83,0xec,0x30,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x44,0x8b};
    static const BYTE layoutSignature[12] = {0x83,0xec,0x30,0x53,0x55,0x56,0x8b,0x74,0x24,0x40,0x57,0x33};
    static const BYTE textSignature[12] = {0x83,0xec,0x10,0x8b,0x54,0x24,0x14,0x53,0x55,0x56,0x66,0x8b};

    BYTE* image = (BYTE*)GetModuleHandleW(NULL);
    DWORD renderRva = 0;
    DWORD layoutRva = 0;
    DWORD textRva = 0;
    LogMainImagePath();
    if (!IsKnownImage(image, &renderRva, &layoutRva, &textRva)) {
        WriteDebugDiagnostic("ThaiFontFix: main executable is not a supported build");
        return FALSE;
    }

    BYTE* render = image + renderRva;
    BYTE* layout = image + layoutRva;
    BYTE* text = image + textRva;
    if (!IsReadableRange(render, sizeof(renderSignature)) || !IsReadableRange(layout, sizeof(layoutSignature)) ||
        !IsReadableRange(text, sizeof(textSignature)) ||
        !MatchesBytes(render, renderSignature, sizeof(renderSignature)) ||
        !MatchesBytes(layout, layoutSignature, sizeof(layoutSignature)) ||
        !MatchesBytes(text, textSignature, sizeof(textSignature))) {
        WriteDebugDiagnostic("ThaiFontFix: expected render/layout/direct-text signatures did not match");
        return FALSE;
    }

    // The text helper starts with a 3-byte SUB followed by a 4-byte MOV;
    // copy both complete instructions into its trampoline.
    void* renderTrampoline = MakeTrampoline(render, 5);
    void* layoutTrampoline = MakeTrampoline(layout, 5);
    void* textTrampoline = MakeTrampoline(text, 7);
    if (renderTrampoline == NULL || layoutTrampoline == NULL || textTrampoline == NULL) {
        WriteDebugDiagnostic("ThaiFontFix: failed to allocate hook trampolines");
        if (renderTrampoline != NULL) VirtualFree(renderTrampoline, 0, MEM_RELEASE);
        if (layoutTrampoline != NULL) VirtualFree(layoutTrampoline, 0, MEM_RELEASE);
        if (textTrampoline != NULL) VirtualFree(textTrampoline, 0, MEM_RELEASE);
        return FALSE;
    }

    BYTE originalRender[5];
    BYTE originalLayout[5];
    for (unsigned i = 0; i < 5; ++i) {
        originalRender[i] = render[i];
        originalLayout[i] = layout[i];
    }
    g_renderTrampoline = renderTrampoline;
    g_layoutTrampoline = layoutTrampoline;
    g_textTrampoline = textTrampoline;

    if (!WriteJump(layout, (const void*)ThaiLayoutHook)) {
        WriteDebugDiagnostic("ThaiFontFix: failed to install the layout hook");
        g_renderTrampoline = NULL;
        g_layoutTrampoline = NULL;
        g_textTrampoline = NULL;
        VirtualFree(renderTrampoline, 0, MEM_RELEASE);
        VirtualFree(layoutTrampoline, 0, MEM_RELEASE);
        VirtualFree(textTrampoline, 0, MEM_RELEASE);
        return FALSE;
    }
    if (!WriteJump(render, (const void*)ThaiRenderHook)) {
        WriteDebugDiagnostic("ThaiFontFix: failed to install the render hook");
        RestoreBytes(layout, originalLayout);
        g_renderTrampoline = NULL;
        g_layoutTrampoline = NULL;
        g_textTrampoline = NULL;
        VirtualFree(renderTrampoline, 0, MEM_RELEASE);
        VirtualFree(layoutTrampoline, 0, MEM_RELEASE);
        VirtualFree(textTrampoline, 0, MEM_RELEASE);
        return FALSE;
    }
    if (!WriteJump(text, (const void*)ThaiTextHook)) {
        WriteDebugDiagnostic("ThaiFontFix: failed to install the direct text hook");
        RestoreBytes(render, originalRender);
        RestoreBytes(layout, originalLayout);
        g_renderTrampoline = NULL;
        g_layoutTrampoline = NULL;
        g_textTrampoline = NULL;
        VirtualFree(renderTrampoline, 0, MEM_RELEASE);
        VirtualFree(layoutTrampoline, 0, MEM_RELEASE);
        VirtualFree(textTrampoline, 0, MEM_RELEASE);
        return FALSE;
    }
    g_hooksInstalled = TRUE;
    return TRUE;
}

#if defined(THAI_FONT_PROXY_WINMM)
static void ResolveWinmmForwarder(unsigned index) {
    EnsureRuntimeInitialized();
    if (index >= 5) ExitProcess(ERROR_INVALID_PARAMETER);
    if (g_winmmForwarders[index] != NULL) return;

    LONG state = InterlockedCompareExchange(&g_winmmInit, 1, 0);
    if (state == 0) {
        char path[MAX_PATH];
        UINT length = GetSystemDirectoryA(path, MAX_PATH);
        if (length != 0 && length + 10 < MAX_PATH) {
            path[length++] = '\\';
            const char suffix[] = "winmm.dll";
            for (unsigned i = 0; i < sizeof(suffix); ++i) path[length + i] = suffix[i];
            g_systemWinmm = LoadLibraryA(path);
            if (g_systemWinmm == g_selfModule) g_systemWinmm = NULL;
            if (g_systemWinmm != NULL) LogPath("ThaiFontFix: system WinMM loaded: ", path);
        }
        if (g_systemWinmm == NULL) WriteDebugDiagnostic("ThaiFontFix: failed to load the system WinMM DLL");
        InterlockedExchange(&g_winmmInit, g_systemWinmm != NULL ? 2 : 3);
    } else {
        while ((state = InterlockedCompareExchange(&g_winmmInit, 2, 2)) == 1) Sleep(0);
    }

    if (g_systemWinmm != NULL) {
        FARPROC function = GetProcAddress(g_systemWinmm, kWinmmExportNames[index]);
        InterlockedCompareExchangePointer((PVOID volatile*)&g_winmmForwarders[index], (PVOID)function, NULL);
        if (function != NULL) LogPath("ThaiFontFix: forwarded WinMM API: ", kWinmmExportNames[index]);
    }
    if (g_winmmForwarders[index] == NULL) {
        LogPath("ThaiFontFix: failed to resolve WinMM API: ", kWinmmExportNames[index]);
        ExitProcess(ERROR_MOD_NOT_FOUND);
    }
}

#define WINMM_PROXY(name, slot, offset) \
    extern "C" __declspec(naked) void WINAPI Proxy_##name(void) { \
        __asm { pushfd } \
        __asm { pushad } \
        __asm { push slot } \
        __asm { call ResolveWinmmForwarder } \
        __asm { add esp, 4 } \
        __asm { popad } \
        __asm { popfd } \
        __asm { jmp dword ptr [g_winmmForwarders + offset] } \
    }

WINMM_PROXY(timeBeginPeriod, 0, 0)
WINMM_PROXY(timeEndPeriod, 1, 4)
WINMM_PROXY(timeGetTime, 2, 8)
WINMM_PROXY(timeKillEvent, 3, 12)
WINMM_PROXY(timeSetEvent, 4, 16)

#elif defined(THAI_FONT_PROXY_DDRAW)
static BOOL IsAbsolutePathA(const char* path) {
    return path != NULL && ((path[0] != 0 && path[1] == ':') || path[0] == '\\' || path[0] == '/');
}

static BOOL BuildDdrawTargetPath(const char* setting, char* path, SIZE_T capacity) {
    if (setting == NULL || setting[0] == 0 || lstrcmpiA(setting, "none") == 0) return FALSE;
    if (lstrcmpiA(setting, "system") == 0) {
        UINT length = GetSystemDirectoryA(path, (UINT)capacity);
        if (length == 0 || length + 11 >= capacity) return FALSE;
        path[length++] = '\\';
        SIZE_T used = length;
        return AppendText(path, capacity, &used, "ddraw.dll");
    }

    if (IsAbsolutePathA(setting)) {
        SIZE_T used = 0;
        path[0] = 0;
        return AppendText(path, capacity, &used, setting);
    }

    path[0] = 0;
    if (!GetProxyDirectory(path, capacity)) return FALSE;
    SIZE_T used = (SIZE_T)lstrlenA(path);
    return AppendText(path, capacity, &used, setting);
}

static HMODULE LoadDdrawTarget(const char* setting) {
    char path[MAX_PATH];
    char selfPath[MAX_PATH];
    if (!BuildDdrawTargetPath(setting, path, MAX_PATH)) return NULL;
    DWORD selfLength = GetModuleFileNameA(g_selfModule, selfPath, MAX_PATH);
    if (selfLength != 0 && selfLength < MAX_PATH && lstrcmpiA(path, selfPath) == 0) return NULL;
    HMODULE module = LoadLibraryA(path);
    if (module == g_selfModule) {
        FreeLibrary(module);
        return NULL;
    }
    return module;
}

static void InitializeDdrawTargets(void) {
    LONG state = InterlockedCompareExchange(&g_ddrawTargetInit, 1, 0);
    if (state == 0) {
        g_customDdraw = LoadDdrawTarget(g_customDdrawSetting);
        g_systemDdraw = LoadDdrawTarget("system");
        InterlockedExchange(&g_ddrawTargetInit, 2);
    } else {
        while (InterlockedCompareExchange(&g_ddrawTargetInit, 2, 2) != 2) Sleep(0);
    }
}

static FARPROC FindDdrawExport(unsigned index) {
    FARPROC function = g_customDdraw != NULL
        ? GetProcAddress(g_customDdraw, kDdrawExportNames[index])
        : NULL;
    if (function == NULL && g_systemDdraw != NULL) {
        function = GetProcAddress(g_systemDdraw, kDdrawExportNames[index]);
    }
    return function;
}

static void ResolveDdrawForwarder(unsigned index) {
    EnsureRuntimeInitialized();
    if (index >= 22) ExitProcess(ERROR_INVALID_PARAMETER);
    if (g_ddrawForwarders[index] != NULL) return;
    InitializeDdrawTargets();
    FARPROC function = FindDdrawExport(index);
    if (function == NULL) {
        if (g_config.debugLog) OutputDebugStringA("ThaiFontFix: configured ddraw targets do not export a requested function\n");
        ExitProcess(ERROR_PROC_NOT_FOUND);
    }
    InterlockedCompareExchangePointer((PVOID volatile*)&g_ddrawForwarders[index], (PVOID)function, NULL);
}

#define DDRAW_PROXY(name, slot, offset) \
    extern "C" __declspec(naked) void WINAPI Proxy_##name(void) { \
        __asm { pushfd } \
        __asm { pushad } \
        __asm { push slot } \
        __asm { call ResolveDdrawForwarder } \
        __asm { add esp, 4 } \
        __asm { popad } \
        __asm { popfd } \
        __asm { jmp dword ptr [g_ddrawForwarders + offset] } \
    }

DDRAW_PROXY(AcquireDDThreadLock, 0, 0)
DDRAW_PROXY(CompleteCreateSysmemSurface, 1, 4)
DDRAW_PROXY(D3DParseUnknownCommand, 2, 8)
DDRAW_PROXY(DDGetAttachedSurfaceLcl, 3, 12)
DDRAW_PROXY(DDInternalLock, 4, 16)
DDRAW_PROXY(DDInternalUnlock, 5, 20)
DDRAW_PROXY(DSoundHelp, 6, 24)
DDRAW_PROXY(DirectDrawCreate, 7, 28)
DDRAW_PROXY(DirectDrawCreateClipper, 8, 32)
DDRAW_PROXY(DirectDrawCreateEx, 9, 36)
DDRAW_PROXY(DirectDrawEnumerateA, 10, 40)
DDRAW_PROXY(DirectDrawEnumerateExA, 11, 44)
DDRAW_PROXY(DirectDrawEnumerateExW, 12, 48)
DDRAW_PROXY(DirectDrawEnumerateW, 13, 52)
DDRAW_PROXY(DllCanUnloadNow, 14, 56)
DDRAW_PROXY(DllGetClassObject, 15, 60)
DDRAW_PROXY(GetDDSurfaceLocal, 16, 64)
DDRAW_PROXY(GetOLEThunkData, 17, 68)
DDRAW_PROXY(GetSurfaceFromDC, 18, 72)
DDRAW_PROXY(RegisterSpecialCase, 19, 76)
DDRAW_PROXY(ReleaseDDThreadLock, 20, 80)
DDRAW_PROXY(SetAppCompatData, 21, 84)

#endif

#if defined(THAI_FONT_FIX_SYRINGE)
struct SyringeHandshakeInfo {
    int cbSize;
    int num_hooks;
    unsigned int checksum;
    DWORD exeFilesize;
    DWORD exeTimestamp;
    unsigned int exeCRC;
    int cchMessage;
    char* Message;
};

static void SetSyringeHandshakeMessage(SyringeHandshakeInfo* info, const char* message) {
    if (info == NULL || info->Message == NULL || info->cchMessage <= 0) return;
    lstrcpynA(info->Message, message, info->cchMessage);
}

extern "C" HRESULT __cdecl SyringeHandshake(SyringeHandshakeInfo* info) {
    if (info == NULL || info->cbSize < (int)sizeof(SyringeHandshakeInfo)) return E_INVALIDARG;

#if defined(THAI_FONT_SYRINGE_RA2)
    const BOOL supported = info->exeFilesize == 4485120u &&
        info->exeTimestamp == 0x3b1ebbedu && info->exeCRC == 0xd33d92edu;
    SetSyringeHandshakeMessage(info, supported
        ? "ThaiFontFix Syringe hook: Red Alert 2 game.exe supported"
        : "ThaiFontFix Syringe hook: target is not the supported Red Alert 2 game.exe");
#elif defined(THAI_FONT_SYRINGE_YR)
    const BOOL supported =
        info->exeFilesize == 4813072u && info->exeTimestamp == 0x3bdf544eu && info->exeCRC == 0x54cc0a13u;
    SetSyringeHandshakeMessage(info, supported
        ? "ThaiFontFix Syringe hook: Yuri's Revenge game executable supported"
        : "ThaiFontFix Syringe hook: target is not a supported Yuri's Revenge executable");
#else
    const BOOL supported = FALSE;
    SetSyringeHandshakeMessage(info, "ThaiFontFix Syringe hook was built without a target executable");
#endif

    return supported ? S_OK : S_FALSE;
}

extern "C" DWORD __cdecl ThaiFontFixSyringeEntry(void*, void*) {
    EnsureRuntimeInitialized();
    return 0; // Syringe resumes the original executable entry point.
}
#endif

#if !defined(THAI_FONT_FIX_INTEGRATED)
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_selfModule = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
#endif
