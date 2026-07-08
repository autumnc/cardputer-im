# 动态词频功能详解

## 实现原理

### 1. 数据结构

```cpp
// 用户词库条目结构
struct UserEntry {
    String code;    // 拼音代码 (如 "zhong", "zhguo", "zg")
    String word;    // 汉字/词组 (如 "中", "中国")
    int count;      // 使用频次
};

std::vector<UserEntry> _userWords;  // 内存中保存用户词库
bool _userDirty = false;             // 脏标志,需要保存
```

**存储位置**:
- 运行时: 内存`_userWords`向量
- 持久化: SPIFFS文件系统`/ime_user.txt`

**容量限制**:
- 最多1000个词条
- 文件大小最大8KB

---

## 2. 单字和词组都支持动态词频

### 判断标准

```cpp
// UTF-8编码规则:
// 单字: word.length() == 3 字节 (如 "中")
// 词组: word.length() > 3 字节 (如 "中国" = 6字节)
```

### bumpFrequency函数流程

```cpp
void IME::bumpFrequency(const String &code, const String &word)
{
    // 步骤1: 查找是否已存在
    for (auto it = _userWords.begin(); it != _userWords.end(); ++it)
    {
        if (it->code == code && it->word == word)
        {
            // 已存在: 频次+1
            it->count++;
            _userDirty = true;
            saveUserDict();
            return;
        }
    }

    // 步骤2: 不存在则添加
    // 支持: word.length() >= 3 (单字和词组)
    if (word.length() >= 3 && code.length() >= 1)
    {
        addUserWord(code, word);  // 添加到用户词库
        // 立即设置count++
        for (auto &p : _userWords)
            if (p.code == code && p.word == word) {
                p.count++;
                break;
            }
    }
}
```

**关键点**:
- ✅ `word.length() >= 3` 包括单字(3字节)和词组(>3字节)
- ✅ 第一次选择时自动添加,频次设为1
- ✅ 再次选择时频次递增

---

## 3. 匹配优先级

### lookup函数中的用户词库匹配

```cpp
// 阶段1: 用户词库匹配 (最高优先级)
std::vector< std::pair<int, String> > userFreq;
for (auto &p : _userWords) {
    if (p.code.length() < qlen) continue;
    if (strncmp(p.code.c_str(), q, qlen) == 0) {
        userFreq.push_back({p.count, p.word});  // 提取频次
    }
}

// 按频次降序排序 (频次高的排前面)
std::sort(userFreq.begin(), userFreq.end(),
    [](const std::pair<int,String> &a, const std::pair<int,String> &b) {
        return a.first > b.second;  // 降序
    });

// 添加到候选列表 (置顶显示)
for (auto &f : userFreq) {
    _all.push_back(f.second);
    if (_all.size() >= 100) break;
}
```

**优先级顺序**:
1. **用户词库** (按频次降序)
2. 精确匹配
3. 简拼匹配
4. 简拼+后字匹配
5. 逐字匹配

---

## 4. 调用时机

### commit函数中触发频次更新

```cpp
bool IME::commit(int idx, String &out)
{
    // ... 选择候选词 ...
    
    if (_prefix.length() > 0)
    {
        // 逐字输入完成: 组合词组
        _prefix += out;
        addUserWord(_codeOrig, _prefix);      // 添加词组
        bumpFrequency(_codeOrig, _prefix);    // 更新频次
        out = _prefix;
    }
    else
    {
        // 单步选择: 单字或词组
        bumpFrequency(_code, out);  // 更新频次
    }
}
```

**触发场景**:
1. **选择单字**: 输入`zhong`→选择`1`→`bumpFrequency("zhong", "中")`
2. **选择词组**: 输入`zhguo`→选择`1`→`bumpFrequency("zhguo", "中国")`
3. **逐字输入**: 输入`zhongguo`→选`中`→选`国`→`bumpFrequency("zhongguo", "中国")`

---

## 5. 实际运行示例

### 示例1: 单字频次调整

```
初始状态: 用户词库为空

输入: zhong
候选: 中、种、重... (系统词库顺序)
选择: 1 (选择"中")
动作: bumpFrequency("zhong", "中")
结果: 用户词库中添加 {code:"zhong", word:"中", count:1}

再次输入: zhong
候选: 中 (频次=1,置顶)、种、重... (用户词库优先)
选择: 1
动作: bumpFrequency("zhong", "中")
结果: 更新为 {code:"zhong", word:"中", count:2}

第三次输入: zhong
候选: 中 (频次=2,依然置顶)、种、重...
```

### 示例2: 词组频次调整

```
初始状态: 用户词库为空

输入: zhguo
候选: 中国、照顾... (系统词库顺序)
选择: 1 (选择"中国")
动作: bumpFrequency("zhguo", "中国")
结果: 用户词库中添加 {code:"zhguo", word:"中国", count:1}

再次输入: zhguo
候选: 中国 (频次=1,置顶)、照顾...
选择: 1
动作: bumpFrequency("zhguo", "中国")
结果: 更新为 {code:"zhguo", word:"中国", count:2}
```

### 示例3: 多词条频次排序

```
用户词库内容:
zhong 中 5
zhong 种 2
zhong 重 3

输入: zhong
匹配: 所有zhong开头的用户词条
排序: 按频次降序 → 中(5)、重(3)、种(2)
显示: 1.中  2.重  3.种  (用户词库置顶)
      4.终  5.钟  6.众... (系统词库)
```

---

## 6. 持久化机制

### SPIFFS文件格式

文件路径: `/ime_user.txt`

格式:
```
zhong 中 5
zhguo 中国 12
zg 总共 8
nihao 你好 3
```

### 加载和保存

```cpp
void IME::loadUserDict()
{
    if (!SPIFFS.begin(false)) return;
    File f = SPIFFS.open("/ime_user.txt", "r");
    if (!f) return;

    while (f.available() && bytesRead < MAX_READ) {
        String line = f.readStringUntil('\n');
        // 解析: <code> <word> <count>
        // 添加到_userWords向量
    }
    f.close();
}

void IME::saveUserDict()
{
    if (!_userDirty) return;
    if (!SPIFFS.begin(false) && !SPIFFS.begin(true)) return;

    File f = SPIFFS.open("/ime_user.txt", "w");
    for (auto &p : _userWords) {
        f.print(p.code); f.print(" ");
        f.print(p.word); f.print(" ");
        f.println(p.count);
    }
    f.close();
    _userDirty = false;
}
```

---

## 7. 内存和性能优化

### 内存占用

```
每个UserEntry: 约20-30字节
1000词条总计: 约20-30KB RAM

SPIFFS文件: 最大8KB
```

### 性能考虑

- ✅ 用户词库在内存中,匹配速度快
- ✅ 每次修改立即保存,防止数据丢失
- ✅ 按频次排序使用`std::sort`,时间复杂度O(n log n)
- ✅ 候选词限制100个,避免内存溢出

---

## 8. 边界情况处理

### 容量限制

```cpp
void IME::addUserWord(const String &code, const String &word)
{
    // 检查是否已存在
    for (auto &p : _userWords)
        if (p.code == code && p.word == word) return;

    // 容量超限: 删除最旧的条目 (FIFO)
    if (_userWords.size() >= 1000)
        _userWords.erase(_userWords.begin());

    _userWords.push_back({code, word, 0});
    _userDirty = true;
    saveUserDict();
}
```

### 频次溢出

- `count`字段为`int`类型
- 理论最大值: 2,147,483,647
- 实际使用中不会溢出(假设每天输入1000次,需要5893年)

---

## 9. 与系统词库的关系

### 独立性

- **用户词库**: 完全独立,不修改系统词库
- **系统词库**: Flash中的`ime_table.bin`,只读
- **合并显示**: 用户词库优先,然后是系统词库

### 冲突处理

```
输��: zhong
用户词库: {zhong, 中, 5}
系统词库: {zhong, 中} {zhong, 种} {zhong, 重}...

显示顺序:
1. 中 (用户词库,频次=5) ← 置顶
2. 种 (系统词库)
3. 重 (系统词库)
```

**去重机制**:
```cpp
// lookup函数中
for (auto &f : userFreq) {
    _all.push_back(f.second);  // 用户词库先添加
}

// 后续匹配系统词库时
for (auto &e : _all)
    if (e == h) { dup = true; break; }  // 检查重复
if (!dup) _all.push_back(h);  // 不重复才添加
```

---

## 总结

### 核心机制

1. **单字和词组都支持**: `word.length() >= 3` 包括所有UTF-8汉字
2. **频次计数**: 选择一次+1,自动排序
3. **置顶显示**: 用户词库优先级最高
4. **持久化**: SPIFFS存储,掉电不丢失
5. **容量控制**: 最多1000词条,先进先出

### 实际效果

- ✅ 常用字/词自动排前面
- ✅ 输入习惯自动学习
- ✅ 无需手动调整词库
- ✅ 跨会话保持(掉电不丢失)

**这就是真正的动态词频功能!** 🎯