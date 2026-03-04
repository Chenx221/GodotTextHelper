#include "pch.h"
#include "hooks.h"
#include <string>
#include <set>
#include <vector>
#include <mutex>
#include <chrono>

constexpr bool ENABLE_CLIPBOARD = true;
constexpr int CLIPBOARD_TIMEOUT_MS = 1000;

std::vector<HookRule> g_HookRules = {
    // 示例 1: 只监控函数名，不限制路径，提取所有字符串参数
    // HookRule("print"),

    // 示例 2: 监控函数名，只提取指定索引的参数
    // HookRule("my_function", {0, 2}),  // 只提取第 0 和第 2 个参数

    // 示例 3: 限制脚本路径 + 函数名 + 参数索引
    // HookRule("res://scripts/player.gd", "take_damage", {0}),  // 只监控 player.gd 中的 take_damage 函数的第 0 个参数

    // 示例 4: 多个规则
    // HookRule("print"),
    // HookRule("push_warning", {0}),
    // HookRule("res://scripts/game_manager.gd", "log_event", {0, 1}),
};

GDScriptCallp_t g_OriginalGDScriptCallp = nullptr;

std::mutex g_ClipboardMutex;
std::string g_ClipboardBuffer;
std::chrono::steady_clock::time_point g_LastTextTime;
bool g_HasPendingText = false;

__declspec(dllexport) __declspec(noinline) void ProcessTextForClipboard(const char* text) {
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
    if (text.empty() || !ENABLE_CLIPBOARD) return;

    ProcessTextForClipboard(text.c_str());

    if (OpenClipboard(NULL)) {
        EmptyClipboard();

        size_t size = (text.length() + 1) * sizeof(char);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);

        if (hMem) {
            char* pMem = (char*)GlobalLock(hMem);
            if (pMem) {
                memcpy(pMem, text.c_str(), size);
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
            }
        }

        CloseClipboard();
        OutputDebugStringA("[Clipboard] Text written to clipboard\n");
    }
}

void CheckAndFlushClipboard() {
    if (!g_HasPendingText || !ENABLE_CLIPBOARD) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_LastTextTime).count();

    if (elapsed >= CLIPBOARD_TIMEOUT_MS) {
        std::lock_guard<std::mutex> lock(g_ClipboardMutex);

        if (!g_ClipboardBuffer.empty()) {
            WriteToClipboard(g_ClipboardBuffer);
            g_ClipboardBuffer.clear();
            g_HasPendingText = false;
        }
    }
}

void AddTextToClipboard(const std::string& text) {
    if (!ENABLE_CLIPBOARD || text.empty()) return;

    std::lock_guard<std::mutex> lock(g_ClipboardMutex);

    CheckAndFlushClipboard();

    if (!g_ClipboardBuffer.empty()) {
        g_ClipboardBuffer += "\n";
    }
    g_ClipboardBuffer += text;
    g_LastTextTime = std::chrono::steady_clock::now();
    g_HasPendingText = true;
}

std::string UTF32ToUTF8(const char32_t* utf32_str) {
    if (!utf32_str) return "";

    try {
        // 计算 UTF-32 字符串长度
        size_t len = 0;
        while (utf32_str[len] != U'\0') {
            len++;
        }

        if (len == 0) return "";

        // 先转为 UTF-16 (wchar_t)
        std::wstring utf16;
        utf16.reserve(len);

        for (size_t i = 0; i < len; i++) {
            char32_t ch = utf32_str[i];
            if (ch <= 0xFFFF) {
                // BMP 字符，直接转换
                utf16 += (wchar_t)ch;
            } else {
                // 超出 BMP 范围，需要代理对
                ch -= 0x10000;
                utf16 += (wchar_t)((ch >> 10) + 0xD800);
                utf16 += (wchar_t)((ch & 0x3FF) + 0xDC00);
            }
        }

        // 使用 WideCharToMultiByte 转为 UTF-8
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, utf16.c_str(), (int)utf16.length(), 
                                              NULL, 0, NULL, NULL);
        if (size_needed <= 0) {
            return "[UTF32->UTF8 Error]";
        }

        std::string utf8(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, utf16.c_str(), (int)utf16.length(), 
                           &utf8[0], size_needed, NULL, NULL);

        return utf8;
    } catch (...) {
        return "[UTF32 Conversion Error]";
    }
}

std::string GetScriptPath(const GDScriptInstance* instance) {
    if (!instance) return "[null_instance]";

    try {
        void** ptr1 = (void**)((BYTE*)instance + 0x18);
        if (!ptr1 || !*ptr1) return "[null_ptr1]";

        void** ptr2 = (void**)((BYTE*)*ptr1 + 0x3C0);
        if (!ptr2 || !*ptr2) return "[null_ptr2]";

        const char32_t* path = (const char32_t*)*ptr2;
        if (!path) return "[null_path]";

        return UTF32ToUTF8(path);
    } catch (...) {
        return "[path_parse_error]";
    }
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

std::string GetFunctionName(const StringName* p_method) {
    if (!p_method) return "[null]";

    try {
        if (p_method->builtin_str) {
            const char* builtin = (const char*)p_method->builtin_str;
            if (builtin && builtin[0] != '\0') {
                return std::string(builtin);
            }
        }

        if (p_method->custom_str) {
            const char32_t* custom = (const char32_t*)p_method->custom_str;
            if (custom && custom[0] != U'\0') {
                return UTF32ToUTF8(custom);
            }
        }
    } catch (...) {
        return "[name_parse_error]";
    }

    return "[unknown]";
}

std::string ExtractStringFromVariant(const Variant* variant) {
    if (!variant) return "[null_variant]";

    try {
        if (variant->type != 0x4) {
            return "";
        }

        if (variant->data) {
            const char32_t* str_data = (const char32_t*)variant->data;
            return UTF32ToUTF8(str_data);
        }
    } catch (...) {
        return "[variant_parse_error]";
    }

    return "";
}

Variant* __fastcall GDScriptCallp_Detour(
    Variant* retstr,
    GDScriptInstance* thisptr,
    const StringName* p_method,
    const Variant** p_args,
    int p_argcount,
    CallError* r_error
) {
    try {
        CheckAndFlushClipboard();

        if (g_HookRules.empty()) {
            return g_OriginalGDScriptCallp(retstr, thisptr, p_method, p_args, p_argcount, r_error);
        }

        std::string functionName = GetFunctionName(p_method);
        std::string scriptPath = GetScriptPath(thisptr);
        const HookRule* rule = FindMatchingRule(scriptPath, functionName);

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

            if (p_args && p_argcount > 0) {
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

    return g_OriginalGDScriptCallp(retstr, thisptr, p_method, p_args, p_argcount, r_error);
}

bool SetupAllHooks() {
    OutputDebugStringA("[Hook] Setting up hooks...\n");

    const char* signatures[] = {
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

    if (g_HasPendingText && ENABLE_CLIPBOARD) {
        std::lock_guard<std::mutex> lock(g_ClipboardMutex);
        if (!g_ClipboardBuffer.empty()) {
            WriteToClipboard(g_ClipboardBuffer);
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
