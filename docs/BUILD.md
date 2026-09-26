# 构建与版本

本项目使用用户下载的 `switch-build-env-20260922` 离线环境；没有升级镜像中的 SDK。

| 项目 | 实际使用版本 |
|---|---|
| 平台 | Linux ARM64 容器，运行于 Apple Silicon macOS |
| 镜像 | devkitpro/devkita64 |
| 镜像摘要 | `sha256:645416b7ca52cbeccec64423d8a536e7f856521eceb964d4e7feeff54c5001f4` |
| devkitA64 | r29.2-1 |
| GCC | 15.2.0 |
| libnx | 4.12.0-1 |
| switch-tools | 1.13.1-1 |
| libtesla | f766e9b607a05e9756843cbd62b3bfb98be1646c，附本地回调补丁 |

所有离线归档均已按用户清单核对 SHA-256。完整版本与来源保存在 `toolchain.lock.json`。下载目录中的 libnx 源码是额外接口参考；真正参与链接的是镜像内的 libnx 4.12.0。

## 常用命令

在源码根目录执行：

```sh
make test
make build
make package
```

主机需提供 Python 3.9+、支持 C++20 的编译器及可运行的 Docker Desktop。`scripts/build.py` 使用镜像内容摘要，而非可变化的 `latest` 标签；容器关闭网络，仅挂载当前源码目录到 `/work`。

如镜像尚未导入，脚本会读取并校验以下归档后导入：

```text
downloads/switch-build-env-20260922/devkita64-linux-arm64.oci.tar
```

源码压缩包不重复包含 650 MiB 的工具链归档。保留你已经下载的目录，或先自行导入相同摘要的镜像。脚本不会隐式联网下载。Docker 导入 OCI 后标签解析在本机曾出现不一致，所以构建始终使用摘要。

如已有相同版本的原生 devkitPro 环境，也可执行：

```sh
make -f Makefile.switch DEVKITPRO=/opt/devkitpro -j4
python3 scripts/check_artifacts.py
```

`make test` 输出为 `build/host/core-tests`，并生成实际绘图逻辑的 `layout.ppm` 预览。Switch 编译产物和日志位于 `build/switch`。

发布流程提供 `dist/touchpad-0.1.1-rc10-sd.zip`、`dist/touchpad-0.1.1-rc10-source.zip`、`dist/SHA256SUMS`。源码包包含修改后的 libtesla、构建脚本和测试；二进制包与源码包各自包含源码文件摘要以便对应。

## 本地 libtesla 修改

本地修改包括隐藏后提交启用、输入轮询锁与线程生命周期修正、异步 IPC 退出保护，以及 rc8 的显示栈加入／移除和隐藏原因记录。只有确认菜单层移除并恢复输入后才提交启用 token；详见 RUNTIME_RC7.md 和 RUNTIME_RC8.md。

补丁和来源说明位于 `third_party/libtesla/TOUCHPAD_PATCH.md`。此修改只编译进 `touchpad.ovl`，不覆盖 SD 卡已有的 Tesla Menu 或加载器。
