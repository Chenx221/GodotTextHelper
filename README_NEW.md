# GodotTextHelper

一个用于 Godot 引擎的 DLL 注入工具，可以监控和提取 GDScript 函数调用及其字符串参数。

## ✨ 功能特性

- 🎯 **精确监控**：通过配置规则监控特定的 GDScript 函数调用
- 📝 **字符串提取**：自动提取并输出函数的字符串参数
- 🔍 **路径过滤**：可以限制只监控特定脚本中的函数
- ⚙️ **灵活配置**：支持指定要提取的参数索引
- 🚀 **低开销**：使用 Microsoft Detours 实现高性能 Hook
- 🌐 **UTF-32 支持**：完整支持 Godot 的 UTF-32 字符串格式

## 🚀 快速开始

### 1. 配置 Hook 规则

编辑 `hooks.cpp` 文件中的配置：

```cpp
std::vector<HookRule> g_HookRules = {
    // 监控所有 print 调用
    HookRule("print"),
    
    // 只监控 player.gd 中的 take_damage 函数
    HookRule("res://scripts/player.gd", "take_damage", {0}),
};
```

### 2. 编译项目

在 Visual Studio 中按 `Ctrl+Shift+B` 编译项目。

### 3. 部署 DLL

将生成的 `winmm.dll` 复制到 Godot 可执行文件所在目录。

### 4. 查看输出

使用 [DebugView](https://docs.microsoft.com/sysinternals/downloads/debugview) 查看监控输出：

```
[GDScript] Function: print, ArgCount: 2
  [Arg 0] String: "Hello World"
  [Arg 1] String: "From GDScript"
```

## 📖 配置指南

### 三种规则类型

#### 1. 监控所有脚本中的函数

```cpp
HookRule("function_name")
```

监控任何脚本中名为 `function_name` 的函数，提取所有字符串参数。

#### 2. 监控函数 + 指定参数

```cpp
HookRule("function_name", {0, 2})
```

只提取第 0 和第 2 个参数（参数索引从 0 开始）。

#### 3. 限制脚本路径 + 函数 + 参数

```cpp
HookRule("res://scripts/player.gd", "take_damage", {0})
```

只监控 `player.gd` 中的 `take_damage` 函数，只提取第 0 个参数。

### 路径匹配

路径使用**部分匹配**：
- `"player.gd"` 匹配 `res://scripts/player.gd`
- `"scripts/"` 匹配 `res://scripts/` 下的所有脚本
- `"addons/my_plugin/"` 匹配插件目录

## 💡 使用示例

### 监控游戏日志

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("print"),
    HookRule("push_warning", {0}),
    HookRule("push_error", {0}),
};
```

### 调试对话系统

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("show_dialogue", {0}),
    HookRule("set_speaker", {0}),
};
```

### 监控特定脚本

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("res://scripts/player.gd", "_ready", {}),
    HookRule("res://scripts/player.gd", "take_damage", {0}),
};
```

更多示例请查看 [HOOK_EXAMPLES.md](HOOK_EXAMPLES.md)

## 📚 文档

- [快速开始指南](QUICKSTART_HOOK.md) - 5 分钟快速配置
- [完整配置文档](HOOK_CONFIGURATION.md) - 详细配置说明和技术细节
- [配置示例集合](HOOK_EXAMPLES.md) - 各种使用场景的配置模板

## 🔧 技术细节

### Hook 目标

- **函数**：`GDScriptInstance::callp`
- **架构**：x64
- **方法**：Microsoft Detours

### 结构体布局

```cpp
struct Variant {
    int type;           // 0x0: 类型标记，0x4 = String
    int padding;
    void* data;         // 0x8: 数据指针（UTF-32 字符串）
};

struct StringName {
    void* _data;        // 0x0
    void* builtin_str;  // 0x8: 内置函数名（UTF-8）
    void* custom_str;   // 0x10: 自定义函数名（UTF-32）
};
```

### 脚本路径提取

- `thisptr + 0x18` → 中间对象指针
- `[thisptr + 0x18] + 0x3C0` → 脚本路径（UTF-32 字符串）

## ⚡ 性能

- **Hook 开销**：约 1-10 微秒每次调用
- **空配置**：几乎零开销（只有一个空检查）
- **建议**：避免监控高频函数（如 `_process`）

## 🔍 故障排除

### Hook 未找到目标函数

```
[Hook] ✗ Failed to find target function after trying all signatures
```

**解决方案**：你的 Godot 版本可能使用不同的编译选项，需要添加新的字节签名。使用 x64dbg 或 IDA Pro 查找函数并提取签名。

### 无输出

1. 确认 DebugView 正在运行
2. 检查 `g_HookRules` 不为空
3. 验证函数名拼写正确

### 路径不匹配

临时使用不指定路径的规则来查看完整路径：

```cpp
HookRule("your_function")  // 会输出完整路径
```

## 🛡️ 兼容性

| 平台 | 支持状态 |
|------|---------|
| Godot 4.x (x64) | ✅ 支持 |
| MinGW64 编译 | ✅ 支持 |
| MSVC 编译 | ⚠️ 需要不同签名 |
| Godot 3.x | ❌ 不支持 |
| x86 (32位) | ❌ 不支持 |

## 🔒 注意事项

- 仅用于开发和调试目的
- 不要在生产环境使用
- 可能违反某些游戏的使用条款
- 修改内存前请谨慎测试

## 🛠️ 构建要求

- Visual Studio 2019 或更高版本
- Windows SDK
- C++17 或更高版本
- Microsoft Detours 库（已包含）

## 📜 许可证

本项目仅供学习和研究使用。

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

特别是：
- 不同 Godot 版本的字节签名
- 新的配置示例
- 错误修复和改进

## 🙏 致谢

- [Microsoft Detours](https://github.com/microsoft/Detours) - Hook 框架
- [Godot Engine](https://godotengine.org/) - 游戏引擎
