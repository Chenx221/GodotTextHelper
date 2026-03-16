#include "pch.h"
#include "config.h"
#include "hooks.h"
#include "utils.h"
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <set>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>

constexpr int CLIPBOARD_TIMEOUT_MS = 300;

bool g_EnableClipboard = false;
bool g_EnableFunctionLog = false;
bool g_FilterDuplicateFunctionLog = false;
bool g_builtinFunctionNameUTF16 = false;
size_t g_gdscriptInstanceOffset = 0x10;
size_t g_gdscriptPathOffset = 0x250;
std::vector<HookRule> g_HookRules;

GDScriptCall_t g_OriginalGDScriptCall = nullptr;

std::mutex g_ClipboardMutex;
std::string g_ClipboardBuffer;
std::chrono::steady_clock::time_point g_LastTextTime;
bool g_HasPendingText = false;

std::mutex g_FunctionLogMutex;
std::set<std::string> g_LoggedFunctionNames;

std::atomic<bool> g_ThreadRunning{false};
std::thread g_ClipboardThread;

// HS65001#-1C@0:winmm.dll:hookme
extern "C" __declspec(dllexport) __declspec(noinline) void hookme(const char* text) {
    if (!text) return;

    volatile int dummy = 0;
    for (const char* p = text; *p; ++p) {
        dummy += *p;
    }

    if (dummy > 0x7FFFFFFF) {
        OutputDebugStringA("[ProcessText] Overflow protection\n");
    }
}

static void WriteToClipboard(const std::string& text) {
    if (text.empty()) return;

    int wideSize = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (wideSize <= 0) {
        OutputDebugStringA("[Clipboard] Failed to convert UTF-8 to UTF-16\n");
        return;
    }

    std::vector<wchar_t> wideText(wideSize);
    if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wideText.data(), wideSize) <= 0) {
        OutputDebugStringA("[Clipboard] UTF-8 to UTF-16 conversion failed\n");
        return;
    }

    if (!OpenClipboard(NULL)) {
        OutputDebugStringA("[Clipboard] Failed to open clipboard\n");
        return;
    }

    EmptyClipboard();

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wideSize * sizeof(wchar_t));
    if (hMem) {
        wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
        if (pMem) {
            memcpy(pMem, wideText.data(), wideSize * sizeof(wchar_t));
            GlobalUnlock(hMem);

            if (SetClipboardData(CF_UNICODETEXT, hMem)) {
            } else {
                GlobalFree(hMem);
                OutputDebugStringA("[Clipboard] Failed to set clipboard data\n");
            }
        } else {
            GlobalFree(hMem);
            OutputDebugStringA("[Clipboard] Failed to lock global memory\n");
        }
    } else {
        OutputDebugStringA("[Clipboard] Failed to allocate global memory\n");
    }

    CloseClipboard();
}

static void CheckAndFlushClipboard() {
    if (!g_HasPendingText) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_LastTextTime).count();

    if (elapsed >= CLIPBOARD_TIMEOUT_MS) {
        std::lock_guard<std::mutex> lock(g_ClipboardMutex);

        if (!g_ClipboardBuffer.empty()) {
            hookme(g_ClipboardBuffer.c_str());

            if (g_EnableClipboard) {
                WriteToClipboard(g_ClipboardBuffer);
            }

            g_ClipboardBuffer.clear();
            g_HasPendingText = false;
        }
    }
}

static void AddTextToClipboard(const std::string& text) {
    if (text.empty()) return;

    std::lock_guard<std::mutex> lock(g_ClipboardMutex);

    if (!g_ClipboardBuffer.empty()) {
        g_ClipboardBuffer += "\n";
    }
    g_ClipboardBuffer += text;
    g_LastTextTime = std::chrono::steady_clock::now();
    g_HasPendingText = true;
}

static const HookRule* FindMatchingRule(const std::string& scriptPath, const std::string& functionName) {
    for (const auto& rule : g_HookRules) {
        if (rule.functionName != functionName) {
            continue;
        }

        if (!rule.scriptPath.empty()) {
            if (scriptPath.find(rule.scriptPath) == std::string::npos) {
                continue;
            }
        }

        return &rule;
    }

    return nullptr;
}

static std::filesystem::path GetFunctionLogPath() {
    HMODULE hModule = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&GetFunctionLogPath),
            &hModule)) {
        return std::filesystem::path("function.log");
    }

    char modulePath[MAX_PATH] = {};
    if (GetModuleFileNameA(hModule, modulePath, MAX_PATH) == 0) {
        return std::filesystem::path("function.log");
    }

    return std::filesystem::path(modulePath).parent_path() / "function.log";
}

static void LogFunctionName(const std::string& functionName) {
    if (functionName.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_FunctionLogMutex);

    if (g_FilterDuplicateFunctionLog) {
        if (!g_LoggedFunctionNames.insert(functionName).second) {
            return;
        }
    }

    std::ofstream output(GetFunctionLogPath(), std::ios::app);
    if (!output.is_open()) {
        return;
    }

    output << functionName << '\n';
}

Variant* __fastcall GDScriptCall_Detour(
    GDScriptInstance* thisptr,
    Variant* retstr,
    const StringName* p_method,
    const Variant** p_args,
    int p_argcount,
    CallError* r_error
) {
    const HookRule* rule = nullptr;

    try {
        CheckAndFlushClipboard();

        std::string functionName = GetFunctionName(p_method);

        if (g_EnableFunctionLog) {
            LogFunctionName(functionName);
        }

        if (g_HookRules.empty()) {
            return g_OriginalGDScriptCall(thisptr, retstr, p_method, p_args, p_argcount, r_error);
        }

        std::string scriptPath = GetScriptPath(thisptr);
        rule = FindMatchingRule(scriptPath, functionName);

        if (rule) {
            if (p_args && p_argcount > 0 && !rule->postHook) {
                if (!rule->argIndices.empty()) {
                    for (int idx : rule->argIndices) {
                        if (idx >= 0 && idx < p_argcount && p_args[idx]) {
                            std::string strValue = ExtractStringFromVariant(p_args[idx]);

                            if (!strValue.empty()) {
                                AddTextToClipboard(strValue);
                            }
                        }
                    }
                } else {
                    for (int i = 0; i < p_argcount; i++) {
                        if (p_args[i]) {
                            std::string strValue = ExtractStringFromVariant(p_args[i]);

                            if (!strValue.empty()) {
                                AddTextToClipboard(strValue);
                            }
                        }
                    }
                }
            }
        }
    } catch (...) {
        OutputDebugStringA("[GDScript] Exception in detour function\n");
    }

	Variant* result = g_OriginalGDScriptCall(thisptr, retstr, p_method, p_args, p_argcount, r_error);

	try {
		if (rule && rule->postHook && result) {
			std::string strValue = ExtractStringFromVariant(result);

			if (!strValue.empty()) {
				AddTextToClipboard(strValue);
			}
		}
	}
	catch (...) {
		OutputDebugStringA("[GDScript] Exception while intercepting return value\n");
	}

	return result;
}

bool SetupAllHooks() {
    OutputDebugStringA("[Hook] Setting up hooks...\n");
    LoadConfiguration(g_EnableClipboard, g_EnableFunctionLog, g_FilterDuplicateFunctionLog, g_builtinFunctionNameUTF16, g_gdscriptInstanceOffset, g_gdscriptPathOffset, g_HookRules);

    {
        std::lock_guard<std::mutex> lock(g_FunctionLogMutex);
        g_LoggedFunctionNames.clear();
    }

    g_ThreadRunning = true;
    g_ClipboardThread = std::thread([]() {
        while (g_ThreadRunning) {
            CheckAndFlushClipboard();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    const char* signatures[] = {


        // godot 3.6.3 x64 self-build (non-optimized)
        "48 89 5C 24 ?? 57 48 83 EC ?? 4C 8B 51 ?? 4D 8B D8"
    };

    const int signatureCount = sizeof(signatures) / sizeof(signatures[0]);
    const char* targetModule = nullptr;
    const char* searchModuleName = targetModule;
    char mainModuleName[MAX_PATH];

    if (!targetModule) {
        HMODULE hMainModule = GetModuleHandleA(nullptr);
        GetModuleFileNameA(hMainModule, mainModuleName, MAX_PATH);
        char* fileName = strrchr(mainModuleName, '\\');
        searchModuleName = fileName ? fileName + 1 : mainModuleName;

        char infoBuffer[512];
        sprintf_s(infoBuffer, "[Hook] Searching in main module: %s\n", searchModuleName);
        OutputDebugStringA(infoBuffer);
    }

    void* targetAddr = nullptr;
    int usedSignatureIndex = -1;

    for (int i = 0; i < signatureCount; i++) {
        char tryBuffer[512];
        sprintf_s(tryBuffer, "[Hook] Trying signature #%d: %s\n", i + 1, signatures[i]);
        OutputDebugStringA(tryBuffer);

        targetAddr = FindPatternInModule(searchModuleName, signatures[i]);

        if (targetAddr) {
            usedSignatureIndex = i;
            char successBuffer[512];
            sprintf_s(successBuffer, "[Hook] Found target function using signature #%d at: 0x%p\n", 
                     i + 1, targetAddr);
            OutputDebugStringA(successBuffer);
            break;
        } else {
            char failBuffer[256];
            sprintf_s(failBuffer, "[Hook] Signature #%d not found, trying next...\n", i + 1);
            OutputDebugStringA(failBuffer);
        }
    }

    if (!targetAddr) {
        char errorBuffer[512];
        sprintf_s(errorBuffer, "[Hook] Failed to find target function after trying all %d signatures in module: %s\n", 
                 signatureCount, searchModuleName);
        OutputDebugStringA(errorBuffer);
        return false;
    }

    g_OriginalGDScriptCall = (GDScriptCall_t)targetAddr;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    LONG error = DetourAttach(&(PVOID&)g_OriginalGDScriptCall, GDScriptCall_Detour);

    if (DetourTransactionCommit() != NO_ERROR) {
        char errorBuffer[256];
        sprintf_s(errorBuffer, "[Hook] Failed to attach detour. Error code: %ld\n", error);
        OutputDebugStringA(errorBuffer);
        return false;
    }

    OutputDebugStringA("[Hook] Target function hooked successfully!\n");
    OutputDebugStringA("[Hook] All hooks set up successfully\n");

    if (g_HookRules.empty()) {
        OutputDebugStringA("[Hook] No hook rules configured. Nothing will be monitored.\n");
    } else {
        char configBuffer[512];
        sprintf_s(configBuffer, "[Hook] Loaded %zu hook rule(s):\n", g_HookRules.size());
        OutputDebugStringA(configBuffer);

        for (size_t i = 0; i < g_HookRules.size(); i++) {
            const auto& rule = g_HookRules[i];

            if (rule.scriptPath.empty()) {
                sprintf_s(configBuffer, "  [%zu] Function: %s", 
                         i + 1, rule.functionName.c_str());
            } else {
                sprintf_s(configBuffer, "  [%zu] Path: %s, Function: %s", 
                         i + 1, rule.scriptPath.c_str(), rule.functionName.c_str());
            }
            OutputDebugStringA(configBuffer);

            if (rule.argIndices.empty()) {
                OutputDebugStringA(", Args: ALL strings\n");
            } else {
                OutputDebugStringA(", Args: {");
                bool first = true;
                for (int idx : rule.argIndices) {
                    if (!first) OutputDebugStringA(", ");
                    sprintf_s(configBuffer, "%d", idx);
                    OutputDebugStringA(configBuffer);
                    first = false;
                }
                OutputDebugStringA("}\n");
            }
        }
    }

    return true;
}

void CleanupAllHooks() {
    OutputDebugStringA("[Hook] Cleaning up hooks...\n");

    g_ThreadRunning = false;
    if (g_ClipboardThread.joinable()) {
        g_ClipboardThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(g_FunctionLogMutex);
        g_LoggedFunctionNames.clear();
    }

    if (g_HasPendingText) {
        std::lock_guard<std::mutex> lock(g_ClipboardMutex);
        if (!g_ClipboardBuffer.empty()) {
            hookme(g_ClipboardBuffer.c_str());

            if (g_EnableClipboard) {
                WriteToClipboard(g_ClipboardBuffer);
            }

            g_ClipboardBuffer.clear();
            g_HasPendingText = false;
        }
    }

    if (g_OriginalGDScriptCall) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&)g_OriginalGDScriptCall, GDScriptCall_Detour);

        if (DetourTransactionCommit() == NO_ERROR) {
            OutputDebugStringA("[Hook] GDScriptCallp detached successfully\n");
        } else {
            OutputDebugStringA("[Hook] Failed to detach GDScriptCallp\n");
        }

        g_OriginalGDScriptCall = nullptr;
    }

    OutputDebugStringA("[Hook] All hooks cleaned up\n");
}
