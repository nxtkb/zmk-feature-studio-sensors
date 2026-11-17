# 本地 0.1：review、扩展边界与试用

2026-11-17。协议 ID 为 `com.nxtkb.studio.sensors.local/0.1`，版本 0.1，私有 field 101。
这是自用试验；未冻结 ABI、未向上游提交，常规 App/固件发布均未启用。
实际 schema 为 `protocol/local/sensors.proto`，早期 draft 和 v0 仅保留历史参考。

## 新 behavior、新硬件需要改什么

| 新增内容 | 固件负责 | App 适配量 |
| --- | --- | --- |
| 普通 behavior，参数属于现有 Studio 类型 | 提供行为及参数 metadata | 通用功能选择/参数编辑可直接呈现；无需按名称增加分支 |
| 新 sensor behavior | 声明 sensor callbacks、合法参数；可选参数角色 | 已知参数类型走通用编辑；只有角色明确时显示方向标签 |
| 同时适用于按键和 sensor 的 behavior | 同时报告 KEY/SENSOR context | 两个选择器都能使用，不做互斥分类 |
| 增加已支持类型的旋钮实例 | 在 keymap 配置传感器，并报告 ID/名称/类型及可选按压关联 | 按描述枚举；不写死两个、左右手或 Sweep Pro 型号 |
| 其他键盘实现同一协议 | 实现相同发现、绑定、校验及生命周期契约 | 共用当前适配器；仍须经过互操作验证 |
| 同类硬件使用另一套通信协议 | 实现对应固件端能力 | 增加协议适配器；有旋钮不代表协议相容 |
| 新参数结构、复杂宏、触控板手势、灯效或屏幕配置 | 定义各自能力、约束和操作 | 需要对应协议能力与 UI；不能自动获得专用编辑器 |

当前 metadata 仍基于 Studio 的两个 uint32 参数值，支持已有 HID、层、常量/枚举、范围等约束。
协议描述可扩展性不等于能表达任意数据结构。未知 sensor kind 仅供查看；未知版本不开放编辑。
设备端验证始终是最终约束，App 预检不能代替固件。

## 本轮 review 后落实的修正

1. 把旧 tag-100 原型从当前实现移走。App 官方 keymap/behaviors schema 恢复原样，模块 schema 独立生成，
   使用显式 SensorWire 适配器和 debug opt-in；精确检查协议与版本。私有编号本身仍有碰撞可能，不公开为通用标准。
2. 避免 keymap -> sensors -> keymap 循环导入。sensor schema 使用独立的叶子 binding DTO，值语义与现有 Studio 一致。
3. sensor ID、描述顺序、内部下标、按压键位置分开。ID 0 和按压位置 0 都通过 presence 表达；没有按压也合法。
4. context 使用集合；参数角色与值类型分开。两个 HID 参数不自动解释为顺/逆时针，通用枚举也能编辑。
5. sensor 写入在同一临界区比较 token、提交完整 binding 和推进 revision。事件取完整副本再调用 driver，
   不持锁发送 HID 或写 flash。Nanopb 读响应使用稳定的行快照，避免长度计算与实际编码读到不同值。
6. 普通 keymap/层/物理布局的公共修改 API 接入状态钩子，覆盖旧 Studio 调用路径。
   当前并未给整个核心 keymap 建立事务锁；不承诺跨普通按键和 sensor 的线性化事务。
7. 复用 Save/Discard/Reset。逐条保存期间若出现新修改，保持 dirty 并返回失败；部分写入不会宣称回滚。
   不兼容或损坏记录不套用到运行状态，也不把 Discard 误报为成功。
8. 存储记录携带 schema 1、16 字节构建兼容签名与 CRC。签名包含 DTS、Kconfig、schema 和行为/模块源代码；
   相应源文件加入 CMake configure 依赖，增量构建也重新计算。签名策略保守，改变固件可能拒绝全部旧 sensor 设置。
9. 原生旋转 driver 在事件边界清除行为切换的余量，RPC 不直接修改 driver 的事件线程状态。已排队动作允许完成。

## 已验证的证据

- App 单元测试 87 项、独立 protocol 测试 6 项通过；Debug APK、Android 测试 APK 与 Debug lint 通过。
- 同一份生产 C `engine.c` + `rpc.c`、Nanopb codec 的宿主测试 11 项通过：
  0/1/3 个 sensor、ID/按压位置 0、稳定 ID 重排、显式继承、校验失败、过期 token、保存/重启/撤销/默认恢复、
  部分保存失败、CRC 损坏、兼容签名变化、保存期间再次修改、并发事件读取 50000 次不出现撕裂 binding。
  这里的 flash 端口是故障可注入的宿主存储，不能替代 Zephyr NVS 或实际掉电验证。
- OnePlus 6 / Android 11 实际 App 通过 adb reverse 连接上述 C 后端，键位 UI 与 C 协议联调共 **15 项通过 / 36.342 秒**。
  测试提供 3 个任意 ID（0/17/29）的旋钮；通过通用参数界面将第三个设为新 behavior 的 `Mode 2`，
  前两个保持原值，保存后用新客户端重新发现并读取成功。新 behavior 仅来自固件 catalog/context，没有 App 专用分支。
  普通 Studio catalog/传输外壳为测试端口，sensor 业务及 codec 执行真实生产 C。截图已人工检查。
- 官方固定 ZMK 的 Sweep Pro 带屏左手（含既有 Codex USB/BLE）构建通过；另一个 Ferris Sweep
  `cradio_left` 零传感器配置、启用同一模块的构建通过。左手 FLASH 473132 B / RAM 218418 B，
  零传感器 FLASH 273140 B / RAM 97806 B；左手 RAM 使用率 83.32%，后续扩充需继续检查预算。两者均为编译验证。
- 当前模块核心与消息补丁均对固定官方基线检查可应用。显示屏 USB 供电与 Codex USB HID 补丁未混入 sensor 补丁。

以上是宿主和构建阶段的记录。后续已刷入实体带屏左手；实机读写、输入链路和传输问题见下文。
实体旋转、分体传感器、保存/掉电、升级及第三方客户端互操作尚未完整验收。

## 板级接入与自用步骤

`nxtkb,studio-sensors` 元数据按 keymap sensors 设备列表的内部顺序声明，协议写入仍以 ID 为准。
Sweep Pro 的可选 `sweep_studio_sensors` shield 当前声明 ID 17/29，stable key `left-dial`/`right-dial`，
类型 1（relative rotary），stock 按压位置 30/35。后端映射到当前物理布局；无按压使用 `0xffffffff`。
其他板可使用任意唯一 ID、名称和逻辑 key。stable key 不能包含路径分隔符，最长 39 字节，名称最长 63 字节。
不提供元数据时可发现 EC11 的默认描述，但缺少可用于跨拓扑迁移的 stable key。

本地自用包与校验和保存在 `nxtkb-app/artifacts/studio-sensors-local-0.1/`。

1. 使用固定官方 ZMK 的隔离 checkout，应用 display USB 供电补丁和需要的 Codex HID 补丁，再应用本模块补丁清单的全部条目（sensor 核心、messages、Studio TX 兼容修复）。
   Sweep Pro 必须从 ZMK west 工作区构建；完整基础命令见 `Sweep-Pro/docs/en/local-build.md`。
2. extra modules 加入本模块和匹配的 messages checkout；snippets 加 `studio-sensors-local`；
   Sweep Pro shields 可加 `sweep_studio_sensors`。不要改用旧 fork 叠加相同补丁。
3. App debug 使用 `-Pnxtkb.experimentalSensorProtocol=true`；默认 debug 与 release/releaseQa 始终关闭实验功能。
4. 确认实际键盘/左右手和外设组合，再让相应设备进入 UF2 bootloader，核对实际挂载点后复制对应 UF2。
   本轮产物里的左手文件只适用于带显示屏的 Sweep Pro 左手。
5. 连接、物理解锁、完整同步。先记录 stock；选层改一个旋转参数，验证两个方向及按压独立。
6. 测撤销；再修改并保存，重启后重读且实际旋转验证；最后测试恢复默认。与普通按键一起改动，检查共享保存状态。
7. 改动固件后先读兼容性提示。旧记录不兼容时保留记录、使用 stock 且保持未保存提示；明确保存当前值或恢复默认后才清理。
   当前不自动迁移 v0 或跨固件设置，也不能把缓存当成可移植模板。

## 正式使用前的剩余边界

没有整键盘快照原子提交；新 StateChanged token 通知尚未接入，冲突主要靠 expected token 检出。
写入结果不确定时重新读取，不自动重发。其他客户端的无 token Save/Discard 仍作用于共享编辑状态。
新增硬件类型及全新参数结构需要版本/能力扩展。正式 wire 分配、迁移与发布资格应等自用验证完成后再评审。


## 实机阶段补充：2026-11-17

- 已在用户确认的 nice!nano 带屏左手刷入试用版，荣耀 Android 16 App 保留设备记录。
- BLE 完整读取 7 层、ID 17/29 的旋钮描述与绑定成功；临时写入只改变目标旋钮参数，普通键、布局及其他绑定保持一致。
- 荣耀曾显示 HOGP 连接但未创建输入设备。只关闭再开启 Sweep Pro 的“输入设备”配置后，Keyboard/Mouse 设备与 UHID fd 正常出现；未删除配对。
- 之后测试还遇到 USB 输出路径与一次重启：代际变化，未保存的 A 恢复 stock。改用纯蓝牙并重新写入后，系统实际捕获到 Q 和 8 对旋钮生成的 A 事件。
- 硬件诊断验证 Discard 恢复全部原 sensor 绑定，普通按键未改；随后实机捕获到 8 对 A 和 8 对 B，双方向字母动作已验证；保存和断电验证仍在进行。
- 实机反复回读出现完整 protobuf 内容缺少 EOF，已通过原版生产 TX 函数的负向对照复现，并加入独立兼容补丁。
  详细证据、修复范围和测试见 [Studio TX backpressure](studio-tx-backpressure.md)。补丁后固件仍需重新刷写实测。
- 本轮持续维持正式发布隔离，未向上游提交。
