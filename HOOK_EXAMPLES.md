# GDScript Hook 配置示例

这里提供一些常见的 Hook 配置示例，可以直接复制到 `hooks.cpp` 中使用。

## 基础示例

### 1. 监控所有日志函数

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("print"),
    HookRule("print_debug"),
    HookRule("print_rich"),
    HookRule("print_verbose"),
    HookRule("push_warning"),
    HookRule("push_error"),
};
```

### 2. 只监控错误和警告

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("push_warning", {0}),  // 只提取警告消息
    HookRule("push_error", {0}),    // 只提取错误消息
};
```

### 3. 监控特定脚本的所有函数调用

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("res://scripts/player.gd", "_ready", {}),
    HookRule("res://scripts/player.gd", "_process", {}),
    HookRule("res://scripts/player.gd", "take_damage", {}),
    HookRule("res://scripts/player.gd", "heal", {}),
};
```

## 游戏开发场景

### 对话系统监控

```cpp
std::vector<HookRule> g_HookRules = {
    // 监控所有对话相关函数
    HookRule("show_dialogue", {0}),        // 对话文本
    HookRule("set_speaker", {0}),          // 说话者名字
    HookRule("add_choice", {0}),           // 选项文本
    
    // 只监控特定场景的对话
    HookRule("res://scenes/intro.gd", "play_cutscene", {0}),
};
```

### 网络日志监控

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("NetworkManager", "send_message", {0, 1}),
    HookRule("NetworkManager", "receive_message", {0}),
    HookRule("NetworkManager", "connect_to_server", {0}),  // 服务器地址
};
```

### 资源加载监控

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("load_scene", {0}),           // 场景路径
    HookRule("load_resource", {0}),        // 资源路径
    HookRule("preload_asset", {0}),        // 资源路径
    HookRule("ResourceLoader", "load", {0}),
};
```

### 本地化/翻译监控

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("tr", {0}),                   // 翻译键
    HookRule("set_text", {0}),             // UI 文本
    HookRule("LocalizationManager", "get_translation", {0}),
};
```

## 调试场景

### 追踪特定玩家行为

```cpp
std::vector<HookRule> g_HookRules = {
    // 只监控 player.gd 中的关键函数
    HookRule("player.gd", "move_to", {}),
    HookRule("player.gd", "attack", {0}),          // 攻击目标
    HookRule("player.gd", "use_item", {0}),        // 使用的物品
    HookRule("player.gd", "interact_with", {0}),   // 交互对象
};
```

### 追踪 AI 决策

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("AIController", "make_decision", {0}),
    HookRule("AIController", "log_state", {0}),
    HookRule("enemy_ai.gd", "choose_action", {}),
};
```

### 调试存档系统

```cpp
std::vector<HookRule> g_HookRules = {
    HookRule("SaveManager", "save_data", {0}),     // 保存的键
    HookRule("SaveManager", "load_data", {0}),     // 加载的键
    HookRule("SaveManager", "delete_save", {0}),   // 删除的存档
};
```

## 插件/模组开发

### 监控整个插件目录

```cpp
std::vector<HookRule> g_HookRules = {
    // 监控你的插件中所有 log 调用
    HookRule("addons/my_plugin/", "log", {0}),
    HookRule("addons/my_plugin/", "error", {0}),
    HookRule("addons/my_plugin/", "debug", {}),
};
```

### 监控插件 API 调用

```cpp
std::vector<HookRule> g_HookRules = {
    // 监控其他模组调用你的 API
    HookRule("MyPluginAPI", "register_item", {0}),      // 物品 ID
    HookRule("MyPluginAPI", "trigger_event", {0}),      // 事件名
    HookRule("MyPluginAPI", "send_command", {0, 1}),    // 命令和参数
};
```

## 性能分析场景

### 监控频繁调用的函数（谨慎使用！）

```cpp
std::vector<HookRule> g_HookRules = {
    // 只在性能测试时启用，生产环境禁用！
    HookRule("_process", {}),
    HookRule("_physics_process", {}),
    HookRule("_input", {}),
};
```

### 只监控错误路径

```cpp
std::vector<HookRule> g_HookRules = {
    // 只监控可能出错的函数
    HookRule("file_not_found", {0}),
    HookRule("invalid_operation", {0}),
    HookRule("null_reference", {0}),
};
```

## 高级配置

### 多环境配置

```cpp
// 开发环境：监控所有内容
#ifdef DEBUG
std::vector<HookRule> g_HookRules = {
    HookRule("print"),
    HookRule("push_warning"),
    HookRule("push_error"),
    // ... 更多规则
};
#else
// 生产环境：只监控错误
std::vector<HookRule> g_HookRules = {
    HookRule("push_error", {0}),
    HookRule("critical_error", {0}),
};
#endif
```

### 选择性监控

```cpp
std::vector<HookRule> g_HookRules = {
    // 监控所有脚本的 print，但只提取第一个参数
    HookRule("print", {0}),
    
    // 只监控 res://core/ 目录下的警告
    HookRule("res://core/", "push_warning", {0}),
    
    // 完全监控 player.gd 中的 take_damage
    HookRule("res://scripts/player.gd", "take_damage", {}),
    
    // 其他脚本的 take_damage 只提取伤害类型（假设在参数 0）
    HookRule("take_damage", {0}),
};
```

## 配置技巧

### 1. 从宽松到严格

初期调试时使用宽松规则：
```cpp
HookRule("my_function")  // 提取所有参数
```

找到问题后收紧规则：
```cpp
HookRule("my_function", {0, 2})  // 只提取需要的参数
```

最终加上路径限制：
```cpp
HookRule("res://scripts/buggy.gd", "my_function", {0})
```

### 2. 使用注释组织规则

```cpp
std::vector<HookRule> g_HookRules = {
    // ===== 系统日志 =====
    HookRule("print"),
    HookRule("push_error"),
    
    // ===== 玩家相关 =====
    HookRule("player.gd", "take_damage", {0}),
    HookRule("player.gd", "level_up", {}),
    
    // ===== 网络相关 =====
    HookRule("NetworkManager", "send_packet", {0}),
    
    // ===== 临时调试（完成后删除）=====
    // HookRule("debug_function", {}),
};
```

### 3. 性能优化

```cpp
// ❌ 不好：会产生大量日志
HookRule("_process")

// ✅ 好：只监控关键函数
HookRule("on_button_clicked", {0})

// ✅ 好：限制到特定脚本
HookRule("player.gd", "_ready", {})

// ✅ 好：只提取需要的参数
HookRule("log", {0})  // 而不是 HookRule("log")
```

## 空配置（禁用 Hook）

如果暂时不需要监控，使用空配置：

```cpp
std::vector<HookRule> g_HookRules = {
    // 空配置 - Hook 已安装但不监控任何函数
};
```

这样 Hook 开销几乎为零（只有一个空检查）。

## 常见错误

### ❌ 错误 1：忘记指定参数

```cpp
HookRule("function_name" {0})  // 缺少逗号
```

应该是：
```cpp
HookRule("function_name", {0})
```

### ❌ 错误 2：路径格式错误

```cpp
HookRule("\\res\\scripts\\player.gd", "func", {})  // 错误的斜杠
```

应该是：
```cpp
HookRule("res://scripts/player.gd", "func", {})  // 或者
HookRule("player.gd", "func", {})                 // 部分匹配
```

### ❌ 错误 3：参数索引越界

```gdscript
func my_func(a: String):  # 只有 1 个参数
    pass
```

```cpp
HookRule("my_func", {0, 1, 2})  // 索引 1 和 2 不存在
```

应该是：
```cpp
HookRule("my_func", {0})
```

## 结语

选择合适的配置可以：
- 减少日志噪音
- 提高性能
- 快速定位问题
- 不干扰正常游戏运行

建议从小规模开始，逐步添加需要的规则！
