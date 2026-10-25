迁移到 NekoTune SDK 3，插件功能与设置不变。

- 插件接口升为 `org.nekotune.IExtensionPlugin/3`，清单 `apiVersion` 为 `3`。
- 音源与服务改用 SDK 3 按能力分组的 Context 和强类型描述符；长曲目下载裁剪改按 `ResolvePurpose::Download` 判断，行为不变。
- 扩展 ID `nekotune.sekai`、配置、存储与密钥库命名空间保持不变，升级后无需重新登录或配置。

安装：下载对应系统的 ZIP，在 NekoTune「扩展」中导入。`SHA256SUMS.txt` 无需导入。

兼容性：需要提供 SDK 3 的宿主；SDK 2.x 宿主无法加载此版本。Linux 使用 Ubuntu 24.04 / GCC 13 / Qt 6.11.2；Windows 使用 MSYS2 UCRT64 / GCC 16 / Qt 6.12.0。插件 Qt 小版本和编译器 ABI 必须与宿主兼容。
