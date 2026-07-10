# 拼音匹配优先级重构 v1.1.0

**会话日期**: 2026-07-10
**项目**: Cardputer-IM (ESP32-S3 输入法设备)
**最终版本**: v1.1.0

---

## 工作内容

### 1. 拼音匹配优先级重构 (8级)
**文件**: `src/service/IME/IME.cpp`

将原来的5级匹配优先级重构为8级:
1. 用户词库单字匹配（前缀匹配）
2. 单字精确匹配（字典）
3. 用户词库词组匹配（前缀匹配）
4. 词组精确匹配（字典）
5. 用户词库简拼匹配（新增）
6. 简拼匹配（字典）
7. 简拼+后字匹配
8. 逐字匹配（兜底）

关键改动:
- 原用户词库匹配拆分为单字(Stage 1)和词组(Stage 3)
- 原字典精确匹配拆分为单字(Stage 2)和词组(Stage 4)
- 新增用户词库简拼匹配(Stage 5)
- 移除 if/else 分支，改为顺序执行的流水线
- 用户词库单字和词组匹配改为前缀匹配（非精确匹配）

### 2. 固件构建
- 使用 Makefile 构建 pinyin 固件
- 合并 bootloader + 分区表的完整刷写固件
- 清理 build/ 目录旧版本文件

### 3. 版本发布
- 版本号: `src/app/app.h` → `1.1.0`
- 标签: git tag `v1.1.0`
- Release: https://github.com/autumnc/cardputer-im/releases/tag/v1.1.0
- 固件: `build/typewriter-pinyin-v1.1.0.bin`
- 发布说明: `RELEASE_v1.1.0.md`

### 提交历史
```
8db453b - Bump version to v1.1.0 and add release notes
f445604 - Refactor pinyin matching to 8-stage priority system
```

---

**会话完成时间**: 2026-07-10
**GitHub Release**: https://github.com/autumnc/cardputer-im/releases/tag/v1.1.0
**会话状态**: ✅ 完成
