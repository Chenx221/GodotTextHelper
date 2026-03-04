#include "pch.h"
#include "hooks.h"
#include "utils.h"
#include <string>
#include <set>
#include <vector>
#include <mutex>
#include <chrono>

constexpr bool ENABLE_CLIPBOARD = false;
constexpr int CLIPBOARD_TIMEOUT_MS = 300;

std::vector<HookRule> g_HookRules = {
    HookRule("_mark_as_read"),
};

GDScriptCallp_t g_OriginalGDScriptCallp = nullptr;

std::mutex g_ClipboardMutex;
std::string g_ClipboardBuffer;
std::chrono::steady_clock::time_point g_LastTextTime;
bool g_HasPendingText = false;

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

void WriteToClipboard(const std::string& text) {
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

void CheckAndFlushClipboard() {
    if (!g_HasPendingText) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_LastTextTime).count();

    if (elapsed >= CLIPBOARD_TIMEOUT_MS) {
        std::lock_guard<std::mutex> lock(g_ClipboardMutex);

        if (!g_ClipboardBuffer.empty()) {
            hookme(g_ClipboardBuffer.c_str());

            if (ENABLE_CLIPBOARD) {
                WriteToClipboard(g_ClipboardBuffer);
            }

            g_ClipboardBuffer.clear();
            g_HasPendingText = false;
        }
    }
}

void AddTextToClipboard(const std::string& text) {
    if (text.empty()) return;

    std::lock_guard<std::mutex> lock(g_ClipboardMutex);

    CheckAndFlushClipboard();

    if (!g_ClipboardBuffer.empty()) {
        g_ClipboardBuffer += "\n";
    }
    g_ClipboardBuffer += text;
    g_LastTextTime = std::chrono::steady_clock::now();
    g_HasPendingText = true;
}

const HookRule* FindMatchingRule(const std::string& scriptPath, const std::string& functionName) {
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

Variant* __fastcall GDScriptCallp_Detour(
    Variant* retstr,
    GDScriptInstance* thisptr,
    const StringName* p_method,
    const Variant** p_args,
    int p_argcount,
    CallError* r_error
) {
    const HookRule* rule = nullptr;

    try {
        CheckAndFlushClipboard();

        if (g_HookRules.empty()) {
            return g_OriginalGDScriptCallp(retstr, thisptr, p_method, p_args, p_argcount, r_error);
        }

        std::string functionName = GetFunctionName(p_method);
        std::string scriptPath = GetScriptPath(thisptr);
        rule = FindMatchingRule(scriptPath, functionName);

        if (rule) {
            char buffer[2048];

            if (rule->scriptPath.empty()) {
                sprintf_s(buffer, "[GDScript] Function: %s, ArgCount: %d\n", 
                         functionName.c_str(), p_argcount);
            } else {
                sprintf_s(buffer, "[GDScript] Path: %s\n  Function: %s, ArgCount: %d\n", 
                         scriptPath.c_str(), functionName.c_str(), p_argcount);
            }
            OutputDebugStringA(buffer);

            if (p_args && p_argcount > 0 && !rule->postHook) {
                if (!rule->argIndices.empty()) {
                    for (int idx : rule->argIndices) {
                        if (idx >= 0 && idx < p_argcount && p_args[idx]) {
                            std::string strValue = ExtractStringFromVariant(p_args[idx]);

                            if (!strValue.empty()) {
                                sprintf_s(buffer, "  [Arg %d] String: \"%s\"\n", 
                                         idx, strValue.c_str());
                                OutputDebugStringA(buffer);
                                AddTextToClipboard(strValue);
                            }
                        }
                    }
                } else {
                    for (int i = 0; i < p_argcount; i++) {
                        if (p_args[i]) {
                            std::string strValue = ExtractStringFromVariant(p_args[i]);

                            if (!strValue.empty()) {
                                sprintf_s(buffer, "  [Arg %d] String: \"%s\"\n", 
                                         i, strValue.c_str());
                                OutputDebugStringA(buffer);
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

	Variant* result = g_OriginalGDScriptCallp(retstr, thisptr, p_method, p_args, p_argcount, r_error);

	try {
		if (rule && rule->postHook && result) {
			std::string strValue = ExtractStringFromVariant(result);

			if (!strValue.empty()) {
				char buffer[2048];
				sprintf_s(buffer, "  [Return] String: \"%s\"\n", strValue.c_str());
				OutputDebugStringA(buffer);
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

    const char* signatures[] = {
		// godot 4.3.1 x64 self-build (non-optimized)
        "41 57 41 56 41 55 41 54 55 57 56 53 48 83 EC ?? 48 8B 05 ?? ?? ?? ?? ?? ?? ?? 4C 8B A4 24",
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

    g_OriginalGDScriptCallp = (GDScriptCallp_t)targetAddr;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    LONG error = DetourAttach(&(PVOID&)g_OriginalGDScriptCallp, GDScriptCallp_Detour);

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

    if (g_HasPendingText) {
        std::lock_guard<std::mutex> lock(g_ClipboardMutex);
        if (!g_ClipboardBuffer.empty()) {
            hookme(g_ClipboardBuffer.c_str());

            if (ENABLE_CLIPBOARD) {
                WriteToClipboard(g_ClipboardBuffer);
            }

            g_ClipboardBuffer.clear();
            g_HasPendingText = false;
        }
    }

    if (g_OriginalGDScriptCallp) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&)g_OriginalGDScriptCallp, GDScriptCallp_Detour);

        if (DetourTransactionCommit() == NO_ERROR) {
            OutputDebugStringA("[Hook] GDScriptCallp detached successfully\n");
        } else {
            OutputDebugStringA("[Hook] Failed to detach GDScriptCallp\n");
        }

        g_OriginalGDScriptCallp = nullptr;
    }

    OutputDebugStringA("[Hook] All hooks cleaned up\n");
}
