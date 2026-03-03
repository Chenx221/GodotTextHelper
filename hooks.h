#pragma once
#include "pch.h"
#include <string>
#include <set>

// ==========================================
// Godot 结构体前向声明
// ==========================================

struct Variant;
struct StringName;
struct GDScriptInstance;
struct CallError;

// ==========================================
// Hook 配置结构
// ==========================================

struct HookRule {
    std::string scriptPath;         // 脚本路径过滤（空 = 不检查路径）
    std::string functionName;       // 函数名（必填）
    std::set<int> argIndices;       // 要提取的参数索引（空 = 提取所有字符串参数）

    HookRule(const std::string& func) 
        : functionName(func) {}

    HookRule(const std::string& func, const std::set<int>& args)
        : functionName(func), argIndices(args) {}

    HookRule(const std::string& path, const std::string& func, const std::set<int>& args)
        : scriptPath(path), functionName(func), argIndices(args) {}
};

// ==========================================
// Godot 结构体定义
// ==========================================

// Godot Variant 类型
struct Variant {
    int type;           // 0x0: 类型标记，0x4 = String
    int padding;
    void* data;         // 0x8: 数据指针 (对于 String 是 UTF-32)
    // ... 其他字段
};

// Godot StringName 结构
struct StringName {
    void* _data;        // 0x0
    void* builtin_str;  // 0x8: 内置函数名 (UTF-8)
    void* custom_str;   // 0x10: 自定义函数名 (UTF-32)
    // ... 其他字段
};

struct CallError {
    int error;
    int argument;
    int expected;
};

// ==========================================
// Hook 函数类型定义
// ==========================================

// GDScriptInstance::callp 函数类型
typedef Variant* (__fastcall* GDScriptCallp_t)(
    Variant* retstr,
    GDScriptInstance* thisptr,
    const StringName* p_method,
    const Variant** p_args,
    int p_argcount,
    CallError* r_error
);

extern GDScriptCallp_t g_OriginalGDScriptCallp;

// ==========================================
// Detour 函数声明区域
// ==========================================

Variant* __fastcall GDScriptCallp_Detour(
    Variant* retstr,
    GDScriptInstance* thisptr,
    const StringName* p_method,
    const Variant** p_args,
    int p_argcount,
    CallError* r_error
);

// ==========================================
// X64dbg 签名解析工具
// ==========================================

// 将 x64dbg 格式的签名转换为 pattern 和 mask
// x64dbg 格式示例: "48 8B ? ? ? 48 89"
// 其中 '?' 表示通配符
struct SignaturePattern {
    std::vector<BYTE> pattern;
    std::string mask;
};

inline SignaturePattern ParseX64dbgSignature(const char* signature) {
    SignaturePattern result;
    std::string sig(signature);
    
    size_t pos = 0;
    while (pos < sig.length()) {
        // 跳过空格
        while (pos < sig.length() && sig[pos] == ' ') pos++;
        if (pos >= sig.length()) break;
        
        // 检查是否是通配符
        if (sig[pos] == '?') {
            result.pattern.push_back(0x00);
            result.mask += '?';
            pos++;
        } else {
            // 读取两个十六进制字符
            if (pos + 1 < sig.length()) {
                char hex[3] = { sig[pos], sig[pos + 1], 0 };
                BYTE value = (BYTE)strtol(hex, nullptr, 16);
                result.pattern.push_back(value);
                result.mask += 'x';
                pos += 2;
            }
        }
    }
    
    return result;
}

// 在模块中按签名搜索地址
inline void* FindPatternInModule(const char* moduleName, const char* x64dbgSignature) {
    HMODULE hModule = GetModuleHandleA(moduleName);
    if (!hModule) {
        return nullptr;
    }

    MODULEINFO moduleInfo;
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(MODULEINFO))) {
        return nullptr;
    }

    SignaturePattern sig = ParseX64dbgSignature(x64dbgSignature);
    if (sig.pattern.empty()) {
        return nullptr;
    }

    BYTE* baseAddress = (BYTE*)moduleInfo.lpBaseOfDll;
    SIZE_T moduleSize = moduleInfo.SizeOfImage;
    
    for (SIZE_T i = 0; i < moduleSize - sig.pattern.size(); i++) {
        bool found = true;
        for (SIZE_T j = 0; j < sig.pattern.size(); j++) {
            if (sig.mask[j] == 'x' && baseAddress[i + j] != sig.pattern[j]) {
                found = false;
                break;
            }
        }
        
        if (found) {
            return (void*)(baseAddress + i);
        }
    }
    
    return nullptr;
}

// ==========================================
// Hook 管理函数
// ==========================================

// 初始化所有 hooks（在 dllmain.cpp 中调用）
bool SetupAllHooks();

// 清理所有 hooks（在 dllmain.cpp 中调用）
void CleanupAllHooks();
