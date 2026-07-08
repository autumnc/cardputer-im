# 手动发布 v2.0.0 Release 指南

由于网络连接问题，代码尚未自动推送。请按以下步骤手动发布：

## 步骤 1: 推送代码和标签

```bash
# 推送代码到远程仓库
git push origin main

# 或者推送到你的 fork
git push autumnc main

# 推送标签
git push origin v2.0.0
# 或
git push autumnc v2.0.0
```

## 步骤 2: 在 GitHub 创建 Release

### 方法 A: 使用 GitHub CLI (推荐)

```bash
# 安装 gh CLI (如果未安装)
# Linux: sudo apt install gh
# macOS: brew install gh

# 登录 GitHub
gh auth login

# 创建 release
gh release create v2.0.0 \
  --title "v2.0.0: 运行时简拼算法，解决编码冲突" \
  --notes-file release_v2.0.0/RELEASE_NOTES.md \
  release_v2.0.0/typewriter-pinyin-v2.0.0-runtime-sh.bin
```

### 方法 B: 使用 GitHub Web UI

1. 访问仓库: https://github.com/scateu/micro-journal-wubi-v4-esp32
2. 点击 "Releases" → "Draft a new release"
3. 选择标签: `v2.0.0`
4. 标题: `v2.0.0: 运行时简拼算法，解决编码冲突`
5. 描述: 复制 `release_v2.0.0/RELEASE_NOTES.md` 的内容
6. 上传文件: `release_v2.0.0/typewriter-pinyin-v2.0.0-runtime-sh.bin`
7. 点击 "Publish release"

## 步骤 3: 发布到 M5Burner (可选)

如果希望用户通过 M5Burner 直接下载：

1. 访问 M5Stack 官方固件库
2. 提交固件到官方列表
3. 或在项目 README 中提供下载链接

## 已准备好的文件

- ✅ `release_v2.0.0/typewriter-pinyin-v2.0.0-runtime-sh.bin` (4.5 MB) - 固件文件
- ✅ `release_v2.0.0/RELEASE_NOTES.md` - 发布说明
- ✅ Git 标签 `v2.0.0` - 已创建
- ✅ Git 提交 `f6ce8bb` - 已提交

## 发布后验证

发布完成后，检查：
- ✅ Release 页面显示正确
- ✅ 固件文件可下载
- ✅ 发布说明完整
- ✅ 标签链接正确

## 备用方案

如果无法推送，可以：
1. 创建 GitHub Issue 说明版本发布
2. 在 README.md 中添加版本信息
3. 通过其他渠道分享固件文件

---

**需要帮助？** 查看 GitHub Release 文档: https://docs.github.com/en/repositories/releasing-projects-on-github
