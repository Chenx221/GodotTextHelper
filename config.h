#pragma once
#include <vector>
#include "hooks.h"

bool LoadConfiguration(bool& enableClipboard, bool& enableFunctionLog, bool& filterDuplicateFunctionLog, bool& builtinFunctionNameUTF32, std::vector<HookRule>& hookRules);
