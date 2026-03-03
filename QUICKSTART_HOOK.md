# 快速开始：配置 GDScript Hook

## 5 分钟快速配置

### 步骤 1: 打开配置文件

编辑 `hooks.cpp` 文件顶部的配置区域。

### 步骤 2: 选择你的场景

#### 🎯 场景 A：监控所有日志（推荐新手）

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("print"),
    HookRule("push_warning"),
    HookRule("push_error"),
};
```

#### 🎯 场景 B：只监控特定函数

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("my_function_name"),  // 替换为你的函数名
};
```

#### 🎯 场景 C：只监控特定脚本

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("player.gd", "take_damage", {}),  // 替换为你的脚本和函数
};
```

### 步骤 3: 编译并运行

1. 按 `Ctrl+Shift+B` 编译项目
2. 复制生成的 `winmm.dll` 到 Godot 可执行文件目录
3. 运行你的 Godot 游戏
4. 使用 [DebugView](https://docs.microsoft.com/sysinternals/downloads/debugview) 查看输出

## 配置语法速查

### 基本格式

```cpp
std::vector<HookRule> g_HookRules = {
    规则1,
    规则2,
    规则3,
    // ... 更多规则
};
```

### 三种规则类型

| 类型 | 语法 | 说明 |
|------|------|------|
| **只函数名** | `HookRule("函数名")` | 监控所有脚本中的该函数 |
| **函数名 + 参数** | `HookRule("函数名", {0, 1})` | 只提取指定索引的参数 |
| **路径 + 函数 + 参数** | `HookRule("路径", "函数名", {0})` | 只监控特定脚本 |

### 参数索引

- `{}` = 提取所有字符串参数
- `{0}` = 只提取第 0 个参数
- `{0, 2, 3}` = 提取第 0、2、3 个参数

## 常用配置模板

### 模板 1：游戏日志分析

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("print"),
    HookRule("print_debug"),
    HookRule("push_warning", {0}),
    HookRule("push_error", {0}),
};
```

### 模板 2：对话系统调试

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("show_dialogue", {0}),
    HookRule("set_speaker", {0}),
    HookRule("add_choice", {0}),
};
```

### 模板 3：网络调试

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("send_message", {0, 1}),
    HookRule("receive_message", {0}),
};
```

### 模板 4：特定脚本监控

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("res://scripts/player.gd", "take_damage", {0}),
    HookRule("res://scripts/player.gd", "use_item", {0}),
};
```

### 模板 5：插件开发

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("addons/my_plugin/", "log", {0}),
    HookRule("addons/my_plugin/", "error", {0}),
};
```

## 输出示例

配置：
```cpp
HookRule("print")
```

GDScript：
```gdscript
print("Hello", "World", 123)
```

输出：
```
[GDScript] Function: print, ArgCount: 3
  [Arg 0] String: "Hello"
  [Arg 1] String: "World"
```

## 检查 Hook 是否工作

启动时应该看到：

```
[Hook] Setting up hooks...
[Hook] Searching in main module: godot.exe
[Hook] Trying signature #1: ...
[Hook] ✓ Found target function using signature #1 at: 0x...
[Hook] ✓ Target function hooked successfully!
[Hook] Loaded 3 hook rule(s):
  [1] Function: print, Args: ALL strings
  [2] Function: push_warning, Args: {0}
  [3] Path: player.gd, Function: take_damage, Args: {0}
```

## 故障排除

### 问题：没有输出

1. 检查 DebugView 是否正在运行并捕获输出
2. 确认 `g_HookRules` 不是空的
3. 确认函数名拼写正确

### 问题：找不到函数

```
[Hook] ✗ Failed to find target function after trying all 1 signatures
```

**解决方案：** 你的 Godot 版本可能不兼容，需要添加新的签名（见 HOOK_CONFIGURATION.md）

### 问题：路径不匹配

确保路径包含在实际脚本路径中：
- ✅ `"player.gd"` 可以匹配 `"res://scripts/player.gd"`
- ✅ `"scripts/"` 可以匹配 `"res://scripts/player.gd"`
- ❌ `"player"` 不能匹配 `"player.gd"`（需要包含 .gd）

## 性能提示

✅ **推荐**：
- 只监控需要的函数
- 使用路径过滤
- 指定参数索引

❌ **避免**：
- 监控高频函数（`_process`、`_physics_process`）
- 提取所有参数（如果不需要）
- 空规则但留空参数索引

## 下一步

- 📖 详细配置：阅读 [HOOK_CONFIGURATION.md](HOOK_CONFIGURATION.md)
- 💡 更多示例：查看 [HOOK_EXAMPLES.md](HOOK_EXAMPLES.md)
- 🔧 高级用法：修改 `hooks.cpp` 中的 `GDScriptCallp_Detour` 函数

## 禁用 Hook

临时禁用（不卸载 DLL）：

```cpp
std::vector<HookRule> g_HookRules = {
    // 留空 - Hook 已安装但不监控
};
```

完全禁用：从 Godot 目录删除 `winmm.dll`

## 需要帮助？

1. 检查配置语法是否正确
2. 确认函数名和路径拼写
3. 使用简单配置测试（如只监控 `print`）
4. 查看日志确认 Hook 是否成功安装
