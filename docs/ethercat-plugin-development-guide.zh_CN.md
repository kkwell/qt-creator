# Embed Labs EtherCAT IDE 插件架构与二次开发指南

## 1. 文档定位

本文是当前 `embed-labs` 分支的中文架构总览、插件使用规范和二次开发入门手册。
它面向首次接手项目的开发人员，也可作为设计评审和代码评审检查表。

本文只描述当前源码中已经存在的能力和明确的剩余边界。历史 ISSUE、旧协议版本和
阶段性测试记录仍保留在其他文档中，但不能替代本文的当前状态摘要。

### 1.1 不遍历源码的功能定位入口

功能所有权、入口符号、依赖、测试和证据边界统一登记在
`docs/ethercat-feature-locator.json`，面向阅读的索引由同一清单生成到
`docs/ethercat-feature-code-map.zh_CN.md`。开发人员和 AI 应先使用定位器，再进入对应
插件目录：

```bash
python3 scripts/ethercat_feature_locator.py list
python3 scripts/ethercat_feature_locator.py find 扫描
python3 scripts/ethercat_feature_locator.py show ethercat.product-api.topology-evidence
python3 scripts/ethercat_feature_locator.py check
```

`find` 用于按功能名、关键词、文件或符号搜索，`show` 输出一个功能的最短修改路径，
`check` 校验清单、源码符号、依赖关系和生成文档是否漂移。新增、拆分或迁移功能时，
必须在同一修改中更新该清单并重新生成代码地图。

定位器同时保存可按需读取的工程知识，而不只是文件索引：

1. **Feature** 回答“功能由谁负责，入口、合同、测试和证据在哪”；
2. **Knowledge Card** 回答“调用链怎样走、哪些不变量不能破坏、修改时检查什么”；
3. **Issue Ledger** 回答“已知问题、当前事实边界、下一步和完成标准是什么”。

开始修改前，不再先遍历整个插件目录。先读取目标功能的最小上下文，或由已经知道的
文件反查影响范围：

```bash
python3 scripts/ethercat_feature_locator.py context ethercat.runtime.manual-control
python3 scripts/ethercat_feature_locator.py issues --status open
python3 scripts/ethercat_feature_locator.py issue ethercat.issue.startup-sdo-compiler
python3 scripts/ethercat_feature_locator.py impact \
  src/plugins/ethercatworkbench/workbenchcontroller.cpp
```

`context` 默认只输出目标 Feature 摘要、最多三个直接前置、所属知识卡的不变量和相关
Issue 摘要；需要完整入口、检查表和问题细节时再加 `--full`。开发人员或 AI 应先读这段
有界上下文，再调用 `show`、`issue` 打开必要信息，不重新扫描全部源码。

`impact` 接受安全的仓库相对路径，即使文件刚被移动或删除，也会区分直接引用、Feature
反向依赖、所属组件和组件下游，并列出直接证据 Issue 与受影响 Issue。查询命令默认不因
无关功能的路径或符号漂移整体失效；需要同时执行全量校验时加 `--strict`。`check` 和
`generate` 始终执行严格全量校验。

完成修改后必须同步维护工程记忆：解决问题时先把稳定结论补充到 Feature/Knowledge
Card，再从活动 Issue 台账删除；新发现的问题登记根因、边界、下一步和验收标准；功能
迁移则更新 Feature 和 Knowledge Card。
最后执行：

```bash
python3 scripts/ethercat_feature_locator.py generate
python3 scripts/ethercat_feature_locator.py check
```

台账只记录可长期复用的工程事实，不保存临时 IP、当前 BootId、某次板卡状态或未经复核
的猜测。清单和各字段都有数量/长度上限；活动台账不接受 `closed` 状态，解决后必须写回
稳定知识并删除该 Issue，由 Git 历史保留演进记录。生成代码地图只保留知识卡和问题摘要，
详细内容按 ID 查询。真实硬件证据仍放在对应测试归档中，并由 Issue 的 `evidencePaths`
引用稳定入口。

当前产品目标是：

1. 以一个 `.ecatproject` 工程作为唯一工程事实来源；
2. 从真实控制器扫描总线，使用原始 ESI XML 识别设备；
3. 由数据驱动 Adapter 将厂家设备映射为通用信号和动作；
4. 由受信编译器生成、签名并验证 ECPKG；
5. 人工界面和自动化入口共用同一工程、控制器会话、运行时证据和操作审计；
6. 控制器脱离 IDE 后仍可自主执行已经部署的周期任务。

## 2. 必须保持的系统边界

- IDE 只产生工程意图、编译输入、运行包和有审计的非实时操作。
- CPU1/FPGA 负责 125 us 等实时周期；UI、MCP、REST 和网络延迟不得进入周期闭环。
- 控制器协议与从站厂家语义是两个不同插件边界。
- Workbench、Gateway 和 Core 中不得出现厂家型号、CiA 402 对象或 PDO 偏移分支。
- 未通过 ESI、Adapter、ECPKG 签名、控制器 Attestation 和运行时代际校验时，写操作
  必须保持禁用。
- Mock、Loopback 和真实硬件证据必须分别记录，不能互相代替。
- 运行任务与管理租约分离。租约释放、客户端断开或 IDE 退出不能停止健康的自主任务。

## 3. 当前组件总览

当前系统由一个无 UI 数据契约库和 11 个 EtherCAT 插件组成。

```text
Qt Creator Core / ExtensionSystem / ProjectExplorer / Utils
                              |
                         EtherCATData
                              |
                         EtherCATCore
              ________________|________________
             |          |          |            |
       EtherCATProject  Devices  DeviceAdapters  ProductApi
             |          |          |            |
             |          |          | ControllerConnectionSnapshot
             |          |          |            |
             |          |     ProjectCompiler   |
             |          |          |            |
             |__________|____ SemanticRuntime __|
                              |
                         EtherCATWorkbench
                         /        |         \
                    Scan(Mock) Diagnostics  AutomationGateway
                                             /          \
                              controller.*(Mock)   selected topology
                                                   (Real/Mock, read-only)
```

箭头表示上层消费下层的公开接口。实现插件不得被 Core 反向依赖。

| 组件 | 路径 | 当前职责 | 明确不负责 |
|---|---|---|---|
| `EtherCATData` | `src/libs/ethercatdata` | 跨插件值对象、工程、ESI、PDO/SDO/DC、控制器和语义运行契约 | QObject、Widget、Socket、全局状态 |
| `EtherCATCore` | `src/plugins/ethercatcore` | Provider 接口、注册表、选择/状态服务、编译/激活/语义服务契约 | 厂家协议、工程持久化、业务 UI |
| `EtherCATProject` | `src/plugins/ethercatproject` | `.ecatproject` 生命周期、唯一工程快照、Undo/Redo 和校验 | ESI 解析、网络、运行时控制 |
| `EtherCATDevices` | `src/plugins/ethercatdevices` | 原始 ESI XML 导入、解析、索引和精确身份查询 | 厂家控制逻辑、控制器连接 |
| `EtherCATDeviceAdapters` | `src/plugins/ethercatdeviceadapters` | 厂家设备到通用信号、动作、PDO/DC profile 的数据驱动适配 | Socket、运行时 PDO 写入、UI |
| `EtherCATProductApi` | `src/plugins/ethercatproductapi` | Embed Labs Product API v1.15、三通道、会话、租约、扫描、部署和原子输出事务 | 其他厂家协议、ESI 语义、页面 |
| `EtherCATProjectCompiler` | `src/plugins/ethercatprojectcompiler` | 外部受信编译器、持久操作账本、detached signing 准备和恢复 | 私钥、控制器、实时任务 |
| `EtherCATSemanticRuntime` | `src/plugins/ethercatsemanticruntime` | ECPKG/签名/映射验证、运行上下文、审批、语义动作和激活事务 | 厂家对象分支、界面布局 |
| `EtherCATWorkbench` | `src/plugins/ethercatworkbench` | 设备树、属性页、统一命令、连接、扫描应用、运行和手动控制界面 | ECAP 编解码、ESI XML 解析 |
| `EtherCATScan` | `src/plugins/ethercatscan` | 可选的本地 Mock 扫描与工程比较 | 真实 Product API 扫描 |
| `EtherCATDiagnostics` | `src/plugins/ethercatdiagnostics` | 可选的本地 Mock WKC/DC/告警趋势 | 真实控制器诊断生产 |
| `EtherCATAutomationGateway` | `src/plugins/ethercatautomationgateway` | 默认关闭的本机 MCP/REST、Mock-only 旧控制器视图、显式选择的 Real/Mock 拓扑证据和经审批语义动作意图 | 点击 UI、选择/调用 Provider、触发扫描、直接调用 Product API、原始 PDO/SDO |

`EasyBoard` 已从当前产品的 CMake/qbs 构建入口退役；源码只作为历史保留，不属于这条
EtherCAT 控制链。新的 EtherCAT 功能不得依赖、加载或复用它，也不得把它作为 Product API
或真实硬件访问的桥接层。

### 3.1 当前直接插件依赖

下表只列运行时必需的 Qt Creator 插件依赖，不重复列出 `EtherCATData`、Qt、Utils、
ExtensionSystem、McpServerLib 和 qtcMonocypher 等库依赖。

| 插件 | 直接依赖的插件 |
|---|---|
| `EtherCATCore` | `Core` |
| `EtherCATDeviceAdapters` | `Core`、`EtherCATCore` |
| `EtherCATDevices` | `Core`、`EtherCATCore` |
| `EtherCATProductApi` | `EtherCATCore` |
| `EtherCATProject` | `Core`、`EtherCATCore`、`ProjectExplorer` |
| `EtherCATProjectCompiler` | `Core`、`EtherCATCore` |
| `EtherCATSemanticRuntime` | `Core`、`EtherCATCore`、`EtherCATProjectCompiler`、`EtherCATProject` |
| `EtherCATWorkbench` | `Core`、`Debugger`、`EtherCATCore`、`EtherCATDevices`、`EtherCATProject`、`EtherCATProjectCompiler`、`EtherCATSemanticRuntime`、`ProjectExplorer` |
| `EtherCATDiagnostics` | `Core`、`EtherCATCore`、`EtherCATProject`、`EtherCATWorkbench` |
| `EtherCATScan` | `Core`、`EtherCATCore`、`EtherCATDevices`、`EtherCATProject`、`EtherCATWorkbench` |
| `EtherCATAutomationGateway` | `Core`、`EtherCATCore`、`EtherCATSemanticRuntime`、`EtherCATWorkbench`、`ProjectExplorer` |

当前必需依赖没有环。`EtherCATSemanticRuntime` 仅在测试构建中额外依赖
`EtherCATDeviceAdapters` 和 `EtherCATProductApi`。修改依赖时必须同时更新同目录 CMake
和 qbs 描述，并重新检查生成的插件 JSON。

## 4. 三类权威状态

### 4.1 工程状态

唯一权威是 `ProjectService` 返回的不可变 `ProjectSnapshot`。它保存：

- Project、Target、Master 和 Slave 的稳定 `NodeId`；
- 主站循环周期和运行模式；
- 从站精确身份、ESI 引用、Alias 和站地址；
- PDO、Startup SDO、DC 和模块选择；
- Adapter 选择、手动控制 envelope、设备参数工程意图和语义绑定引用。

页面不得保留第二份可写工程模型。编辑必须调用 `ProjectService` 的检查方法，再监听
`projectChanged()` 重新读取快照。

当前工程格式为 v8。每个从站的 `configuration.deviceParameters` 只保存用户配置意图：
单从站最多 256 项、全工程最多 4096 项；参数 ID 使用最长 256 字符的有界 ASCII 文法并
严格升序、不得重复；值使用无浮点歧义的 canonical `EngineeringValue`（Boolean、十进制
字符串整数、最简精确有理数或 ASCII 枚举）。非空参数必须绑定该从站的精确 ESI SHA-256
和完整 Adapter 选择。写入时还必须把调用者所见的 ESI 摘要和 Adapter 选择作为 expected
value token 传给 `setDeviceParameterConfiguration()`；当前值已变化时必须拒绝，不能把草稿
重绑到另一个设备定义。

Adapter v4 已提供必需的 `parameterDefinitions` 闭集、独立的 manifest/definition domain
hash 和 Authorization v2 精确摘要闭包；Core 的 `validateConfiguredDeviceParameters()` 可在调用者
提供精确 ESI、完整 Adapter/Profile 选择、`Qualified` 状态以及 `signatureVerified`、
`realHardwareAllowed` 双重信任结果时，继续校验 required、ID、类型和工程范围。工程 v8 的
持久化入口本身仍只验证通用语法，不会凭保存值自动完成资格化。

当前树没有生产 v4 Adapter/Authorization 资产，已安装的 SV630N Adapter 仍为 v3 且动作保持
disabled；compiler projection、Product API 在线实测参数证据和 configured/observed/match 界面也
尚未完成，因此任一非空设备参数仍会在调用外部编译器前 fail closed。后续扫描得到的实测值属于
带 Session/Boot/拓扑/设备身份的在线证据，只用于界面显示“工程设定值 / 扫描实测值 / 是否一致”，
不得写回 `ProjectSnapshot` 冒充工程意图。当前字段或 v4 合同本身都不授权部署、SDO 下载或电机
运动。

### 4.2 设备目录状态

唯一权威是 `DeviceRepositoryProvider`。内置原始 XML 位于：

- `share/qtcreator/ethercat/esi`

用户导入的原始 XML 存入应用资源目录下的 `ethercat/esi/library`。匹配必须同时使用
VendorId、ProductCode、Revision 和原始 XML SHA-256。显示名称、树序号和站地址都不是
设备型号身份。

Adapter v4 参数定义属于签名设备目录事实，不属于工程或在线会话事实。每项定义包含 ID、类型、
单位、完整工程约束、required/default 规则、配置投影和允许的实测来源；最多 256 项并按 ID 严格
升序。`project-only`/`unavailable` 必须明确原因，CoE 配置只允许固定 `PS` Startup SDO，CoE 实测
只允许固定 SDO upload，二者同时存在时必须指向同一个严格类型对象。Authorization v2 仅授权
v4，并精确覆盖每项定义的 domain-separated digest；Authorization v1 仍只授权 v3，不能跨版本
复用。

### 4.3 在线控制器状态

唯一权威是选定 `ControllerConnectionProvider` 发布的完整
`ControllerConnectionSnapshot`。它包含连接、Session/Boot、租约、控制器状态、包状态、
真实拓扑和操作进度。

消费者只复制快照。不得缓存 Socket、协议对象、RequestId 分配器或 Provider 私有对象。

跨真实控制器和 Mock 扫描选择拓扑时，使用 Core 的 `TopologyService`。调用者必须显式提供
`source + providerId + project/master scope`；服务只即时读取原始 Provider 证据，不缓存拓扑、
不自增代际，也不触发连接或扫描。Real 的 generation 从 Session/Boot/Request/Response/Capture
证据派生，Mock 的 generation 是扫描快照 `NodeId`。只有 `Fresh` 且 generation 有效的结果可进入
Provider 级“当前证据”判断；这不证明它与当前 `ProjectSnapshot` 修订匹配，也不授权编译或执行，
消费者仍须校验工程修订、来源策略和操作意图。`Stale` 和 `Incomplete` 仅供显示与诊断，Real 与
Mock 不自动替补。标记为 `mock` 的 `ControllerConnectionSnapshot` 也会 fail closed，不能借
Provider 类型冒充真实来源。

Workbench 的真实控制器路径只按当前工程保存的 Provider/Profile 精确选择查询该服务，并且
只把 `Fresh RealController` 证据投影到设备树、页面、FreeRun 判断和“应用当前总线”写入门禁。
证据陈旧、不完整、scope/profile 不匹配或 Provider 被移除时，连接状态仍可显示，但在线拓扑
立即清空且不会改用同 scope 的其他 Provider。

Mock 扫描 Provider 的选择由 Core `ScanProviderSelectionService` 按 `project/master scope`
保存稳定 Provider ID。它只保存选择，不保存 Provider 指针、扫描结果或拓扑，也不会启动、取消
或清除扫描。没有显式选择、Provider 不可用或 Provider 被移除时，消费者必须显示为空或不可用，
不能动态选择第一个或“最优”备用 Provider；同一 ID 重新注册后才可恢复。扫描正在进行时也不得
切换或清除选择。这个选择属于本机进程会话，不进入 `.ecatproject` 或签名工程身份；以后从本地
偏好恢复时，必须等工程打开后重新校验 Project/Master scope。

Workbench 已在 Master General 页提供显式 Mock topology Provider 选择，并通过 `TopologyService`
只把该 scope 的 `Fresh MockScan` 证据投影到树和自动化只读结果。偏好存于单个最多 128 项的
本地 LRU 记录；Provider 移除时保留 ID 但立即清空结果，同 ID 重新注册后才恢复。选择和恢复不
调用扫描、控制或 Discover，也不修改工程字节。

`ScanWorkflow` 的 start/compare/accept/discard 现在也复核同一 `project/master scope`、
显式 Provider ID、Registry 中的同一 Provider 实例以及 `Fresh MockScan` 证据。start 在清除旧
结果前完成请求和绑定校验；compare 与 accept 在一次操作中持续锁定同一个 snapshot `NodeId`，
Provider 移除、选择变化、错 scope、陈旧或被替换的结果都会 fail closed。cancel 和 shutdown
仍可无条件安全停止。accept 会在比较后及写入前同步重读工程修订；如果工程已经写入但随后刷新
证据失败，或 direct `projectChanged` 使最终 `exactMatch/acceptAllowed` 失效，结果会明确报告“工程
已更新但验证失败”，不会误报成未写入，也不会显示接受成功。

这个接受门禁依赖 GUI 线程中连续的同步检查与写入，不能解释成通用原子事务：`ProjectService`
尚无 offline topology compare-and-swap API，`ScanProvider` 也没有跨调用者 operation generation。
需要跨线程或多个协调器共同修改工程时，应先补齐公共 CAS/operation 合同。

Workbench 还通过 `AutomationService` 为 Gateway 生成同一选择的值快照：每个
Project/Master scope 最多各含一个精确 Real 和 Mock `AutomationTopologyView`，其 lookup 仍由
`TopologyService` 即时产生。Gateway 只消费这个值快照，不持有/查询 Provider，也不能改变选择
或触发扫描。新增 `topology.list-selected` 对每个选择发布来源、lookup 状态和 freshness；只有
`Fresh` 当前证据携带 slaves 与脱敏的 generation/evidence hash，失效、陈旧、不完整、错 scope
或 Provider 移除仅发布状态，不保留旧 slaves，也不在 Real/Mock 间替补。

## 5. 插件间调用规范

### 5.1 服务与 Provider 的区别

以下对象是单例服务，通过 Qt Creator 对象池获取：

- `SelectionService`
- `StateService`
- `ProviderRegistry`
- `ScanProviderSelectionService`
- `TopologyService`
- `AutomationService`
- `SemanticRuntimeService`
- `RuntimePackageActivationService`
- `RuntimePackageCompilerPreparationCoordinator`

以下能力是可替换 Provider，通过 `ProviderRegistry` 查询：

- `ProjectService`
- `DeviceRepositoryProvider`
- `DeviceAdapterProvider`
- `PropertyPageProvider`
- `ControllerConnectionProvider`
- `ScanProvider`
- `DiagnosticsProvider`
- `RuntimePackageCompilerProvider`
- `RuntimePackageCompilerProjectRequestBuilder`

禁止构造第二个全局服务，也禁止通过 `findChild()` 或 QObject 树寻找其他插件页面。

### 5.2 注册和卸载

Provider 插件的标准生命周期是：

```cpp
void Plugin::initialize()
{
    m_provider = std::make_unique<MyProvider>();
    ExtensionSystem::PluginManager::addObject(m_provider.get());
}

ShutdownFlag Plugin::aboutToShutdown()
{
    m_provider->shutdown();
    ExtensionSystem::PluginManager::removeObject(m_provider.get());
    return SynchronousShutdown;
}
```

注册 ID 必须全局唯一且长期稳定。消费者需监听
`ProviderRegistry::providerAboutToBeRemoved()`，在回调内立即丢弃指针和派生状态。
重复 ID 不会进入 Registry；冲突会按 ID 合并、以固定容量保留为注册诊断，并显示在
应用程序输出中。

### 5.3 异步控制和部署命令的接受与完成

控制器、部署和语义运行等异步 Provider 的 `Utils::Result<>` 只表示请求是否被本地接受，
不表示异步操作成功。`ProjectService` 的保存、Undo/Redo 和快照修改等同步方法在返回时已经
完成，不能套用本节的异步终态规则。

正确流程是：

1. 用当前完整快照计算按钮可用性；
2. 构造带稳定 Scope 和代际的请求；合同明确采用 OperationId 时同时生成不可复用 ID；
3. 调用 Provider；
4. 若调用被接受，等待 Provider 发布新的完整快照或终态信号；
5. 使用权威终态更新 UI 和应用程序输出；
6. 不使用页面本地变量猜测控制器已经完成。

### 5.4 ID 和代际

- 跨插件节点只使用 `NodeId`。
- Provider 使用稳定 `Utils::Id`，Profile 使用 Provider 内稳定 `NodeId`。
- 对明确采用 OperationId 的部署、语义操作和持久事务，使用非零、不可复用的 ID；普通
  `ProjectService` 修改和不含该字段的控制请求不得自行伪造 OperationId。
- SessionId、BootId、package generation、configurationId、topology/runtime generation、
  catalog revision 和 mapping digest 必须作为完整 epoch 比较，不能只比其中一个字段。
- 显示名称、行号、`QModelIndex`、Widget 指针、站号和 PI offset 都不能作为持久身份。

### 5.5 线程和信号

- Provider 的公开 QObject 和信号归属 GUI 线程。
- Socket、解析和文件工作可在私有线程进行，但只能通过排队信号发布不可变结果。
- 卸载前必须停止定时器、线程、监听器、网络操作和延迟回调。
- 页面销毁不等于取消控制器操作；操作由 Provider 或持久 coordinator 所有。

### 5.6 错误与输出

- 用户可执行的故障必须包含：发生了什么、权威状态、最短修复动作。
- 普通握手、轮询和心跳不写入应用程序输出。
- 不在状态栏重复显示控制器连接详情。
- 不泄露私有 IP、绝对路径、密钥、证书或原始协议数据。
- UI 不自行翻译厂家错误码；Provider 先转成结构化通用错误，再由 Workbench 精简显示。
- 失败后不得保留伪 Connected、伪 Running 或伪成功状态。

## 6. 统一用户工作流

### 6.1 扫描、识别和配置

```text
打开工程
  -> Workbench 选择当前 Master
配置控制器 IPv4
  -> ProductApi 内部固定使用 15200 / 15201 / 15202
连接
  -> 三通道握手并读取权威快照
自动尝试 Acquire
  -> 只取得管理控制权，不扫描、不运行
用户点击重新扫描
  -> Configuration -> DiscoverTopology
识别
  -> 精确 ESI -> 精确 Adapter -> 模块证据
应用当前总线
  -> 写入 ProjectSnapshot，保留可证明兼容的配置
编辑
  -> PDO / Startup SDO / DC / Adapter / 手动 envelope
  -> 设备参数工程意图（当前仅有 v8 持久化底座）
保存工程
```

连接不是扫描。物理总线未变化时，用户不需要每次运行都重新扫描。
扫描实测设备参数的读取、证据绑定和 configured/observed/match 页面尚未实现；扫描拓扑成功
不能伪造这些值，也不能把上一次会话的观察值保存进工程。

### 6.2 编译、签名和激活

```text
当前 ProjectSnapshot + 新鲜真实拓扑证据
  -> RuntimePackageCompilerProjectRequestBuilder
  -> 外部受信 compiler: compile
  -> detached signing request
  -> 外部 signer/HSM
  -> compiler: finalize
  -> compiler: verify
  -> RuntimePackageActivationPreparationRequest / Result
  -> SemanticRuntime 再验证
  -> ProductApi 上传、校验、激活
  -> 工程写入精确 binding artifact
```

生产私钥不得进入 IDE、仓库或普通编译器进程。编译器 provisioning、输入 profile、
可执行文件、公钥和所有依赖必须经过 SHA-256 固定和权限检查。

### 6.3 手动控制

选中已经写入工程的 Slave、Module 或 Channel 后，右侧 `Control` 页面读取
`SemanticRuntimeContext`。完整链路是：

```text
签名 Adapter 动作
  -> 工程实例绑定
  -> semantic-binding-v2
  -> signed action definitions
  -> ECPKG 生产签名
  -> 控制器 Attestation
  -> 用户确认
  -> SemanticRuntimeService::submit
  -> 完整 ConsistencyGroup OutputTransaction
  -> CPU1 周期边界整组生效或整组拒绝
```

仅在线扫描到、但还未应用到工程和激活匹配运行包的节点，必须显示控制不可用原因，
不能临时猜测 PDO 位或直接写对象。

### 6.4 自动运行和停止

下方原生控制区和顶部/树菜单复用同一 ActionManager 命令：

| 用户动作 | 权威前置状态 | 实际行为 |
|---|---|---|
| Run | `SHUTDOWN` 且存在可恢复的 active slot、generation 和 configurationId | Restore 当前持久包，再 Start；不扫描。当前实现尚未在此门禁证明包与工程完全一致 |
| Run | `OP_SAFE` | 按当前 `ProjectSnapshot.masterConfiguration.timingMode` 选择 FreeRun/DC Start |
| Run | `PAUSED` | Resume |
| Debug | `RUNNING` | Pause |
| Debug | `PAUSED` | Resume |
| Controlled Stop | `RUNNING`/`PAUSED` | ControlledStop 并验证安全终态 |

`Controlled Stop` 不是安全急停。安全急停、STO 和硬件故障仍由现场安全系统和控制器负责。

## 7. 新增一种控制器 Provider

1. 新建独立插件，不在 `EtherCATProductApi` 中增加厂家 `switch`。
2. 仅依赖 `EtherCATData`、`EtherCATCore`、通信库和必要 Qt 模块。
3. 实现 `ControllerConnectionProvider`，定义稳定 Provider/Profile ID。
4. 将端口、证书、通道、协议消息、CRC 和重连策略全部封装在插件内部。
5. 对外发布通用 `ControllerConnectionSnapshot`。
6. 对不支持的控制、部署、资源或输出能力明确返回 Unsupported。
7. Scope、Session、Boot、代际变化时使旧的拓扑、包、资源和操作证据失效。
8. 先完成 codec golden frame、分包/粘包、Loopback、超时和错误注入测试。
9. 再开启单独真实硬件门禁，记录操作前后只读快照和残留连接/租约检查。

绝对禁止：把厂家协议头暴露给 Workbench，或让页面直接持有 Socket。

## 8. 新增一种从站和 Adapter

### 8.1 ESI

1. 保存厂家原始 XML，不简化、不重写。
2. 记录文件大小和 SHA-256。
3. 验证 VendorId、ProductCode、Revision、PDO、Startup 和 DC 声明。
4. 放入内置 ESI 目录，或通过设备库导入用户固定目录。

### 8.2 Adapter

1. 定义精确身份和 ESI SHA 匹配。
2. 声明完整 PDO profile，而不是零散对象列表。
3. 模块化设备声明每个 slot、ModuleIdent 和 slot-relative signal template。
4. 声明通用 semantic signal definition、类型、方向、单位、比例和范围。
5. 声明完整动作步骤、参数、ConsistencyGroup、TTL、恢复策略和安全值。
6. 生成独立签名授权并安装到：
   - `share/qtcreator/ethercat/adapter-authorizations`
   - `share/qtcreator/ethercat/adapter-authorization-trust`
7. 为真实硬件资格增加正例和签名/哈希/范围/缺失信号负例。

设备参数页面不得按厂家或对象索引硬编码字段。后续 Adapter 合同必须用签名参数定义声明
稳定参数 ID、工程值类型、单位、范围、Startup/运行时投影和允许读取的实测证据来源，再由
Workbench 数据驱动呈现。当前 v8 工程值尚未获得这层定义资格，所以不能编译、部署或用于
运动。

Adapter 中不得硬编码运行时 ResourceId、站地址或 PI offset。它们由当前工程和编译产物
绑定。

当前 XB6 数字输出已经具有完整组写入验证。SV630N 速度动作仍因参考单位到 RPM 的换算
证据不足而保持禁用；在证据闭合前不得用真机运动验收代替缺失的单位合同。

## 9. 新增右侧页面或统一命令

### 9.1 PropertyPage

实现 `PropertyPageProvider`：

- 用稳定 Page ID；
- 用 `PropertyPageContext` 接收当前 Project/Node；
- 页面只读取快照并调用公开服务；
- context 变化后清除草稿、异步回调和旧对象指针；
- Provider 移除时立即销毁或切换到明确不可用状态。

### 9.2 QAction

一个业务命令只能注册一次。顶部工具栏、左下控制区和树节点右键菜单都引用同一个
ActionManager `QAction`。不要为每个视图复制按钮逻辑。

启用状态必须由统一 controller/coordinator 计算；页面不得独立实现另一套前置条件。

### 9.3 Qt Creator UI 规则

- 颜色使用 `Utils::creatorColor()` 或 `QPalette`。
- 字体使用 `Utils::StyleHelper::uiFont()`。
- 间距必须使用仓库的 spacing token API；当前源码全名为
  `Utils::StyleHelper::SpacingTokens`，后续若公共别名迁移为 `Utils::SpacingTokens`，以
  `AGENTS.md` 和当前 `stylehelper.h` 为准。
- 不使用硬编码颜色、透明文字或像素字体。
- 调用 Utils 自由函数时显式写 `Utils::`。
- 使用 `QTC_ASSERT`、`QTC_CHECK`、`QTC_GUARD`，不使用 `Q_ASSERT`。

## 10. 扩展 Automation Gateway

Gateway 当前有 14 个工具。原有 `controller.*` 仍是 Mock-only 只读视图，运行时上下文读取和
经审批语义动作意图的边界也不变；不能把它们当成真实 Product API 会话的查询入口。向后兼容
的 `controller-tools/v1` envelope 通过 `gateway.get-protocol` 协商 additive
`controller-tools/v1.1` revision，后者新增独立 `topology.list-selected`：它只列出 Workbench
按 `TopologyService` 精确选择后写入 `AutomationService` 的 Real/Mock 拓扑值证据。扩展规则是：

1. MCP 与 REST 调用同一个 Dispatcher、参数校验、OperationId journal 和 audit。
2. 监听默认关闭，只绑定 `127.0.0.1`。
3. 只读查询每次从 `AutomationService`/`SemanticRuntimeService` 获取新鲜值；Gateway 不查询
   Provider，不自行挑选来源，也不把 status-only 记录恢复成拓扑。
4. 写操作只能提交已经签名、已绑定、需要审批的通用语义动作。
5. 自动化调用者不能批准自己的操作。
6. Gateway 不连接控制器、不获取租约、不扫描、不部署、不直接运动。
7. selected-topology 每次最多 512 条记录、合计 4096 个从站、紧凑 JSON 最大
   2 MiB；OperationId 重放 journal 同时受 1024 项和 8 MiB 总预算约束。越界或
   不完整证据必须整个请求 fail closed，不得截断后伪装成成功。
7. 禁止添加 Product API message number、PDO/SDO、寄存器、Shell 或 CPU1 接口。

需要自动化连接、扫描、部署和运行时，应先建立通用、策略化的
`EngineeringOperationCoordinator`，让人工 UI 和 Gateway 调用同一服务，而不是让 Gateway
点击按钮或复制 Workbench 状态机。

## 11. 构建和测试规范

### 11.1 修改前

```sh
git status --short --branch
git log -1 --oneline
```

必须保持在本地 `embed-labs` 分支。除非用户明确授权，不 fetch、pull、merge、rebase、
切换分支、push 或创建 PR。用户自己的未跟踪文件不得加入提交。

### 11.2 构建描述

修改任意 `CMakeLists.txt` 时，必须同步同目录 `.qbs`；反向亦然。新增/删除源文件、依赖和
测试目标都要双向一致。

### 11.3 推荐验证层级

1. schema、canonical JSON、SHA 和签名正负例；
2. `git diff --check`；
3. 目标插件构建；
4. `QT_QPA_PLATFORM=offscreen` 的单测试函数；
5. 插件完整 QtTest；
6. 直接依赖插件回归；
7. `WITH_TESTS=OFF` 产品构建和插件清单；
8. 启用/禁用和卸载生命周期；
9. Product API codec/Loopback；
10. 最后才是真实硬件门禁；
11. 可见 GUI 只在用户明确允许时启动。

示例：

```sh
cmake --build "$BUILD_DIR" --target EtherCATWorkbench -j2
QT_QPA_PLATFORM=offscreen \
  "$BUILD_DIR/Embed Labs.app/Contents/MacOS/Embed Labs" \
  -test EtherCATWorkbench,testName
```

真实硬件测试结束时必须证明：目标安全状态、无当前/锁存故障、无残留租约、无残留
Product API 连接。对输出的 API/过程映像证据不能宣称为物理端子电气反馈。

## 12. 当前代码与验证证据

### 12.1 已集成到当前源码

- 工程打开后进入 EtherCAT Workbench，并选择 Master。
- IPv4 配置、三通道连接、自动 Acquire 和心跳续约。
- 用户明确触发的真实扫描和设备树展示。
- 固定 ESI 库、用户 XML 导入、XB6/SV630N 精确识别。
- 将当前总线应用到工程，并保存 PDO、Startup、DC 和 Adapter 选择。
- Product API v1.15 拓扑证据、v1.14 输出事务和旧版本有界兼容。
- 受信 ECPKG、语义绑定、动作定义、控制器 Attestation 和激活服务。
- 选中设备、模块或通道后的语义 Control 页面。
- XB6 完整 16 通道 ConsistencyGroup 的原子手动输出链路。
- MCP/REST 共用 SemanticRuntime 的动作意图入口；动作意图不等于绕过审批自动执行。
  原有控制器状态/拓扑/设备/诊断工具仍是 Mock-only；独立 `topology.list-selected` 可读取
  Workbench 已精确选择的 Real/Mock 拓扑值证据，但不是完整真实状态、遥测或操作查询入口。

### 12.2 历史真实硬件证据边界

项目已有 125 us DC、WKC 11/11、运行、暂停、恢复、受控停止以及 XB6 输出事务的真实
硬件门禁记录。这些记录证明对应版本和拓扑曾经通过，不代表当前开发板镜像、当前运行包
和当前工程已经自动通过。每次交付仍必须使用精确二进制、ECPKG、BootId、拓扑和完整
epoch 重新验收。

SV630N 速度动作仍因参考单位到 RPM 的换算证据不足而禁用，不能把离线动作定义或历史
DC 运行记录宣称为真机运动验证。

## 13. 当前缺口和工程化路线

### P0：形成可重复交付闭环

1. 完成 Mac 受信编译器可执行 bundle、`provisioning.json` 和当前工程
   `compile-inputs.json` 的交付与验收。
2. 在 Workbench 中完成用户可见的“编译—等待 detached signature—恢复—验证—激活”
   流程，而不要求手工选择任意 ECPKG。
3. 用当前真实 ProjectSnapshot 和新鲜扫描证据重新编译、部署并完成 DC/XB6 输出验收。
4. Startup SDO 目前可编辑，但当前 compiler request builder 对非空 Startup SDO 仍会
   fail closed；需由编译器合同和 IDE 同步支持后再开放。
5. 完成签名 Adapter 设备参数定义、ProjectSnapshot 到编译请求的精确 projection，以及
   Product API 扫描实测参数证据；在三者闭合前，v8 中任一非空设备参数继续 fail closed，
   不进入部署或运动路径。

### P1：统一业务协调层

1. Core 已建立来源隔离 `TopologyService` 和会话级 `ScanProviderSelectionService`；Workbench
   的 Real/Mock 展示、ScanWorkflow 的 Mock 操作，以及 Gateway 的 selected-topology 只读视图
   已按显式选择接入同一 Provider、Scope、generation/freshness 合同。Gateway 只收到
   `AutomationService` 值快照，不拥有 Provider 或扫描入口。
2. 将连接、租约、扫描、配置、编译、部署、运行和停止从 5,000 行级
   `WorkbenchController` 逐步迁移到无 UI 的 `EngineeringOperationCoordinator`。
3. 建立 UI、Gateway、SemanticRuntime、Compiler 和 Activation 共用的持久 Operation
   journal 与审计索引。
4. 将 Scan/Diagnostics 的菜单贡献点移到 Core 公共契约，移除它们对 Workbench 的反向
   编译依赖。

### P2：扩展通用设备和自动流程

1. 增加 AdapterCatalogService，让 UI 和 Gateway 读取同一真实 Adapter 目录。
2. 抽取 canonical JSON、SHA、签名和工件身份到无 UI 公共库，减少实现漂移。
3. 增加简约步骤编辑器，将通用动作、条件、延时、循环次数和一直运行编译为任务包。
4. 新增精确 DI、模拟量和更多驱动器 Adapter；不在底层添加厂家分支。
5. 闭合 SV630N 参考单位到工程单位/RPM 的签名证据后，再开放速度动作。

## 14. 已知架构风险

- `ProviderRegistry` 会拒绝重复 Provider ID，并在应用程序输出发布可见错误；新 Provider
  仍必须用测试固定 ID 唯一性，不能依赖运行时诊断代替设计审查。
- Gateway、SemanticRuntime、Compiler 和激活服务各有操作记录，尚未形成统一查询入口。
- 旧文档混合大量历史 ISSUE，部分协议版本和工具数量已经过期。
- `EtherCATScan`/`EtherCATDiagnostics` 名称容易让新开发人员误认为它们是真实来源；当前
  它们主要是 Mock 插件。
- Workbench 是当前主要协调器，继续向其中堆叠状态机会增加 UI 生命周期耦合。

## 15. 新技术人员五天学习路线

### 第一天：数据和工程

- 阅读 `src/libs/ethercatdata`。
- 阅读 `ethercatcore/providers.h`、`providerregistry.*`。
- 跟踪 `.ecatproject` 打开到 `ProjectSnapshot` 发布。
- 完成一个只读 NodeId/ProjectSnapshot 小测试。

### 第二天：ESI 和 Adapter

- 导入一份测试 ESI，查看精确 identity 和 SHA。
- 跟踪 DeviceRepository 到 Adapter resolution。
- 阅读 XB6/SV630N adapter、授权和负例测试。

### 第三天：在线和 UI

- 跟踪 Workbench 的 Master Communication 页面。
- 跟踪 ProductApi connect、snapshot、DiscoverTopology 和 output transaction。
- 理解一个 QAction 如何同时出现在工具栏、左下控制区和树菜单。

### 第四天：编译和语义运行

- 跟踪 ProjectSnapshot 到 compiler request。
- 阅读 preparation coordinator、detached signing 和 activation service。
- 跟踪 Control 页面到 SemanticRuntime 和 OutputTransaction。

### 第五天：独立小功能

- 新增一个只读属性或 Gateway 查询字段。
- 补充失败优先测试、offscreen 验证、完整插件回归和文档。
- 由评审者检查职责边界、ID、代际、错误输出和卸载生命周期。

## 16. 提交前检查表

- [ ] 功能属于正确插件，没有厂家逻辑泄漏到 Core/Workbench/Gateway。
- [ ] 长期状态只有一个所有者，跨插件传递不可变值。
- [ ] ID 稳定、唯一；未使用名称、行号、Widget 或偏移作为身份。
- [ ] 异步请求区分“已接受”和“已完成”。
- [ ] OperationId、BootId 和完整 epoch 都经过校验。
- [ ] Provider 移除、工程关闭和插件卸载可安全清理。
- [ ] 错误简短、可执行，不泄露私密信息。
- [ ] UI 使用 Qt Creator 颜色、字体和间距规范。
- [ ] CMake/qbs 同步。
- [ ] 单元、集成、生命周期和负例测试通过。
- [ ] Mock、Loopback、真实硬件结论分开。
- [ ] 只提交本 ISSUE 文件，不包含 `AGENTS.md` 或用户残留。
- [ ] 提交信息每行不超过 72 字符。

## 17. 相关源码和文档入口

- `docs/plugin-architecture.md`：历史架构演进和 ISSUE 证据。
- `docs/ethercat-core-api.md`：Core 公共接口。
- `docs/ethercat-project-format.md`：工程格式。
- `docs/ethercat-devices-repository.md`：ESI 目录。
- `docs/ethercat-device-adapters.md`：Adapter 数据模型和历史资格。
- `docs/ethercat-online-controller.md`：真实控制器工作流和硬件证据。
- `docs/ethercat-product-api.md`：Product API adapter 历史合同。
- `docs/ethercat-workbench.md`：Workbench 页面和交互演进。
- `docs/ethercat-automation-gateway.md`：Gateway 历史合同。
- `doc/qtcreatordev/src/creating-plugins.qdoc`：Qt Creator 插件入门。
- `doc/qtcreatordev/src/plugin-lifecycle.qdoc`：插件生命周期。
- `doc/qtcreatordev/src/actionmanager.qdoc`：统一命令系统。
