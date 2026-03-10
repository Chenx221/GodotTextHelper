#include "pch.h"
#include "utils.h"
#include "hooks.h"

std::string UTF32ToUTF8(const char32_t* utf32_str) {
    if (!utf32_str) return "";

    try {
        size_t len = 0;
        while (utf32_str[len] != U'\0') {
            len++;
        }

        if (len == 0) return "";

        std::wstring utf16;
        utf16.reserve(len);

        for (size_t i = 0; i < len; i++) {
            char32_t ch = utf32_str[i];
            if (ch <= 0xFFFF) {
                utf16 += (wchar_t)ch;
            } else {
                ch -= 0x10000;
                utf16 += (wchar_t)((ch >> 10) + 0xD800);
                utf16 += (wchar_t)((ch & 0x3FF) + 0xDC00);
            }
        }

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

std::string UTF16ToUTF8(const char16_t* utf16_str) {
    if (!utf16_str) return "";

    try {
        size_t len = 0;
        while (utf16_str[len] != u'\0') {
            len++;
        }

        if (len == 0) return "";

        int size_needed = WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)utf16_str, (int)len, 
                                              NULL, 0, NULL, NULL);
        if (size_needed <= 0) {
            return "[UTF16->UTF8 Error]";
        }

        std::string utf8(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)utf16_str, (int)len, 
                           &utf8[0], size_needed, NULL, NULL);

        return utf8;
    } catch (...) {
        return "[UTF16 Conversion Error]";
    }
}

// 请参考readme.md进行配置
// 未记录的版本请自行IDA GDScriptInstance::get_script/GDScript::get_script_path
std::string GetScriptPath(const GDScriptInstance* instance) {
    if (!instance) return "[null_instance]";

    try {
        void** ptr1 = (void**)((BYTE*)instance + g_gdscriptInstanceOffset);
        if (!ptr1 || !*ptr1) return "[null_ptr1]";

        void** ptr2 = (void**)((BYTE*)*ptr1 + g_gdscriptPathOffset);
        if (!ptr2 || !*ptr2) return "[null_ptr2]";

            const char16_t* path = (const char16_t*)*ptr2;
            if (!path) return "[null_path]";

            return UTF16ToUTF8(path);
    } catch (...) {
        return "[path_parse_error]";
    }
}

std::string GetFunctionName(const StringName* p_method) {
    if (!p_method) return "[null]";

    try {
        void* data = p_method->_data;
        if (!data) return "[null_data]";
        void** cname_ptr = (void**)((BYTE*)data + 0x8);
        if (cname_ptr && *cname_ptr) {
            if (g_builtinFunctionNameUTF16) {
                const char16_t* cname = (const char16_t*)*cname_ptr;
                if (cname && cname[0] != u'\0') {
                    return UTF16ToUTF8(cname);
                }
            } else {
                const char* cname = (const char*)*cname_ptr;
                if (cname && cname[0] != '\0') {
                    return std::string(cname);
                }
            }
        }

        void** name_data_ptr = (void**)((BYTE*)data + 0x10);
        if (name_data_ptr && *name_data_ptr) {
                const char16_t* name_str = (const char16_t*)*name_data_ptr;
                if (name_str && name_str[0] != u'\0') {
                    return UTF16ToUTF8(name_str);
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
            const char16_t* str_data = (const char16_t*)variant->data;
            return UTF16ToUTF8(str_data);
        }
    } catch (...) {
        return "[variant_parse_error]";
    }

    return "";
}

SignaturePattern ParseX64dbgSignature(const char* signature) {
    SignaturePattern result;
    std::string sig(signature);

    size_t pos = 0;
    while (pos < sig.length()) {
        while (pos < sig.length() && sig[pos] == ' ') pos++;
        if (pos >= sig.length()) break;

        if (sig[pos] == '?') {
            result.pattern.push_back(0x00);
            result.mask += '?';
            pos++;
            if (pos < sig.length() && sig[pos] == '?') {
                pos++;
            }
        } else {
            if (pos + 1 < sig.length()) {
                char hex[3] = { sig[pos], sig[pos + 1], 0 };
                unsigned char value = (unsigned char)strtol(hex, nullptr, 16);
                result.pattern.push_back(value);
                result.mask += 'x';
                pos += 2;
            }
        }
    }

    return result;
}

void* FindPatternInModule(const char* moduleName, const char* x64dbgSignature) {
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

    unsigned char* baseAddress = (unsigned char*)moduleInfo.lpBaseOfDll;
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
