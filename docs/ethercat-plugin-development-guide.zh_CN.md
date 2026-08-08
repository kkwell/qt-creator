# Embed Labs EtherCAT IDE 插件架构与二次开发指南

## 1. 文档定位

本文是当前 `embed-labs` 分支的中文架构总览、插件使用规范和二次开发入门手册。
它面向首次接手项目的开发人员，也可作为设计评审和代码评审检查表。

本文只描述当前源码中已经存在的能力和明确的剩余边界。历史 ISSUE、旧协议版本和
阶段性测试记录仍保留在其他文档中，但不能替代本文的当前状态摘要。

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
                                  (Mock)
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
| `EtherCATAutomationGateway` | `src/plugins/ethercatautomationgateway` | 默认关闭的本机 MCP/REST、Mock-only 控制器视图和经审批语义动作意图 | 点击 UI、直接调用 Product API、原始 PDO/SDO |

`EasyBoard` 是独立的历史插件，不属于这条 EtherCAT 控制链。新的 EtherCAT 功能不得依赖
或复用它的状态。

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
- Adapter 选择、手动控制 envelope 和语义绑定引用。

页面不得保留第二份可写工程模型。编辑必须调用 `ProjectService` 的检查方法，再监听
`projectChanged()` 重新读取快照。

### 4.2 设备目录状态

唯一权威是 `DeviceRepositoryProvider`。内置原始 XML 位于：

- `share/qtcreator/ethercat/esi`

用户导入的原始 XML 存入应用资源目录下的 `ethercat/esi/library`。匹配必须同时使用
VendorId、ProductCode、Revision 和原始 XML SHA-256。显示名称、树序号和站地址都不是
设备型号身份。

### 4.3 在线控制器状态

唯一权威是选定 `ControllerConnectionProvider` 发布的完整
`ControllerConnectionSnapshot`。它包含连接、Session/Boot、租约、控制器状态、包状态、
真实拓扑和操作进度。

消费者只复制快照。不得缓存 Socket、协议对象、RequestId 分配器或 Provider 私有对象。

## 5. 插件间调用规范

### 5.1 服务与 Provider 的区别

以下对象是单例服务，通过 Qt Creator 对象池获取：

- `SelectionService`
- `StateService`
- `ProviderRegistry`
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
保存工程
```

连接不是扫描。物理总线未变化时，用户不需要每次运行都重新扫描。

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

Gateway 当前有 13 个工具，包括 Mock-only 的只读控制器视图、运行时上下文读取和经审批
语义动作意图。控制器工具当前明确拒绝非 Mock controller context，不能把它们当成真实
Product API 会话的查询入口。扩展规则是：

1. MCP 与 REST 调用同一个 Dispatcher、参数校验、OperationId journal 和 audit。
2. 监听默认关闭，只绑定 `127.0.0.1`。
3. 只读查询每次从 `AutomationService`/`SemanticRuntimeService` 获取新鲜值。
4. 写操作只能提交已经签名、已绑定、需要审批的通用语义动作。
5. 自动化调用者不能批准自己的操作。
6. Gateway 不连接控制器、不获取租约、不扫描、不部署、不直接运动。
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
  当前控制器状态/拓扑/诊断工具仍是 Mock-only，尚未接入真实 Product API 快照。

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

### P1：统一业务协调层

1. 将真实 topology 与 Mock scan 统一到通用 TopologyService/Provider，避免 Workbench、
   Gateway 和属性页看到不同来源。
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

- `ProviderRegistry` 当前会静默忽略重复 Provider ID。新 Provider 上线前必须人工和测试
  检查 ID 唯一性，后续应改为可见启动诊断并 fail closed。
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
