# 启动资源与堆预算核查（2026-09-23）

调查目标：用户报告 rc5 仍在 LaunchProgram 返回 00010801，先依据官方源码核实需求，不再凭物理池剩余量决定缩堆大小。

来源固定为 Atmosphère 1.11.2 和构建实际使用的 libnx v4.12.0。官方源码归档与展开内容位于 `build/research/`，产物数字见 `build/research/touchpad-memory-audit-20260923.json`。本次没有修改或安装后台二进制。

## 官方规则

1. Atmosphère 1.11.2 支持 HOS 22.5.0。官方变更记录说明 20.0.0 后自定义系统模块可用内存缩减；1.11.2 内核源码进一步表明，21.0.0 及以后从 Applet 池调给 System 的额外空间为 7 MiB，20.x 为 14 MiB，更早版本为 40 MiB。这是内存池分配政策，并非单个自制模块的 7 MiB 上限。
2. 物理池和资源组是两个不同限制。PM 在现代固件使用动态 System 组额度，初始上限由总额度减去 Application、Applet 额度及 5 MiB 保留量得到；不能拿旧版本表中的 269/328 MiB 或旧分支的额外 32 MiB 作为本机可用值。
3. 内核创建进程时为 NSO 映像保留 PhysicalMemoryMax 额度；启动时再保留一个 ThreadCountMax 名额及主线程栈额度。保留失败可返回 LimitReached（module 1，description 132，即 00010801）。物理页分配失败另有 OutOfMemory 路径。因此现有返回码不支持把问题直接认定为“物理 System 池仅差一点”。
4. 启动失败后没有 PID，不区分 CreateProcess 失败与 StartProcess 失败后 PM 清理进程。旧日志也不能排除线程额度问题。

来源：

- [1.11.2 发布记录](https://github.com/Atmosphere-NX/Atmosphere/releases/tag/1.11.2)
- [内核内存池分配](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/libraries/libmesosphere/source/board/nintendo/nx/kern_k_system_control.cpp)
- [PM 资源组额度](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/stratosphere/pm/source/impl/pm_spec.cpp)
- [KProcess 初始化与启动](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/libraries/libmesosphere/source/kern_k_process.cpp)

## 本项目实际启动请求

Loader 对每个 NSO 使用 `align_up(max(text_end, ro_end, rw_end) + bss_size, 4096)`，不是简单相加段大小。以下从现有实际产物重新计算；这些包只含一个 main NSO，没有额外启动参数。固定 heap 数组在 BSS 内，已经计入映像，不能再重复相加。

| 产物 | NSO 映像（字节） | 主线程栈（字节） | 两项合计（字节） | 合计 MiB |
| --- | ---: | ---: | ---: | ---: |
| 极小探针 | 94,208 | 16,384 | 110,592 | 0.1055 |
| rc3 | 4,984,832 | 65,536 | 5,050,368 | 4.8164 |
| rc4 | 2,887,680 | 65,536 | 2,953,216 | 2.8164 |
| rc5 | 2,101,248 | 32,768 | 2,134,016 | 2.0352 |

这是上述两次内存额度请求的合计，另需至少一个主线程名额、物理页及相应内核/服务资源。它不是保证能完整初始化的总预算。

旧日志中的 System 物理空余为 3,645,440 字节，正确换算约 3.4766 MiB。此前写作“3.64 MiB”和“rc4 2.88 MiB”混用了十进制字节数与 MiB；实际 rc4 映像约 2.754 MiB。

[Loader 计算逻辑](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/stratosphere/loader/source/ldr_process_creation.cpp)

## rc5 的内部堆过于紧张

libnx framebufferCreate 通过 aligned_alloc 分配显示缓冲；NV 初始化调用 tmemCreate，也先从本程序堆分配传输内存。此前 DESIGN 文档说它们由系统服务提供而未计入堆，是错误的。当前 640×360 RGBA4444 单缓冲配置至少需要：

| 堆内分配 | 数量 |
| --- | ---: |
| 显示缓冲（按 128 行对齐到 384 行） | 480 KiB |
| 线性绘图画布 | 450 KiB |
| NV 传输内存 | 256 KiB |
| 三个工作线程栈及按页向上取整的 TLS/reent | 至少 92 KiB |
| 已知合计 | 至少 1,278 KiB |
| rc5 固定堆 | 1,280 KiB |

剩余最多 2 KiB，还没有计入 malloc 元数据、对齐空洞、文件缓冲、C++ 对象和其他库分配。rc5 不具备可依赖的运行余量；继续直接缩堆不合理。当前绘图结构宜暂按至少 1.5–2 MiB 堆作工程预算，最终必须测运行时峰值；这不是官方最低值或可运行保证。

此问题发生在程序开始执行之后，不能拿它来解释当前 LaunchProgram 本身返回的 00010801。它是源码核查发现的另一项缺陷。

来源：[framebuffer.c](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/source/display/framebuffer.c)、[nv.c](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/source/services/nv.c)、[tmem.c](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/source/kernel/tmem.c)、[thread.c](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/source/kernel/thread.c)、[alloc.c](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/source/runtime/alloc.c)。

## 能直接取得的缺失数据

Atmosphère 提供 `pm:dmnt` 命令 **65001**：`AtmosphereGetCurrentLimitInfo`。输入为 `u32 group, u32 resource`，输出为两个 `s64`（current、limit）。System group=0；resource=0..4 分别读取内存字节数、线程、事件、传输内存、会话。服务内部直接查询 PM 持有的真实资源组句柄。Diag 已能连接 pm:dmnt，但尚未调用此接口；此前说“后台所属组额度无法直接访问”不正确。

下一步应在后台启动前后记录这些值，算出 `available = limit - current`，再与上述启动请求对比。若内存额度不足，才能给出需要减少的确切字节数；若线程等额度耗尽，缩堆没有作用。还应阻止在探针仍运行时混测后台，或明确记录探针 PID 和占用。

[接口声明](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/libraries/libstratosphere/include/stratosphere/pm/impl/pm_debug_monitor_interface.hpp)、[服务实现](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/stratosphere/pm/source/pm_debug_monitor_service.cpp)、[额度读取实现](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/stratosphere/pm/source/impl/pm_spec.cpp)。

结论：目前不能负责任地指定“缩到 1 MiB/512 KiB 一定能用”。必须先补齐真实额度；若需进一步减小后台，应先消除重复画布等实际分配或减少代码体积，保证缩小后的堆仍满足运行需求。

## 能否换用其他池（追加核查）

可以研究改变内存来源。7 MiB 不是 Switch 总内存，也不是全部 System 池容量。旧日志中的物理池总量为 Application 3285 MiB、Applet 500 MiB、System 232.15625 MiB、SystemNonSecure 44.125 MiB；当时 Applet 物理空余约 403.3047 MiB，而 System 仅约 3.4766 MiB。空余量是该次诊断时的快照，不是游戏运行时或系统小程序启动时的保证值。

Atmosphère 自带的 `memlet`（旧日志 PID 128、program 0100000000000421）提供了两条可参考路径：

- **后台本身使用 Applet 池与 Applet 资源组。** memlet 的 NPDM 配置为 `pool_partition=1`，内核能力 `application_type=2`。PM 按 application_type 选择资源组，Loader 按 pool_partition 选择物理池，不能只修改其中之一就认为已解决两层限制。Touchpad 采用同类配置是有源码依据的候选方案，仍需验证该后台的服务权限、系统小程序共存和运行峰值。
- **小后台通过 memlet 借工作内存。** memlet 服务命令 65000 创建来自 Applet 的共享内存，按 1 MiB 单位申请，单次请求上限 128 MiB，分配失败会逐 MiB 降低请求。原生调用方是 ams.mitm 的 romfs 构建逻辑，按需取得并释放。这个上限不意味着允许 Touchpad 长期独占 128 MiB。后台仍须先从自己的资源组成功启动。

第二种方案不能机械地把全部 malloc 换到共享内存：内核 Shared 内存状态没有 CanAlias / CanTransfer / CanDeviceMap 等数据内存能力，不能将普通共享工作缓冲直接当成 libnx 的线程栈、NV transfer memory 或显示设备映射缓冲。应只迁移兼容的普通数据，或采用第一种方式取得正常的进程数据内存。

还可以维护自定义内核来重划内存池，但将空间从游戏或 Applet 拿走会减少它们的预算。官方缩小额外 System 空间的直接原因就是 20.x/21.x 浏览器所需 Applet 内存增长。仅将 7 MiB 常量改大不构成兼容性解决方案。

`BoostSystemMemoryResourceLimit` 名称也容易误导：5.0.0 以后其实现主要调整 Application 限额与 unsafe 映射限额，不会自动扩大本项目正在使用的普通 System 池和固定 BSS。不能当作一键解除限制。

来源：[memlet 配置](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/stratosphere/memlet/memlet.json)、[共享内存服务](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/stratosphere/memlet/source/memlet_service.cpp)、[PM 分组与调额](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/stratosphere/pm/source/impl/pm_spec.cpp)、[内核内存状态](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/libraries/libmesosphere/include/mesosphere/kern_k_memory_block.hpp)。本次仅研究，未修改 NPDM 或部署试验版本。
