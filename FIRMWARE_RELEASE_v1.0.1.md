# P0修复固件发布报告

**发布日期**: 2026-07-09
**版本**: v1.0.1-p0-fixed
**构建时间**: 2026-07-09 08:52

---

## 📦 固件信息

### 主固件文件
```
文件名: typewriter-pinyin.bin
路径: /home/ywz/cardputer-im/build/typewriter-pinyin.bin
大小: 4.5 MB
MD5:  63b4684a02886e63b0d83b59539fd25b
编译时间: 2026-07-09 08:52
```

### 原始固件(不含bootloader)
```
文件名: firmware-pinyin.bin
路径: /home/ywz/cardputer-im/build/firmware-pinyin.bin
大小: 4.4 MB
说明: 仅包含应用程序,需要手动合并bootloader和分区表
```

---

## ✅ 包含的修复

此固件包含以下P0级别严重问题修复:

### 1. EPD内存分配失败处理 ✅
- **问题**: 内存分配失败后进入死循环,设备永久锁死
- **修复**: 显示错误信息,优雅退出,允许用户重启恢复
- **文件**: `src/display/EPD/display_EPD.cpp`

### 2. WiFi DMA缓冲区管理 ✅
- **问题**: 分配失败后可能导致WiFi操作崩溃
- **修复**: 添加`wifi_available`标志,优雅禁用WiFi功能
- **文件**: `src/app/app.cpp`, `src/service/Sync/Sync.cpp`

### 3. 多核心竞态条件 ✅
- **问题**: Editor类buffer访问缺少锁保护,可能导致数据损坏
- **修复**: 为`getBufferSize()`和`resetBuffer()`添加RAII锁
- **文件**: `src/service/Editor/Editor.h`

---

## 🔧 刷写说明

### 方法1: 使用M5Burner(推荐)
1. 下载 `typewriter-pinyin.bin`
2. 打开M5Burner
3. 选择设备: M5Cardputer
4. 选择固件文件并刷写

### 方法2: 使用esptool命令行
```bash
esptool --chip auto --port /dev/ttyUSB0 --baud 921600 \
  --before default_reset write_flash -z 0x0 \
  typewriter-pinyin.bin
```

**注意**: 将 `/dev/ttyUSB0` 替换为您的实际串口设备

---

## 📊 编译统计

### 内存使用
```
RAM:   44.2% (144,696 / 327,680 bytes)
Flash: 87.3% (4,578,093 / 5,242,880 bytes)
```

### IME词库统计
```
拼音音节: 433
词组数量: 74,636
词组字节: 1,153,089 bytes (1.1 MB)
独特汉字: 6,750
记录总数: 7,292
实际大小: 1,231,445 bytes (1.2 MB)
槽位大小: 2,097,152 bytes (2 MB, 使用率58%)
```

### 编译时间
- PlatformIO编译: ~37秒
- 总编译时间: ~3分钟(包括词库生成和固件合并)

---

## ⚠️ 重要提示

### 与旧版本的差异
此固件包含重要的安全修复,**强烈建议**从旧版本升级:

| 场景 | 旧版本行为 | 新版本行为 |
|------|-----------|-----------|
| PSRAM分配失败 | 永久死锁 | 显示错误,可重启 |
| WiFi DMA分配失败 | 可能崩溃 | WiFi功能禁用,仍可编辑 |
| 多核心buffer访问 | 可能数据损坏 | 线程安全,数据一致 |

### 兼容性
- ✅ 兼容所有M5Cardputer设备
- ✅ 配置文件格式不变
- ✅ 可从v1.0.0直接升级

---

## 🔄 后续计划

### P1级别修复(下一版本)
- 用户词库输入验证(单行长度限制)
- 文件大小整数溢出检查
- WiFi配置加载改进

### P2级别优化
- IME内存管理优化
- 查找算法性能优化

---

## 📝 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0.1-p0-fixed | 2026-07-09 | P0级别安全修复 |
| v1.0.0 | 2026-07-08 | 首个正式发布版本 |

---

## ✅ 验证清单

编译前检查:
- ✅ 代码审查完成
- ✅ P0修复验证通过
- ✅ 编译无警告/错误
- ✅ 固件大小正常(4.5MB)
- ✅ MD5校验生成

刷写前建议:
- ⚠️ 备份当前配置(`/wifi.json`, `/config.json`)
- ⚠️ 记录当前固件版本
- ⚠️ 准备回滚方案(保存旧固件)

---

**建议**: 在测试设备上验证此固件后再部署到生产环境。