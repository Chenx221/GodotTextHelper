#include "pch.h"
#include "hooks.h"
#include <string>
#include <set>
#include <vector>

// ==========================================
// 配置区域
// ==========================================

// Hook 规则列表
// 留空则不监控任何函数
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

// ==========================================
// 原始函数指针
// ==========================================

GDScriptCallp_t g_OriginalGDScriptCallp = nullptr;

// ==========================================
// 辅助函数
// ==========================================

// 将 UTF-32 (char32_t*) 转换为 UTF-8 (std::string)
// 使用 Windows API 代替已弃用的 codecvt
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

// 从 GDScriptInstance 提取脚本路径
// 根据注释: [[rdx+0x18]+0x3C0] 可获取脚本路径
std::string GetScriptPath(const GDScriptInstance* instance) {
    if (!instance) return "[null_instance]";

    try {
        // 读取 instance + 0x18
        void** ptr1 = (void**)((BYTE*)instance + 0x18);
        if (!ptr1 || !*ptr1) return "[null_ptr1]";

        // 读取 [ptr1] + 0x3C0
        void** ptr2 = (void**)((BYTE*)*ptr1 + 0x3C0);
        if (!ptr2 || !*ptr2) return "[null_ptr2]";

        // 假设路径是 UTF-32 字符串
        const char32_t* path = (const char32_t*)*ptr2;
        if (!path) return "[null_path]";

        return UTF32ToUTF8(path);
    } catch (...) {
        return "[path_parse_error]";
    }
}

// 检查规则是否匹配
const HookRule* FindMatchingRule(const std::string& scriptPath, const std::string& functionName) {
    for (const auto& rule : g_HookRules) {
        // 检查函数名是否匹配
        if (rule.functionName != functionName) {
            continue;
        }

        // 如果规则指定了路径，检查路径是否匹配
        if (!rule.scriptPath.empty()) {
            if (scriptPath.find(rule.scriptPath) == std::string::npos) {
                continue;  // 路径不匹配
            }
        }

        // 找到匹配的规则
        return &rule;
    }

    return nullptr;  // 没有匹配的规则
}

// 从 StringName 提取函数名
std::string GetFunctionName(const StringName* p_method) {
    if (!p_method) return "[null]";

    try {
        // 先尝试内置函数名 (0x8 偏移, UTF-8)
        if (p_method->builtin_str) {
            const char* builtin = (const char*)p_method->builtin_str;
            if (builtin && builtin[0] != '\0') {
                return std::string(builtin);
            }
        }

        // 再尝试自定义函数名 (0x10 偏移, UTF-32)
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

// 从 Variant 提取字符串
std::string ExtractStringFromVariant(const Variant* variant) {
    if (!variant) return "[null_variant]";

    try {
        // 检查类型是否为 String (0x4)
        if (variant->type != 0x4) {
            return "";  // 不是字符串类型，返回空
        }

        // 提取 UTF-32 字符串数据 (0x8 偏移)
        if (variant->data) {
            const char32_t* str_data = (const char32_t*)variant->data;
            return UTF32ToUTF8(str_data);
        }
    } catch (...) {
        return "[variant_parse_error]";
    }

    return "";
}

// ==========================================
// Detour 函数实现
// ==========================================

Variant* __fastcall GDScriptCallp_Detour(
    Variant* retstr,
    GDScriptInstance* thisptr,
    const StringName* p_method,
    const Variant** p_args,
    int p_argcount,
    CallError* r_error
) {
    try {
        // 如果没有配置规则，直接返回
        if (g_HookRules.empty()) {
            return g_OriginalGDScriptCallp(retstr, thisptr, p_method, p_args, p_argcount, r_error);
        }

        // 获取函数名
        std::string functionName = GetFunctionName(p_method);

        // 获取脚本路径
        std::string scriptPath = GetScriptPath(thisptr);

        // 查找匹配的规则
        const HookRule* rule = FindMatchingRule(scriptPath, functionName);

        if (rule) {
            char buffer[2048];

            // 输出基本信息
            if (rule->scriptPath.empty()) {
                sprintf_s(buffer, "[GDScript] Function: %s, ArgCount: %d\n", 
                         functionName.c_str(), p_argcount);
            } else {
                sprintf_s(buffer, "[GDScript] Path: %s\n  Function: %s, ArgCount: %d\n", 
                         scriptPath.c_str(), functionName.c_str(), p_argcount);
            }
            OutputDebugStringA(buffer);

            // 提取参数
            if (p_args && p_argcount > 0) {
                // 如果规则指定了参数索引，只提取指定的
                if (!rule->argIndices.empty()) {
                    for (int idx : rule->argIndices) {
                        if (idx >= 0 && idx < p_argcount && p_args[idx]) {
                            std::string strValue = ExtractStringFromVariant(p_args[idx]);

                            if (!strValue.empty()) {
                                sprintf_s(buffer, "  [Arg %d] String: \"%s\"\n", 
                                         idx, strValue.c_str());
                                OutputDebugStringA(buffer);
                            }
                        }
                    }
                } else {
                    // 否则提取所有字符串参数
                    for (int i = 0; i < p_argcount; i++) {
                        if (p_args[i]) {
                            std::string strValue = ExtractStringFromVariant(p_args[i]);

                            if (!strValue.empty()) {
                                sprintf_s(buffer, "  [Arg %d] String: \"%s\"\n", 
                                         i, strValue.c_str());
                                OutputDebugStringA(buffer);
                            }
                        }
                    }
                }
            }
        }
    } catch (...) {
        OutputDebugStringA("[GDScript] Exception in detour function\n");
    }

    // 调用原始函数
    return g_OriginalGDScriptCallp(retstr, thisptr, p_method, p_args, p_argcount, r_error);
}

bool SetupAllHooks() {
    OutputDebugStringA("[Hook] Setting up hooks...\n");

    // 定义多组签名，优先级从上到下
    const char* signatures[] = {
        // Godot 4.3.0 x64 MinGW64 (Self build godot.windows.template_release.x86_64.exe)
        "41 57 41 56 41 55 41 54 55 57 56 53 48 83 EC ?? 48 8B 05 ?? ?? ?? ?? ?? ?? ?? 4C 8B A4 24",

    };

    const int signatureCount = sizeof(signatures) / sizeof(signatures[0]);

    const char* targetModule = nullptr;

    // 获取模块名用于日志输出
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

    // 尝试所有签名
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
            sprintf_s(successBuffer, "[Hook] ✓ Found target function using signature #%d at: 0x%p\n", 
                     i + 1, targetAddr);
            OutputDebugStringA(successBuffer);
            break;
        } else {
            char failBuffer[256];
            sprintf_s(failBuffer, "[Hook] ✗ Signature #%d not found, trying next...\n", i + 1);
            OutputDebugStringA(failBuffer);
        }
    }

    // 如果所有签名都失败
    if (!targetAddr) {
        char errorBuffer[512];
        sprintf_s(errorBuffer, "[Hook] ✗ Failed to find target function after trying all %d signatures in module: %s\n", 
                 signatureCount, searchModuleName);
        OutputDebugStringA(errorBuffer);
        OutputDebugStringA("[Hook] Tip: Check if signatures are correct or set 'targetModule' to the correct module name\n");
        return false;
    }

    g_OriginalGDScriptCallp = (GDScriptCallp_t)targetAddr;

    // 附加 Detour
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    LONG error = DetourAttach(&(PVOID&)g_OriginalGDScriptCallp, GDScriptCallp_Detour);

    if (DetourTransactionCommit() != NO_ERROR) {
        char errorBuffer[256];
        sprintf_s(errorBuffer, "[Hook] Failed to attach detour. Error code: %ld\n", error);
        OutputDebugStringA(errorBuffer);
        return false;
    }

    OutputDebugStringA("[Hook] ✓ Target function hooked successfully!\n");
    OutputDebugStringA("[Hook] All hooks set up successfully\n");

    // 输出监控配置
    if (g_HookRules.empty()) {
        OutputDebugStringA("[Hook] ⚠ No hook rules configured. Nothing will be monitored.\n");
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

    // 分离 GDScriptCallp hook
    if (g_OriginalGDScriptCallp) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&)g_OriginalGDScriptCallp, GDScriptCallp_Detour);

        if (DetourTransactionCommit() == NO_ERROR) {
            OutputDebugStringA("[Hook] ✓ GDScriptCallp detached successfully\n");
        } else {
            OutputDebugStringA("[Hook] ✗ Failed to detach GDScriptCallp\n");
        }

        g_OriginalGDScriptCallp = nullptr;
    }

    OutputDebugStringA("[Hook] All hooks cleaned up\n");
}
