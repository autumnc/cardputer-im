# 用户词库管理功能

## 功能概述

Cardputer IME 提供了完整的用户词库管理功能,支持删除单个词条、清空整个词库、以及按条件批量删除。

## 快速使用: 删除模式 (推荐)

### 使用方法

类似两分输入的`u`键模式,使用`v`键进入删除模式:

1. **进入删除模式**: 按`v`键(拼音输入状态下,第一个字母)
2. **输入拼音**: 输入要删除词条的拼音(��`zhong`、`zhguo`)
3. **选择删除**: 数字键选择要删除的词条,回车确认
4. **自动退出**: 删除后自动退出删除模式

### 示例

```
输入: v          → 进入删除模式(只匹配用户词库)
输入: vzhong     → 显示用户词库中zhong开头的词条
选择: 1          → 删除第一个候选词(如"中")
结果: 词条已删除,自动退出删除模式
```

### 特点

- **精确匹配**: 只显示用户词库的词条,避免误删系统词库
- **安全确认**: 选择后才删除,有确认过程
- **自动退出**: 删除后自动退出删除模式
- **直观易用**: 和两分输入`u`键一致的操作逻辑

---

## API 接口

### 1. 删除单个词条

```cpp
void IME::removeUserWord(const String &code, const String &word)
```

**功能**: 删除指定的用户词条

**参数**:
- `code`: 拼音代码(如"zhong"、"zhguo")
- `word`: 汉字或词组(如"中"、"中国")

**示例**:
```cpp
IME &ime = IME::getInstance();

// 删除单字
ime.removeUserWord("zhong", "中");

// 删除词组
ime.removeUserWord("zhguo", "中国");
ime.removeUserWord("zg", "总共");
```

**使用场景**:
- 删除误输入的词
- 删除不再需要的词条
- 精确控制词库内容

---

### 2. 清空整个用户词库

```cpp
void IME::clearUserDict()
```

**功能**: 清空所有用户词条,重置词库

**示例**:
```cpp
IME &ime = IME::getInstance();
ime.clearUserDict();
```

**使用场景**:
- 想要重新学习输入习惯
- 词库混乱需要重置
- 设备转让前清空个人数据

**注意**: 此操作不可恢复,会同时删除SPIFFS文件`/ime_user.txt`

---

### 3. 按条件批���删���

```cpp
void IME::pruneUserDict(int minCount = 0)
```

**功能**: 删除频次低于指定值的词条

**参数**:
- `minCount`: 最小频次阈值(默认0)

**删除规则**:
- `minCount = 0`: 删除所有`count < 0`的词条(从未被选择的)
- `minCount = 1`: 删除所有`count < 1`的词条(频次为0)
- `minCount = N`: 删除所有`count < N`的词条

**示例**:
```cpp
IME &ime = IME::getInstance();

// 删除从未被选择的词条(频次为0)
ime.pruneUserDict(1);

// 删除频次低于5的低频词条
ime.pruneUserDict(5);

// 删除频次低于10的词条,保留高频词
ime.pruneUserDict(10);
```

**使用场景**:
- 批量清理低频词
- 优化词库质量
- 减少词库大小

---

### 4. 查询词库大小

```cpp
size_t IME::userDictSize() const
```

**功能**: 获取当前用户词库的词条数量

**返回值**: 用户词条总数(最大1000)

**示例**:
```cpp
IME &ime = IME::getInstance();
size_t count = ime.userDictSize();
Serial.printf("User dict size: %d\n", count);
```

---

## 集成到UI的方式

### 方案1: 长按删除键

在输入法候选词界面,长按数字键删除对应的用户词条:

```cpp
// 在handleKey()中添加逻辑
if (key >= '1' && key <= '9' && longPress) {
    int idx = key - '1';
    if (idx < (int)_page.size()) {
        String word = _page[idx];
        // 提示用户确认删除
        // 确认后调用removeUserWord()
        removeUserWord(_code, word);
    }
}
```

### 方案2: 设置菜单

在设置菜单中添加"用户词库管理"选项:

```cpp
// 菜单结构
- 输入法设置
  - 用户词库管理
    - 查看词库 (显示词条列表)
    - 删除词条 (选择后删除)
    - 清空词��� (确认后清空)
    - 优化词库 (删除低频词)
```

### 方案3: 组合键快捷操作

定义特殊组合键:

```cpp
// Ctrl+Delete: 清空用户词库
// Alt+Delete: 删除当前候选词
// Shift+数字: 删除对应的候选词
```

---

## 实现细节

### 数据持久化

用户词库保存在SPIFFS文件系统:

```
路径: /ime_user.txt
格式: <拼音> <汉字> <频次>
限制: 最大8KB, 最多1000词条
```

### 频次机制

```cpp
// 初始添加: count = 0
addUserWord(code, word);  // 新词条count=0

// 选择一次: count++
bumpFrequency(code, word);  // count += 1

// 删除低频词:
pruneUserDict(1);  // 删除count < 1的词条
```

### 内存管理

```cpp
std::vector<UserEntry> _userWords;  // 内存中保存
bool _userDirty = false;             // 脏标志

// 修改后自动保存
_userDirty = true;
saveUserDict();  // 写入SPIFFS
```

---

## 使用示例代码

### 完整的UI集成示例

```cpp
#include "service/IME/IME.h"

void setup() {
    IME &ime = IME::getInstance();
    ime.begin();
}

// 删除当前选中的候选词
void deleteCurrentCandidate() {
    IME &ime = IME::getInstance();

    // 获取当前输入的拼音和候选词
    String code = ime.composition();
    auto &candidates = ime.candidates();

    if (candidates.size() > 0) {
        String word = candidates[0];  // 第一个候选词

        // 显示确认对话框
        if (confirmDialog("删除词条?", word)) {
            ime.removeUserWord(code, word);
            showMessage("已删除: " + word);
        }
    }
}

// 清空用户词库(需确认)
void resetUserDict() {
    IME &ime = IME::getInstance();

    if (confirmDialog("清空用户词库?", "此操作不可恢复")) {
        size_t before = ime.userDictSize();
        ime.clearUserDict();
        showMessage("已清空 " + String(before) + " 个词条");
    }
}

// 优化用户词库(删除低频词)
void optimizeUserDict() {
    IME &ime = IME::getInstance();

    size_t before = ime.userDictSize();
    ime.pruneUserDict(1);  // 删除频次为0的词条
    size_t after = ime.userDictSize();

    showMessage("优化完成: 删除 " + String(before - after) + " 个低频词");
}
```

---

## 注意事项

1. **数据安全**:
   - `clearUserDict()` 会永久删除所有数据,建议添加确认对话框
   - `pruneUserDict()` 不可恢复,建议提示用户

2. **性能考虑**:
   - 每次修改都会写入SPIFFS,避免频繁调用
   - `pruneUserDict()` 会遍历整个词库,建议在后台执行

3. **线程安全**:
   - 所有方法都是单例模式,无需额外加锁
   - 但避免在输入过程中同时修改词库

4. **存储限制**:
   - SPIFFS最大8KB,1000词条
   - 定期`pruneUserDict()`优化可保持词库高效

---

## 更新日志

- 2026-07-08: 新增用户词库管理功能
  - `removeUserWord()` 删除单个词条
  - `clearUserDict()` 清空整个词库
  - `pruneUserDict()` 按条件批量删除
  - `userDictSize()` 查询词库大小