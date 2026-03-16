#pragma once
#include "pch.h"
#include <string>
#include <set>

struct Variant;
struct StringName;
struct GDScriptInstance;
struct CallError;

struct HookRule {
	std::string scriptPath;
	std::string functionName;
	std::set<int> argIndices;
	bool postHook = false;

	HookRule(const std::string& func) 
		: functionName(func) {}

	HookRule(const std::string& func, bool post)
		: functionName(func), postHook(post) {}

	HookRule(const std::string& func, const std::set<int>& args)
		: functionName(func), argIndices(args) {}

	HookRule(const std::string& func, const std::set<int>& args, bool post)
		: functionName(func), argIndices(args), postHook(post) {}

	HookRule(const std::string& path, const std::string& func, const std::set<int>& args)
		: scriptPath(path), functionName(func), argIndices(args) {}

	HookRule(const std::string& path, const std::string& func, const std::set<int>& args, bool post)
		: scriptPath(path), functionName(func), argIndices(args), postHook(post) {}

	HookRule(const std::string& path, const std::string& func, bool post)
		: scriptPath(path), functionName(func), postHook(post) {}
};

struct Variant {
    int type;
    int padding;
    void* data;
};

struct StringName {
    void* _data;
};
// +0x0: SafeRefCount refcount
// +0x4: SafeNumeric<unsigned int> static_count
// +0x8: const char *cname
// +0x10: String name

struct CallError {
    int error;
    int argument;
    int expected;
};

typedef Variant* (__fastcall* GDScriptCall_t)(
    Variant* retstr,
    GDScriptInstance* thisptr,
    const StringName* p_method,
    const Variant** p_args,
    int p_argcount,
    CallError* r_error
);

extern GDScriptCall_t g_OriginalGDScriptCall;
extern bool g_builtinFunctionNameUTF16;
extern size_t g_gdscriptInstanceOffset;
extern size_t g_gdscriptPathOffset;

Variant* __fastcall GDScriptCall_Detour(
	Variant* retstr,
    GDScriptInstance* thisptr,
    const StringName* p_method,
    const Variant** p_args,
    int p_argcount,
    CallError* r_error
);

bool SetupAllHooks();
void CleanupAllHooks();
