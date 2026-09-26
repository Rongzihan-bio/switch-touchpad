# Touchpad 0.1.1-rc6：Applet 内存试验版

本版按用户指定方向，把后台的内存来源从 System 改为 Applet。当前完成电脑端构建和结构检查，尚未在 Switch 上证明可启动或运行。

## 改了什么

- NPDM `pool_partition=1`：从 Applet 物理内存池分配。
- NPDM 内核能力 `application_type=2`：由 PM 归入 Applet 资源组。两项分别检查，不能只改物理池。
- 固定堆恢复至 4 MiB，主线程栈恢复至 64 KiB。实际 NSO 映像 4,984,832 字节，加主线程栈为 5,050,368 字节，约 4.8164 MiB；固定堆已包含在 NSO 的 BSS 中。
- `__nx_applet_type` 仍为 `AppletType_None`：后台不建立 AM 小程序会话，不请求前台焦点，仍通过 PM 手动启动、常驻至关机。
- 画布和显示仍使用 640×360 单缓冲，保留当前服务权限和触摸逻辑。本轮优先隔离内存来源变化。
- Diag 0.3.0 增加 Atmosphère `pm:dmnt` 65001 查询，分别记录 System／Applet 的内存、线程、事件、传输内存和会话用量、上限、差额。查询失败原样记录，不把失败当作零额度。

配置参考 Atmosphère 1.11.2 的 [memlet](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/stratosphere/memlet/memlet.json)，分组规则见 [PM 实现](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/stratosphere/pm/source/impl/pm_spec.cpp)。使用这种分类不代表 Touchpad 的图形和 HID 服务已通过实测。

## 安装及本轮首测

1. 主机完全关机，在电脑备份并移除旧的 `atmosphere/contents/4200000054504144`，避免保留 `flags/boot2.flag`。保留 `config/touchpad` 中的设置和历史日志。
2. 合并 rc6 包里的 `atmosphere`、`switch` 目录至 SD 根目录。包内含新后台、Tesla 插件、输入测试程序和 Diag 0.3.0；不含 Atmosphère 或 Tesla 加载器替换件。安全推出后装回。
3. 完整重启。以应用模式／全内存 hbmenu 打开 **Touchpad Diag 0.3.0**，等到 READY 后按 **A 一次**。本轮不用 B，也不先从 Tesla 启动；B 是旧的 System 极小探针对照，已经运行的探针会导致 A 要求重启。
4. 记录固定在顶部的 Launch 返回码。即使为 `00000000`，仍需等待采集结束，核对后台 PID、命名端口和 `startup.log`。这些不等于显示／输入成功。
5. 若启动成功，退出 Diag，打开 Touchpad Input Test，在 Tesla 中查看“显示层：已就绪”和错误码 0，再启用触摸并验证按下、移动、抬手和 HOME 停止。
6. 基础链路通过后，再验证后台运行时系统小程序正常打开和返回、休眠恢复及较长时间运行。观察同一 Applet 池的资源争用，不把一次成功当作兼容性验收。

若仍失败，关机后读回 `config/touchpad/diagnostics/` 中本次报告及 `config/touchpad/startup.log`（若存在）。日志会记录卡上后台 SHA-256，防止把旧安装当作新版测试。无需反复重试。

## 内存与停用的含义

Applet 配额为多个进程共享；旧日志的数百 MiB 空余只是当时快照。本版的约 4.82 MiB 是映像加主线程栈预算，不包含其他进程代为申请的驱动资源，也不是官方运行保证值。4 MiB 堆内部已知基础分配至少 1,278 KiB，实际峰值仍需测量。

Tesla 的“关闭并断开虚拟手柄”停止输入和断开设备，**不会退出后台或归还整块固定堆**；完整重启后才清除该后台。本版不创建自启标记，因此重启后可在不启动 Touchpad 的状态下对照系统小程序行为。

## 电脑端验证范围

交叉编译成功，产物检查解码 ACID 的实际池标志，以及 ACI0／ACID 两份 Applet application_type。发布检查还覆盖映像与主栈不超过 5 MiB、命名权限、NSP 内容、NRO/NACP 和源码摘要。电脑端触摸／绘图测试不代表实机内存分配和服务调用成功。
