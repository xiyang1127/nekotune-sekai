本次为发布规范维护版本，插件功能和设置不变。

- 原生安装包统一为 `<插件ID>-<版本>-linux.zip` / `windows.zip`；平台无关主题使用 `<插件ID>-<版本>.zip`。
- 下载附件只保留安装包和 `SHA256SUMS.txt`，不再提供 JSON 构建信息附件。
- `SHA256SUMS.txt` 覆盖所有 ZIP，构建环境在 CI 中校验并记录。
- 发布前自动校验标签、清单版本、ZIP 结构和平台完整性。

安装：下载对应系统的 ZIP，在 NekoTune「扩展」中导入。`SHA256SUMS.txt` 无需导入。

兼容性：需要支持原生扩展 API v2 的宿主。Linux 使用 Ubuntu 24.04 / GCC 13 / Qt 6.11.2；Windows 使用 MSYS2 UCRT64。插件 Qt 小版本和编译器 ABI 必须与宿主兼容。
