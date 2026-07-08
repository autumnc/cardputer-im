# Cardputer IM - 拼音输入法固件

> Fork 自 [micro-journal-wubi-v4-esp32](https://github.com/scateu/micro-journal-wubi-v4-esp32.git) 并进行了大量修改和增强

## 项目简介

这是为 M5Stack Cardputer ADV 设备定制的中文拼音输入法固件，在原有五笔输入法基础上完全重构了拼音输入引擎，实现了精确匹配优先、动态词频学习、用户词库管理等高级功能。

### 主要特性

- **5级匹配优先级**: 用户词库 → 精确匹配 → 简拼匹配 → 简拼+后字匹配 → 逐字匹配
- **动态词频**: 单字和词组都支持自动频次调整，常用词条自动置顶
- **用户词库管理**: `v`键删除模式，直观易用
- **海量词库**: 74,636词组 + 6,750单字，GB2312范围完整覆盖
- **无PSRAM优化**: 词库存储在Flash（2MB），内存仅占用几KB

## 与上游项目的区别

本项目基于 [micro-journal-wubi-v4-esp32](https://github.com/scateu/micro-journal-wubi-v4-esp32.git) 进行了以下重大改进：

### 核心改动

1. **拼音输入引擎完全重构**
   - 原项目：简单的拼音匹配，精确匹配会被简拼覆盖
   - 本项目：5级优先级匹配，精确匹配优先，符合中文输入习惯

2. **动态词频功能**
   - 原项目：无动态词频
   - 本项目：单字+词组都支持频次调整，自动学习用户习惯

3. **用户词库管理**
   - 原项目：仅支持添加用户词
   - 本项目：`v`键删除模式、清空词库、批量删除等完整管理功能

4. **词库大幅扩充**
   - 原项目：约5000词组
   - 本项目：74,636词组，GB2312完整覆盖

5. **匹配算法优化**
   - 实现"简拼+后字"匹配（如`zhguo`→`中国`）
   - `zh/ch/sh`作为特殊声母保持两字母
   - 更智能的前缀匹配和降级逻辑

## 固件下载

### 最新版本

- **完整固件**（推荐）: `build/typewriter-pinyin.bin` (4.5MB，含bootloader)
- **纯净固件**: `firmware-pinyin.bin` (4.4MB，仅应用)

### 刷写方法

```bash
# 完整固件（新设备或完整刷写）
esptool --chip auto --port /dev/ttyUSB0 --baud 921600 \
  --before default_reset write_flash -z 0x0 build/typewriter-pinyin.bin

# 纯净固件（已有设备升级）
esptool --chip auto --port /dev/ttyUSB0 --baud 921600 \
  --before default_reset write_flash -z 0x10000 firmware-pinyin.bin
```

## 使用指南

### 基本输入

```
输入: zhong
候选: 中、种、重... (精确匹配优先)

输入: zg
候选: 中国、总共... (简拼匹配)

输入: zhguo
候选: 中国 (zh简拼 + guo完整拼音)
```

### 动态词频

- 选择单字或词组后，频次自动+1
- 下次输入时，常用词条自动置顶
- 最多保存1000个用户词条

### 删除模式（`v`键）

```
1. 按 v 键 → 显示 "DD:" 和所有用户词条
2. 输入拼音过滤（可选）
3. 选择数字 → 删除该词条
4. 自动退出删除模式
```

### 两分输入（`u`键）

原项目的两分拆字输入保持不变：

```
按 u 键 → 显示 "LF:"
输入两分代码 → 选择拆字
```

## 编译说明

### 环境要求

- PlatformIO
- Python 3
- ESP32-S3 工具链

### 编译步骤

```bash
# 克隆仓库
git clone https://github.com/autumnc/cardputer-im.git
cd cardputer-im

# 编译拼音固件
make firmware-pinyin.bin

# 生成完整固件（含bootloader）
cd build
./build_firmwares.sh
```

### 词库生成

词库源文件: `/media/sf_share/pinyin-utf.txt`

```bash
# 重新生成词库
python3 IME/gen_ime.py --scheme pinyin \
  --src IME/pinyin_simp.dict.yaml \
  --word-src /media/sf_share/pinyin-utf.txt \
  --slot 2097152 \
  --out IME/ime_table_pinyin.bin
```

## 技术细节

### 内存优化（无PSRAM适配）

- **词库存储**: Flash 2MB slot，通过mmap读取
- **内存占用**: 
  - 前缀索引: 677条 (~2.7KB)
  - 用户词库: 最多1000词条 (~8KB)
  - 候选列表: 限制100个

### 文件结构

```
├── IME/
│   ├── gen_ime.py          # 词库生成脚本
│   ├── ime_table_pinyin.bin # 2MB词库文件
│   └── pinyin_simp.dict.yaml # 词库源文件
├── src/service/IME/
│   ├── IME.h               # IME核心头文件
│   ├── IME.cpp             # IME实现（匹配逻辑）
│   └── lf_data.cpp         # 两分数据
├── build/
│   └── typewriter-pinyin.bin # 完整固件
└── firmware-pinyin.bin     # 纯净固件
```

### 关键算法

#### 5级匹配优先级

1. **用户词库**: 按频次降序，最高优先级
2. **精确匹配**: 完整拼音匹配（如`zhong`→`中`）
3. **简拼匹配**: 纯辅音输入（如`zg`→`中国`）
4. **简拼+后字**: 混合匹配（如`zhguo`→`中国`）
5. **逐字匹配**: 前缀匹配，最后兜底

#### 动态词频实现

```cpp
struct UserEntry {
    String code;   // 拼音代码
    String word;   // 汉字/词组
    int count;     // 使用频次
};
```

- 选择词条时调用`bumpFrequency(code, word)`
- 频次递增并保存到SPIFFS
- 下次匹配时按频次排序

## 文档

- [PINYIN_REFACTOR_v4.md](PINYIN_REFACTOR_v4.md) - 拼音重构详细说明
- [USER_DICT_MANAGEMENT.md](USER_DICT_MANAGEMENT.md) - 用户词库管理文档
- [DYNAMIC_FREQUENCY_DETAIL.md](DYNAMIC_FREQUENCY_DETAIL.md) - 动态词频实现细节

## 兼容性

- **硬件**: M5Stack Cardputer ADV (ESP32-S3)
- **Flash**: 至少8MB
- **PSRAM**: 不需要（已优化）

## 已知问题

- 词库文件较大（1.2MB），占用87% Flash空间
- 用户词库最多1000条，超出后会FIFO淘汰

## 致谢

- 感谢 [scateu](https://github.com/scateu) 的原项目 [micro-journal-wubi-v4-esp32](https://github.com/scateu/micro-journal-wubi-v4-esp32.git) 提供了优秀的基础框架
- 词库来源：`/media/sf_share/pinyin-utf.txt`

## 许可证

继承自上游项目的开源许可证。

## 更新日志

### v4.0 (2026-07-08)

- 完全重构拼音输入引擎
- 实现5级匹配优先级
- 添加动态词频功能（单字+词组）
- 实现`v`键删除模式
- 词库扩充至74,636词组
- 无PSRAM优化

---

**原作者**: scateu  
**修改者**: autumnc  
**修改日期**: 2026-07-08