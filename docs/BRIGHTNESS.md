# 1.1.0-beta.4：直接调节系统亮度

2026-10-02，用户希望无实体手柄时方便调亮度，经比较后选择“在 Touchpad 菜单直接加亮度控制”，本版不实现原生长按 HOME 快捷菜单。

## 使用

1. 侧滑呼出 Tesla／Ultrahand，进入 Touchpad。
2. 点击“屏幕亮度”；不需要先启动手柄后台。
3. 拖动滑条或用左右键调节，范围 5%～100%，立即应用。
4. 若希望系统记住选择，点击“保存系统亮度”；只拖动不主动调用系统保存。
5. 点击“返回 Touchpad”，再“启用并返回游戏”恢复虚拟手柄。

页面显示系统返回的亮度百分比与自动亮度开关。自动亮度原样保留，开启时实际亮度仍可能随环境变化。该页面操作的是系统亮度设置，不是虚拟控件的不透明度。

## 实现与错误处理

- 只在 `.ovl` 菜单中使用 libnx 的 `lbl` 接口，未给后台增加权限、线程或内存池需求。后台 NPDM 和生命周期逻辑保持原样。
- 进入页面及隐藏后重新显示时读取当前亮度；从不将页面旧值写回系统，也不在初始化时设置默认亮度。
- 调整顺序是设置 → 应用 → 读取确认；失败显示具体阶段与返回码，不能将未确认的值当成成功。
- 手动点击保存时才调用 `lblSaveCurrentSetting`，避免拖动时反复保存。保存失败可重试；读取或应用失败时不继续自动保存。
- 保留系统自动亮度开关，不调用开／关屏幕、自动亮度切换、环境光传感器写入或 HOME 注入接口。
- 菜单隐藏、退出亮度页或插件退出时关闭拥有的 lbl 会话；下一次进入重新连接。lbl 服务不可用时保留错误显示，其他菜单项仍可使用。
- 新增操作记录在 `config/touchpad/overlay.log` 的 `brightness-read/set/save` 条目。

接口依据是固定工具链 libnx 4.12.0 的 [lbl.h](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/include/switch/services/lbl.h) 与 [lbl.c](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/source/services/lbl.c)，系统亮度参数为 0～1 的浮点数。本版调整映射为 0.05～1.0，读取可显示原系统的 0% 设置。

原生长按 HOME 的评估：libnx 有 `HomeButtonLongPressing` 系统动作枚举，但当前后台和 Tesla 均使用 `AppletType_None`，不能直接依赖 applet 会话调用；普通 HDLS HOME 又会进入现有的立即暂停逻辑。用户已经选择直接亮度控制，本版未改变这两条路径。[applet 接口](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/include/switch/services/applet.h)、[初始化实现](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/source/services/applet.c)。

## 验证状态

七组电脑端 ASan／UBSan 测试通过。新增亮度测试覆盖只读进入／刷新、上下限、保持自动亮度、打开／读取／设置／应用／确认失败、非法浮点返回、显式保存与失败重试。固定工具链编译及结构检查通过，后台镜像加主栈仍为 5,066,752 字节（4.8320 MiB）。

实机待验证：OLED 上的实际调光、系统记住亮度、自动亮度开启时的效果、休眠后重新读取，以及返回游戏后重新启用。电脑测试和编译成功不代表这些已通过。
