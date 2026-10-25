# Release 文件规范

版本使用 SemVer，Git 标签固定为 `v<版本>`，标签与 `extension.json` 中的版本必须一致。
标题统一为「Project SEKAI 扩展 v<版本>」。新版本使用新标签；附件命名修正保留现有版本与标签。

| 文件 | 命名 |
| --- | --- |
| Linux 安装包 | `<插件ID>-<版本>-linux.zip` |
| Windows 安装包 | `<插件ID>-<版本>-windows.zip` |
| 平台无关主题 | `<插件ID>-<版本>.zip` |
| 安装包 SHA-256 | `SHA256SUMS.txt` |

原生 Release 只包含 Linux 和 Windows 两个 ZIP，以及校验文件，共 3 个附件；主题共 2 个附件。
不上传 JSON 构建信息附件。构建时仍校验 Qt 与编译器信息，并记录到 CI 日志。
ZIP 内的 `extension.json` 是宿主安装所需的清单，必须保留。
短平台名目前对应 x86_64 构建；新增其他架构时须先调整命名规则，禁止将 ARM 包命名为同一个文件。
GitHub 自动提供的 Source code 归档不是安装包。

SDK 原始包位于 `build/packages/`。构建及 CTest 通过后执行：

```sh
uv run --no-project --python 3.12 scripts/release.py prepare --manifest sekai/extension.json --build-info build/build-info.json
```

发行附件位于 `build/release/`。准备命令要求输出目录为空，防止混入旧版本产物。
将同一次工作流各平台附件汇总到 `dist/` 后，检查版本、ZIP 根目录清单及平台并生成校验文件：

```sh
uv run --no-project --python 3.12 scripts/release.py finalize \
  --manifest sekai/extension.json --output dist --platforms linux-x86_64 windows-x86_64 --tag v3.0.0
uv run --no-project --python 3.12 scripts/release.py verify \
  --manifest sekai/extension.json --output dist --platforms linux-x86_64 windows-x86_64 --tag v3.0.0
cd dist
sha256sum -c SHA256SUMS.txt
```

标签推送会运行所有平台测试，再生成含安装包、校验文件及发行说明的草稿 Release。
维护者核对附件、兼容性说明和校验结果后公开发布。校验文件只覆盖 ZIP，不包含自身。
缺平台、旧版本、重复文件和额外附件（包括 JSON）会阻止发布。
