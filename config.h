#pragma once
#include <vector>
#include "hooks.h"

bool LoadConfiguration(bool& enableClipboard, bool& enableFunctionLog, bool& filterDuplicateFunctionLog, bool& builtinFunctionNameUTF16, size_t& gdscriptInstanceOffset, size_t& gdscriptPathOffset, std::vector<HookRule>& hookRules);
