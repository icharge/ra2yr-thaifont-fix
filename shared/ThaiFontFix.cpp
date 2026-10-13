#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>
#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
#include <usp10.h>
#endif

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

enum {
    TRUE_TYPE_STYLE_REGULAR = 0,
    TRUE_TYPE_STYLE_BOLD = 1,
    TRUE_TYPE_STYLE_ITALIC = 2,
    TRUE_TYPE_STYLE_BOLD_ITALIC = 3
};

#if defined(THAI_FONT_FIX_BINK_TRUETYPE_DEFAULT_RENDER)
#define THAI_FONT_FIX_BINK_TRUETYPE_RENDER_DEFAULT TRUE
#else
#define THAI_FONT_FIX_BINK_TRUETYPE_RENDER_DEFAULT FALSE
#endif

struct FontFixConfig {
    BOOL enabled;
    BOOL centerMarks;
    BOOL debugLog;
    int toneMarkRaise;
    int maxExtraSpacing;
    BOOL trueTypePreview;
    BOOL trueTypeRender;
    int trueTypeStyle;
    int trueTypeSize;
    char trueTypeFont[LF_FACESIZE];
};

#if defined(THAI_FONT_PROXY_WINMM) || defined(THAI_FONT_FIX_INTEGRATED)
static FontFixConfig g_config = { TRUE, TRUE, TRUE, 2, 8, FALSE, THAI_FONT_FIX_BINK_TRUETYPE_RENDER_DEFAULT, TRUE_TYPE_STYLE_REGULAR, 0, "Arial" };
#else
static FontFixConfig g_config = { TRUE, TRUE, FALSE, 2, 8, FALSE, THAI_FONT_FIX_BINK_TRUETYPE_RENDER_DEFAULT, TRUE_TYPE_STYLE_REGULAR, 0, "Arial" };
#endif
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
#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
static volatile LONG g_loggedTrueTypePreview;
static volatile LONG g_loggedTrueTypeRender;
static volatile LONG g_loggedTrueTypeRenderFallback;
static BOOL g_trueTypeDefaultsAdded;
#endif

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

#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
struct TrueTypeLine {
    BYTE cp874[256];
    int length;
    int x;
    int y;
    int right;
    int color;
    WORD glyphColors[256];
    SHORT characterStarts[256];
    SHORT characterEnds[256];
    BYTE* mask;
    DWORD maskWidth;
    DWORD maskHeight;
    DWORD maskContentWidth;
    DWORD maskContentHeight;
};

struct TrueTypeRenderRun {
    BOOL enabled;
    BOOL valid;
    BOOL captured;
    BOOL sawGlyph;
    BOOL completed;
    BOOL rendered;
    BOOL fallbackLogged;
    const char* failureReason;
    unsigned failureCodeUnit;
    DWORD fontHeight;
    int boxX;
    int boxY;
    int boxWidth;
    int boxHeight;
    WORD* surface;
    DWORD pitchWords;
    int captureX;
    int captureY;
    DWORD captureWidth;
    DWORD captureHeight;
    WORD* background;
    WORD* bitmapRendered;
    DWORD expectedGlyphCount;
    DWORD seenGlyphCount;
    BYTE expectedCodes[1024];
    unsigned lineCount;
    TrueTypeLine lines[32];
};
#endif

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
#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
    TrueTypeRenderRun trueType;
#endif
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

static BOOL GetDebugLogPath(char* path, SIZE_T capacity) {
    if (!GetProxyConfigurationPath(path, capacity)) return FALSE;
    SIZE_T length = (SIZE_T)lstrlenA(path);
    if (length < 4) return FALSE;
    path[length - 3] = 'l';
    path[length - 2] = 'o';
    path[length - 1] = 'g';
    return TRUE;
}

static BOOL ClearDebugLog(void) {
    char path[MAX_PATH];
    if (!GetDebugLogPath(path, MAX_PATH)) return FALSE;
    HANDLE logFile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (logFile == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle(logFile);
    return TRUE;
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

#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
static int GetTrueTypeFontWeight(void) {
    return g_config.trueTypeStyle == TRUE_TYPE_STYLE_BOLD ||
        g_config.trueTypeStyle == TRUE_TYPE_STYLE_BOLD_ITALIC ? FW_BOLD : FW_NORMAL;
}

static BOOL IsTrueTypeFontItalic(void) {
    return g_config.trueTypeStyle == TRUE_TYPE_STYLE_ITALIC ||
        g_config.trueTypeStyle == TRUE_TYPE_STYLE_BOLD_ITALIC;
}

static DWORD GetConfiguredTrueTypeFontHeight(DWORD nativeHeight) {
    if (g_config.trueTypeSize >= 8 && g_config.trueTypeSize <= 64) {
        return (DWORD)g_config.trueTypeSize;
    }
    return nativeHeight >= 8 && nativeHeight <= 64 ? nativeHeight : 16;
}

static void InvalidateTrueTypeRun(TrueTypeRenderRun* run, const char* reason) {
    if (run == NULL) return;
    if (run->failureReason == NULL) run->failureReason = reason;
    run->valid = FALSE;
}

static const char* GetTrueTypeFontStyleName(void) {
    switch (g_config.trueTypeStyle) {
    case TRUE_TYPE_STYLE_BOLD: return "Bold";
    case TRUE_TYPE_STYLE_ITALIC: return "Italic";
    case TRUE_TYPE_STYLE_BOLD_ITALIC: return "BoldItalic";
    default: return "Regular";
    }
}

static BOOL SaveTrueTypePreviewBitmap(const char* path, const BYTE* pixels,
    DWORD width, DWORD height, DWORD sourceStrideWidth) {
    if (path == NULL || pixels == NULL || width == 0 || height == 0 ||
        width > 8192 || height > 128 || sourceStrideWidth < width ||
        sourceStrideWidth > 8192) return FALSE;

    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER infoHeader;
    ZeroMemory(&fileHeader, sizeof(fileHeader));
    ZeroMemory(&infoHeader, sizeof(infoHeader));
    DWORD rowBytes = width * 4;
    fileHeader.bfType = 0x4d42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader);
    fileHeader.bfSize = fileHeader.bfOffBits + rowBytes * height;
    infoHeader.biSize = sizeof(infoHeader);
    infoHeader.biWidth = (LONG)width;
    infoHeader.biHeight = (LONG)height;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = rowBytes * height;

    HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;

    DWORD written = 0;
    BOOL success = WriteFile(file, &fileHeader, sizeof(fileHeader), &written, NULL) &&
        written == sizeof(fileHeader) &&
        WriteFile(file, &infoHeader, sizeof(infoHeader), &written, NULL) &&
        written == sizeof(infoHeader);
    for (DWORD row = 0; success && row < height; ++row) {
        const BYTE* source = pixels + (SIZE_T)(height - row - 1) * sourceStrideWidth * 4;
        success = WriteFile(file, source, rowBytes, &written, NULL) && written == rowBytes;
    }
    CloseHandle(file);
    return success;
}

static void TryCreateTrueTypePreview(short* text, FontState* state) {
    if (!g_config.trueTypePreview ||
        InterlockedCompareExchange(&g_loggedTrueTypePreview, 0, 0) != 0 || text == NULL) return;

    BYTE cp874[128];
    WCHAR unicode[128];
    int length = 0;
    BOOL hasThai = FALSE;
    for (; length < (int)(sizeof(cp874)); ++length) {
        short value = 0;
        if (!IsReadableRange(text + length, sizeof(short)) ||
            !CopyMemorySafely(&value, text + length, sizeof(value))) return;
        if (value == 0) break;
        if (value < 0 || value > 0xff || value < 0x20 || value == 0x7f) return;
        cp874[length] = (BYTE)value;
        if (value >= 0xa1 && value <= 0xfb) hasThai = TRUE;
    }
    if (length == 0 || length >= (int)(sizeof(cp874)) || !hasThai) return;

    int wideLength = MultiByteToWideChar(874, 0, (LPCCH)cp874, length,
        unicode, (int)(sizeof(unicode) / sizeof(unicode[0])));
    if (wideLength <= 0 || wideLength > (int)(sizeof(unicode) / sizeof(unicode[0]))) return;

    DWORD nativeHeight = state != NULL && IsReadableRange(state->data + 8, sizeof(DWORD))
        ? *(DWORD*)(state->data + 8) : 16;
    DWORD fontHeight = GetConfiguredTrueTypeFontHeight(nativeHeight);
    DWORD width = (DWORD)length * fontHeight * 2 + 16;
    DWORD height = fontHeight + 8;
    if (width > 8192 || height > 128) return;

    HDC dc = CreateCompatibleDC(NULL);
    if (dc == NULL) return;
    BITMAPINFO bitmapInfo;
    ZeroMemory(&bitmapInfo, sizeof(bitmapInfo));
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = (LONG)width;
    bitmapInfo.bmiHeader.biHeight = -(LONG)height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    BYTE* pixels = NULL;
    HBITMAP bitmap = CreateDIBSection(dc, &bitmapInfo, DIB_RGB_COLORS,
        (void**)&pixels, NULL, 0);
    if (bitmap == NULL || pixels == NULL) {
        if (bitmap != NULL) DeleteObject(bitmap);
        DeleteDC(dc);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    if (oldBitmap == NULL || oldBitmap == HGDI_ERROR) {
        DeleteObject(bitmap);
        DeleteDC(dc);
        return;
    }
    char fontFace[LF_FACESIZE];
    CopyMemory(fontFace, g_config.trueTypeFont, sizeof(fontFace));
    fontFace[LF_FACESIZE - 1] = 0;
    HFONT font = CreateFontA(-(int)fontHeight, 0, 0, 0, GetTrueTypeFontWeight(),
        IsTrueTypeFontItalic(), FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontFace);
    HFONT oldFont = font != NULL ? (HFONT)SelectObject(dc, font) : NULL;
    if (font == NULL || oldFont == NULL || oldFont == (HFONT)HGDI_ERROR) {
        if (oldFont != NULL && oldFont != (HFONT)HGDI_ERROR) SelectObject(dc, oldFont);
        if (font != NULL) DeleteObject(font);
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        return;
    }
    ZeroMemory(pixels, (SIZE_T)width * height * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    SetTextAlign(dc, TA_LEFT | TA_TOP | TA_NOUPDATECP);

    SCRIPT_STRING_ANALYSIS analysis = NULL;
    HRESULT analyzed = ScriptStringAnalyse(dc, unicode, wideLength, wideLength * 3 + 16,
        -1, SSA_GLYPHS | SSA_FALLBACK, 0, NULL, NULL, NULL, NULL, NULL, &analysis);
    BOOL rendered = analyzed == S_OK && analysis != NULL &&
        ScriptStringOut(analysis, 2, 2, 0, NULL, 0, 0, FALSE) == S_OK;
    SIZE measured = {0, 0};
    const SIZE* measuredPointer = analysis != NULL ? ScriptString_pSize(analysis) : NULL;
    if (measuredPointer != NULL) measured = *measuredPointer;

    char previewPath[MAX_PATH];
    BOOL saved = FALSE;
    if (rendered && GetProxyDirectory(previewPath, MAX_PATH)) {
        SIZE_T pathLength = (SIZE_T)lstrlenA(previewPath);
        if (AppendText(previewPath, MAX_PATH, &pathLength, "thaifontfix-truetype-preview.bmp")) {
            DWORD previewWidth = measured.cx > 0 ? (DWORD)measured.cx + 4 : width;
            DWORD previewHeight = measured.cy > 0 ? (DWORD)measured.cy + 4 : height;
            if (previewWidth > width) previewWidth = width;
            if (previewHeight > height) previewHeight = height;
            saved = SaveTrueTypePreviewBitmap(previewPath, pixels,
                previewWidth, previewHeight, width);
        }
    }

    if (analysis != NULL) ScriptStringFree(&analysis);
    if (oldFont != NULL) SelectObject(dc, oldFont);
    if (font != NULL) DeleteObject(font);
    if (oldBitmap != NULL) SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);

    if (saved && InterlockedCompareExchange(&g_loggedTrueTypePreview, 1, 0) == 0) {
        if (g_config.debugLog) {
            char message[2048];
            SIZE_T used = 0;
            char value[16];
            wsprintfA(message,
                "ThaiFontFix: Uniscribe CP874 preview saved font=%s characters=%d size=%ldx%ld path=%s cp874=",
                fontFace, length, (long)measured.cx, (long)measured.cy, previewPath);
            used = (SIZE_T)lstrlenA(message);
            for (int i = 0; i < length && used < sizeof(message); ++i) {
                wsprintfA(value, "%02X", (unsigned)cp874[i]);
                if (!AppendText(message, sizeof(message), &used, value)) break;
            }
            if (AppendText(message, sizeof(message), &used, " unicode=")) {
                for (int i = 0; i < wideLength && used < sizeof(message); ++i) {
                    wsprintfA(value, "U+%04X", (unsigned)unicode[i]);
                    if (!AppendText(message, sizeof(message), &used, value)) break;
                }
            }
            WriteDebugDiagnostic(message);
        }
    }
}

static DWORD GetTrueTypeFontHeight(void* font) {
    if (font == NULL || !IsReadableRange((BYTE*)font + 4, sizeof(BYTE*))) {
        return GetConfiguredTrueTypeFontHeight(16);
    }
    BYTE* data = *(BYTE**)((BYTE*)font + 4);
    if (data == NULL) return GetConfiguredTrueTypeFontHeight(16);
    if (!IsReadableRange(data + 8, sizeof(DWORD))) return GetConfiguredTrueTypeFontHeight(16);
    DWORD height = *(DWORD*)(data + 8);
    return GetConfiguredTrueTypeFontHeight(height);
}

static void InitializeTrueTypeRun(TrueTypeRenderRun* run, void* font,
    short* text, int x, int y, int maxWidth, int maxHeight) {
    ZeroMemory(run, sizeof(*run));
    if (!g_config.trueTypeRender) return;
    run->enabled = TRUE;
    run->valid = TRUE;
    if (text == NULL) {
        InvalidateTrueTypeRun(run, "null input string");
        return;
    }
    BOOL terminated = FALSE;
    for (DWORD i = 0; i < sizeof(run->expectedCodes); ++i) {
        short value = 0;
        if (!IsReadableRange(text + i, sizeof(short)) ||
            !CopyMemorySafely(&value, text + i, sizeof(value))) {
            InvalidateTrueTypeRun(run, "input string is unreadable");
            return;
        }
        if (value == 0) {
            terminated = TRUE;
            break;
        }
        if (value == '\r' || value == '\n') continue;
        if (value < 0x20 || value == 0x7f || value > 0xff) {
            run->failureCodeUnit = (unsigned)(unsigned short)value;
            InvalidateTrueTypeRun(run, "unsupported input code unit");
            return;
        }
        run->expectedCodes[run->expectedGlyphCount++] = (BYTE)value;
    }
    if (!terminated || run->expectedGlyphCount == sizeof(run->expectedCodes)) {
        InvalidateTrueTypeRun(run, "input string is unterminated or too long");
        return;
    }
    run->fontHeight = GetTrueTypeFontHeight(font);
    run->boxX = x - 16;
    run->boxY = y - 4;
    run->boxWidth = maxWidth > 0 && maxWidth <= 4096 ? maxWidth + 32 : 1056;
    run->boxHeight = maxHeight > 0 && maxHeight <= 512 ? maxHeight + 8 : 136;
}

static DWORD GetRuntimeGlyphWidth(void* font, unsigned short code, DWORD fontHeight) {
    if (font == NULL || code > 0xff || !IsReadableRange((BYTE*)font + 4, sizeof(BYTE*))) {
        return fontHeight / 2;
    }
    BYTE* data = *(BYTE**)((BYTE*)font + 4);
    if (data == NULL) return fontHeight / 2;
    if (!IsReadableRange(data, 0x24)) return fontHeight / 2;
    DWORD stride = *(DWORD*)(data + 0x14);
    WORD* map = *(WORD**)(data + 0x18);
    BYTE* records = *(BYTE**)(data + 0x1c);
    DWORD count = *(DWORD*)(data + 0x20);
    if (stride < 1 || stride > 128 || map == NULL || records == NULL || count == 0 || count > 0x10000 ||
        !IsReadableRange(map + code, sizeof(WORD))) return fontHeight / 2;
    WORD entry = map[code];
    if (entry == 0 || entry > count) return 0;
    BYTE* record = records + (SIZE_T)(entry - 1) * stride;
    if (!IsReadableRange(record, 1)) return fontHeight / 2;
    BYTE width = record[0];
    return width <= 64 ? width : fontHeight / 2;
}

static void CaptureTrueTypeBackground(TrueTypeRenderRun* run, void* font,
    int glyphX, int glyphY) {
    if (run == NULL || !run->enabled || !run->valid || run->captured) return;
    if (font == NULL || !IsReadableRange((BYTE*)font + 0x0c, sizeof(WORD*)) ||
        !IsReadableRange((BYTE*)font + 0x10, sizeof(DWORD))) {
        InvalidateTrueTypeRun(run, "surface fields are unreadable");
        return;
    }

    WORD* surface = *(WORD**)((BYTE*)font + 0x0c);
    DWORD pitchWords = *(DWORD*)((BYTE*)font + 0x10);
    if (surface == NULL || pitchWords == 0 || pitchWords > 8192) {
        InvalidateTrueTypeRun(run, "surface pointer or pitch is invalid");
        return;
    }

    RECT clip;
    if (!IsReadableRange((BYTE*)font + 0x30, sizeof(clip)) ||
        !CopyMemorySafely(&clip, (BYTE*)font + 0x30, sizeof(clip)) ||
        clip.left < 0 || clip.top < 0 || clip.right < clip.left || clip.bottom < clip.top ||
        clip.right >= (LONG)pitchWords) {
        InvalidateTrueTypeRun(run, "surface clip is invalid");
        return;
    }

    int left = run->boxX;
    int top = run->boxY;
    int right = left + run->boxWidth;
    int bottom = top + run->boxHeight;
    if (glyphX < left) left = glyphX - 8;
    if (glyphY < top) top = glyphY - 2;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (left < clip.left) left = clip.left;
    if (top < clip.top) top = clip.top;
    if (right > clip.right + 1) right = clip.right + 1;
    if (bottom > clip.bottom + 1) bottom = clip.bottom + 1;
    if (right > (int)pitchWords) right = (int)pitchWords;
    if (bottom <= top || right <= left || right - left > 4096 || bottom - top > 512) {
        InvalidateTrueTypeRun(run, "capture rectangle is invalid");
        return;
    }

    if (right <= left || bottom <= top || right - left > 4096 || bottom - top > 512) {
        InvalidateTrueTypeRun(run, "capture rectangle is invalid");
        return;
    }

    DWORD width = (DWORD)(right - left);
    DWORD height = (DWORD)(bottom - top);
    SIZE_T rowBytes = (SIZE_T)width * sizeof(WORD);
    SIZE_T backupBytes = rowBytes * height;
    if (backupBytes == 0 || backupBytes > 4 * 1024 * 1024) {
        InvalidateTrueTypeRun(run, "capture rectangle is too large");
        return;
    }
    WORD* background = (WORD*)HeapAlloc(GetProcessHeap(), 0, backupBytes);
    if (background == NULL) {
        InvalidateTrueTypeRun(run, "capture buffer allocation failed");
        return;
    }
    for (DWORD row = 0; row < height; ++row) {
        WORD* source = surface + (SIZE_T)(top + (int)row) * pitchWords + left;
        if (!IsReadableRange(source, rowBytes) || !IsWritableRange(source, rowBytes) ||
            !CopyMemorySafely(background + (SIZE_T)row * width, source, rowBytes)) {
            HeapFree(GetProcessHeap(), 0, background);
            InvalidateTrueTypeRun(run, "surface pixels are unreadable");
            return;
        }
    }

    run->surface = surface;
    run->pitchWords = pitchWords;
    run->captureX = left;
    run->captureY = top;
    run->captureWidth = width;
    run->captureHeight = height;
    run->background = background;
    run->captured = TRUE;
}

static BOOL ResolveTrueTypeColor(void* font, int color, int* resolved) {
    if (resolved == NULL) return FALSE;
    if (color != -1) {
        if (color < 0 || color > 0xffff) return FALSE;
        *resolved = color;
        return TRUE;
    }
    if (font == NULL || !IsReadableRange((BYTE*)font + 0x24, sizeof(WORD))) return FALSE;
    *resolved = *(WORD*)((BYTE*)font + 0x24);
    return TRUE;
}

static void RecordTrueTypeGlyph(TrueTypeRenderRun* run, void* font,
    unsigned short code, int x, int y, int color) {
    if (run == NULL || !run->enabled || !run->valid) return;
    run->sawGlyph = TRUE;
    if (code < 0x20 || code > 0xff || code == 0x7f) {
        run->failureCodeUnit = code;
        InvalidateTrueTypeRun(run, "unsupported glyph code unit");
        return;
    }
    if (run->seenGlyphCount >= run->expectedGlyphCount ||
        run->expectedCodes[run->seenGlyphCount] != (BYTE)code) {
        run->failureCodeUnit = code;
        InvalidateTrueTypeRun(run, "glyph order does not match input string");
        return;
    }
    int resolvedColor = 0;
    if (!ResolveTrueTypeColor(font, color, &resolvedColor)) {
        InvalidateTrueTypeRun(run, "glyph color is invalid");
        return;
    }
    CaptureTrueTypeBackground(run, font, x, y);
    if (!run->valid) return;

    TrueTypeLine* line = NULL;
    for (unsigned i = 0; i < run->lineCount; ++i) {
        if (run->lines[i].y == y) {
            line = &run->lines[i];
            break;
        }
    }
    if (line == NULL) {
        if (run->lineCount >= sizeof(run->lines) / sizeof(run->lines[0])) {
            InvalidateTrueTypeRun(run, "too many text lines");
            return;
        }
        line = &run->lines[run->lineCount++];
        ZeroMemory(line, sizeof(*line));
        line->x = x;
        line->y = y;
        line->right = x;
        line->color = resolvedColor;
    }
    if (line->length >= (int)sizeof(line->cp874)) {
        InvalidateTrueTypeRun(run, "text line is too long");
        return;
    }
    line->cp874[line->length] = (BYTE)code;
    line->glyphColors[line->length] = (WORD)resolvedColor;
    ++line->length;
    ++run->seenGlyphCount;

    int spacing = 0;
    if (IsReadableRange((BYTE*)font + 0x2c, sizeof(int))) spacing = *(int*)((BYTE*)font + 0x2c);
    if (spacing < 0 || spacing > 32) spacing = 0;
    int right = x + (int)GetRuntimeGlyphWidth(font, code, run->fontHeight) + spacing;
    if (right > line->right) line->right = right;
}

static BOOL CreateTrueTypeLineMask(TrueTypeLine* line, DWORD fontHeight) {
    if (line == NULL || line->length <= 0) return FALSE;
    WCHAR unicode[256];
    int wideLength = MultiByteToWideChar(874, 0, (LPCCH)line->cp874,
        line->length, unicode, (int)(sizeof(unicode) / sizeof(unicode[0])));
    if (wideLength <= 0 || wideLength > (int)(sizeof(unicode) / sizeof(unicode[0]))) return FALSE;

    DWORD targetWidth = line->right - line->x;
    if (targetWidth < 1) targetWidth = (DWORD)line->length * fontHeight / 2;
    DWORD width = (DWORD)line->length * fontHeight * 2 + 16;
    if (width < targetWidth + 16) width = targetWidth + 16;
    if (width < 64) width = 64;
    if (width > 8192) return FALSE;
    DWORD height = fontHeight + 8;
    if (height < 16) height = 16;
    if (height > 128) return FALSE;

    HDC dc = CreateCompatibleDC(NULL);
    if (dc == NULL) return FALSE;
    BITMAPINFO bitmapInfo;
    ZeroMemory(&bitmapInfo, sizeof(bitmapInfo));
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = (LONG)width;
    bitmapInfo.bmiHeader.biHeight = -(LONG)height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    BYTE* pixels = NULL;
    HBITMAP bitmap = CreateDIBSection(dc, &bitmapInfo, DIB_RGB_COLORS,
        (void**)&pixels, NULL, 0);
    if (bitmap == NULL || pixels == NULL) {
        if (bitmap != NULL) DeleteObject(bitmap);
        DeleteDC(dc);
        return FALSE;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    if (oldBitmap == NULL || oldBitmap == HGDI_ERROR) {
        DeleteObject(bitmap);
        DeleteDC(dc);
        return FALSE;
    }
    ZeroMemory(pixels, (SIZE_T)width * height * 4);
    char fontFace[LF_FACESIZE];
    CopyMemory(fontFace, g_config.trueTypeFont, sizeof(fontFace));
    fontFace[LF_FACESIZE - 1] = 0;
    HFONT font = CreateFontA(-(int)fontHeight, 0, 0, 0, GetTrueTypeFontWeight(),
        IsTrueTypeFontItalic(), FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontFace);
    HFONT oldFont = font != NULL ? (HFONT)SelectObject(dc, font) : NULL;
    if (font == NULL || oldFont == NULL || oldFont == (HFONT)HGDI_ERROR) {
        if (oldFont != NULL && oldFont != (HFONT)HGDI_ERROR) SelectObject(dc, oldFont);
        if (font != NULL) DeleteObject(font);
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        return FALSE;
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    SetTextAlign(dc, TA_LEFT | TA_TOP | TA_NOUPDATECP);

    SCRIPT_STRING_ANALYSIS analysis = NULL;
    HRESULT analyzed = ScriptStringAnalyse(dc, unicode, wideLength, wideLength * 3 + 16,
        -1, SSA_GLYPHS | SSA_FALLBACK, 0, NULL, NULL, NULL, NULL, NULL, &analysis);
    BOOL rendered = analyzed == S_OK && analysis != NULL &&
        ScriptStringOut(analysis, 2, 2, 0, NULL, 0, 0, FALSE) == S_OK;
    SIZE measured = {0, 0};
    const SIZE* measuredPointer = analysis != NULL ? ScriptString_pSize(analysis) : NULL;
    if (measuredPointer != NULL) measured = *measuredPointer;
    BOOL positionsValid = rendered && wideLength == line->length;
    for (int i = 0; positionsValid && i < line->length; ++i) {
        int leading = 0;
        int trailing = 0;
        if (ScriptStringCPtoX(analysis, i, FALSE, &leading) != S_OK ||
            ScriptStringCPtoX(analysis, i, TRUE, &trailing) != S_OK ||
            leading < -32768 || leading > 32767 || trailing < -32768 || trailing > 32767) {
            positionsValid = FALSE;
            break;
        }
        line->characterStarts[i] = (SHORT)leading;
        line->characterEnds[i] = (SHORT)trailing;
    }
    if (analysis != NULL) ScriptStringFree(&analysis);

    BOOL success = FALSE;
    if (positionsValid && measured.cx > 0 && measured.cy > 0 &&
        (DWORD)measured.cx + 2 < width && (DWORD)measured.cy + 2 < height) {
        SIZE_T maskBytes = (SIZE_T)width * height;
        line->mask = (BYTE*)HeapAlloc(GetProcessHeap(), 0, maskBytes);
        if (line->mask != NULL) {
            for (SIZE_T i = 0; i < maskBytes; ++i) {
                BYTE* pixel = pixels + i * 4;
                line->mask[i] = (pixel[0] > 96 || pixel[1] > 96 || pixel[2] > 96) ? 1 : 0;
            }
            line->maskWidth = width;
            line->maskHeight = height;
            line->maskContentWidth = (DWORD)measured.cx;
            line->maskContentHeight = (DWORD)measured.cy;
            success = TRUE;
        }
    }

    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return success;
}

static BOOL CompleteTrueTypeRun(TrueTypeRenderRun* run) {
    if (run == NULL || !run->enabled) return FALSE;
    if (!run->valid) return FALSE;
    if (!run->captured) {
        InvalidateTrueTypeRun(run, "text surface was not captured");
        return FALSE;
    }
    if (run->lineCount == 0) {
        InvalidateTrueTypeRun(run, "text run contains no drawable lines");
        return FALSE;
    }
    for (unsigned i = 0; i < run->lineCount; ++i) {
        if (!CreateTrueTypeLineMask(&run->lines[i], run->fontHeight)) {
            InvalidateTrueTypeRun(run, "Uniscribe/GDI text mask creation failed");
            for (unsigned j = 0; j < run->lineCount; ++j) {
                if (run->lines[j].mask != NULL) HeapFree(GetProcessHeap(), 0, run->lines[j].mask);
                run->lines[j].mask = NULL;
            }
            return FALSE;
        }
    }

    SIZE_T rowBytes = (SIZE_T)run->captureWidth * sizeof(WORD);
    SIZE_T backupBytes = rowBytes * run->captureHeight;
    run->bitmapRendered = (WORD*)HeapAlloc(GetProcessHeap(), 0, backupBytes);
    if (run->bitmapRendered == NULL) {
        InvalidateTrueTypeRun(run, "surface backup allocation failed");
        return FALSE;
    }
    for (DWORD row = 0; row < run->captureHeight; ++row) {
        WORD* source = run->surface + (SIZE_T)(run->captureY + (int)row) * run->pitchWords + run->captureX;
        if (!IsReadableRange(source, rowBytes) || !IsWritableRange(source, rowBytes) ||
            !CopyMemorySafely(run->bitmapRendered + (SIZE_T)row * run->captureWidth, source, rowBytes)) {
            HeapFree(GetProcessHeap(), 0, run->bitmapRendered);
            run->bitmapRendered = NULL;
            InvalidateTrueTypeRun(run, "surface backup read failed");
            return FALSE;
        }
    }

    BOOL wrotePixels = TRUE;
    __try {
        for (DWORD row = 0; row < run->captureHeight; ++row) {
            WORD* destination = run->surface + (SIZE_T)(run->captureY + (int)row) * run->pitchWords + run->captureX;
            CopyMemory(destination, run->background + (SIZE_T)row * run->captureWidth, rowBytes);
        }
        for (unsigned i = 0; i < run->lineCount; ++i) {
            TrueTypeLine* line = &run->lines[i];
            DWORD sourceWidth = line->maskContentWidth;
            DWORD sourceHeight = line->maskContentHeight;
            // Keep the GDI/Uniscribe advance. Stretching the entire scratch
            // bitmap to the game's narrower FNT width smears or drops strokes.
            DWORD targetWidth = sourceWidth;
            if (targetWidth == 0 || targetWidth > 4096) continue;
            WORD colorByX[4096];
            for (DWORD x = 0; x < targetWidth; ++x) colorByX[x] = (WORD)line->color;
            for (int character = 0; character < line->length; ++character) {
                int left = line->characterStarts[character];
                int right = line->characterEnds[character];
                if (left > right) {
                    int temporary = left;
                    left = right;
                    right = temporary;
                }
                if (left < 0) left = 0;
                if (right > (int)targetWidth) right = (int)targetWidth;
                for (int x = left; x < right; ++x) {
                    colorByX[x] = line->glyphColors[character];
                }
            }
            for (DWORD y = 0; y < run->fontHeight; ++y) {
                DWORD sourceY = 2 + (DWORD)((SIZE_T)y * sourceHeight / run->fontHeight);
                if (sourceY >= line->maskHeight) continue;
                int targetY = line->y + (int)y;
                if (targetY < run->captureY || targetY >= run->captureY + (int)run->captureHeight) continue;
                for (DWORD x = 0; x < targetWidth; ++x) {
                    DWORD sourceX = 2 + x;
                    if (sourceX >= line->maskWidth || line->mask[(SIZE_T)sourceY * line->maskWidth + sourceX] == 0) continue;
                    int targetX = line->x + (int)x;
                    if (targetX < run->captureX || targetX >= run->captureX + (int)run->captureWidth) continue;
                    run->surface[(SIZE_T)targetY * run->pitchWords + targetX] = colorByX[x];
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        wrotePixels = FALSE;
        InvalidateTrueTypeRun(run, "surface became invalid while drawing");
        __try {
            for (DWORD row = 0; row < run->captureHeight; ++row) {
                WORD* destination = run->surface + (SIZE_T)(run->captureY + (int)row) * run->pitchWords + run->captureX;
                CopyMemory(destination, run->bitmapRendered + (SIZE_T)row * run->captureWidth, rowBytes);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Surface memory became invalid while drawing; no safe recovery is possible.
        }
    }

    HeapFree(GetProcessHeap(), 0, run->bitmapRendered);
    run->bitmapRendered = NULL;

    if (wrotePixels && InterlockedCompareExchange(&g_loggedTrueTypeRender, 1, 0) == 0 && g_config.debugLog) {
        char message[256];
        DWORD measuredWidth = run->lineCount != 0 ? run->lines[0].maskContentWidth : 0;
        int gameWidth = run->lineCount != 0 ? run->lines[0].right - run->lines[0].x : 0;
        wsprintfA(message,
            "ThaiFontFix: live TrueType text rendered font=%s style=%s size=%lu px lines=%u first-line=%lu px (game advance=%d px)",
            g_config.trueTypeFont, GetTrueTypeFontStyleName(), (unsigned long)run->fontHeight,
            run->lineCount, (unsigned long)measuredWidth, gameWidth);
        WriteDebugDiagnostic(message);
    }
    return wrotePixels;
}

static void LogTrueTypeFallbackIfNeeded(TrueTypeRenderRun* run, BOOL rendered) {
    if (run == NULL || !run->enabled || run->fallbackLogged || rendered || !g_config.debugLog ||
        (!run->sawGlyph && run->expectedGlyphCount == 0 && run->failureReason == NULL)) return;
    run->fallbackLogged = TRUE;
    LONG logIndex = InterlockedIncrement(&g_loggedTrueTypeRenderFallback);
    if (logIndex > 8) return;
    char message[320];
    const char* reason = run->failureReason;
    if (reason == NULL) {
        if (run->seenGlyphCount == 0) {
            reason = "no glyph callbacks matched this text context";
        } else if (run->seenGlyphCount < run->expectedGlyphCount) {
            reason = "glyph callbacks ended before the full text run";
        } else {
            reason = "run did not complete";
        }
    }
    wsprintfA(message,
        "ThaiFontFix: TrueType fallback #%lu: %s (glyphs=%lu/%lu size=%lu px code=0x%04X)",
        (unsigned long)logIndex, reason, (unsigned long)run->seenGlyphCount,
        (unsigned long)run->expectedGlyphCount, (unsigned long)run->fontHeight,
        run->failureCodeUnit);
    WriteDebugDiagnostic(message);
}

static void CleanupTrueTypeRun(TrueTypeRenderRun* run) {
    if (run == NULL) return;
    if (run->background != NULL) HeapFree(GetProcessHeap(), 0, run->background);
    run->background = NULL;
    if (run->bitmapRendered != NULL) HeapFree(GetProcessHeap(), 0, run->bitmapRendered);
    run->bitmapRendered = NULL;
    for (unsigned i = 0; i < run->lineCount; ++i) {
        if (run->lines[i].mask != NULL) HeapFree(GetProcessHeap(), 0, run->lines[i].mask);
        run->lines[i].mask = NULL;
    }
}

static BOOL EnsureTrueTypeConfiguration(const char* path) {
    if (path == NULL || path[0] == 0) return FALSE;
#if THAI_FONT_FIX_BINK_TRUETYPE_RENDER_DEFAULT
    const char* defaultRender = "1";
#else
    const char* defaultRender = "0";
#endif
    static const char missingValue[] = "__THAIFONTFIX_MISSING_6F4D7A__";
    const char* keys[] = { "Render", "Font", "Style", "Preview", "Size" };
    const char* defaults[] = { defaultRender, "Arial", "Regular", "0", "0" };
    BOOL added = FALSE;
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        char value[64];
        GetPrivateProfileStringA("TrueType", keys[i], missingValue,
            value, sizeof(value), path);
        if (lstrcmpA(value, missingValue) != 0 && value[0] != 0) continue;
        if (!WritePrivateProfileStringA("TrueType", keys[i], defaults[i], path)) {
            return FALSE;
        }
        added = TRUE;
    }
    g_trueTypeDefaultsAdded = added;
    return TRUE;
}
#endif

static void LoadConfiguration(void) {
    char path[MAX_PATH];
    path[0] = 0;
    if (!GetProxyConfigurationPath(path, MAX_PATH)) {
        WriteDebugDiagnostic("ThaiFontFix: unable to construct the proxy-specific configuration path");
        return;
    }

#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
    if (!EnsureTrueTypeConfiguration(path)) {
        OutputDebugStringA("ThaiFontFix: could not add missing TrueType defaults to thaifontfix.ini\n");
    }
#endif

    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        LogPath("ThaiFontFix: configuration file not found: ", path);
        return;
    }

    g_config.enabled = GetPrivateProfileIntA("FontFix", "Enabled", 1, path) != 0;
    g_config.centerMarks = GetPrivateProfileIntA("FontFix", "CenterMarks", 1, path) != 0;
    g_config.debugLog = GetPrivateProfileIntA("FontFix", "DebugLog", 0, path) != 0;
    g_config.toneMarkRaise = (int)GetPrivateProfileIntA("FontFix", "ToneMarkRaise", 2, path);
    g_config.maxExtraSpacing = (int)GetPrivateProfileIntA("FontFix", "MaxExtraSpacing", 8, path);
    if (g_config.toneMarkRaise < 0) g_config.toneMarkRaise = 0;
    if (g_config.toneMarkRaise > 16) g_config.toneMarkRaise = 16;
    if (g_config.maxExtraSpacing < 0) g_config.maxExtraSpacing = 0;
    if (g_config.maxExtraSpacing > 8) g_config.maxExtraSpacing = 8;

#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
    g_config.trueTypePreview = GetPrivateProfileIntA("TrueType", "Preview", 0, path) != 0;
    // The standalone Bink test build can enable the replacement by default;
    // regular release and installer builds remain opt-in.
    g_config.trueTypeRender = GetPrivateProfileIntA("TrueType", "Render",
        THAI_FONT_FIX_BINK_TRUETYPE_RENDER_DEFAULT, path) != 0;
    GetPrivateProfileStringA("TrueType", "Font", "Arial",
        g_config.trueTypeFont, LF_FACESIZE, path);
    g_config.trueTypeFont[LF_FACESIZE - 1] = 0;
    if (g_config.trueTypeFont[0] == 0) lstrcpyA(g_config.trueTypeFont, "Arial");
    char fontStyle[32];
    GetPrivateProfileStringA("TrueType", "Style", "Regular", fontStyle,
        sizeof(fontStyle), path);
    if (lstrcmpiA(fontStyle, "Bold") == 0) {
        g_config.trueTypeStyle = TRUE_TYPE_STYLE_BOLD;
    } else if (lstrcmpiA(fontStyle, "Italic") == 0) {
        g_config.trueTypeStyle = TRUE_TYPE_STYLE_ITALIC;
    } else if (lstrcmpiA(fontStyle, "BoldItalic") == 0 ||
        lstrcmpiA(fontStyle, "Bold Italic") == 0) {
        g_config.trueTypeStyle = TRUE_TYPE_STYLE_BOLD_ITALIC;
    } else {
        g_config.trueTypeStyle = TRUE_TYPE_STYLE_REGULAR;
    }
    g_config.trueTypeSize = (int)GetPrivateProfileIntA("TrueType", "Size", 0, path);
    if (g_config.trueTypeSize != 0 &&
        (g_config.trueTypeSize < 8 || g_config.trueTypeSize > 64)) {
        g_config.trueTypeSize = 0;
    }
#endif

#if defined(THAI_FONT_PROXY_DDRAW)
    GetPrivateProfileStringA("Proxy", "CustomDll", "ddraw-custom.dll",
        g_customDdrawSetting, MAX_PATH, path);
#endif

    if (g_config.debugLog) {
        if (!ClearDebugLog()) {
            OutputDebugStringA("ThaiFontFix: could not clear thaifontfix.log at startup\n");
        }
        char proxyPath[MAX_PATH];
        DWORD proxyLength = GetModuleFileNameA(g_selfModule, proxyPath, MAX_PATH);
        if (proxyLength != 0 && proxyLength < MAX_PATH) LogPath("ThaiFontFix: proxy module: ", proxyPath);
        LogPath("ThaiFontFix: configuration file: ", path);
        WriteDebugDiagnostic(g_config.enabled
            ? "ThaiFontFix: configuration loaded; font fix enabled"
            : "ThaiFontFix: configuration loaded; font fix disabled");
#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
        if (g_config.trueTypePreview) {
            char message[LF_FACESIZE + 96];
            wsprintfA(message, "ThaiFontFix: Bink TrueType preview enabled; font=%s style=%s",
                g_config.trueTypeFont, GetTrueTypeFontStyleName());
            WriteDebugDiagnostic(message);
        }
        if (g_config.trueTypeRender) {
            char message[LF_FACESIZE + 160];
            char sizeLabel[24];
            if (g_config.trueTypeSize == 0) {
                lstrcpyA(sizeLabel, "game height");
            } else {
                wsprintfA(sizeLabel, "%d px", g_config.trueTypeSize);
            }
            wsprintfA(message, "ThaiFontFix: Bink live TrueType rendering enabled for all text; font=%s style=%s size=%s",
                g_config.trueTypeFont, GetTrueTypeFontStyleName(), sizeLabel);
            WriteDebugDiagnostic(message);
        }
        if (g_trueTypeDefaultsAdded) {
            WriteDebugDiagnostic("ThaiFontFix: added missing TrueType defaults to thaifontfix.ini");
        }
#endif
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

#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
static LayoutContext* FindTrueTypeContextForFont(LayoutContext* context, void* font) {
    for (; context != NULL; context = context->previous) {
        if (context->font == font && context->trueType.enabled) return context;
    }
    return NULL;
}
#endif

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

#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
    InitializeTrueTypeRun(&context.trueType, font, text, x, y, maxWidth, maxHeight);
#endif

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

#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
    TryCreateTrueTypePreview(text, context.state);
#endif

    int result = original(font, target, text, x, y, maxWidth, maxHeight, flags, param9, param10);

    FlushPendingTone((RenderFunction)g_renderTrampoline, &context, FALSE);
#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
    LogTrueTypeFallbackIfNeeded(&context.trueType, context.trueType.rendered);
    CleanupTrueTypeRun(&context.trueType);
#endif
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

#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
    InitializeTrueTypeRun(&context.trueType, font, text, x, y, maxWidth, maxHeight);
#endif

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

#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
    TryCreateTrueTypePreview(text, context.state);
#endif

    int result = original(font, text, x, y, maxWidth, maxHeight);

    FlushPendingTone((RenderFunction)g_renderTrampoline, &context, FALSE);
#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
    LogTrueTypeFallbackIfNeeded(&context.trueType, context.trueType.rendered);
    CleanupTrueTypeRun(&context.trueType);
#endif
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
#if defined(THAI_FONT_FIX_BINK_TRUETYPE)
    LayoutContext* trueTypeContext = FindTrueTypeContextForFont(g_currentContext, font);
    if (trueTypeContext == NULL && renderData != NULL) {
        for (LayoutContext* item = g_currentContext; item != NULL; item = item->previous) {
            if (item->trueType.enabled && item->font != NULL &&
                IsReadableRange((BYTE*)item->font + 4, sizeof(BYTE*)) &&
                *(BYTE**)((BYTE*)item->font + 4) == renderData) {
                trueTypeContext = item;
                break;
            }
        }
    }
    if (trueTypeContext != NULL) {
        RecordTrueTypeGlyph(&trueTypeContext->trueType, font, code, x, y, color);
        if (trueTypeContext->trueType.enabled) {
            int result = original(font, code, x, y, color);
            TrueTypeRenderRun* run = &trueTypeContext->trueType;
            if (!run->completed && run->valid && run->expectedGlyphCount != 0 &&
                run->seenGlyphCount == run->expectedGlyphCount) {
                run->completed = TRUE;
                run->rendered = CompleteTrueTypeRun(run);
                LogTrueTypeFallbackIfNeeded(run, run->rendered);
            }
            LeaveCriticalSection(&g_fontLock);
            return result;
        }
    }
#endif
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
