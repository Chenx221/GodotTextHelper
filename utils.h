#pragma once
#include <string>
#include <vector>

struct Variant;
struct StringName;
struct GDScriptInstance;

// UTF-32 to UTF-8 conversion
std::string UTF32ToUTF8(const char32_t* utf32_str);

// UTF-16 to UTF-8 conversion
std::string UTF16ToUTF8(const char16_t* utf16_str);

// Get script path from GDScriptInstance
std::string GetScriptPath(const GDScriptInstance* instance);

// Get function name from StringName
std::string GetFunctionName(const StringName* p_method);

// Extract string from Variant
std::string ExtractStringFromVariant(const Variant* variant);

// Signature pattern matching
struct SignaturePattern {
    std::vector<unsigned char> pattern;
    std::string mask;
};

// Parse x64dbg-style signature string
SignaturePattern ParseX64dbgSignature(const char* signature);

// Find pattern in module
void* FindPatternInModule(const char* moduleName, const char* x64dbgSignature);

