# GDScript Hook 配置指南

## 功能说明

这个 hook 会拦截 Godot 引擎中的 `GDScriptInstance::callp` 函数，用于监控 GDScript 函数调用和提取字符串参数。

## 配置 Hook 规则

在 `hooks.cpp` 文件顶部，你可以配置 Hook 规则：

```cpp
std::vector<HookRule> g_HookRules = {
    // 规则配置...
};
```

### 规则格式

每个 `HookRule` 有三种构造方式：

#### 1. 只监控函数名（任何脚本，提取所有字符串参数）

```cpp
HookRule("function_name")
```

**示例：**
```cpp
HookRule("print"),           // 监控所有 print 调用
HookRule("push_warning"),    // 监控所有 push_warning 调用
```

#### 2. 监控函数名 + 指定参数索引

```cpp
HookRule("function_name", {arg_index1, arg_index2, ...})
```

**示例：**
```cpp
HookRule("my_function", {0, 2}),     // 只提取第 0 和第 2 个参数
HookRule("log_event", {0}),          // 只提取第 0 个参数
```

#### 3. 限制脚本路径 + 函数名 + 参数索引

```cpp
HookRule("res://path/to/script.gd", "function_name", {arg_index1, ...})
```

**示例：**
```cpp
HookRule("res://scripts/player.gd", "take_damage", {0}),     // 只监控 player.gd 中的 take_damage
HookRule("res://ui/menu.gd", "show_message", {0, 1}),        // 只监控 menu.gd 中的 show_message
```

### 完整配置示例

```cpp
std::vector<HookRule> g_HookRules = {
    // 示例 1: 监控所有 print 调用
    HookRule("print"),

    // 示例 2: 监控所有 push_warning，只提取第 0 个参数
    HookRule("push_warning", {0}),

    // 示例 3: 只监控 player.gd 中的 take_damage 函数
    HookRule("res://scripts/player.gd", "take_damage", {0}),

    // 示例 4: 监控 game_manager.gd 中的 log_event，提取第 0 和 1 个参数
    HookRule("res://scripts/game_manager.gd", "log_event", {0, 1}),

    // 示例 5: 监控任何脚本中的 _ready 函数（调试用）
    HookRule("_ready"),
};
```

### 路径匹配规则

路径使用**部分匹配**，因此：

- `"player.gd"` 会匹配 `res://scripts/player.gd`
- `"scripts/"` 会匹配 `res://scripts/` 下的所有脚本
- `"res://addons/my_plugin/"` 只匹配插件目录下的脚本

⚠️ **注意**：如果不指定路径（使用构造函数 1 或 2），则匹配**所有脚本**中的该函数。

### 参数索引说明

- 参数索引从 `0` 开始
- 如果不指定参数索引（留空集合），则提取**所有字符串类型**的参数
- 如果指定了索引，**只提取这些索引**的参数（即使是非字符串类型也会尝试，但只有字符串会输出）

## 输出格式

### 不限制路径的输出

```
[GDScript] Function: print, ArgCount: 2
  [Arg 0] String: "Hello World"
  [Arg 1] String: "From GDScript"
```

### 限制路径的输出

```
[GDScript] Path: res://scripts/player.gd
  Function: take_damage, ArgCount: 2
  [Arg 0] String: "Fire"
```

## Hook 初始化日志

成功时：

```
[Hook] Setting up hooks...
[Hook] Searching in main module: godot.windows.template_release.x86_64.exe
[Hook] Trying signature #1: 41 57 41 56 41 55 41 54 55 57 56 53 48 83 EC ?? ...
[Hook] ✓ Found target function using signature #1 at: 0x00007FF6B2C4A8E0
[Hook] ✓ Target function hooked successfully!
[Hook] All hooks set up successfully
[Hook] Loaded 3 hook rule(s):
  [1] Function: print, Args: ALL strings
  [2] Path: player.gd, Function: take_damage, Args: {0}
  [3] Function: log_event, Args: {0, 1}
```

无规则配置时：

```
[Hook] ⚠ No hook rules configured. Nothing will be monitored.
```

## 签名配置

在 `hooks.cpp` 的 `SetupAllHooks()` 函数中，定义了用于查找目标函数的字节签名：

```cpp
const char* signatures[] = {
    // Godot 4.3.0 x64 MinGW64
    "41 57 41 56 41 55 41 54 55 57 56 53 48 83 EC ?? 48 8B 05 ?? ?? ?? ?? ?? ?? ?? 4C 8B A4 24",
    
    // 可以添加更多签名用于不同版本的 Godot
    // "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC ??",
};
```

### 添加新签名

如果你的 Godot 版本使用了不同的编译选项，可能需要添加新的签名：

1. 使用 x64dbg 或 IDA Pro 找到 `GDScriptInstance::callp` 函数
2. 复制函数开头的字节码（建议至少 20-30 字节）
3. 将可变的字节替换为 `??`
4. 添加到 `signatures[]` 数组

**例如：**

```cpp
const char* signatures[] = {
    // Godot 4.3.0 MinGW64
    "41 57 41 56 41 55 41 54 55 57 56 53 48 83 EC ?? 48 8B 05 ?? ?? ?? ?? ?? ?? ?? 4C 8B A4 24",
    
    // Godot 4.3.0 MSVC
    "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ??",
    
    // Godot 4.2.x
    "40 53 48 83 EC ?? 48 8B D9 E8 ?? ?? ?? ?? 48 8B",
};
```

Hook 会按顺序尝试每个签名，直到找到匹配的为止。

## 输出格式

当监控的函数被调用时，会输出以下信息：

```
[GDScript] Function: print, ArgCount: 2
  [Arg 0] String: "Hello World"
  [Arg 1] String: "From GDScript"
```

只会输出字符串类型的参数，其他类型（int, float, bool 等）会被忽略。

## 调试日志

Hook 初始化时会输出以下日志：

```
[Hook] Setting up hooks...
[Hook] Searching in main module: godot.windows.template_release.x86_64.exe
[Hook] Trying signature #1: 41 57 41 56 41 55 41 54 55 57 56 53 48 83 EC ?? ...
[Hook] ✓ Found target function using signature #1 at: 0x00007FF6B2C4A8E0
[Hook] ✓ Target function hooked successfully!
[Hook] All hooks set up successfully
[Hook] Monitoring 2 specific function(s):
  - print
  - push_warning
```

如果找不到函数，会输出：

```
[Hook] ✗ Signature #1 not found, trying next...
[Hook] ✗ Signature #2 not found, trying next...
[Hook] ✗ Failed to find target function after trying all 2 signatures in module: godot.exe
[Hook] Tip: Check if signatures are correct or set 'targetModule' to the correct module name
```

## 使用场景示例

### 场景 1: 监控游戏日志系统

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("print"),
    HookRule("push_warning"),
    HookRule("push_error"),
};
```

在 GDScript 中：
```gdscript
print("Player spawned at position:", position)
push_warning("Low memory!")
```

输出：
```
[GDScript] Function: print, ArgCount: 2
  [Arg 0] String: "Player spawned at position:"
[GDScript] Function: push_warning, ArgCount: 1
  [Arg 0] String: "Low memory!"
```

### 场景 2: 只监控特定脚本的特定函数

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("res://scripts/player.gd", "take_damage", {0}),
    HookRule("res://scripts/enemy.gd", "on_death", {}),
};
```

在 player.gd 中：
```gdscript
func take_damage(damage_type: String, amount: int):
    print("Taking", amount, "damage from", damage_type)
```

输出：
```
[GDScript] Path: res://scripts/player.gd
  Function: take_damage, ArgCount: 2
  [Arg 0] String: "Fire"
```

### 场景 3: 调试对话系统

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("dialogue", "show_text", {0}),         // 只提取对话文本
    HookRule("dialogue", "set_speaker", {0}),       // 只提取说话者名字
};
```

### 场景 4: 监控整个目录

```cpp
std::vector<HookRule> g_HookRules = {
    // 监控 addons/my_plugin/ 下所有 log 调用
    HookRule("addons/my_plugin/", "log", {0}),
};
```

## 技术细节

### 函数签名

```cpp
Variant* __fastcall GDScriptInstance::callp(
    Variant* retstr,                    // RCX - 返回值结构体
    GDScriptInstance* this,             // RDX - 脚本实例
    const StringName* p_method,         // R8  - 函数名
    const Variant** p_args,             // R9  - 参数数组
    int p_argcount,                     // 栈 - 参数个数
    CallError* r_error                  // 栈 - 错误信息
)
```

### StringName 结构

- `0x8` 偏移：内置函数名（UTF-8 编码）
- `0x10` 偏移：自定义函数名（UTF-32 编码）

### Variant 结构

- `0x0` 偏移：类型标记（`0x4` = String）
- `0x8` 偏移：数据指针（对于 String 是 UTF-32 字符串）

### 提取返回值

如果需要提取返回值，可以在 `GDScriptCallp_Detour` 函数中，在调用原始函数后处理 `retstr`：

```cpp
// 调用原始函数
Variant* result = g_OriginalGDScriptCallp(retstr, thisptr, p_method, p_args, p_argcount, r_error);

// 提取返回值
if (result && rule) {
    std::string retValue = ExtractStringFromVariant(result);
    if (!retValue.empty()) {
        char buffer[512];
        sprintf_s(buffer, "  [Return] String: \"%s\"\n", retValue.c_str());
        OutputDebugStringA(buffer);
    }
}

return result;
```

### 提取非字符串类型参数

当前实现只提取字符串类型（`type == 0x4`）。如果需要其他类型：

```cpp
// 在 ExtractStringFromVariant 函数中
if (variant->type == 0x1) {  // Bool
    bool value = *(bool*)variant->data;
    return value ? "true" : "false";
}
else if (variant->type == 0x2) {  // Int
    int64_t value = *(int64_t*)variant->data;
    char buffer[32];
    sprintf_s(buffer, "%lld", value);
    return std::string(buffer);
}
// ... 更多类型
```

### 修改参数或阻止调用

在调用原始函数前，你可以：

```cpp
// 修改参数（小心！可能导致崩溃）
// 例如：替换第一个字符串参数
if (p_args && p_argcount > 0) {
    // 创建新的 Variant...
}

// 阻止函数调用
if (functionName == "dangerous_function") {
    OutputDebugStringA("[GDScript] Blocked dangerous_function call!\n");
    // 返回一个空的 Variant
    return retstr;  // 不调用原始函数
}
```

## 故障排除

### Hook 失败

1. 检查目标程序是否是 x64 架构
2. 验证 Godot 版本是否匹配
3. 尝试添加新的字节签名
4. 使用 DebugView 查看详细日志

### 路径不匹配

1. 使用 DebugView 查看实际的脚本路径
2. 临时使用不指定路径的规则来查看完整路径
3. 确认路径格式是否正确（`res://` vs 相对路径）

### 无法提取字符串

1. 确认 Variant 结构体定义是否正确
2. 检查偏移量是否与你的 Godot 版本匹配
3. 使用调试器验证内存布局
4. 尝试输出参数的 type 字段：
   ```cpp
   char buffer[128];
   sprintf_s(buffer, "  [Arg %d] Type: 0x%X\n", i, p_args[i]->type);
   OutputDebugStringA(buffer);
   ```

### 脚本路径提取失败

如果看到 `[null_instance]`、`[null_ptr1]` 等错误：

1. 偏移量 `0x18` 和 `0x3C0` 可能不适用于你的 Godot 版本
2. 使用调试器（x64dbg/IDA）检查 `GDScriptInstance` 的实际内存布局
3. 查找指向脚本路径的指针链

当前偏移量：
- `thisptr + 0x18` → 中间对象指针
- `[thisptr + 0x18] + 0x3C0` → 脚本路径（UTF-32 字符串）

### 崩溃问题

1. 检查是否正确处理了空指针
2. 确认函数签名与实际调用约定匹配
3. 验证结构体定义与实际内存布局一致
4. 确保不修改只读内存

## 示例用法

### 监控所有 print 语句

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("print"),
};
```

然后在 GDScript 中：

```gdscript
func _ready():
    print("Game initialized")  # 会被捕获
    print("Player count:", 5, "Position:", Vector2(100, 200))  # 只捕获字符串参数
```

输出：

```
[GDScript] Function: print, ArgCount: 1
  [Arg 0] String: "Game initialized"
[GDScript] Function: print, ArgCount: 5
  [Arg 0] String: "Player count:"
  [Arg 2] String: "Position:"
```

### 监控自定义函数

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("my_custom_function", {0}),  // 只提取第一个参数
};
```

```gdscript
func my_custom_function(message: String, value: int):
    print(message, value)

func _ready():
    my_custom_function("Hello", 42)  # 会被捕获
```

输出：

```
[GDScript] Function: my_custom_function, ArgCount: 2
  [Arg 0] String: "Hello"
```

### 只监控特定脚本

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("player.gd", "take_damage", {}),  // player.gd 中的所有 take_damage 调用
};
```

```gdscript
# res://scripts/player.gd
func take_damage(type: String, amount: int):
    print("Damage:", type, amount)

# res://scripts/enemy.gd  
func take_damage(type: String, amount: int):
    print("Enemy damage:", type, amount)  # 这个不会被监控
```

## 性能考虑

- Hook 本身开销很小（几个 CPU 周期）
- 字符串提取和转换有一定开销（每次约 1-10 微秒）
- 路径匹配使用字符串查找（`std::string::find`）
- **建议**：
  - 只监控你真正关心的函数
  - 使用路径过滤减少不必要的检查
  - 指定参数索引而不是提取所有参数
  - 避免在高频调用的函数上使用（如 `_process`、`_physics_process`）

### 性能对比

| 配置 | 每帧开销（估算） |
|------|-----------------|
| 空规则列表 | ~0 ns |
| 监控 1 个低频函数（如 print） | ~1-10 μs |
| 监控 10 个函数 | ~10-100 μs |
| 监控所有函数（不推荐） | ~100-1000 μs |

## 兼容性

- ✅ Godot 4.x (x64)
- ✅ MinGW64 编译的 Godot
- ⚠️ MSVC 编译的 Godot（可能需要不同的签名）
- ❌ Godot 3.x（结构体布局不同）
- ❌ x86 (32位) 版本

## 许可证

本项目采用与主项目相同的许可证。
