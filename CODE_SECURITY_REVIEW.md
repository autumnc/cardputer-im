# 代码安全审查报告

**项目**: Cardputer-IM (ESP32 墨水屏输入法设备)
**审查日期**: 2026-07-09
**审查重点**: 内存泄漏、死锁漏洞、安全漏洞、优化建议

---

## 🔴 严重问题 (可能导致系统死锁或崩溃)

### 1. **EPD显示内存分配失败处理不当** (display_EPD.cpp:57-63)

```cpp
framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
if (!framebuffer)
{
    Serial.println("alloc memory failed !!!");
    while (1)
        ;  // ❌ 死循环,无退出机制
}
```

**问题**:
- 内存分配失败后进入**无限死循环**,导致设备完全锁死
- 没有给用户任何反馈机制
- 无法恢复,只能强制重启

**影响**: 如果PSRAM不足或损坏,设备将永久卡死在此处

**修复建议**:
```cpp
framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
if (!framebuffer)
{
    _log("CRITICAL: EPD framebuffer allocation failed!\n");

    JsonDocument &app = status();
    app["error"] = "Display memory failed.\nDevice needs reboot.";
    app["screen"] = ERRORSCREEN;

    _ready = true;  // 允许显示错误屏幕
    return;         // 优雅退出,让用户看到错误信息
}
```

---

### 2. **WiFi DMA缓冲区管理不当** (app.cpp:128-136)

```cpp
extern void *wifi_scan_reserve;
wifi_scan_reserve = heap_caps_malloc(32768, MALLOC_CAP_DMA);
if (!wifi_scan_reserve) {
    _log("Failed to allocate DMA buffer for WiFi\n");
    JsonDocument &app = status();
    app["error"] = "Memory allocation failed.\nCannot use WiFi features.";
    app["screen"] = ERRORSCREEN;
    // ⚠️ 继续运行,但DMA缓冲区指针为NULL
}
```

**问题**:
- 分配失败后继续运行,wifi_scan_reserve为NULL
- sync_start() (Sync.cpp:90) 会尝试释放这个NULL指针:
  ```cpp
  if (wifi_scan_reserve) { free(wifi_scan_reserve); wifi_scan_reserve = nullptr; }
  ```
  虽然有检查,但如果其他地方直接使用这个指针会崩溃

**影响**: 可能导致WiFi功能不可用或随机崩溃

**修复建议**:
```cpp
if (!wifi_scan_reserve) {
    _log("WiFi DMA buffer allocation failed - WiFi disabled\n");
    JsonDocument &app = status();
    app["wifi_available"] = false;  // 明确标记WiFi不可用
    // 不设置ERRORSCREEN,允许设备继续作为纯文本编辑器使用
}
```

---

### 3. **多核心竞态条件风险** (Editor类, 缺少关键锁保护)

**问题位置**: Editor.cpp 多处

虽然实现了`Editor::Lock`机制,但以下关键函数**没有使用锁**:

1. **getBufferSize()** (Editor.h:104):
   ```cpp
   int getBufferSize() { return strlen(buffer); }
   ```
   - ❌ 无锁,多核心并发读取buffer可能读到不一致数据

2. **resetBuffer()** (Editor.h:105):
   ```cpp
   void resetBuffer() { memset(buffer, '\0', sizeof(buffer)); }
   ```
   - ❌ 无锁,与编辑操作并发可能导致数据损坏

3. **addChar/removeLastChar等编辑函数** - 需要确认是否都通过keyboard()获取锁

**影响**:
- Core 0 (键盘) 编辑buffer时,Core 1 (显示) 的autosave可能读取到不一致状态
- 可能导致文件损坏或显示异常

**修复建议**:
```cpp
int getBufferSize() {
    Lock guard(*this);
    return strlen(buffer);
}

void resetBuffer() {
    Lock guard(*this);
    memset(buffer, '\0', sizeof(buffer));
}
```

---

## 🟠 中等问题 (可能导致内存泄漏或性能问题)

### 4. **IME词库索引内存管理** (IME.cpp)

**位置**: 多处std::vector使用

```cpp
// IME.cpp:369-374
_all.clear();
if (_all.capacity() > 100)
    _all.shrink_to_fit();  // ⚠️ 频繁shrink_to_fit可能导致内存碎片

_page.clear();
if (_page.capacity() > _pageSize)
    _page.shrink_to_fit();
```

**问题**:
- 每次reset()都可能触发内存释放和重新分配
- `shrink_to_fit()`在某些STL实现中可能导致额外的内存拷贝
- ESP32上频繁内存分配/释���会加剧堆碎片化

**建议**:
```cpp
// 预留合理的初始容量,避免频繁重分配
if (_all.capacity() < 50)
    _all.reserve(50);  // 初始预留
// 只在容量过大时才shrink,提高阈值
if (_all.capacity() > 200)  // 提高阈值
    _all.shrink_to_fit();
```

---

### 5. **用户词库加载无大小限制** (IME.cpp:131-167)

```cpp
void IME::loadUserDict()
{
    // ...
    const size_t MAX_READ = 8192;  // ✅ 有读取限制
    size_t bytesRead = 0;

    while (f.available() && bytesRead < MAX_READ) {
        String line = f.readStringUntil('\n');  // ⚠️ 单行无长度限制
        line.trim();
        bytesRead += line.length();  // ⚠️ 不包括trim()删除的字符

        if (line.length() < 3) continue;
        // ... 添加到_userWords
    }
}
```

**问题**:
- 虽然限制了总读取量8KB,但**单行长度无限制**
- 恶意构造的超长行(如10000字符)可能导致String内存分配失败
- `bytesRead`统计不准确(trim前的长度)

**影响**: 可能被恶意文件触发内存耗尽

**修复建议**:
```cpp
while (f.available() && bytesRead < MAX_READ) {
    String line = f.readStringUntil('\n');

    // 限制单行长度
    if (line.length() > 256) {
        _log("[IME] User dict line too long, skipping\n");
        continue;
    }

    line.trim();
    bytesRead += line.length() + 1;  // +1 for '\n'

    // ...
}
```

---

### 6. **WiFi配置加载缺少输入验证** (WifiEntry.cpp:128-204)

```cpp
void wifi_config_load()
{
    String wifiString = file.readString();  // ⚠️ 无大小限制
    JsonDocument configDoc;
    DeserializationError error = deserializeJson(configDoc, wifiString);

    if (error) {
        // 删除损坏的文件
        gfs()->remove("/wifi.json");  // ⚠️ 可能删除用户重要配置
        return;
    }
}
```

**问题**:
- 读取文件无大小限制,恶意SD卡可能导致内存耗尽
- JSON解析失败直接删除文件,用户可能误操作丢失配置

**建议**:
```cpp
// 限制读取大小
const size_t MAX_WIFI_CONFIG = 4096;
size_t fileSize = file.size();
if (fileSize > MAX_WIFI_CONFIG) {
    _log("wifi.json too large: %u bytes\n", fileSize);
    app["error"] = "WiFi config too large";
    app["screen"] = ERRORSCREEN;
    file.close();
    return;
}

// 解析失败时备份而不是删除
if (error) {
    _log("wifi.json parse error, backing up\n");
    gfs()->rename("/wifi.json", "/wifi.json.bak");
    return;
}
```

---

### 7. **文件保存时的潜在整数溢出** (Editor.cpp:247-248)

```cpp
size_t newLength = getBufferSize();
size_t windowEnd = seekPos + loadedLength;  // ⚠️ 可能溢出
```

**问题**:
- 如果`seekPos`和`loadedLength`都接近`SIZE_MAX`,加法可能溢出
- 导致`windowEnd`值错误,可能误判为"无尾部数据"

**建议**:
```cpp
// 检查加法是否溢出
if (seekPos > SIZE_MAX - loadedLength) {
    _log("File offset overflow: seekPos=%u, loadedLength=%u\n",
         (unsigned)seekPos, (unsigned)loadedLength);
    app["error"] = "File too large";
    app["screen"] = ERRORSCREEN;
    return;
}
size_t windowEnd = seekPos + loadedLength;
```

---

## 🟡 轻微问题 (性能优化建议)

### 8. **IME查找算法优化机会**

**位置**: IME.cpp:412-833 (lookup函数)

当前实现有多个复杂的匹配阶段,存在优化空间:

1. **重复的去重检查** (多处):
   ```cpp
   bool dup = false;
   for (auto &e : _all) if (e == h) { dup = true; break; }
   if (!dup) _all.push_back(h);
   ```
   - 时间复杂度O(n²),���_all有100项时,每次添加都要比较100次

   **优化建议**: 使用`std::unordered_set`跟踪已添加的候选:
   ```cpp
   std::unordered_set<String> seen;
   // ... 在添加时
   if (seen.find(h) == seen.end()) {
       _all.push_back(h);
       seen.insert(h);
   }
   ```

2. **简拼提取算法** (IME.cpp:608-630):
   - 多次字符串扫描,可优化为单次遍历
   - 使用状态机模式会更高效

---

### 9. **String对象频繁创建销毁**

**问题位置**: 多处

```cpp
// IME.cpp:48 displayCode() - 每次调用都创建新String
String displayCode() const {
    String result;  // ⚠️ 频繁内存分配
    result.reserve(_prefix.length() + _code.length());
    result = _prefix;
    result += _code;
    return result;  // ⚠️ 返回值拷贝
}
```

**建议**:
```cpp
// 使用成员变量缓存,避免频繁分配
mutable String _displayCodeCache;
mutable bool _displayCodeDirty = true;

String displayCode() const {
    if (_displayCodeDirty) {
        _displayCodeCache = _prefix + _code;
        _displayCodeDirty = false;
    }
    return _displayCodeCache;
}
```

---

### 10. **sync_send() 的内存检查逻辑错误** (Sync.cpp:262)

```cpp
if (fileSize + SYNC_HEAP_MARGIN > ESP.getFreeHeap()) {
    // ⚠️ 逻辑错误: fileSize + margin 可能溢出
    sync_stop();
    // ...
}
```

**问题**: 如果`fileSize`很大,加法可能溢出,导致检查失效

**正确写法**:
```cpp
size_t freeHeap = ESP.getFreeHeap();
if (fileSize > freeHeap || (freeHeap - fileSize) < SYNC_HEAP_MARGIN) {
    // 分别检查,避免溢出
    sync_stop();
    // ...
}
```

---

## 🔵 其他发现

### 11. **配置文件缺少完整性校验**

当前`config.json`和`wifi.json`缺少校验机制:

- ✅ 建议: 在JSON中添加checksum字段
- ✅ 建议: 加载时验证文件完整性
- ✅ 建议: 重要配置变更前创建备份

### 12. **日志敏感信息泄露风险**

```cpp
// WifiEntry.cpp:119
_log("[sync_start] AP[%d]: SSID='%s', has_password=%d\n",
     i, ssid ? ssid : "(null)", pass ? 1 : 0);
```

**问题**: WiFi密码虽未打印,但SSID可能泄露位置信息

**建议**: 生产版本禁用详细WiFi日志

---

## 📊 总结

### 优先级修复顺序:

1. **P0 - 立即修复** (可能导致死锁):
   - EPD内存分配失败处理 (#1)
   - 多核心竞态条件 (#3)

2. **P1 - 高优先级** (可能导致崩溃):
   - WiFi DMA缓冲区管理 (#2)
   - 用户词库输入验证 (#5)
   - 文件大小整数溢出 (#7, #10)

3. **P2 - 中优先级** (性能和稳定性):
   - IME内存管理优化 (#4)
   - WiFi配置加载改进 (#6)

4. **P3 - 低优先级** (性能优化):
   - IME查找算法优化 (#8)
   - String对象优化 (#9)
   - 配置文件校验 (#11)

### 内存泄漏检查结果:

- ✅ **无明显内存泄漏**: 代码中使用了RAII模式和自动清理机制
- ⚠️ **潜在风险**: std::vector的频繁clear/shrink可能导致堆碎片
- ⚠️ **WiFi DMA缓冲区**: 全局指针,生命周期管理需改进

### 死锁风险检查结果:

- 🔴 **高风险**: EPD内存分配失败死循环
- 🟡 **中等风险**: 多核心竞态条件(Editor锁不完整)
- ✅ **已处理**: FreeRTOS递归锁使用正确(status()和Editor())

---

**建议**: 优先修复P0和P1级别问题,特别是EPD内存分配和多核心同步问题,这些可能导致设备完全锁死或数据损坏。
