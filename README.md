# NekoTune 世界计划 / Project SEKAI扩展

独立维护的 C++20 / Qt / QML 社区扩展，ID 为 nekotune.sekai。支持五区歌曲与演唱版本；下载裁切需要系统 PATH 中的 ffmpeg 和 ffprobe。
详细功能和接口见 [sekai/README.md](sekai/README.md)。

## 安装

从 [Actions](https://github.com/xiyang1127/nekotune-sekai/actions) 下载对应平台的 ZIP，
或从 Releases 下载已验收的 `<插件ID>-<版本>-linux.zip` 或 `windows.zip`，在 NekoTune「扩展」中导入并确认信任。
升级导入新包后确认替换，已有 ID、配置和数据保持不变。

## 构建与调试

需要 CMake 3.28+、Ninja、C++20、Qt Core / Network / Qml / Test，酷狗另需 zlib。
Linux CI 使用 Ubuntu 24.04 / GCC 13 / Qt 6.11.2；Windows 使用 MSYS2 UCRT64。
原生插件的 Qt 小版本、编译器工具链需要与宿主对应。

SDK 固定版本见 nekotune-sdk.json；CI 只取该版本的 sdk/，不编译宿主。
本地使用已安装的 SDK 或宿主源码中的 sdk 目录：

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/sdk
    # 没有 SDK 安装包时，用 -DNEKOTUNE_SDK_SOURCE_DIR=/path/to/NekoTune/sdk 替代上面的前缀参数
    cmake --build build -j2
    ctest --test-dir build --output-on-failure

ZIP 在 build/packages/；挂载 build/extensions/nekotune.sekai/ 调试。
修改源码后重新构建，宿主检测产物变化并重载。

## CI 与发布

提交或 PR 自动运行 Linux/Windows 构建、离线协议测试、中英翻译校验、
共享库加载与反复激活／停用测试，并提供短文件名 ZIP 和 SHA-256 校验文件，构建信息仅记录于 CI 日志。
更新 sekai/extension.json 的版本后推送同版本的 v<version> 标签，
自动创建草稿 Release。实际安装、更新与真实播放验收后再公开。

CI 使用模拟网络和凭据，不登录真实账号。Windows MSYS2 包会滚动更新，
Qt 小版本不符合当前基线时明确失败；升级宿主时同步更新 SDK 配置并验收。


安装包及校验文件的统一命名与复现命令见 [发布规范](RELEASING.md)。本次更新见 [发行说明](RELEASE_NOTES.md)。
