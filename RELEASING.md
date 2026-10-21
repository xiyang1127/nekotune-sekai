# Release 文件规范

版本使用 SemVer，Git 标签固定为 `v<版本>`，标签与 `extension.json` 中的版本必须一致。
标题统一为「Project SEKAI 扩展 v<版本>」。每次发布使用新版本，不覆盖既有 Release。

| 文件 | 命名 |
| --- | --- |
| 安装包 | `<插件ID>-<版本>-<平台>.nekotune.zip` |
| 构建信息 | `<插件ID>-<版本>-<平台>.build-info.json` |
| 所有附件的 SHA-256 | `SHA256SUMS.txt` |

原生平台使用 `linux-x86_64`、`windows-x86_64`；主题平台使用 `any`。
原生 Release 包含两个平台的安装包及构建信息，共 5 个附件；主题共 3 个附件。
GitHub 自动提供的 Source code 归档不是安装包。
构建信息记录插件 ID、版本、平台、API 版本、源码提交及对应安装包；原生插件还记录 Qt 和编译器环境。

SDK 原始包位于 `build/packages/`。构建及 CTest 通过后执行：

```sh
uv run --no-project --python 3.12 scripts/release.py prepare --manifest sekai/extension.json --build-info build/build-info.json
```

规范附件位于 `build/release/`。准备命令要求输出目录为空，防止混入旧版本产物。
将各平台附件汇总到 `dist/` 后，检查版本、ZIP 根目录清单、平台和构建信息并生成校验文件：

```sh
uv run --no-project --python 3.12 scripts/release.py finalize \
  --manifest sekai/extension.json --output dist --platforms linux-x86_64 windows-x86_64 --tag v2.0.1
uv run --no-project --python 3.12 scripts/release.py verify \
  --manifest sekai/extension.json --output dist --platforms linux-x86_64 windows-x86_64 --tag v2.0.1
cd dist
sha256sum -c SHA256SUMS.txt
```

标签推送会运行所有平台测试，再生成含完整附件和发行说明的草稿 Release。
维护者核对附件、兼容性说明和校验结果后公开发布。校验文件覆盖安装包与构建信息，不包含自身。
每个平台必须来自同一源码提交，缺平台、旧版本、重复文件和额外附件会阻止发布。
