# 字典词组前缀匹配 v1.2.0

**会话日期**: 2026-07-12
**项目**: Cardputer-IM (ESP32-S3 输入法设备)
**最终版本**: v1.2.0

---

## 工作内容

### 1. 字典词组前缀匹配
**文件**: `src/service/IME/IME.cpp`

将阶段4的字典词组匹配从精确匹配改为前缀匹配：

- **原行为**: 必须输入完整拼音才能匹配，如 `zhongguo` 才能匹配"中国"
- **新行为**: 输入部分拼音即可匹配，如 `zhg` 即可匹配 `zhongguo`(中国)、`bjs` 匹配 `beijingshi`(北京市)

关键改动:
- `IME.cpp:664` — 条件 `(int)cl == qlen` → `(int)cl >= qlen`
- 使词组匹配行为与单字匹配一致，提升输入效率

### 2. 固件构建
- 使用 `make pinyin` 构建 pinyin 固件
- 生成 `typewriter-pinyin.bin` (4.6 MB)

### 3. 版本发布
- 标签: git tag `v1.2.0`
- Release: https://github.com/autumnc/cardputer-im/releases/tag/v1.2.0
- 固件: `typewriter-pinyin.bin`
- 发布说明: `RELEASE_v1.2.0.md`

### 提交历史
```
6d14ce4 - Change dictionary phrase matching from exact to prefix match
```

---

**会话完成时间**: 2026-07-12
**GitHub Release**: https://github.com/autumnc/cardputer-im/releases/tag/v1.2.0
**会话状态**: ✅ 完成
