// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "hooks.h"
#include <Psapi.h>

// 使用 pragma 导出转发到系统的 dinput8.dll
// 注意：需要先删除或注释掉 dinput8.def 文件中的EXPORTS部分
#pragma comment(linker, "/export:DirectInput8Create=C:\\Windows\\System32\\dinput8.DirectInput8Create")
#pragma comment(linker, "/export:DllCanUnloadNow=C:\\Windows\\System32\\dinput8.DllCanUnloadNow,PRIVATE")
#pragma comment(linker, "/export:DllGetClassObject=C:\\Windows\\System32\\dinput8.DllGetClassObject,PRIVATE")
#pragma comment(linker, "/export:DllRegisterServer=C:\\Windows\\System32\\dinput8.DllRegisterServer,PRIVATE")
#pragma comment(linker, "/export:DllUnregisterServer=C:\\Windows\\System32\\dinput8.DllUnregisterServer,PRIVATE")


// 初始化函数
bool Initialize() {
    OutputDebugStringA("[DInput8 Proxy] Initializing hooks\n");

    // 设置 hooks
    return SetupAllHooks();
}

// 清理函数
void Cleanup() {
    CleanupAllHooks();
    OutputDebugStringA("[DInput8 Proxy] Cleanup complete\n");
}


BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        Initialize();
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        Cleanup();
        break;
    }
    return TRUE;
}

