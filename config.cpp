#include "pch.h"
#include "config.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>

namespace {

	using json = nlohmann::json;

	std::string ToLowerString(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
			});
		return value;
	}

	std::filesystem::path GetConfigPath() {
		HMODULE hModule = nullptr;
		if (!GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(&GetConfigPath),
			&hModule)) {
			return std::filesystem::path("config.json");
		}

		char modulePath[MAX_PATH] = {};
		if (GetModuleFileNameA(hModule, modulePath, MAX_PATH) == 0) {
			return std::filesystem::path("config.json");
		}

		return std::filesystem::path(modulePath).parent_path() / "config.json";
	}

	std::string GetDefaultConfigContent() {
		json defaultConfig = {
			{ "clipboard", false },
			{ "logFunctionName", false },
			{ "filterDuplicateFunctionLog", false },
			{ "gdscriptInstanceOffset", "0x18" },
			{ "gdscriptPathOffset", "0x3C0" },
			{ "rules", json::array() },
			{ "builtinFunctionNameUTF32", false }
		};
		return defaultConfig.dump(4);
	}

	json GetDefaultConfigJson() {
		return json::parse(GetDefaultConfigContent());
	}

	bool WriteConfigFile(const std::filesystem::path& configPath, const json& root) {
		std::ofstream output(configPath);
		if (!output.is_open()) {
			char buffer[512];
			sprintf_s(buffer, "[Config] Failed to write config file: %s\n", configPath.string().c_str());
			OutputDebugStringA(buffer);
			return false;
		}

		output << root.dump(4);
		return true;
	}

	bool MergeMissingTopLevelDefaults(json& root) {
		bool changed = false;
		const json defaults = GetDefaultConfigJson();

		for (auto it = defaults.begin(); it != defaults.end(); ++it) {
			if (!root.contains(it.key())) {
				root[it.key()] = it.value();
				changed = true;
			}
		}

		return changed;
	}

	bool ParseOffsetValue(const json& value, size_t& result, const char* fieldName, std::string& error) {
		if (value.is_number_unsigned()) {
			result = value.get<size_t>();
			return true;
		}

		if (value.is_number_integer()) {
			long long signedValue = value.get<long long>();
			if (signedValue < 0) {
				error = std::string(fieldName) + " must be non-negative";
				return false;
			}

			result = static_cast<size_t>(signedValue);
			return true;
		}

		if (value.is_string()) {
			const std::string text = value.get<std::string>();
			if (text.empty()) {
				error = std::string(fieldName) + " must not be empty";
				return false;
			}

			errno = 0;
			char* end = nullptr;
			unsigned long long parsed = std::strtoull(text.c_str(), &end, 0);
			if (errno == ERANGE || end != text.c_str() + text.size()) {
				error = std::string(fieldName) + " must be an integer or hex string";
				return false;
			}

			result = static_cast<size_t>(parsed);
			return true;
		}

		error = std::string(fieldName) + " must be an integer or string";
		return false;
	}

	bool EnsureConfigFileExists(const std::filesystem::path& configPath) {
		if (std::filesystem::exists(configPath)) {
			return true;
		}

		std::ofstream output(configPath);
		if (!output.is_open()) {
			char buffer[512];
			sprintf_s(buffer, "[Config] Failed to create config file: %s\n", configPath.string().c_str());
			OutputDebugStringA(buffer);
			return false;
		}

		output << GetDefaultConfigContent();
		OutputDebugStringA("[Config] Created default json config file\n");
		return true;
	}

	std::filesystem::path BuildBrokenConfigPath(const std::filesystem::path& configPath) {
		std::filesystem::path brokenPath = configPath;
		brokenPath += ".broken";

		if (!std::filesystem::exists(brokenPath)) {
			return brokenPath;
		}

		for (int index = 1; index < 1000; ++index) {
			std::filesystem::path candidate = configPath;
			candidate += ".broken." + std::to_string(index);
			if (!std::filesystem::exists(candidate)) {
				return candidate;
			}
		}

		return brokenPath;
	}

	bool MoveBrokenConfig(const std::filesystem::path& configPath) {
		std::error_code ec;
		std::filesystem::path brokenPath = BuildBrokenConfigPath(configPath);
		std::filesystem::rename(configPath, brokenPath, ec);
		if (ec) {
			char buffer[512];
			sprintf_s(buffer, "[Config] Failed to rename broken config: %s\n", ec.message().c_str());
			OutputDebugStringA(buffer);
			return false;
		}

		char buffer[512];
		sprintf_s(buffer, "[Config] Invalid config renamed to: %s\n", brokenPath.string().c_str());
		OutputDebugStringA(buffer);
		EnsureConfigFileExists(configPath);
		return true;
	}

	bool ParseRuleJson(const json& ruleJson, HookRule& rule, std::string& error) {
		if (!ruleJson.is_object()) {
			error = "rule must be an object";
			return false;
		}

		std::string scriptPath;
		std::string functionName;
		std::set<int> argIndices;
		bool postHook = false;

		if (!ruleJson.contains("function") || !ruleJson["function"].is_string()) {
			error = "rule missing string function";
			return false;
		}

		functionName = ruleJson["function"].get<std::string>();
		if (functionName.empty()) {
			error = "rule function is empty";
			return false;
		}

		if (ruleJson.contains("script")) {
			if (!ruleJson["script"].is_string()) {
				error = "rule script must be a string";
				return false;
			}

			scriptPath = ruleJson["script"].get<std::string>();
		}

		if (ruleJson.contains("args")) {
			if (!ruleJson["args"].is_array()) {
				error = "rule args must be an array";
				return false;
			}

			for (const auto& argJson : ruleJson["args"]) {
				if (!argJson.is_number_integer()) {
					error = "rule args must contain integers";
					return false;
				}

				argIndices.insert(argJson.get<int>());
			}
		}

		if (ruleJson.contains("post")) {
			if (!ruleJson["post"].is_boolean()) {
				error = "rule post must be a boolean";
				return false;
			}

			postHook = ruleJson["post"].get<bool>();
		}

		for (auto it = ruleJson.begin(); it != ruleJson.end(); ++it) {
			const std::string key = ToLowerString(it.key());
			if (key != "script" && key != "function" && key != "args" && key != "post") {
				error = "unknown rule field";
				return false;
			}
		}

		rule = HookRule(functionName);
		rule.scriptPath = scriptPath;
		rule.argIndices = argIndices;
		rule.postHook = postHook;
		return true;
	}

	bool TryLoadConfigurationJson(const std::filesystem::path& configPath, bool& enableClipboard, bool& enableFunctionLog, bool& filterDuplicateFunctionLog, bool& builtinFunctionNameUTF32, size_t& gdscriptInstanceOffset, size_t& gdscriptPathOffset, std::vector<HookRule>& hookRules, std::string& error) {
		std::ifstream input(configPath);
		if (!input.is_open()) {
			error = "failed to open config file";
			return false;
		}

		json root;
		try {
			input >> root;
		}
		catch (const std::exception& ex) {
			error = ex.what();
			return false;
		}

		if (!root.is_object()) {
			error = "root must be a json object";
			return false;
		}

		const bool addedMissingDefaults = MergeMissingTopLevelDefaults(root);

		if (root.contains("clipboard")) {
			if (!root["clipboard"].is_boolean()) {
				error = "clipboard must be a boolean";
				return false;
			}

			enableClipboard = root["clipboard"].get<bool>();
		}

		if (root.contains("logFunctionName")) {
			if (!root["logFunctionName"].is_boolean()) {
				error = "logFunctionName must be a boolean";
				return false;
			}

			enableFunctionLog = root["logFunctionName"].get<bool>();
		}

		if (root.contains("filterDuplicateFunctionLog")) {
			if (!root["filterDuplicateFunctionLog"].is_boolean()) {
				error = "filterDuplicateFunctionLog must be a boolean";
				return false;
			}

			filterDuplicateFunctionLog = root["filterDuplicateFunctionLog"].get<bool>();
		}

		if (root.contains("builtinFunctionNameUTF32")) {
			if (!root["builtinFunctionNameUTF32"].is_boolean()) {
				error = "builtinFunctionNameUTF32 must be a boolean";
				return false;
			}
			builtinFunctionNameUTF32 = root["builtinFunctionNameUTF32"].get<bool>();
		}

		if (root.contains("gdscriptInstanceOffset")) {
			if (!ParseOffsetValue(root["gdscriptInstanceOffset"], gdscriptInstanceOffset, "gdscriptInstanceOffset", error)) {
				return false;
			}
		}

		if (root.contains("gdscriptPathOffset")) {
			if (!ParseOffsetValue(root["gdscriptPathOffset"], gdscriptPathOffset, "gdscriptPathOffset", error)) {
				return false;
			}
		}

		if (root.contains("rules")) {
			if (!root["rules"].is_array()) {
				error = "rules must be an array";
				return false;
			}

			int index = 0;
			for (const auto& ruleJson : root["rules"]) {
				++index;
				HookRule rule("");
				std::string ruleError;
				if (!ParseRuleJson(ruleJson, rule, ruleError)) {
					error = "invalid rule #" + std::to_string(index) + ": " + ruleError;
					return false;
				}

				hookRules.push_back(rule);
			}
		}

		for (auto it = root.begin(); it != root.end(); ++it) {
			const std::string key = ToLowerString(it.key());
			if (key != "clipboard" && key != "logfunctionname" && key != "filterduplicatefunctionlog" && key != "gdscriptinstanceoffset" && key != "gdscriptpathoffset" && key != "rules" && key != "builtinfunctionnameutf32") {
				error = "unknown top-level field: " + it.key();
				return false;
			}
		}

		if (addedMissingDefaults) {
			if (!WriteConfigFile(configPath, root)) {
				error = "failed to update config file with missing defaults";
				return false;
			}

			OutputDebugStringA("[Config] Added missing default config fields\n");
		}

		return true;
	}

}

bool LoadConfiguration(bool& enableClipboard, bool& enableFunctionLog, bool& filterDuplicateFunctionLog, bool& builtinFunctionNameUTF32, size_t& gdscriptInstanceOffset, size_t& gdscriptPathOffset, std::vector<HookRule>& hookRules) {
	enableClipboard = false;
	enableFunctionLog = false;
	filterDuplicateFunctionLog = false;
	builtinFunctionNameUTF32 = false;
	gdscriptInstanceOffset = 0x18;
	gdscriptPathOffset = 0x3C0;
	hookRules.clear();

	const std::filesystem::path configPath = GetConfigPath();
	if (!EnsureConfigFileExists(configPath)) {
		return false;
	}

	std::string error;
	if (!TryLoadConfigurationJson(configPath, enableClipboard, enableFunctionLog, filterDuplicateFunctionLog, builtinFunctionNameUTF32, gdscriptInstanceOffset, gdscriptPathOffset, hookRules, error)) {
		char buffer[1024];
		sprintf_s(buffer, "[Config] Invalid json config: %s\n", error.c_str());
		OutputDebugStringA(buffer);
		MoveBrokenConfig(configPath);
		enableClipboard = false;
		enableFunctionLog = false;
		filterDuplicateFunctionLog = false;
		builtinFunctionNameUTF32 = false;
		gdscriptInstanceOffset = 0x18;
		gdscriptPathOffset = 0x3C0;
		hookRules.clear();
		return false;
	}

	char buffer[512];
	sprintf_s(buffer, "[Config] Loaded config: clipboard=%s, logFunctionName=%s, filterDuplicateFunctionLog=%s, builtinFunctionNameUTF32=%s, gdscriptInstanceOffset=0x%zX, gdscriptPathOffset=0x%zX, rules=%zu\n",
		enableClipboard ? "on" : "off",
		enableFunctionLog ? "on" : "off",
		filterDuplicateFunctionLog ? "on" : "off",
		builtinFunctionNameUTF32 ? "on" : "off",
		gdscriptInstanceOffset,
		gdscriptPathOffset,
		hookRules.size());
	OutputDebugStringA(buffer);
	return true;
}
