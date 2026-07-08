# Cardputer 拼音输入法重构 v4.0

## 核心改进 (2026-07-08)

### 1. 新的5级匹配优先级 (严格按序)

按照需求文档《ESP32实现拼音输入法匹配逻辑.md》实现:

1. **用户词库** (最高优先级)
   - 单字+词组都支持动态频次
   - 按使用频次降序排序
   - 最多保存1000个用户字/词

2. **精确匹配** (完整拼音)
   - 输入zhong→直接显示"中"等精确匹配
   - 区分有无元音,有元音时优先精确匹配
   - 单字+词组都在此阶段匹配

3. **简拼匹配** (纯辅音输入)
   - 输入zg→匹配"中国""总共"等简拼
   - zh/ch/sh保持两字母,其他声母取首字母
   - 无元音时触发简拼匹配

4. **简拼+后字匹配**
   - 输入zhguo→匹配zh简拼+guo完整拼音→"中国"
   - 从后往前检测最后一个音节
   - 简拼部分纯辅音,后字部分完整拼音

5. **逐字匹配** (最后兜底)
   - 前缀匹配,降级逐字组合
   - 输入超出最长匹配时触发

### 2. 动态词频功能

- **单字支持**: bumpFrequency扩展到单字(word.length==3)
- **词组支持**: 词组(word.length>3)也自动调频
- **持久化**: SPIFFS保存用户字/词,最多1000个
- **自动置顶**: 用户常用字/词自动排在前面

### 3. 词库优化

- **来源**: `/media/sf_share/pinyin-utf.txt` (74,636词组)
- **过滤**: 只提取GB2312范围内的字/词
- **大小**: 2MB slot (实际使用1.2MB,58%利用率)
- **内容**: 6,750单字 + 74,636词组

### 4. 无PSRAM适配

- **词库存储**: Flash 2MB slot (通过mmap读取)
- **内存索引**: 677条前缀索引 (~2.7KB)
- **用户词库**: SPIFFS存储 (最多8KB/1000词)
- **候选词限制**: 100个候选词,避免内存溢出
- **避免String分配**: 简拼+后字匹配使用raw bytes比较

### 5. 代码重构

- **完全重写lookup()**: 清晰分离5个匹配阶段
- **删除旧shorthand+tail逻辑**: 新逻辑更简洁高效
- **gen_ime.py修复**: load_words正确加载所有GB2312词组
- **IME3格式**: 保持向后兼容

## 测试场景

根据需求文档验证:

### 精确匹配优先
```
输入: z
显示: z开头单字 (早、在、做...)

输入: zh
显示: zh开头单字 (中、这、着...)

输入: zhong
显示: "中"等精确匹配 (不是zh简拼)
```

### 简拼匹配
```
输入: zg
显示: 中国、总共、常见等简拼匹配
然后: 逐字匹配(z开头的词+g开头的字)
```

### 简拼+后字匹配
```
输入: zhguo
显示: 中国 (zh简拼+guo完整拼音)
匹配: 照顾、只顾等zhg开头的词组

输入: zhg
显示: 中国、照顾 (zhg简拼)
```

### 动态词频
```
选择: "中" (单字)
下次输入zhong: "中"置顶显示

选择: "中国" (词组)
下次输入zhguo: "中国"置顶显示
```

## 技术细节

### gen_ime.py修改
```python
def all_gb2312(s, gb2312_set):
    for c in s:
        if ord(c) not in gb2312_set:
            return False
    return True

def load_words(path, gb2312_set):
    for e in parts[1:]:
        # 单字(len 1) 和 词组(len >= 2)
        if len(e) >= 1 and all_gb2312(e, gb2312_set):
            yield code, e, rank
```

### IME.cpp核心逻辑
```cpp
void IME::lookup() {
    // 阶段1: 用户词库 (频次排序)
    // 阶段2: 精确匹配 (hasVowel=true)
    // 阶段3: 简拼匹配 (hasVowel=false)
    // 阶段4: 简拼+后字匹配
    // 阶段5: 逐字匹配
}

void IME::bumpFrequency(code, word) {
    // 单字(word.length==3)和词组都支持
}
```

### Makefile编译
```bash
python3 IME/gen_ime.py --scheme pinyin \
  --src IME/pinyin_simp.dict.yaml \
  --word-src /media/sf_share/pinyin-utf.txt \
  --slot 2097152 \
  --out IME/ime_table_pinyin.bin
```

## 固件生成

```bash
make firmware-pinyin.bin
```

刷写方法:
```bash
esptool --chip auto --port /dev/ttyUSB0 --baud 921600 \
  --before default_reset write_flash -z 0x0 firmware-pinyin.bin
```

---

**重构日期**: 2026-07-08
**状态**: ✅ 完成,等待编译测试