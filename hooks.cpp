#include "pch.h"
#include "config.h"
#include "hooks.h"
#include "utils.h"
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <regex>
#include <set>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>

constexpr int CLIPBOARD_TIMEOUT_MS = 300;

bool g_EnableClipboard = false;
bool g_EnableFunctionLog = false;
bool g_FilterDuplicateFunctionLog = false;
bool g_builtinFunctionNameUTF32 = false;
size_t g_gdscriptInstanceOffset = 0x18;
size_t g_gdscriptPathOffset = 0x3C0;
std::vector<HookRule> g_HookRules;
std::string g_RegexFilter;
static std::regex g_RegexFilterRegex;
static bool g_HasRegexFilter = false;

GDScriptCallp_t g_OriginalGDScriptCallp = nullptr;

typedef void(__fastcall* RichTextLabelAddText_t)(void* thisptr, const void* p_text);
typedef void(__fastcall* GodotStringCtorFromUtf32_t)(void* thisptr, const char32_t* p_cstr);
typedef void(__fastcall* GodotStringDtor_t)(void* thisptr);

static RichTextLabelAddText_t g_OriginalRichTextLabelAddText = nullptr;
static GodotStringCtorFromUtf32_t g_GodotStringCtorFromUtf32 = nullptr;
static GodotStringDtor_t g_GodotStringDtor = nullptr;

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

    std::string processedText = text;

    if (g_HasRegexFilter) {
        try {
            processedText = std::regex_replace(text, g_RegexFilterRegex, "");
        }
        catch (...) {
            OutputDebugStringA("[Regex] Error during regex_replace\n");
        }
    }

    if (processedText.empty()) return;
    std::lock_guard<std::mutex> lock(g_ClipboardMutex);

    if (!g_ClipboardBuffer.empty()) {
        g_ClipboardBuffer += "\n";
    }
    g_ClipboardBuffer += processedText;
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

static void* ResolveFirstRelCallTarget(const void* startAddress, size_t searchBytes) {
    if (!startAddress || searchBytes < 5) {
        return nullptr;
    }

    const unsigned char* code = (const unsigned char*)startAddress;
    for (size_t i = 0; i + 4 < searchBytes; ++i) {
        if (code[i] != 0xE8) {
            continue;
        }

        int32_t rel = 0;
        memcpy(&rel, code + i + 1, sizeof(rel));
        const unsigned char* target = code + i + 5 + rel;
        return (void*)target;
    }

    return nullptr;
}

static std::string TranslateRichTextLabelText(const std::string& inputText) {
    // Translation pipeline placeholder. Keep original text for now.
    return inputText;
}

void __fastcall RichTextLabelAddText_Detour(void* thisptr, const void* p_text) {
    if (!g_OriginalRichTextLabelAddText) {
        return;
    }

    try {
        const std::string originalText = ExtractUtf8FromGodotString(p_text);
        const std::u32string translatedTextUtf32 = U"测试中文";
        const std::string translatedTextUtf8 = UTF32ToUTF8(translatedTextUtf32.c_str());

        if (translatedTextUtf8 != originalText &&
            g_GodotStringCtorFromUtf32 &&
            g_GodotStringDtor) {
            void* temp_godot_string = nullptr;
            if (translatedTextUtf32.empty()) {
                g_OriginalRichTextLabelAddText(thisptr, p_text);
                return;
            }

            // 【修正 3】：传 &temp_godot_string 作为 this
            // 构造函数会把生成的 CowData 地址写进 temp_godot_string
            g_GodotStringCtorFromUtf32(&temp_godot_string, translatedTextUtf32.c_str());

            // 调用原函数
            g_OriginalRichTextLabelAddText(thisptr, &temp_godot_string);

            // 析构
            //g_GodotStringDtor(&temp_godot_string);
            return;
        }
    } catch (...) {
        OutputDebugStringA("[RichTextLabel] Exception in add_text detour\n");
    }

    g_OriginalRichTextLabelAddText(thisptr, p_text);
}

static void ResolveRichTextSupportFunctions(const char* moduleName) {
    const char* stringCtorSignatures[] = {
        // godot 4.6.1 x64 (official)
        "53 48 83 EC ?? ?? ?? ?? 48 89 CB 45 85 C0 74",
    };

    const char* stringDtorSignatures[] = {
        // godot 4.6.1 x64 (official) (xref)
        "4C 8D 1D ?? ?? ?? ?? 45 31 C0 48 89 C2",
    };

    for (const char* signature : stringCtorSignatures) {
        void* addr = FindPatternInModule(moduleName, signature);
        if (addr) {
            g_GodotStringCtorFromUtf32 = (GodotStringCtorFromUtf32_t)addr;
            char info[256];
            sprintf_s(info, "[RichTextLabel] String::String(const char32_t*) found at 0x%p\n", addr);
            OutputDebugStringA(info);
            break;
        }
    }

    if (!g_GodotStringCtorFromUtf32) {
        OutputDebugStringA("[RichTextLabel] String::String(const char32_t*) signature not found\n");
    }

    for (const char* signature : stringDtorSignatures) {
        void* xrefAddr = FindPatternInModule(moduleName, signature);
        if (!xrefAddr) {
            continue;
        }

        void* dtorAddr = ResolveFirstRelCallTarget(xrefAddr, 0x80);
        if (dtorAddr) {
            g_GodotStringDtor = (GodotStringDtor_t)dtorAddr;
            char info[256];
            sprintf_s(info, "[RichTextLabel] String::~String resolved from xref 0x%p -> 0x%p\n", xrefAddr, dtorAddr);
            OutputDebugStringA(info);
            break;
        }

        char warn[256];
        sprintf_s(warn, "[RichTextLabel] String::~String xref found at 0x%p but call target not resolved\n", xrefAddr);
        OutputDebugStringA(warn);
    }

    if (!g_GodotStringDtor) {
        OutputDebugStringA("[RichTextLabel] String::~String signature not found\n");
    }
}

static void SetupRichTextLabelAddTextHook(const char* moduleName) {
    const char* addTextSignatures[] = {
        // godot 4.6.1 x64 (official)
        "41 57 41 56 41 55 41 54 55 57 56 53 48 81 EC ?? ?? ?? ?? 0F 29 B4 24 ?? ?? ?? ?? 0F 29 BC 24 ?? ?? ?? ?? 44 0F 29 84 24 ?? ?? ?? ?? 48 89 CB 48 89 D6 80 B9",
    };

    void* targetAddr = nullptr;
    for (const char* signature : addTextSignatures) {
        targetAddr = FindPatternInModule(moduleName, signature);
        if (targetAddr) {
            break;
        }
    }

    if (!targetAddr) {
        OutputDebugStringA("[RichTextLabel] add_text signature not found\n");
        return;
    }

    g_OriginalRichTextLabelAddText = (RichTextLabelAddText_t)targetAddr;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG error = DetourAttach(&(PVOID&)g_OriginalRichTextLabelAddText, RichTextLabelAddText_Detour);

    if (DetourTransactionCommit() != NO_ERROR) {
        char errorBuffer[256];
        sprintf_s(errorBuffer, "[RichTextLabel] Failed to attach add_text detour. Error code: %ld\n", error);
        OutputDebugStringA(errorBuffer);
        g_OriginalRichTextLabelAddText = nullptr;
        return;
    }

    char successBuffer[256];
    sprintf_s(successBuffer, "[RichTextLabel] add_text hooked at 0x%p\n", targetAddr);
    OutputDebugStringA(successBuffer);
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

        std::string functionName = GetFunctionName(p_method);

        if (g_EnableFunctionLog) {
            LogFunctionName(functionName);
        }

        if (g_HookRules.empty()) {
            return g_OriginalGDScriptCallp(retstr, thisptr, p_method, p_args, p_argcount, r_error);
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

	Variant* result = g_OriginalGDScriptCallp(retstr, thisptr, p_method, p_args, p_argcount, r_error);

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
    LoadConfiguration(g_EnableClipboard, g_EnableFunctionLog, g_FilterDuplicateFunctionLog, g_builtinFunctionNameUTF32, g_gdscriptInstanceOffset, g_gdscriptPathOffset, g_HookRules, g_RegexFilter);

    g_HasRegexFilter = false;
    if (!g_RegexFilter.empty()) {
        try {
            g_RegexFilterRegex = std::regex(g_RegexFilter);
            g_HasRegexFilter = true;
        } catch (...) {
            OutputDebugStringA("[Hook] Failed to compile regexFilter regex\n");
        }
    }

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
        // godot 4.5.1 x64 (official)
        // godot 4.5 x64 (official)
        "41 55 41 54 55 57 56 53 48 83 EC ?? 48 8B 05 ?? ?? ?? ?? 48 8B 5A",

        // godot 4.3.0 x64 (official)
        "41 57 41 56 41 55 41 54 55 57 56 53 48 81 EC ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? ?? ?? ?? 4C 8B A4 24 ?? ?? ?? ?? 4C 8B 72",

		// godot 4.2.2 x64 (official)
        // godot 4.2.1 x64 (official)
        // godot 4.2 x64 (official)
        "41 57 41 56 41 55 41 54 55 57 56 53 48 83 EC ?? 4C 8B B4 24 ?? ?? ?? ?? 4C 8B 62",

        // godot 4.7-dev2 x64 (official)
        // godot 4.6.1 x64 (official)
        // godot 4.6 x64 (official)
        // godot 4.4.1 x64 (official)
        // godot 4.4 x64 (official)
        "41 57 41 56 41 55 41 54 55 57 56 53 48 83 EC ?? 48 8B 05 ?? ?? ?? ?? 4C 8B B4 24",

		// godot 4.1.4 x64 (official)
        // godot 4.1.3 x64 (official)
        // godot 4.1.2 x64 (official)
        // godot 4.1.1 x64 (official)
        // godot 4.1 x64 (official)
        "41 57 41 56 41 55 41 54 55 57 56 53 48 83 EC ?? 48 8B 7A ?? 48 89 8C 24",

		// godot 4.0.4 x64 (official)
        // godot 4.0.3 x64 (official)
        // godot 4.0.2 x64 (official)
        // godot 4.0.1 x64 (official)
        // godot 4.0 x64 (official)
        "41 57 41 56 41 55 41 54 55 57 56 53 48 83 EC ?? 4C 8B 7A ?? 48 89 8C 24",

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

    ResolveRichTextSupportFunctions(searchModuleName);
    SetupRichTextLabelAddTextHook(searchModuleName);

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

    if (g_OriginalRichTextLabelAddText) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&)g_OriginalRichTextLabelAddText, RichTextLabelAddText_Detour);

        if (DetourTransactionCommit() == NO_ERROR) {
            OutputDebugStringA("[Hook] RichTextLabel.add_text detached successfully\n");
        } else {
            OutputDebugStringA("[Hook] Failed to detach RichTextLabel.add_text\n");
        }

        g_OriginalRichTextLabelAddText = nullptr;
    }

    g_GodotStringCtorFromUtf32 = nullptr;
    g_GodotStringDtor = nullptr;

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
