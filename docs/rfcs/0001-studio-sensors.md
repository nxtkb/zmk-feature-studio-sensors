# RFC 0001：Studio 传感器绑定编辑

状态：**设计草案，已有本地 0.1 实现；未冻结 wire ABI，也未提交上游**。
日期：2026-11-17。维护位置：独立模块 `zmk-feature-studio-sensors`。

## 1. 目标与边界

让任意采用 ZMK keymap sensors 的键盘，通过 Studio 客户端读取和编辑按层传感器绑定。
旋钮是首个支持的类型。实现必须独立于 Sweep Pro、EC11 引脚、左右手数量、显示屏、
Codex 和 Android。另一个有 1 个或 3 个旋钮的键盘，不应需要修改客户端的型号判断。

首版沿用 ZMK 的语义：**每个传感器在每层绑定一个 sensor behavior**。
`inc_dec_kp` 恰好有两个可变参数；固定旋转行为的两个方向仍由固件内的 behavior 定义。
协议不把旋转模拟成负数按键位置，也不把所有 sensor 等同于两个普通按键。
任意组合两个行为、动态宏、轴曲线、触控板配置和通用设备遥测都不在本提案首版范围。

本文的 MUST 表示设计要求，不表示本地实现已全部验收。当前实现、review 结论和未完成项见 [本地试用记录](../local-trial.md)。

## 2. 已核对的上游基础

| 基础 | 核对结果 | 设计影响 |
| --- | --- | --- |
| Studio RPC | protobuf + framing，使用现有 USB UART / BLE GATT | 沿用传输、请求 ID、锁和错误机制 |
| 当前 message schema | `studio.proto` 静态导入 core / behaviors / keymap | 模块注册 C handler 不能自行增加可解码的消息类型 |
| RPC handler 标识 | 固定版本中 subsystem/request choice 使用 `uint8_t` | 不能任意选大编号；相关扩大变更需独立评审 |
| 官方能力表 | Assigning behaviors to encoders 仍列为低优先级 | 不能把本模块描述为官方已支持 |
| keymap sensors | 当前固件按 `sensors` 列表顺序关联 `sensor-bindings` | 内部下标需要通过后端映射，不能成为跨固件持久标识 |
| 普通按键绑定 | 现有 `BehaviorBinding` 有行为本地 ID 和两个参数 | 复用该值结构，不再造 HID 编码和行为列表 |
| 保存 | 现有 keymap 保存逐项处理 | 不能仅因复用了 Save RPC 就宣称原子保存 |

基线：ZMK `4de31336`，messages `6cb4c28`；也核对了在线 main 中的 RPC header 和 messages。
有限关键字搜索未找到直接对应的 encoder schema PR；这不是“上游没有相关工作”的证明。
提交前必须重新检索并与维护者确认范围。

来源：

- [Studio RPC](https://zmk.dev/docs/development/studio-rpc-protocol)
- [Studio capabilities](https://zmk.dev/docs/features/studio)
- [消息定义](https://github.com/zmkfirmware/zmk-studio-messages/blob/main/proto/zmk/studio.proto)
- [RPC 注册接口](https://github.com/zmkfirmware/zmk/blob/main/app/include/zmk/studio/rpc.h)
- [Keymap sensor 配置](https://zmk.dev/docs/config/keymap)
- [Module creation](https://zmk.dev/docs/development/module-creation)
- [Protobuf 演进规则](https://protobuf.dev/programming-guides/proto3/#updating)

## 3. 分层与复用

| 层 | 所有者 | 依赖与职责 |
| --- | --- | --- |
| 协议 schema | 独立模块；未来可迁入 messages 仓库 | 语言无关、无产品名、版本及错误语义 |
| Studio 适配器 | 模块 + 少量核心集成补丁 | 解码、锁、能力发现、RPC、通知、共享 Save/Discard/Reset |
| keymap sensor 后端 | 通用 ZMK C API | 列举、ID 映射、按层读写、验证、同步、持久化 |
| 板级描述 | 各键盘 board/shield | 逻辑标识、名称、类型、可选按压位置；不写 App 判断分支 |
| 客户端协议适配 | App 的独立 protocol/client 层 | 能力与版本协商、状态模型、ID 路由；不依赖 Compose |
| UI | Android / Web / iOS 各自实现 | 显示固件描述的传感器，按角色渲染参数，提供通用回退 |

第一阶段采用“**模块 + 最小集成补丁**”，不宣传零补丁。传输保持原样。
目标后端 API 应能被 CLI、Studio 或其他客户端适配器共用，不把 protobuf 类型传进 keymap 核心。
独立模块先拥有协议草案和测试；硬件仓库只负责声明元数据、固定模块版本和显式接入。

## 4. 协议入口与能力发现

候选方式是在现有 `keymap.Request/Response` 中增加一个 typed sensor 入口，内部消息
放在独立 `sensors.proto`。正式 envelope 字段号 **TBD，等待未来上游协调**。本地 0.1 暂用私有 field 101，
只在显式启用并匹配 `com.nxtkb.studio.sensors.local/0.1` 的客户端中使用；不声称保留此编号。
这样可复用现有 keymap 锁定处理，也便于旧固件把未知 keymap 操作返回为 `RPC_NOT_FOUND`。
不在本轮为旋钮单独创造通用 Any/JSON RPC 总线或新 GATT 服务。

当前固定版本的未知顶层 subsystem 路径可能提前返回、未回填原 request ID；
因此不能假设“随便增加顶层 subsystem 后，旧固件都能及时返回可匹配的错误”。
兼容测试必须覆盖真实旧固件解码与调度，而不是只 mock 一个理想错误。

候选 `Describe` 返回：

- 精确 `protocol_id`、major/minor、支持的操作、声明的资源上限。
- 传感器描述列表及相关行为的 context / 参数展示语义。
- 当前状态 token、保存保证。

协商规则：

1. 首版 Describe / Read / Write 都需要现有 Studio 解锁；锁定是已知状态，不能记为不支持。
2. 只有明确且可匹配的 `RPC_NOT_FOUND` 才标记该会话不支持。超时是未知／不可达，不能等同于不支持，也不自动重试写入。
3. 匹配 protocol ID 和支持的 major 才解释数据。更高 minor 的未知可选字段可忽略；未知必需能力或更高 major 不允许编辑。
4. **服务存在且列表为空是合法状态**。服务不支持、零传感器、只读、有可写传感器必须可区分。
5. 能否写入同时取决于服务操作、传感器权限、已知类型、可用行为元数据和实时解锁状态。
6. 能力绑定当前会话与固件拓扑；重连重新协商。缓存不能授予写权限。

若上游以后统一增加 `core.get_capabilities`，可以把本服务描述放入该列表。
首版不要求先建成一个覆盖所有未来功能的全局扩展平台。

## 5. 传感器身份与硬件描述

描述列表的顺序仅用于展示。所有读取结果和写入请求携带显式 sensor ID，layer 使用现有稳定 layer ID。
这些 ID 是协议标识，不能当成矩阵位置、DT phandle 或 BLE peripheral 编号。

每个 `SensorDescriptor` 包含：

- `sensor_id`：当前拓扑内唯一；ID 0 合法，通过 presence 区分缺失。
- 可选 `stable_key`：板级明确声明的持久逻辑名称，不从 DT 节点名、左右手猜出。
- `display_name`、类型、每传感器的可写／继承能力。
- 可选 `press_key_position`：当前 active physical layout 中普通按键的位置；0 合法，缺失表示没有关联，不表示位置 0。

不要求旋钮一定有按压、位于左／右手、使用 EC11 或连接在 central 上。
分体事件的内部 sensor index 由后端映射；本提案不改 split wire format。
active physical layout 改变时，关联位置可能改变，必须更新拓扑 token，客户端重新描述。
未知 sensor kind 可以显示名称与原始详情，但不得按旋钮语义写入。

持久身份有两档：

- 有显式 stable key：固件可以执行经过验证的迁移；键、behavior 定义和参数语义都兼容才可恢复。
- 无 stable key：只允许同一拓扑内恢复；固件更新使映射改变时保留备份并回到 stock 或报告不可迁移，绝不按旧下标套用。

原型的 `keymap/s/<layer>/<index>` 不能无条件迁入新存储格式。没有旧拓扑证据时，禁止猜测迁移。

## 6. 行为能力与参数语义

继续从 Studio behavior catalog 获取行为和参数约束。本服务提供 keyed-by-behavior-ID 的补充事实：

- `supported_contexts` 是集合，可同时支持 KEY 和 SENSOR；不能用互斥 `is_sensor` 代替。
- 参数展示角色由 driver/backend 声明，例如 parameter 1 是 CLOCKWISE_ACTION、parameter 2 是 COUNTERCLOCKWISE_ACTION。
- 约束说明“值是什么”，角色说明“值做什么”。HID usage 类型本身不代表方向。

客户端只有在角色已知且与约束相容时提供顺／逆时针快捷编辑。
自定义 behavior 没有这些角色时，提供普通参数编辑和原始信息；不根据名字或两个 HID 参数猜含义。
固定旋转 behavior 没有可写方向参数时，可以更换整个 sensor behavior；不假装能修改其内嵌绑定。
参数跨多个 metadata set、有条件参数或未知角色时，必须完整遵守约束，不能拼接出固件不接受的组合。

普通按压行为列表与 sensor 列表按 context 过滤；双 context 行为应在两边都可用。
固件是写入验证的最终权威，客户端预检不能替代它。

## 7. 读写与继承

当前可执行文件为 [local/sensors.proto](../../protocol/local/sensors.proto)，只适用于本地 0.1。
[早期候选 schema](../../protocol/draft/sensors.proto) 保留作设计历史。两者均不构成上游分配的 wire API。
本地 schema 使用独立叶子 BehaviorBinding DTO，沿用 Studio 值语义，避免 keymap 导入 sensors 后循环导入。

- Describe：一次返回完整、有界的描述快照。
- ReadLayer：按 layer ID 读取完整、有界的 sensor binding 列表，每条带 sensor ID。
- SetBinding：首版每次修改一个 sensor-layer binding，携带预期 token。
- binding 值是显式 oneof：`behavior` 或 `inherit`。没有值是非法请求，不默认写入 behavior 0。
- 继承表示该层不指定 sensor behavior，继续遵循 ZMK 传感器层处理规则。
  屏蔽动作是另一种语义，不能与继承混为一谈；首版没有后端支持就不公布该能力。

请求中的 ID 使用 presence；缺失 layer ID 必须报错，不能意外改到合法的第 0 层。
成功返回固件接受的 binding、状态 token 和 unsaved 状态；响应必须与请求操作、layer 和 sensor 对应。
错误有明确类型；空响应、未知 error enum 和缺失 result 都不能当成功。

资源限制由设备公布并在设备端执行：传感器数量、UTF-8 名称字节数、响应总字节数等。
客户端也有自己的解析上限，取双方较小值；不能信任对端声明的任意大上限。
无需硬编码“最多两个旋钮”；若未来单条描述超出上限，应先协商分页或新协议版本，不能静默截断。

## 8. 状态一致性与并发

状态 token 包含 generation、topology、revision：

- generation 是设备产生的 16 字节不透明代际标识，重启、修订计数回绕或重建状态时更换。
- topology 是 16 字节不透明兼容性标识，ID 映射、传感器描述、行为语义或物理关联变化时更换。
- revision 在 generation 内为 uint32，0 合法；所有共享配置变更使它前进。

不要直接 hash protobuf 序列化字节生成拓扑 ID：其序列化不保证 canonical。
实现应定义自己的稳定规范化输入，或使用构建生成的不透明兼容性 ID。
这些 token 是状态一致性工具，不是设备身份认证或安全密钥。

固件在同一锁／串行临界区内比较 expected token、验证 ID/参数、提交单条 RAM 修改并推进 revision。
不匹配返回 STATE_CONFLICT，不改变 RAM、dirty 状态或 NVS。
旋转事件消费路径也必须参与同步：在同一保护下复制完整 binding，再释放锁调用 driver/行为队列，
避免 RPC 写入多字段结构时事件读到一半旧值、一半新值。不能持锁执行可能回调、阻塞或发送 HID 的行为。
成功写入的生效点定义为后续事件取得新 binding；已经排队的旧动作允许正常完成，不能假装修改会
撤回已经发送的按键。驱动积累的旋转余量及行为切换后的重置规则也必须进入运行时测试。
普通按键、层重排/恢复、物理布局、sensor、保存/撤销/默认恢复的成功变更都需接入共享状态钩子。
schema 中放一个 revision 字段并不会自行建立这些保证。

通知用于使 UI 失效，不能代替写入前置条件。通知丢失时，expected token 仍必须阻止覆盖旧状态。
未带 token 的旧 Studio 客户端仍可使用已有 keymap API；它的变更必须推进新 token。
但是旧 Save/Discard RPC 没有 token，仍可能涉及别的客户端当前更改。
首版保留现有“共享会话编辑状态”的边界，不能宣称整个 Studio 已实现跨客户端事务隔离。

写入超时或断连后，结果是未知。重连、解锁、重新协商并读取实际状态供用户判断；
不自动重发、自动保存或自动回滚。重复请求的可靠幂等事务是后续提案，request ID 不是持久事务 ID。

## 9. 保存、撤销、默认恢复与升级

传感器与普通按键必须参与同一套 Studio Save / Discard / Reset；不在 UI 加一个互不相干的“保存旋钮”。
模块需要共享生命周期钩子，返回错误必须传到原调用方；仅注册 settings handler 或 reset callback 不够。

| 操作 | 最低保证 |
| --- | --- |
| SetBinding 成功 | 单条 RAM 修改完整生效；标记 dirty；不立即写 flash |
| 校验／冲突失败 | RAM、dirty、NVS 均不变 |
| Save 成功 | 所有参与者成功持久化后才返回成功并清除相应 dirty 状态 |
| Save 部分失败 | 返回失败；公布 best-effort；不能宣称已回滚此前写入，失败项保持 dirty |
| Discard | 恢复最近持久化配置；读取或验证失败时报告失败，不将不完整状态当成干净快照 |
| Reset | 普通键、sensor、层和布局都遵循明确 stock 恢复语义；失败不报告全局成功 |

首版声明 `STUDIO_BEST_EFFORT`，不声明 `SNAPSHOT_ATOMIC`。
完整键盘快照双槽/journal、提交标记、CRC、掉电恢复等若要成为保证，需要另一个共享存储设计，
不能通过给 sensor 单独换 NVS key 实现整个 keymap 的原子性。

存储必须有 schema 版本和拓扑兼容性；sensor durable key 与行为稳定语义引用应独立于运行时本地 ID。
ZMK 现有 behavior local-ID table 可用于其承诺的固件内持久化范围，不能把裸数字导出为跨固件模板 ID。
缺失/改变 behavior、重排 sensor、增加 sensor 或升级存储 schema 均须有明确保留、迁移或拒绝策略。
任何迁移失败不得静默把一个旋钮的设置套到另一个旋钮。

## 10. App、缓存与其他固件

协议适配器应输出统一领域模型：支持状态、版本、operations、descriptors、behavior support、
带 token 的 binding 快照。UI 不直接读取 protobuf 的 `sensor_count` 或供应商 tag。

适配器分三类：正式上游（未来）、明确注册的模块协议、显式开启的旧原型。
正式客户端应保留 pristine 上游 schema，把原型／模块解码放入独立适配器；仅加运行时开关并不能
消除 compiled-in 私有字段与其他实现的 wire 冲突。本地 App 已恢复 pristine 官方 keymap/behaviors
schema，通过独立 SensorWire 适配器访问 field 101，模块 schema 单独生成。显式 debug 开关、
精确协议 ID 和版本检查限制实验范围；这仍不等于获得了通用 wire 编号或上游互操作保证。
未知 firmware 不自动尝试写入猜测协议；同样实现本协议的其他键盘无需品牌白名单。
底层并不采用 ZMK sensors 的其他固件可实现同一语义的后端或独立客户端适配器，
但“有旋钮”本身不证明协议兼容，也不要求本轮兼容其他固件的私有协议。

缓存携带协议 ID/版本、设备标识、拓扑、描述与读取时间。缓存只读；重连重新握手。
本机快照不等于可跨设备应用的模板。模板应用需独立验证 sensor stable key、行为语义及参数，
不依赖显示顺序、本地 behavior ID、设备名或旋钮数。现有官方模板继续只应用按压键。

## 11. 分阶段交付与上游拆分

| 阶段 | 可审查产物 | 完成门槛 |
| --- | --- | --- |
| A：本轮设计 | 原型审计、RFC、独立模块、候选 schema、schema 测试；默认发布隔离 | 明确未实现内容，不提前冻结协议 |
| B：共享后端 | 不依赖 protobuf 的 C API、状态/生命周期钩子、标识与存储处理 | 固件层测试覆盖读写与错误；至少两个不同 board 配置 |
| C：协议实现 | Describe/Read/Set、角色/context、token、锁、限制；App 通用适配器 | 真实旧固件/客户端兼容矩阵及跨客户端测试 |
| D：发布资格 | 故障注入、重启及掉电、迁移、0/1/多个旋钮实机验证 | 才能将模块固定版本接入正式键盘构建 |
| E：贡献上游 | messages 提案、核心后端/元数据、Studio client/UI 分开的变更 | 维护者确认范围与字段号；清晰离散 commits |

upstream 顺序建议：先讨论 keymap sensor C API 和行为 metadata，继而评审消息定义，最后客户端 UI。
若维护者倾向把 API 直接放进 keymap 子系统而不是 nested sensor request，可调整 envelope，
保留这里的身份、语义与存储契约，不让 App UI 依赖某种外层包装。
不先冻结“通用扩展注册平台”作为旋钮功能的前置工程。

## 12. 正式接入前必须解决与验证的事项

- 上游接受的能力发现入口、envelope 字段号及参数角色存放位置；不会自行将 tag 100 视为保留空间。
- 板级 stable key / press association 的 DT 语法和 stock topology 生成规则。
- 通用 keymap 生命周期/状态钩子的最小 API，包含普通旧客户端的写入路径。
- 0、1、3 个 sensor；无按压；按压位置 0；重新排列 descriptor；只读、未知类型和双 context 行为。
- 省略与显式 ID 0、继承、参数约束组合、未知字段/枚举、版本不匹配、资源上限和 malformed 请求。
- 锁定、过期 token、第二客户端更改、旧 RPC 改键、通知丢失、超时后读取实际状态。
- 存储失败、部分保存、恢复/重启、sensor 新增/重排、behavior 变化、旧 v0 数据不可安全迁移。
- 旋转事件与 RPC 同时发生、行为切换、余量状态和已排队动作；证明没有撕裂 binding 或死锁。
- central/peripheral、无传感器构建、BLE/USB、RAM/flash 预算及支持版本的补丁应用检查。

以上是完整验收要求，**不是已经全部通过的项目**。已完成的 C 后端、手机联调和两种板级构建
证据见 [本地试用记录](../local-trial.md)；这些不能替代实体旋转、BLE/USB、掉电或升级验证。
