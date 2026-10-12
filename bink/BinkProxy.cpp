#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "BinkExports.generated.h"

extern "C" void __cdecl ThaiFontFixInitialize(void);

static HMODULE g_proxyModule;
static HMODULE g_realBink;
static volatile LONG g_realBinkInit;
static FARPROC g_binkForwarders[BINK_EXPORT_COUNT];

static BOOL BuildRealBinkPath(char* path, SIZE_T capacity) {
    if (path == NULL || capacity == 0 || g_proxyModule == NULL) return FALSE;
    DWORD length = GetModuleFileNameA(g_proxyModule, path, (DWORD)capacity);
    if (length == 0 || length >= capacity) return FALSE;
    while (length != 0 && path[length - 1] != '\\' && path[length - 1] != '/') --length;
    static const char realName[] = "binkw32.bin";
    if (length == 0 || (SIZE_T)length + sizeof(realName) > capacity) return FALSE;
    CopyMemory(path + length, realName, sizeof(realName));
    return TRUE;
}

static void EnsureRealBinkLoaded(void) {
    LONG state = InterlockedCompareExchange(&g_realBinkInit, 1, 0);
    if (state == 0) {
        char path[MAX_PATH];
        if (BuildRealBinkPath(path, MAX_PATH)) {
            g_realBink = LoadLibraryA(path);
            if (g_realBink == g_proxyModule) {
                FreeLibrary(g_realBink);
                g_realBink = NULL;
            }
        }
        if (g_realBink == NULL) {
            OutputDebugStringA("ThaiFontFix: could not load the renamed original binkw32.bin\n");
        }
        InterlockedExchange(&g_realBinkInit, g_realBink != NULL ? 2 : 3);
    } else {
        while ((state = InterlockedCompareExchange(&g_realBinkInit, 2, 2)) == 1) Sleep(0);
    }
}

static void ResolveBinkForwarder(unsigned index) {
    ThaiFontFixInitialize();
    if (index >= BINK_EXPORT_COUNT) ExitProcess(ERROR_INVALID_PARAMETER);
    if (g_binkForwarders[index] != NULL) return;

    EnsureRealBinkLoaded();
    if (g_realBink != NULL) {
        FARPROC function = GetProcAddress(g_realBink, g_binkExportNames[index]);
        InterlockedCompareExchangePointer((PVOID volatile*)&g_binkForwarders[index], (PVOID)function, NULL);
    }
    if (g_binkForwarders[index] == NULL) {
        OutputDebugStringA("ThaiFontFix: could not resolve an original Bink export\n");
        ExitProcess(ERROR_PROC_NOT_FOUND);
    }
}

#define BINK_PROXY(index, offset) \
    extern "C" __declspec(naked) void WINAPI BinkProxy##index(void) { \
        __asm { pushfd } \
        __asm { pushad } \
        __asm { push index } \
        __asm { call ResolveBinkForwarder } \
        __asm { add esp, 4 } \
        __asm { popad } \
        __asm { popfd } \
        __asm { jmp dword ptr [g_binkForwarders + offset] } \
    }

#include "BinkProxyFunctions.generated.inc"
#undef BINK_PROXY

static DWORD WINAPI InitializeThaiFontFix(LPVOID) {
    ThaiFontFixInitialize();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_proxyModule = instance;
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(NULL, 0, InitializeThaiFontFix, NULL, 0, NULL);
        if (thread != NULL) {
            CloseHandle(thread);
        } else {
            OutputDebugStringA("ThaiFontFix: Bink proxy could not start its initialization thread\n");
        }
    }
    return TRUE;
}
