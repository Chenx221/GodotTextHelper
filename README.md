# GodotTextHelper

// godot 4.7-dev2 x64 (official)

// godot 4.6.1 x64 (official)

// godot 4.6 x64 (official)

```json
"builtinFunctionNameUTF32": true,
"gdscriptPathOffset": "0x350",
```



// godot 4.5.1 x64 (official)

// godot 4.5 x64 (official)

```json
"builtinFunctionNameUTF32": true,
"gdscriptPathOffset": "0x390",
```



// godot 4.4.1 x64 (official)

// godot 4.4 x64 (official)

```json
"gdscriptPathOffset": "0x418",
```



// godot 4.3 x64 (official)

```
Leave as default
```



// godot 4.2.2 x64 (official)

// godot 4.2.1 x64 (official)

// godot 4.2 x64 (official)

```json
"gdscriptPathOffset": "0x3B0",
```



// godot 4.1.4 x64 (official)

// godot 4.1.3 x64 (official)

// godot 4.1.2 x64 (official)

// godot 4.1.1 x64 (official)

// godot 4.1 x64 (official)

```json
"gdscriptPathOffset": "0x390",
```



// godot 4.0.4 x64 (official)

// godot 4.0.3 x64 (official)

// godot 4.0.2 x64 (official)

// godot 4.0.1 x64 (official)

// godot 4.0 x64 (official)

```json
"gdscriptPathOffset": "0x3B8",
```



---

示例配置

```

{
    "clipboard": false,
    "logFunctionName": true,
    "filterDuplicateFunctionLog": true,
    "gdscriptInstanceOffset": "0x18",
    "gdscriptPathOffset": "0x3C0",
    "builtinFunctionNameUTF32": false,
    "rules": [
        {
            "function": "add_text",
            "script": "res://scripts/UI.gd",
            "args": [0],
            "post": false
        },
        {
            "function": "say_dialog",
            "args": [0, 1]
        },
        {
            "function": "_on_battle_start",
            "post": true
        }
    ],
    "regexFilter": "\\[[^\\]]*?\\]"
}
```



---

翻译内嵌（实验性）
