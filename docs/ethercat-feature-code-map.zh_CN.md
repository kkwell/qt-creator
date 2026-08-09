# Embed Labs EtherCAT 功能与源码定位地图

> 本文件由 `docs/ethercat-feature-locator.json` 确定性生成。
> 不要直接编辑；先修改机器清单，再运行：
> `python3 scripts/ethercat_feature_locator.py generate`

## 1. 不搜索源码的使用方法

```sh
python3 scripts/ethercat_feature_locator.py list
python3 scripts/ethercat_feature_locator.py find 扫描
python3 scripts/ethercat_feature_locator.py show ethercat.product-api.topology-evidence
python3 scripts/ethercat_feature_locator.py context ethercat.product-api.topology-evidence
python3 scripts/ethercat_feature_locator.py issues --status open
python3 scripts/ethercat_feature_locator.py impact src/plugins/ethercatproductapi/productapisession.cpp
python3 scripts/ethercat_feature_locator.py check
```

`find` 只查询已审校的功能清单；`show` 直接返回负责插件、入口符号、
公共合同、测试和文档。新增或移动文件后，`check` 会拒绝不存在、
未跟踪或符号已消失的映射。

## 2. 组件和依赖方向

| 组件 ID | Target | 目录 | 直接依赖 | 职责 |
|---|---|---|---|---|
| `ethercat-data` | `EtherCATData` | [`src/libs/ethercatdata`](../src/libs/ethercatdata) | — | 跨插件共享的不可变值类型、枚举、稳定标识和数据合同。 |
| `ethercat-core` | `EtherCATCore` | [`src/plugins/ethercatcore`](../src/plugins/ethercatcore) | `ethercat-data` | Provider 注册、公共服务接口、状态与选择等扩展边界。 |
| `ethercat-project` | `EtherCATProject` | [`src/plugins/ethercatproject`](../src/plugins/ethercatproject) | `ethercat-data`、`ethercat-core` | 工程格式、迁移、持久化、Undo/Redo 和工程变更服务。 |
| `ethercat-devices` | `EtherCATDevices` | [`src/plugins/ethercatdevices`](../src/plugins/ethercatdevices) | `ethercat-data`、`ethercat-core` | ESI XML 解析、固定设备库、导入和设备描述索引。 |
| `ethercat-device-adapters` | `EtherCATDeviceAdapters` | [`src/plugins/ethercatdeviceadapters`](../src/plugins/ethercatdeviceadapters) | `ethercat-data`、`ethercat-core` | 厂家无关 Adapter 目录、精确型号匹配、授权和信任校验。 |
| `ethercat-product-api` | `EtherCATProductApi` | [`src/plugins/ethercatproductapi`](../src/plugins/ethercatproductapi) | `ethercat-data`、`ethercat-core` | Embed Labs 控制器三通道 Product API、会话、协议和在线 Provider。 |
| `ethercat-project-compiler` | `EtherCATProjectCompiler` | [`src/plugins/ethercatprojectcompiler`](../src/plugins/ethercatprojectcompiler) | `ethercat-data`、`ethercat-core` | 工程快照投影、外部编译器调用、幂等账本和准备事务。 |
| `ethercat-semantic-runtime` | `EtherCATSemanticRuntime` | [`src/plugins/ethercatsemanticruntime`](../src/plugins/ethercatsemanticruntime) | `ethercat-data`、`ethercat-core`、`ethercat-project`、`ethercat-project-compiler` | 签名 ECPKG 证据、语义绑定、动作执行和激活事务。 |
| `ethercat-workbench` | `EtherCATWorkbench` | [`src/plugins/ethercatworkbench`](../src/plugins/ethercatworkbench) | `ethercat-data`、`ethercat-core`、`ethercat-devices`、`ethercat-project`、`ethercat-project-compiler`、`ethercat-semantic-runtime` | 统一工程树、命令、属性页、在线状态和应用程序输出。 |
| `ethercat-scan` | `EtherCATScan` | [`src/plugins/ethercatscan`](../src/plugins/ethercatscan) | `ethercat-data`、`ethercat-core`、`ethercat-devices`、`ethercat-project`、`ethercat-workbench` | 仅 Mock 的扫描、拓扑比较和接受到离线工程流程。 |
| `ethercat-diagnostics` | `EtherCATDiagnostics` | [`src/plugins/ethercatdiagnostics`](../src/plugins/ethercatdiagnostics) | `ethercat-data`、`ethercat-core`、`ethercat-project`、`ethercat-workbench` | 仅 Mock 的诊断采样、告警生命周期和诊断属性页。 |
| `ethercat-automation-gateway` | `EtherCATAutomationGateway` | [`src/plugins/ethercatautomationgateway`](../src/plugins/ethercatautomationgateway) | `ethercat-data`、`ethercat-core`、`ethercat-semantic-runtime`、`ethercat-workbench` | 默认关闭且仅回环绑定的 MCP/REST 自动化入口。 |

## 3. 功能快速索引

| 功能 ID | 功能 | Owner | 边界 | 第一入口 |
|---|---|---|---|---|
| `ethercat.data.domain-contracts` | 跨插件数据合同 | `EtherCATData` | `contract-only` | [`src/libs/ethercatdata/offlineconfiguration.cpp`](../src/libs/ethercatdata/offlineconfiguration.cpp) |
| `ethercat.core.provider-registry` | Provider 注册与唯一性 | `EtherCATCore` | `engineering-only` | [`src/plugins/ethercatcore/providerregistry.cpp`](../src/plugins/ethercatcore/providerregistry.cpp) |
| `ethercat.core.topology-service` | 来源隔离的统一拓扑服务 | `EtherCATCore` | `engineering-only` | [`src/plugins/ethercatcore/topologyservice.cpp`](../src/plugins/ethercatcore/topologyservice.cpp) |
| `ethercat.core.manual-control-contract` | 通用手动控制合同 | `EtherCATCore` | `contract-only` | [`src/plugins/ethercatcore/manualcontrolcontract.cpp`](../src/plugins/ethercatcore/manualcontrolcontract.cpp) |
| `ethercat.project.model-format` | 工程格式与迁移 | `EtherCATProject` | `engineering-only` | [`src/plugins/ethercatproject/ethercatprojectformat.cpp`](../src/plugins/ethercatproject/ethercatprojectformat.cpp) |
| `ethercat.project.mutation` | 工程变更、Undo 与 CAS | `EtherCATProject` | `engineering-only` | [`src/plugins/ethercatproject/projectserviceimpl.cpp`](../src/plugins/ethercatproject/projectserviceimpl.cpp) |
| `ethercat.devices.esi-repository` | ESI 设备库与 XML 解析 | `EtherCATDevices` | `engineering-only` | [`src/plugins/ethercatdevices/devicerepository.cpp`](../src/plugins/ethercatdevices/devicerepository.cpp) |
| `ethercat.adapters.catalog-authorization` | Adapter 目录、型号适配与授权 | `EtherCATDeviceAdapters` | `engineering-only` | [`src/plugins/ethercatdeviceadapters/adapterpackagerepository.cpp`](../src/plugins/ethercatdeviceadapters/adapterpackagerepository.cpp) |
| `ethercat.product-api.transport-session` | Product API 三通道会话 | `EtherCATProductApi` | `real-controller` | [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp) |
| `ethercat.product-api.telemetry` | 状态、性能与告警遥测 | `EtherCATProductApi` | `real-controller` | [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp) |
| `ethercat.product-api.control-lifecycle` | 租约、状态切换与故障复位 | `EtherCATProductApi` | `real-controller` | [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp) |
| `ethercat.product-api.topology-evidence` | 真实总线扫描与拓扑证据 | `EtherCATProductApi` | `real-controller` | [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp) |
| `ethercat.product-api.package-deployment` | 运行包部署与激活协议 | `EtherCATProductApi` | `real-controller` | [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp) |
| `ethercat.product-api.runtime-resources` | 通用运行时资源读取 | `EtherCATProductApi` | `real-controller` | [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp) |
| `ethercat.product-api.semantic-attestation` | 语义映射在线证明 | `EtherCATProductApi` | `real-controller` | [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp) |
| `ethercat.product-api.output-transactions` | 原子输出事务 | `EtherCATProductApi` | `real-controller` | [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp) |
| `ethercat.compiler.project-projection` | 工程快照到编译请求 | `EtherCATProjectCompiler` | `engineering-only` | [`src/plugins/ethercatprojectcompiler/provisionedruntimepackagecompilerprojectrequestbuilder.cpp`](../src/plugins/ethercatprojectcompiler/provisionedruntimepackagecompilerprojectrequestbuilder.cpp) |
| `ethercat.compiler.backend` | 外部编译器与不可变工件 | `EtherCATProjectCompiler` | `engineering-only` | [`src/plugins/ethercatprojectcompiler/provisionedruntimepackagecompilerprovider.cpp`](../src/plugins/ethercatprojectcompiler/provisionedruntimepackagecompilerprovider.cpp) |
| `ethercat.compiler.preparation` | 可恢复的编译准备事务 | `EtherCATProjectCompiler` | `engineering-only` | [`src/plugins/ethercatprojectcompiler/durableruntimepackagecompilerpreparationcoordinator.cpp`](../src/plugins/ethercatprojectcompiler/durableruntimepackagecompilerpreparationcoordinator.cpp) |
| `ethercat.runtime.package-evidence` | 签名 ECPKG 与运行包证据 | `EtherCATSemanticRuntime` | `engineering-only` | [`src/plugins/ethercatsemanticruntime/runtimepackageevidencerepository_p.cpp`](../src/plugins/ethercatsemanticruntime/runtimepackageevidencerepository_p.cpp) |
| `ethercat.runtime.binding-actions` | 实例语义绑定与动作定义 | `EtherCATSemanticRuntime` | `engineering-only` | [`src/plugins/ethercatsemanticruntime/semanticbindingartifact_p.cpp`](../src/plugins/ethercatsemanticruntime/semanticbindingartifact_p.cpp) |
| `ethercat.runtime.activation` | 受信运行包激活事务 | `EtherCATSemanticRuntime` | `real-controller` | [`src/plugins/ethercatsemanticruntime/runtimepackageactivationservice_p.cpp`](../src/plugins/ethercatsemanticruntime/runtimepackageactivationservice_p.cpp) |
| `ethercat.runtime.manual-control` | 通用语义手动控制执行 | `EtherCATSemanticRuntime` | `real-controller` | [`src/plugins/ethercatsemanticruntime/semanticruntimeexecutor.cpp`](../src/plugins/ethercatsemanticruntime/semanticruntimeexecutor.cpp) |
| `ethercat.workbench.details-routing` | 右侧属性页动态路由 | `EtherCATWorkbench` | `engineering-only` | [`src/plugins/ethercatworkbench/detailsview.cpp`](../src/plugins/ethercatworkbench/detailsview.cpp) |
| `ethercat.workbench.general-overview` | 工程主要信息与主站概览 | `EtherCATWorkbench` | `engineering-only` | [`src/plugins/ethercatworkbench/generalpage.cpp`](../src/plugins/ethercatworkbench/generalpage.cpp) |
| `ethercat.workbench.project-navigation` | 工程打开、模式和设备树 | `EtherCATWorkbench` | `engineering-only` | [`src/plugins/ethercatworkbench/workbenchnavigation.cpp`](../src/plugins/ethercatworkbench/workbenchnavigation.cpp) |
| `ethercat.workbench.communication` | IP、连接、扫描和快捷控制 | `EtherCATWorkbench` | `real-controller` | [`src/plugins/ethercatworkbench/communicationpage.cpp`](../src/plugins/ethercatworkbench/communicationpage.cpp) |
| `ethercat.workbench.configuration-pages` | PDO、Startup SDO 和 DC 配置页 | `EtherCATWorkbench` | `engineering-only` | [`src/plugins/ethercatworkbench/processdatapage.cpp`](../src/plugins/ethercatworkbench/processdatapage.cpp) |
| `ethercat.workbench.esi-library` | ESI 设备库界面 | `EtherCATWorkbench` | `engineering-only` | [`src/plugins/ethercatworkbench/esirepositorypage.cpp`](../src/plugins/ethercatworkbench/esirepositorypage.cpp) |
| `ethercat.workbench.coe-view` | CoE 参数浏览与 Startup 复制 | `EtherCATWorkbench` | `mock-only` | [`src/plugins/ethercatworkbench/coeonlinepage.cpp`](../src/plugins/ethercatworkbench/coeonlinepage.cpp) |
| `ethercat.workbench.deployment` | 编译准备与运行包部署页 | `EtherCATWorkbench` | `real-controller` | [`src/plugins/ethercatworkbench/deploymentpage.cpp`](../src/plugins/ethercatworkbench/deploymentpage.cpp) |
| `ethercat.workbench.semantic-control` | 节点右侧手动控制页 | `EtherCATWorkbench` | `real-controller` | [`src/plugins/ethercatworkbench/semanticcontrolpage.cpp`](../src/plugins/ethercatworkbench/semanticcontrolpage.cpp) |
| `ethercat.workbench.output-status` | 统一应用程序输出和状态投影 | `EtherCATWorkbench` | `mixed-real-loopback` | [`src/plugins/ethercatworkbench/workbenchcontroller.cpp`](../src/plugins/ethercatworkbench/workbenchcontroller.cpp) |
| `ethercat.scan.mock-workflow` | Mock 扫描与拓扑比较 | `EtherCATScan` | `mock-only` | [`src/plugins/ethercatscan/mockscanprovider.cpp`](../src/plugins/ethercatscan/mockscanprovider.cpp) |
| `ethercat.diagnostics.mock-stream` | Mock 诊断流 | `EtherCATDiagnostics` | `mock-only` | [`src/plugins/ethercatdiagnostics/mockdiagnosticsprovider.cpp`](../src/plugins/ethercatdiagnostics/mockdiagnosticsprovider.cpp) |
| `ethercat.gateway.loopback-transport` | 回环 MCP 与 REST 监听器 | `EtherCATAutomationGateway` | `loopback-only` | [`src/plugins/ethercatautomationgateway/gatewayserver.cpp`](../src/plugins/ethercatautomationgateway/gatewayserver.cpp) |
| `ethercat.gateway.controller-views-intents` | 自动化只读视图与语义意图 | `EtherCATAutomationGateway` | `loopback-only` | [`src/plugins/ethercatautomationgateway/automationdispatcher.cpp`](../src/plugins/ethercatautomationgateway/automationdispatcher.cpp) |
| `ethercat.gateway.contract-tools` | Adapter、工件与协议查询工具 | `EtherCATAutomationGateway` | `loopback-only` | [`src/plugins/ethercatautomationgateway/automationdispatcher.cpp`](../src/plugins/ethercatautomationgateway/automationdispatcher.cpp) |

## 4. 按领域查看修改入口

### 4.1 架构与公共合同

#### `ethercat.data.domain-contracts` — 跨插件数据合同

定义工程、控制器、资源、输出事务和语义运行时共享值类型。

- Owner：`EtherCATData`（[`src/libs/ethercatdata`](../src/libs/ethercatdata)）
- 运行边界：`contract-only`
- 证据边界：`unit`
- 修改入口：
  - [`src/libs/ethercatdata/offlineconfiguration.cpp`](../src/libs/ethercatdata/offlineconfiguration.cpp)：离线配置值合同校验；`validateProcessDataConfiguration`
- 公共合同：
  - [`src/libs/ethercatdata/controllerconnection.h`](../src/libs/ethercatdata/controllerconnection.h)：控制器在线共享合同；`ControllerConnectionSnapshot`、`ControllerControlRequest`
  - [`src/libs/ethercatdata/projectsnapshot.h`](../src/libs/ethercatdata/projectsnapshot.h)：工程快照合同；`ProjectSnapshot`
- 定向测试：
  - [`src/plugins/ethercatcore/ethercatcoretests.cpp`](../src/plugins/ethercatcore/ethercatcoretests.cpp)（`unit`）：`testProjectSnapshotValueSemantics`、`testRuntimeResourceValueSemantics`
- 相关文档：[`docs/ethercat-core-api.md`](../docs/ethercat-core-api.md)
- 边界提醒：跨插件只传递此库或 EtherCATCore 的公开合同，不包含其他插件的私有头文件。

#### `ethercat.core.provider-registry` — Provider 注册与唯一性

发现公共 Provider、拒绝重复稳定 ID，并以有界诊断报告冲突。

- Owner：`EtherCATCore`（[`src/plugins/ethercatcore`](../src/plugins/ethercatcore)）
- 运行边界：`engineering-only`
- 证据边界：`unit`、`offscreen-ui`
- 修改入口：
  - [`src/plugins/ethercatcore/providerregistry.cpp`](../src/plugins/ethercatcore/providerregistry.cpp)：对象池发现、唯一性和有界诊断；`ProviderRegistry::handleObjectAdded`、`ProviderRegistry::recordDuplicateProvider`
- 公共合同：
  - [`src/plugins/ethercatcore/providers.h`](../src/plugins/ethercatcore/providers.h)：公共 Provider 基类和启动诊断；`class ETHERCATCORE_EXPORT Provider`、`ProviderStartupDiagnostic`
- 定向测试：
  - [`src/plugins/ethercatcore/ethercatcoretests.cpp`](../src/plugins/ethercatcore/ethercatcoretests.cpp)（`unit`）：`testProviderRegistryTracksObjectPool`
  - [`src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp`](../src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp)（`offscreen-ui`）：`testProviderStartupDiagnosticsPresentation`
- 相关文档：[`docs/ethercat-plugin-development-guide.zh_CN.md`](../docs/ethercat-plugin-development-guide.zh_CN.md)
- 前置功能：`ethercat.data.domain-contracts`
- 边界提醒：同一职责只允许一个稳定 Provider ID；冲突对象不会成为备用实现。

#### `ethercat.core.topology-service` — 来源隔离的统一拓扑服务

按精确来源、Provider 和工程 Scope 即时投影真实或 Mock 拓扑，并派生可比较的 generation 与 freshness。

- Owner：`EtherCATCore`（[`src/plugins/ethercatcore`](../src/plugins/ethercatcore)）
- 运行边界：`engineering-only`
- 证据边界：`unit`
- 修改入口：
  - [`src/plugins/ethercatcore/topologyservice.cpp`](../src/plugins/ethercatcore/topologyservice.cpp)：精确来源查询、代际派生和失效通知；`TopologyService::topology`、`TopologySnapshot::generation`、`TopologyService::handleProviderAboutToBeRemoved`
- 公共合同：
  - [`src/plugins/ethercatcore/topologyservice.h`](../src/plugins/ethercatcore/topologyservice.h)：统一只读拓扑选择、来源、代际和 Provider 质量合同；`class ETHERCATCORE_EXPORT TopologyService`、`TopologySelection`、`TopologyGeneration`、`TopologyLookupResult`、`hasFreshProviderEvidence`
- 定向测试：
  - [`src/plugins/ethercatcore/ethercatcoretests.cpp`](../src/plugins/ethercatcore/ethercatcoretests.cpp)（`unit`）：`testTopologyServiceKeepsRealAndMockEvidenceSeparate`
- 相关文档：[`docs/ethercat-plugin-development-guide.zh_CN.md`](../docs/ethercat-plugin-development-guide.zh_CN.md)
- 前置功能：`ethercat.data.domain-contracts`、`ethercat.core.provider-registry`
- 边界提醒：服务不缓存拓扑、不触发连接或扫描；Real 与 Mock 永不自动替补。Fresh 只表示所选 Provider 仍暴露这一代证据，不证明与当前 ProjectSnapshot 修订匹配，也不授权编译或执行。
- 边界提醒：标记为 mock 的 ControllerConnectionSnapshot 会被拒绝，不能借 ControllerConnectionProvider 类型冒充真实来源。
- 边界提醒：Workbench 已按显式 Provider/Profile 只消费 Fresh RealController 证据；Mock 扫描的显式选择合同仍待独立闭合。

#### `ethercat.core.manual-control-contract` — 通用手动控制合同

验证工程单位换算、手动包络、超时策略和安全边界，不含厂家协议分支。

- Owner：`EtherCATCore`（[`src/plugins/ethercatcore`](../src/plugins/ethercatcore)）
- 运行边界：`contract-only`
- 证据边界：`unit`
- 修改入口：
  - [`src/plugins/ethercatcore/manualcontrolcontract.cpp`](../src/plugins/ethercatcore/manualcontrolcontract.cpp)：通用工程值与手动包络校验；`validateManualControlEnvelope`
- 公共合同：
  - [`src/plugins/ethercatcore/manualcontrolcontract.h`](../src/plugins/ethercatcore/manualcontrolcontract.h)：手动控制校验公开接口；`ManualControlContractValidation`、`validateManualControlEnvelope`
- 定向测试：
  - [`src/plugins/ethercatcore/ethercatcoretests.cpp`](../src/plugins/ethercatcore/ethercatcoretests.cpp)（`unit`）：`testExactEngineeringConversionContract`、`testManualControlEnvelopeContract`
- 相关文档：[`docs/ethercat-plugin-development-guide.zh_CN.md`](../docs/ethercat-plugin-development-guide.zh_CN.md)
- 前置功能：`ethercat.data.domain-contracts`
- 边界提醒：厂家对象、PDO 偏移和 CiA402 步骤应由 Adapter 提供，Core 不得写死。

### 4.2 工程模型

#### `ethercat.project.model-format` — 工程格式与迁移

维护当前 v7 工程 JSON、严格结构校验和旧版本逐级迁移。

- Owner：`EtherCATProject`（[`src/plugins/ethercatproject`](../src/plugins/ethercatproject)）
- 运行边界：`engineering-only`
- 证据边界：`unit`
- 修改入口：
  - [`src/plugins/ethercatproject/ethercatprojectformat.cpp`](../src/plugins/ethercatproject/ethercatprojectformat.cpp)：工程读写、校验和迁移；`formatVersion`、`CURRENT_FORMAT_VERSION`
- 公共合同：
  - [`src/libs/ethercatdata/projectsnapshot.h`](../src/libs/ethercatdata/projectsnapshot.h)：对外工程快照；`ProjectSnapshot`、`OfflineSlaveConfiguration`
- 定向测试：
  - [`src/plugins/ethercatproject/ethercatprojecttests.cpp`](../src/plugins/ethercatproject/ethercatprojecttests.cpp)（`unit`）：`testFormatRoundTripAndCorruption`、`testVersionSixStationAddressMigration`
- 相关文档：[`docs/ethercat-project-format.md`](../docs/ethercat-project-format.md)
- 前置功能：`ethercat.data.domain-contracts`
- 边界提醒：新增持久化字段时必须同时提供迁移、严格解析、往返和损坏输入测试。

#### `ethercat.project.mutation` — 工程变更、Undo 与 CAS

所有界面编辑通过 ProjectService 写入工程，并保留 Undo/Redo 与激活比较交换语义。

- Owner：`EtherCATProject`（[`src/plugins/ethercatproject`](../src/plugins/ethercatproject)）
- 运行边界：`engineering-only`
- 证据边界：`unit`
- 修改入口：
  - [`src/plugins/ethercatproject/projectserviceimpl.cpp`](../src/plugins/ethercatproject/projectserviceimpl.cpp)：工程变更服务实现；`ProjectServiceImpl::replaceOfflineSlaves`、`ProjectServiceImpl::setStartupProject`
- 公共合同：
  - [`src/plugins/ethercatcore/providers.h`](../src/plugins/ethercatcore/providers.h)：跨插件工程服务；`class ETHERCATCORE_EXPORT ProjectService`、`replaceOfflineSlaves`
- 定向测试：
  - [`src/plugins/ethercatproject/ethercatprojecttests.cpp`](../src/plugins/ethercatproject/ethercatprojecttests.cpp)（`unit`）：`testDocumentUndoRedoAndAtomicFailure`、`testRuntimePackageActivationProjectCompareAndSet`
- 相关文档：[`docs/ethercat-plugin-development-guide.zh_CN.md`](../docs/ethercat-plugin-development-guide.zh_CN.md)
- 前置功能：`ethercat.project.model-format`、`ethercat.core.provider-registry`
- 边界提醒：页面不得直接修改文档内部对象，也不得维护第二份工程状态。

### 4.3 设备、ESI 与 Adapter

#### `ethercat.devices.esi-repository` — ESI 设备库与 XML 解析

从固定目录加载或导入原始 ESI XML，建立设备、模块、PDO、SDO 和 DC 描述索引。

- Owner：`EtherCATDevices`（[`src/plugins/ethercatdevices`](../src/plugins/ethercatdevices)）
- 运行边界：`engineering-only`
- 证据边界：`unit`
- 修改入口：
  - [`src/plugins/ethercatdevices/devicerepository.cpp`](../src/plugins/ethercatdevices/devicerepository.cpp)：固定目录索引和导入；`DeviceRepository::importFiles`、`DeviceRepository::devices`
  - [`src/plugins/ethercatdevices/esiparser.cpp`](../src/plugins/ethercatdevices/esiparser.cpp)：严格 XML 解析；`parseEsiFile`
- 公共合同：
  - [`src/libs/ethercatdata/devicedescription.h`](../src/libs/ethercatdata/devicedescription.h)：规范化 ESI 描述；`DeviceDescription`、`DeviceIdentity`
- 定向测试：
  - [`src/plugins/ethercatdevices/ethercatdevicestests.cpp`](../src/plugins/ethercatdevices/ethercatdevicestests.cpp)（`unit`）：`testParserReadsOperationalData`、`testRepositoryImportFilterAndRebuild`
- 相关文档：[`docs/ethercat-devices-repository.md`](../docs/ethercat-devices-repository.md)
- 前置功能：`ethercat.data.domain-contracts`、`ethercat.core.provider-registry`
- 边界提醒：ESI 描述设备事实；不能用名称猜测 VendorId、ProductCode 或 Revision。

#### `ethercat.adapters.catalog-authorization` — Adapter 目录、型号适配与授权

加载 v1/v2/v3 Adapter，按精确设备身份选择，并验证独立授权和生产信任链。

- Owner：`EtherCATDeviceAdapters`（[`src/plugins/ethercatdeviceadapters`](../src/plugins/ethercatdeviceadapters)）
- 运行边界：`engineering-only`
- 证据边界：`unit`、`artifact`
- 修改入口：
  - [`src/plugins/ethercatdeviceadapters/adapterpackagerepository.cpp`](../src/plugins/ethercatdeviceadapters/adapterpackagerepository.cpp)：目录加载、精确匹配和授权投影；`AdapterPackageRepository::resolveDevice`、`AdapterPackageRepository::authorizationStatus`
  - [`src/plugins/ethercatdeviceadapters/deviceadapterauthorization_p.cpp`](../src/plugins/ethercatdeviceadapters/deviceadapterauthorization_p.cpp)：授权签名与信任校验；`applyDeviceAdapterAuthorizations`
- 公共合同：
  - [`src/libs/ethercatdata/deviceadapter.h`](../src/libs/ethercatdata/deviceadapter.h)：厂家无关 Adapter 和动作合同；`DeviceAdapterManifest`、`DeviceControlAction`
- 定向测试：
  - [`src/plugins/ethercatdeviceadapters/ethercatdeviceadapterstests.cpp`](../src/plugins/ethercatdeviceadapters/ethercatdeviceadapterstests.cpp)（`artifact`）：`testV3SignedActionContract`、`testExactIdentityAndEsiMatching`、`testInstalledProductionAuthorizations`
- 相关文档：[`docs/ethercat-device-adapters.md`](../docs/ethercat-device-adapters.md)
- 前置功能：`ethercat.data.domain-contracts`
- 边界提醒：新增厂家或型号优先只增加 ESI、Adapter、授权和测试，不向 Product API 或 Workbench 添加厂家分支。

### 4.4 真实控制器在线功能

#### `ethercat.product-api.transport-session` — Product API 三通道会话

管理 Control、Push、Bulk TCP 通道、握手、心跳、重连和统一快照。

- Owner：`EtherCATProductApi`（[`src/plugins/ethercatproductapi`](../src/plugins/ethercatproductapi)）
- 运行边界：`real-controller`
- 证据边界：`loopback`、`hardware-gated`
- 修改入口：
  - [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp)：三通道会话生命周期；`ProductApiSession::connectToController`、`ProductApiSession::disconnectFromController`
  - [`src/plugins/ethercatproductapi/productapicodec.cpp`](../src/plugins/ethercatproductapi/productapicodec.cpp)：ECAP 帧编解码；`encodeFrame`、`decodeHelloAck`
- 公共合同：
  - [`src/libs/ethercatdata/controllerconnection.h`](../src/libs/ethercatdata/controllerconnection.h)：厂商无关连接快照；`ControllerConnectionRequest`、`ControllerConnectionSnapshot`
  - [`src/plugins/ethercatcore/providers.h`](../src/plugins/ethercatcore/providers.h)：控制器 Provider 接口；`class ETHERCATCORE_EXPORT ControllerConnectionProvider`
- 定向测试：
  - [`src/plugins/ethercatproductapi/ethercatproductapitests.cpp`](../src/plugins/ethercatproductapi/ethercatproductapitests.cpp)（`loopback`）：`testThreeChannelInitialSnapshot`、`testSessionReconnectAndGeneration`、`testChannelConnectionFailureDiagnostics`
- 相关文档：[`docs/ethercat-product-api.md`](../docs/ethercat-product-api.md)、[`docs/ethercat-online-controller.md`](../docs/ethercat-online-controller.md)
- 前置功能：`ethercat.core.provider-registry`、`ethercat.data.domain-contracts`
- 边界提醒：生产实现可连接真实控制器；普通自动化测试使用本地回环服务，二者不能混称。

#### `ethercat.product-api.telemetry` — 状态、性能与告警遥测

解码并发布控制器状态、周期计数、WKC、DC、性能、故障位和告警事件。

- Owner：`EtherCATProductApi`（[`src/plugins/ethercatproductapi`](../src/plugins/ethercatproductapi)）
- 运行边界：`real-controller`
- 证据边界：`loopback`、`hardware-gated`
- 修改入口：
  - [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp)：Push 遥测处理和统一快照更新；`Protocol::decodeControllerState`、`Protocol::decodePerformanceSnapshot`、`Protocol::MessageType::AlarmRaised`
- 公共合同：
  - [`src/libs/ethercatdata/controllerconnection.h`](../src/libs/ethercatdata/controllerconnection.h)：厂商无关遥测合同；`ControllerStateSummary`、`ControllerPerformanceSummary`、`ControllerAlarmSummary`、`ControllerFault`
- 定向测试：
  - [`src/plugins/ethercatproductapi/ethercatproductapitests.cpp`](../src/plugins/ethercatproductapi/ethercatproductapitests.cpp)（`loopback`）：`testSemanticControllerState`、`testSemanticPerformanceSnapshot`、`testControllerErrorAttribution`
- 相关文档：[`docs/ethercat-product-api.md`](../docs/ethercat-product-api.md)、[`docs/ethercat-online-controller.md`](../docs/ethercat-online-controller.md)
- 前置功能：`ethercat.product-api.transport-session`
- 边界提醒：真实遥测在 Product API；EtherCATDiagnostics 当前仅提供 Mock 诊断流。
- 边界提醒：故障输出必须区分 current 与 latched 位并保留告警详情。

#### `ethercat.product-api.control-lifecycle` — 租约、状态切换与故障复位

执行取得控制、配置、FreeRun/DC 启停、暂停恢复、受控停止和确认式故障复位。

- Owner：`EtherCATProductApi`（[`src/plugins/ethercatproductapi`](../src/plugins/ethercatproductapi)）
- 运行边界：`real-controller`
- 证据边界：`loopback`、`hardware-gated`
- 修改入口：
  - [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp)：控制命令状态机和租约保护；`ProductApiSession::executeControlCommand`、`Protocol::MessageType::StartDc`
- 公共合同：
  - [`src/libs/ethercatdata/controllerconnection.h`](../src/libs/ethercatdata/controllerconnection.h)：通用控制命令合同；`ControllerControlCommand`、`ControllerControlRequest`、`ControllerControlProgress`
- 定向测试：
  - [`src/plugins/ethercatproductapi/ethercatproductapitests.cpp`](../src/plugins/ethercatproductapi/ethercatproductapitests.cpp)（`loopback`）：`testControlLifecycle`、`testFaultResetLifecycle`、`testLeaseExpiryPreservesAutonomousRuntime`
  - [`src/plugins/ethercatproductapi/ethercatproductapitests.cpp`](../src/plugins/ethercatproductapi/ethercatproductapitests.cpp)（`hardware-gated`）：`testHardwareControlLifecycle`
- 相关文档：[`docs/ethercat-online-controller.md`](../docs/ethercat-online-controller.md)
- 前置功能：`ethercat.product-api.transport-session`
- 边界提醒：连接断开或租约过期只释放管理所有权，不得停止已经部署的自治周期任务。

#### `ethercat.product-api.topology-evidence` — 真实总线扫描与拓扑证据

按用户明确请求读取真实从站顺序、身份、模块和带代际的拓扑证据。

- Owner：`EtherCATProductApi`（[`src/plugins/ethercatproductapi`](../src/plugins/ethercatproductapi)）
- 运行边界：`real-controller`
- 证据边界：`loopback`、`hardware-gated`
- 修改入口：
  - [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp)：扫描请求、分页证据和失效处理；`DiscoverTopologyEvidence`、`TopologyEvidence`
- 公共合同：
  - [`src/libs/ethercatdata/controllerconnection.h`](../src/libs/ethercatdata/controllerconnection.h)：在线拓扑证据合同；`ControllerTopologySnapshot`、`ControllerTopologySlave`、`ControllerTopologyEvidenceValidity`
- 定向测试：
  - [`src/plugins/ethercatproductapi/ethercatproductapitests.cpp`](../src/plugins/ethercatproductapi/ethercatproductapitests.cpp)（`loopback`）：`testTopologyEvidenceCodec`、`testTopologyEvidenceLifecycle`
  - [`src/plugins/ethercatproductapi/ethercatproductapitests.cpp`](../src/plugins/ethercatproductapi/ethercatproductapitests.cpp)（`hardware-gated`）：`testHardwareControlLifecycle`
- 相关文档：[`docs/ethercat-online-controller.md`](../docs/ethercat-online-controller.md)
- 前置功能：`ethercat.product-api.transport-session`
- 边界提醒：真实扫描入口在 Product API/Workbench；EtherCATScan 插件只提供 Mock 工作流。
- 边界提醒：连接成功不会隐式扫描，只有用户点击重新扫描才刷新拓扑。

#### `ethercat.product-api.package-deployment` — 运行包部署与激活协议

上传、校验、激活、回滚 ECPKG，并投影包状态和审计进度。

- Owner：`EtherCATProductApi`（[`src/plugins/ethercatproductapi`](../src/plugins/ethercatproductapi)）
- 运行边界：`real-controller`
- 证据边界：`loopback`、`not-hardware-qualified`
- 修改入口：
  - [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp)：分块上传和包命令状态机；`ProductApiSession::deployPackage`、`BulkBegin`
- 公共合同：
  - [`src/libs/ethercatdata/controllerconnection.h`](../src/libs/ethercatdata/controllerconnection.h)：部署请求、状态和审计合同；`ControllerPackageDeploymentRequest`、`ControllerPackageDeploymentProgress`
- 定向测试：
  - [`src/plugins/ethercatproductapi/ethercatproductapitests.cpp`](../src/plugins/ethercatproductapi/ethercatproductapitests.cpp)（`loopback`）：`testPackageDeploymentLifecycle`、`testPackageDeploymentGuardsAndIdempotency`
- 相关文档：[`docs/ethercat-product-api.md`](../docs/ethercat-product-api.md)
- 前置功能：`ethercat.product-api.control-lifecycle`
- 边界提醒：部署协议能力存在不等于本次已在真实硬件完成验收；真实结论必须来自独立硬件门禁。

#### `ethercat.product-api.runtime-resources` — 通用运行时资源读取

查询包绑定的资源目录和同周期快照，为厂家无关输入读取提供底层通道。

- Owner：`EtherCATProductApi`（[`src/plugins/ethercatproductapi`](../src/plugins/ethercatproductapi)）
- 运行边界：`real-controller`
- 证据边界：`loopback`、`not-hardware-qualified`
- 修改入口：
  - [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp)：资源目录和定向快照请求；`ProductApiSession::refreshRuntimeResources`、`ProductApiSession::requestRuntimeResourceSnapshot`
- 公共合同：
  - [`src/libs/ethercatdata/runtimeresource.h`](../src/libs/ethercatdata/runtimeresource.h)：通用资源与质量合同；`RuntimeResourceCatalog`、`RuntimeResourceSnapshot`、`RuntimeResourceDescriptor`
- 定向测试：
  - [`src/plugins/ethercatproductapi/ethercatproductapitests.cpp`](../src/plugins/ethercatproductapi/ethercatproductapitests.cpp)（`loopback`）：`testRuntimeResourceLoopbackLifecycle`、`testTargetedRuntimeResourceSnapshotLifecycle`
- 相关文档：[`docs/ethercat-product-api.md`](../docs/ethercat-product-api.md)
- 前置功能：`ethercat.product-api.transport-session`
- 边界提醒：ResourceId 必须与完整包 epoch 和签名映射绑定，不能按名称、站号或 PDO 偏移猜测。

#### `ethercat.product-api.semantic-attestation` — 语义映射在线证明

查询控制器当前包的签名映射摘要、安全标志和完整运行时 epoch。

- Owner：`EtherCATProductApi`（[`src/plugins/ethercatproductapi`](../src/plugins/ethercatproductapi)）
- 运行边界：`real-controller`
- 证据边界：`loopback`、`artifact`、`not-hardware-qualified`
- 修改入口：
  - [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp)：在线映射证明请求和失效处理；`ProductApiSession::requestRuntimeSemanticMappingAttestation`、`ProductApiSession::supportsRuntimeSemanticMappingAttestation`
- 公共合同：
  - [`src/libs/ethercatdata/semanticmappingattestation.h`](../src/libs/ethercatdata/semanticmappingattestation.h)：映射证明值合同；`RuntimeSemanticMappingAttestationRequest`、`RuntimeSemanticMappingAttestationResult`
- 定向测试：
  - [`src/plugins/ethercatproductapi/ethercatproductapitests.cpp`](../src/plugins/ethercatproductapi/ethercatproductapitests.cpp)（`loopback`）：`testSemanticBindingAttestationFormatV2`、`testSemanticAttestationLoopbackLifecycle`、`testSemanticAttestationInvalidationAndStaleResponse`
- 相关文档：[`docs/ethercat-product-api.md`](../docs/ethercat-product-api.md)
- 前置功能：`ethercat.product-api.runtime-resources`
- 边界提醒：控制器只返回证明摘要；完整 canonical 语义工件来自 IDE 持有并验证的同一 ECPKG。

#### `ethercat.product-api.output-transactions` — 原子输出事务

按完整一致性组、OperationId、输出代际和有限 TTL 原子提交通用输出值。

- Owner：`EtherCATProductApi`（[`src/plugins/ethercatproductapi`](../src/plugins/ethercatproductapi)）
- 运行边界：`real-controller`
- 证据边界：`loopback`、`not-hardware-qualified`
- 修改入口：
  - [`src/plugins/ethercatproductapi/productapisession.cpp`](../src/plugins/ethercatproductapi/productapisession.cpp)：输出策略、状态和事务协议；`ProductApiSession::applyRuntimeOutputTransaction`、`ProductApiSession::requestRuntimeOutputGroupPolicy`
- 公共合同：
  - [`src/libs/ethercatdata/runtimeoutputtransaction.h`](../src/libs/ethercatdata/runtimeoutputtransaction.h)：通用输出事务合同；`RuntimeOutputTransactionRequest`、`RuntimeOutputTransactionResult`、`RuntimeOutputGroupPolicy`
- 定向测试：
  - [`src/plugins/ethercatproductapi/ethercatproductapitests.cpp`](../src/plugins/ethercatproductapi/ethercatproductapitests.cpp)（`loopback`）：`testOutputTransactionGoldenFrames`、`testOutputTransactionCompleteGroupGuards`
  - [`src/plugins/ethercatproductapi/ethercatproductapitests.cpp`](../src/plugins/ethercatproductapi/ethercatproductapitests.cpp)（`not-hardware-qualified`）：`testHardwareApi038ProviderAcceptance`
- 相关文档：[`docs/ethercat-product-api.md`](../docs/ethercat-product-api.md)
- 前置功能：`ethercat.product-api.control-lifecycle`、`ethercat.product-api.runtime-resources`
- 边界提醒：当前 Qt 真机 API-038 Provider 验收测试仍明确跳过，不能宣称任意真实输出已通过。

### 4.5 编译与准备

#### `ethercat.compiler.project-projection` — 工程快照到编译请求

把工程、真实拓扑、ESI、Adapter、目标能力和固定构建身份投影为严格编译输入。

- Owner：`EtherCATProjectCompiler`（[`src/plugins/ethercatprojectcompiler`](../src/plugins/ethercatprojectcompiler)）
- 运行边界：`engineering-only`
- 证据边界：`unit`、`artifact`
- 修改入口：
  - [`src/plugins/ethercatprojectcompiler/provisionedruntimepackagecompilerprojectrequestbuilder.cpp`](../src/plugins/ethercatprojectcompiler/provisionedruntimepackagecompilerprojectrequestbuilder.cpp)：严格工程投影和 fail-closed 校验；`ProvisionedRuntimePackageCompilerProjectRequestBuilder::build`
- 公共合同：
  - [`src/plugins/ethercatcore/runtimepackagecompilerprojectrequestbuilder.h`](../src/plugins/ethercatcore/runtimepackagecompilerprojectrequestbuilder.h)：工程编译请求构建边界；`class ETHERCATCORE_EXPORT RuntimePackageCompilerProjectRequestBuilder`
- 定向测试：
  - [`src/plugins/ethercatprojectcompiler/ethercatprojectcompilertests.cpp`](../src/plugins/ethercatprojectcompiler/ethercatprojectcompilertests.cpp)（`artifact`）：`testProjectRequestBuilderProvisioningAndDeterminism`、`testProjectRequestBuilderFailsClosedOnUnprovenTopology`
- 相关文档：[`docs/ethercat-plugin-development-guide.zh_CN.md`](../docs/ethercat-plugin-development-guide.zh_CN.md)
- 前置功能：`ethercat.project.model-format`、`ethercat.adapters.catalog-authorization`、`ethercat.product-api.topology-evidence`
- 边界提醒：不受支持的 PDO、Startup SDO 或 DC 选择必须结构化拒绝，不能静默回退。

#### `ethercat.compiler.backend` — 外部编译器与不可变工件

调用已配置的外部 compile/finalize/query/verify 后端，并保存幂等操作证据。

- Owner：`EtherCATProjectCompiler`（[`src/plugins/ethercatprojectcompiler`](../src/plugins/ethercatprojectcompiler)）
- 运行边界：`engineering-only`
- 证据边界：`unit`、`artifact`
- 修改入口：
  - [`src/plugins/ethercatprojectcompiler/provisionedruntimepackagecompilerprovider.cpp`](../src/plugins/ethercatprojectcompiler/provisionedruntimepackagecompilerprovider.cpp)：双 Profile 外部进程、空白环境和启动前后复验；`ProvisionedRuntimePackageCompilerProvider::compile`、`ProvisionedRuntimePackageCompilerProvider::verify`、`validateExecutionFiles`
  - [`src/plugins/ethercatprojectcompiler/compilerruntimebundleprofile.cpp`](../src/plugins/ethercatprojectcompiler/compilerruntimebundleprofile.cpp)：API-068 签名编译器树、外部信任锚和导入根闭集校验；`CompilerRuntimeBundleProfile::load`、`CompilerRuntimeBundleProfile::validateCurrent`、`CompilerRuntimeBundleProfile::compilerImportRoot`
  - [`src/plugins/ethercatprojectcompiler/compilerpythonruntimeprofile.cpp`](../src/plugins/ethercatprojectcompiler/compilerpythonruntimeprofile.cpp)：API-070 签名 companion 与可重定位 Python 树闭集校验；`CompilerPythonRuntimeProfile::load`、`CompilerPythonRuntimeProfile::validateCurrent`
  - [`src/plugins/ethercatprojectcompiler/compileroperationstore.cpp`](../src/plugins/ethercatprojectcompiler/compileroperationstore.cpp)：持久幂等账本；`CompilerOperationStore`
- 公共合同：
  - [`src/plugins/ethercatcore/runtimepackagecompilerprovider.h`](../src/plugins/ethercatcore/runtimepackagecompilerprovider.h)：可替换编译器 Provider 合同；`class ETHERCATCORE_EXPORT RuntimePackageCompilerProvider`
  - [`src/plugins/ethercatprojectcompiler/compilerruntimebundleprofile.h`](../src/plugins/ethercatprojectcompiler/compilerruntimebundleprofile.h)：API-068 固定版本、外部公钥和不可变树身份合同；`struct CompilerRuntimeBundleExpectation`、`class CompilerRuntimeBundleProfile`
  - [`src/plugins/ethercatprojectcompiler/compilerpythonruntimeprofile.h`](../src/plugins/ethercatprojectcompiler/compilerpythonruntimeprofile.h)：API-070 固定版本、独立公钥和便携身份合同；`struct CompilerPythonRuntimeExpectation`、`class CompilerPythonRuntimeProfile`
- 定向测试：
  - [`src/plugins/ethercatprojectcompiler/ethercatprojectcompilertests.cpp`](../src/plugins/ethercatprojectcompiler/ethercatprojectcompilertests.cpp)（`artifact`）：`testPythonRuntimeProfileVerifiesSignedInstalledTree`、`testRuntimeBundleProfileVerifiesInstalledTree`、`testCompileProcessAndImmutableEvidence`、`testFinalizeQueryVerifyAndRestart`
- 相关文档：[`docs/ethercat-plugin-development-guide.zh_CN.md`](../docs/ethercat-plugin-development-guide.zh_CN.md)、[`docs/ethercat-compiler-runtime.md`](../docs/ethercat-compiler-runtime.md)
- 前置功能：`ethercat.compiler.project-projection`
- 边界提醒：生产私钥不进入 IDE；签名由外部 signer 或 HSM 完成。
- 边界提醒：API-068 与 API-070 使用独立外部信任锚；Provider 不从 PATH、系统 Python 或复制出的 wrapper 推断运行时。
- 边界提醒：当前双 Profile 正例是 unit/artifact 动态修正版 fixture，不代表产品 bootstrap、原生修正版 API-070 或真机验收。

#### `ethercat.compiler.preparation` — 可恢复的编译准备事务

串联 compile、detached signing、finalize、verify，并以日志恢复取消或崩溃后的事务。

- Owner：`EtherCATProjectCompiler`（[`src/plugins/ethercatprojectcompiler`](../src/plugins/ethercatprojectcompiler)）
- 运行边界：`engineering-only`
- 证据边界：`unit`、`artifact`
- 修改入口：
  - [`src/plugins/ethercatprojectcompiler/durableruntimepackagecompilerpreparationcoordinator.cpp`](../src/plugins/ethercatprojectcompiler/durableruntimepackagecompilerpreparationcoordinator.cpp)：持久准备协调器；`DurableRuntimePackageCompilerPreparationCoordinator::doStart`
  - [`src/plugins/ethercatprojectcompiler/runtimepackagecompilerpreparationjournal.cpp`](../src/plugins/ethercatprojectcompiler/runtimepackagecompilerpreparationjournal.cpp)：事务日志与 CAS；`RuntimePackageCompilerPreparationJournal`
- 公共合同：
  - [`src/plugins/ethercatcore/runtimepackagecompilerpreparationcoordinator.h`](../src/plugins/ethercatcore/runtimepackagecompilerpreparationcoordinator.h)：准备事务公共边界；`class ETHERCATCORE_EXPORT RuntimePackageCompilerPreparationCoordinator`
- 定向测试：
  - [`src/plugins/ethercatprojectcompiler/ethercatprojectcompilertests.cpp`](../src/plugins/ethercatprojectcompiler/ethercatprojectcompilertests.cpp)（`artifact`）：`testPreparationCoordinatorSuccessAndCancellation`、`testPreparationJournalCasRequiresExactPredecessor`
- 相关文档：[`docs/ethercat-plugin-development-guide.zh_CN.md`](../docs/ethercat-plugin-development-guide.zh_CN.md)
- 前置功能：`ethercat.compiler.backend`
- 边界提醒：恢复必须复用原 OperationId 和不可变意图；相同 ID 的不同意图必须拒绝。

### 4.6 签名运行时与控制

#### `ethercat.runtime.package-evidence` — 签名 ECPKG 与运行包证据

校验 canonical ECPKG、Ed25519 生产签名、信任锚、配置和完整来源证据。

- Owner：`EtherCATSemanticRuntime`（[`src/plugins/ethercatsemanticruntime`](../src/plugins/ethercatsemanticruntime)）
- 运行边界：`engineering-only`
- 证据边界：`artifact`
- 修改入口：
  - [`src/plugins/ethercatsemanticruntime/runtimepackageevidencerepository_p.cpp`](../src/plugins/ethercatsemanticruntime/runtimepackageevidencerepository_p.cpp)：受信包证据导入和重载；`RuntimePackageEvidenceRepository::load`、`RuntimePackageEvidenceRepository::import`
  - [`src/plugins/ethercatsemanticruntime/ecpkgcontainer.cpp`](../src/plugins/ethercatsemanticruntime/ecpkgcontainer.cpp)：canonical 容器解析；`parseCanonicalEcpkgContainer`
- 公共合同：
  - [`src/libs/ethercatdata/runtimepackageactivation.h`](../src/libs/ethercatdata/runtimepackageactivation.h)：激活前证据合同；`RuntimePackageActivationRequest`、`RuntimePackageActivationIdentity`
- 定向测试：
  - [`src/plugins/ethercatsemanticruntime/ethercatsemanticruntimetests.cpp`](../src/plugins/ethercatsemanticruntime/ethercatsemanticruntimetests.cpp)（`artifact`）：`testSignedEcpkgTransferredPackages`、`testRuntimePackageEvidenceRepositoryRejectsUnsafeInputs`
- 相关文档：[`docs/ethercat-plugin-development-guide.zh_CN.md`](../docs/ethercat-plugin-development-guide.zh_CN.md)
- 前置功能：`ethercat.compiler.backend`
- 边界提醒：内部 _p.h 是插件私有实现，不得成为其他插件的包含依赖。

#### `ethercat.runtime.binding-actions` — 实例语义绑定与动作定义

从同一签名包构建项目设备实例、信号、动作、参数和一致性组运行上下文。

- Owner：`EtherCATSemanticRuntime`（[`src/plugins/ethercatsemanticruntime`](../src/plugins/ethercatsemanticruntime)）
- 运行边界：`engineering-only`
- 证据边界：`unit`、`artifact`
- 修改入口：
  - [`src/plugins/ethercatsemanticruntime/semanticbindingartifact_p.cpp`](../src/plugins/ethercatsemanticruntime/semanticbindingartifact_p.cpp)：签名语义绑定验证；`verifySemanticBindingArtifact`
  - [`src/plugins/ethercatsemanticruntime/semanticactionruntimefactory_p.cpp`](../src/plugins/ethercatsemanticruntime/semanticactionruntimefactory_p.cpp)：动作运行态投影；`buildSemanticActionRuntimeStates`
- 公共合同：
  - [`src/libs/ethercatdata/semanticruntime.h`](../src/libs/ethercatdata/semanticruntime.h)：只公开已验证的通用语义运行态；`SemanticRuntimeContext`、`SemanticActionRuntimeState`
- 定向测试：
  - [`src/plugins/ethercatsemanticruntime/ethercatsemanticruntimetests.cpp`](../src/plugins/ethercatsemanticruntime/ethercatsemanticruntimetests.cpp)（`artifact`）：`testSemanticBindingV2TransferredPackage`、`testSemanticActionRuntimeFactoryFailsClosed`
- 相关文档：[`docs/ethercat-plugin-development-guide.zh_CN.md`](../docs/ethercat-plugin-development-guide.zh_CN.md)、[`docs/ethercat-device-adapters.md`](../docs/ethercat-device-adapters.md)
- 前置功能：`ethercat.runtime.package-evidence`、`ethercat.adapters.catalog-authorization`、`ethercat.product-api.runtime-resources`、`ethercat.product-api.semantic-attestation`
- 边界提醒：同型号多个实例必须使用各自签名绑定 ID，禁止按名字、位置或站号推断可写资源。

#### `ethercat.runtime.activation` — 受信运行包激活事务

把编译证明、控制器部署、运行时 Attestation 和工程 CAS 组合为可恢复事务。

- Owner：`EtherCATSemanticRuntime`（[`src/plugins/ethercatsemanticruntime`](../src/plugins/ethercatsemanticruntime)）
- 运行边界：`real-controller`
- 证据边界：`unit`、`loopback`、`not-hardware-qualified`
- 修改入口：
  - [`src/plugins/ethercatsemanticruntime/runtimepackageactivationservice_p.cpp`](../src/plugins/ethercatsemanticruntime/runtimepackageactivationservice_p.cpp)：受信部署和激活协调；`TrustedRuntimePackageActivationService::start`、`deployPackage`
- 公共合同：
  - [`src/plugins/ethercatcore/runtimepackageactivationservice.h`](../src/plugins/ethercatcore/runtimepackageactivationservice.h)：Workbench 可调用的激活服务；`class ETHERCATCORE_EXPORT RuntimePackageActivationService`
- 定向测试：
  - [`src/plugins/ethercatsemanticruntime/ethercatsemanticruntimetests.cpp`](../src/plugins/ethercatsemanticruntime/ethercatsemanticruntimetests.cpp)（`loopback`）：`testTrustedRuntimePackageActivation`、`testStrictProviderCardinality`
- 相关文档：[`docs/ethercat-plugin-development-guide.zh_CN.md`](../docs/ethercat-plugin-development-guide.zh_CN.md)
- 前置功能：`ethercat.runtime.package-evidence`、`ethercat.product-api.package-deployment`
- 边界提醒：状态汇总只用于展示；包、控制器和工程各自仍是其事实来源。

#### `ethercat.runtime.manual-control` — 通用语义手动控制执行

在审批、实时读取、完整组校验和超时策略后执行签名语义动作。

- Owner：`EtherCATSemanticRuntime`（[`src/plugins/ethercatsemanticruntime`](../src/plugins/ethercatsemanticruntime)）
- 运行边界：`real-controller`
- 证据边界：`unit`、`loopback`、`not-hardware-qualified`
- 修改入口：
  - [`src/plugins/ethercatsemanticruntime/semanticruntimeexecutor.cpp`](../src/plugins/ethercatsemanticruntime/semanticruntimeexecutor.cpp)：语义读取、审批和输出事务编排；`SemanticRuntimeExecutor::submit`、`SemanticRuntimeExecutor::approve`、`applyRuntimeOutputTransaction`
  - [`src/plugins/ethercatsemanticruntime/semanticactionplan_p.cpp`](../src/plugins/ethercatsemanticruntime/semanticactionplan_p.cpp)：私有签名动作计划构建；`buildSemanticActionPlan`
- 公共合同：
  - [`src/plugins/ethercatcore/semanticruntimeservice.h`](../src/plugins/ethercatcore/semanticruntimeservice.h)：UI 与自动化共享的语义服务；`class ETHERCATCORE_EXPORT SemanticRuntimeService`
  - [`src/libs/ethercatdata/semanticruntime.h`](../src/libs/ethercatdata/semanticruntime.h)：语义操作和审计合同；`SemanticOperationRequest`、`SemanticOperationRecord`
- 定向测试：
  - [`src/plugins/ethercatsemanticruntime/ethercatsemanticruntimetests.cpp`](../src/plugins/ethercatsemanticruntime/ethercatsemanticruntimetests.cpp)（`loopback`）：`testExecutorExecutesApi038Xb6Action`、`testExecutorRejectsUnauthorizedManualActionBeforeApply`
- 相关文档：[`docs/ethercat-plugin-development-guide.zh_CN.md`](../docs/ethercat-plugin-development-guide.zh_CN.md)
- 前置功能：`ethercat.runtime.binding-actions`、`ethercat.product-api.output-transactions`、`ethercat.core.manual-control-contract`
- 边界提醒：动作只使用 Adapter 和签名包定义的通用语义，不直接访问原始 PDO、SDO、寄存器或厂家协议。

### 4.7 Workbench 界面

#### `ethercat.workbench.details-routing` — 右侧属性页动态路由

根据当前工程树节点装配对应属性页，并为可控设备优先选择 Control 页。

- Owner：`EtherCATWorkbench`（[`src/plugins/ethercatworkbench`](../src/plugins/ethercatworkbench)）
- 运行边界：`engineering-only`
- 证据边界：`offscreen-ui`
- 修改入口：
  - [`src/plugins/ethercatworkbench/detailsview.cpp`](../src/plugins/ethercatworkbench/detailsview.cpp)：属性页生命周期、焦点和默认页策略；`DetailsView::setCurrentNode`、`DetailsView::rebuildPagesWithPreferredKey`、`shouldDefaultToControlPage`
- 公共合同：
  - [`src/plugins/ethercatcore/providers.h`](../src/plugins/ethercatcore/providers.h)：可扩展属性页合同；`class ETHERCATCORE_EXPORT PropertyPageProvider`、`PropertyPageContext`
- 定向测试：
  - [`src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp`](../src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp)（`offscreen-ui`）：`testBuiltInDevicePages`、`testDetailsKeyboardFocusContinuity`、`testControlPageSelectionPolicy`
- 相关文档：[`docs/ethercat-workbench.md`](../docs/ethercat-workbench.md)
- 前置功能：`ethercat.core.provider-registry`
- 边界提醒：页面通过公共服务取数和提交变更，不能持有第二套工程或控制器状态。

#### `ethercat.workbench.general-overview` — 工程主要信息与主站概览

显示和编辑工程、主站关键属性，并汇总运行模式、周期和从站数量。

- Owner：`EtherCATWorkbench`（[`src/plugins/ethercatworkbench`](../src/plugins/ethercatworkbench)）
- 运行边界：`engineering-only`
- 证据边界：`offscreen-ui`
- 修改入口：
  - [`src/plugins/ethercatworkbench/generalpage.cpp`](../src/plugins/ethercatworkbench/generalpage.cpp)：工程与主站 General 页；`GeneralPage::setContext`、`GeneralPage::commitMasterConfiguration`、`GeneralPage::refreshMasterSummary`
- 公共合同：
  - [`src/libs/ethercatdata/projectsnapshot.h`](../src/libs/ethercatdata/projectsnapshot.h)：工程主要信息来源；`ProjectSnapshot`
  - [`src/plugins/ethercatcore/providers.h`](../src/plugins/ethercatcore/providers.h)：编辑提交边界；`class ETHERCATCORE_EXPORT ProjectService`
- 定向测试：
  - [`src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp`](../src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp)（`offscreen-ui`）：`testEditableProjectGeneralWorkflow`、`testEditableMasterGeneralWorkflow`
- 相关文档：[`docs/ethercat-workbench.md`](../docs/ethercat-workbench.md)
- 前置功能：`ethercat.workbench.details-routing`、`ethercat.project.mutation`
- 边界提醒：工程关闭或未打开时不保留上一工程摘要。

#### `ethercat.workbench.project-navigation` — 工程打开、模式和设备树

工程打开后进入 EtherCAT Workbench，并统一显示工程、主站、从站、模块和状态树。

- Owner：`EtherCATWorkbench`（[`src/plugins/ethercatworkbench`](../src/plugins/ethercatworkbench)）
- 运行边界：`engineering-only`
- 证据边界：`offscreen-ui`
- 修改入口：
  - [`src/plugins/ethercatworkbench/workbenchnavigation.cpp`](../src/plugins/ethercatworkbench/workbenchnavigation.cpp)：设备树、筛选、状态和上下文菜单；`WorkbenchNavigationWidget::showContextMenu`、`WorkbenchNavigationWidget::updateProjectSummary`
  - [`src/plugins/ethercatworkbench/ethercatworkbenchplugin.cpp`](../src/plugins/ethercatworkbench/ethercatworkbenchplugin.cpp)：工程打开后的模式呈现；`scheduleProjectPresentation`、`activateProjectPresentation`
- 公共合同：
  - [`src/plugins/ethercatcore/selectionservice.h`](../src/plugins/ethercatcore/selectionservice.h)：当前树选择合同；`class ETHERCATCORE_EXPORT SelectionService`
- 定向测试：
  - [`src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp`](../src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp)（`offscreen-ui`）：`testProjectOpenShowsMasterDetails`、`testNavigationHeaderResizePersistence`、`testNavigationCommandsUseActionManager`
- 相关文档：[`docs/ethercat-workbench.md`](../docs/ethercat-workbench.md)
- 前置功能：`ethercat.project.mutation`
- 边界提醒：没有打开工程时树必须为空；SelectionService 只保存选择，不保存工程或控制器副本。

#### `ethercat.workbench.communication` — IP、连接、扫描和快捷控制

统一 Communication 页、顶部命令和 Qt Creator 快捷按钮的连接与控制流程。

- Owner：`EtherCATWorkbench`（[`src/plugins/ethercatworkbench`](../src/plugins/ethercatworkbench)）
- 运行边界：`real-controller`
- 证据边界：`offscreen-ui`、`loopback`
- 修改入口：
  - [`src/plugins/ethercatworkbench/communicationpage.cpp`](../src/plugins/ethercatworkbench/communicationpage.cpp)：嵌入式通信和控制页；`CommunicationPage::updateControllerControl`、`CommunicationPage::updateTopology`
  - [`src/plugins/ethercatworkbench/workbenchcontroller.cpp`](../src/plugins/ethercatworkbench/workbenchcontroller.cpp)：UI 无关控制编排和精确真实拓扑投影；`WorkbenchController::connectController`、`WorkbenchController::beginControllerStartup`、`WorkbenchController::beginControllerStop`、`WorkbenchController::selectedRealTopology`、`WorkbenchController::projectedControllerConnectionSnapshot`
- 公共合同：
  - [`src/plugins/ethercatcore/providers.h`](../src/plugins/ethercatcore/providers.h)：厂商无关控制器接口；`class ETHERCATCORE_EXPORT ControllerConnectionProvider`
- 定向测试：
  - [`src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp`](../src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp)（`offscreen-ui`）：`testControllerCommunicationControlWorkflow`、`testControllerCommunicationDoesNotAutoDiscover`、`testControllerQuickStopToShutdown`、`testWorkbenchUsesExactRealTopologySelection`
- 相关文档：[`docs/ethercat-workbench.md`](../docs/ethercat-workbench.md)、[`docs/ethercat-online-controller.md`](../docs/ethercat-online-controller.md)
- 前置功能：`ethercat.product-api.control-lifecycle`、`ethercat.product-api.topology-evidence`
- 边界提醒：页面只调用 ControllerConnectionProvider 和 Core TopologyService，不依赖 Product API Codec。
- 边界提醒：连接报错后由 Provider 快照决定是否保留会话；输出必须显示可操作根因。
- 边界提醒：Workbench 只投影工程显式选择的 Fresh RealController 拓扑；Mock、陈旧、不完整、scope/profile 不匹配和 Provider 移除均清空且不替补。

#### `ethercat.workbench.configuration-pages` — PDO、Startup SDO 和 DC 配置页

在右侧属性区编辑厂家无关过程数据、启动参数和分布式时钟配置。

- Owner：`EtherCATWorkbench`（[`src/plugins/ethercatworkbench`](../src/plugins/ethercatworkbench)）
- 运行边界：`engineering-only`
- 证据边界：`offscreen-ui`
- 修改入口：
  - [`src/plugins/ethercatworkbench/processdatapage.cpp`](../src/plugins/ethercatworkbench/processdatapage.cpp)：PDO 与过程映像编辑；`ProcessDataPage::submitConfiguration`
  - [`src/plugins/ethercatworkbench/startuppage.cpp`](../src/plugins/ethercatworkbench/startuppage.cpp)：有序 Startup SDO 编辑；`StartupPage::submitConfiguration`
  - [`src/plugins/ethercatworkbench/dcpage.cpp`](../src/plugins/ethercatworkbench/dcpage.cpp)：ESI DC 模式和周期编辑；`DcPage::submitConfiguration`、`DcPage::selectEsiMode`
- 公共合同：
  - [`src/libs/ethercatdata/offlineconfiguration.h`](../src/libs/ethercatdata/offlineconfiguration.h)：离线配置值合同；`ProcessDataConfiguration`、`StartupConfiguration`、`DcConfiguration`
- 定向测试：
  - [`src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp`](../src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp)（`offscreen-ui`）：`testEditableProcessDataWorkflow`、`testEditableStartupWorkflow`、`testEditableDcWorkflow`
- 相关文档：[`docs/ethercat-offline-configuration.md`](../docs/ethercat-offline-configuration.md)、[`docs/ethercat-workbench.md`](../docs/ethercat-workbench.md)
- 前置功能：`ethercat.devices.esi-repository`、`ethercat.project.mutation`
- 边界提醒：这些页面只修改本地工程；生成运行包和部署是独立步骤。

#### `ethercat.workbench.esi-library` — ESI 设备库界面

在 Workbench 中导入原始 XML、拖放文件、重建索引并查看设备描述。

- Owner：`EtherCATWorkbench`（[`src/plugins/ethercatworkbench`](../src/plugins/ethercatworkbench)）
- 运行边界：`engineering-only`
- 证据边界：`offscreen-ui`
- 修改入口：
  - [`src/plugins/ethercatworkbench/esirepositorypage.cpp`](../src/plugins/ethercatworkbench/esirepositorypage.cpp)：ESI 库用户交互；`EsiRepositoryPage::importFiles`、`EsiRepositoryPage::reloadDescriptions`、`EsiRepositoryPage::dropEvent`
- 公共合同：
  - [`src/plugins/ethercatcore/providers.h`](../src/plugins/ethercatcore/providers.h)：设备库和导入任务合同；`class ETHERCATCORE_EXPORT DeviceRepositoryProvider`、`class ETHERCATCORE_EXPORT DeviceImportJob`
- 定向测试：
  - [`src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp`](../src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp)（`offscreen-ui`）：`testEsiDeviceDragDropWorkflow`、`testEsiRepositoryGeneralWorkflow`、`testEsiRepositoryEmptyGuidance`
- 相关文档：[`docs/ethercat-devices-repository.md`](../docs/ethercat-devices-repository.md)、[`docs/ethercat-workbench.md`](../docs/ethercat-workbench.md)
- 前置功能：`ethercat.devices.esi-repository`、`ethercat.workbench.details-routing`
- 边界提醒：XML 原文件和 SHA 是设备事实来源；界面不简化或重写厂家 XML。

#### `ethercat.workbench.coe-view` — CoE 参数浏览与 Startup 复制

汇总 ESI Startup 参数、PDO 条目和本地配置，并把选定对象复制到 Startup SDO。

- Owner：`EtherCATWorkbench`（[`src/plugins/ethercatworkbench`](../src/plugins/ethercatworkbench)）
- 运行边界：`mock-only`
- 证据边界：`offscreen-ui`
- 修改入口：
  - [`src/plugins/ethercatworkbench/coeonlinepage.cpp`](../src/plugins/ethercatworkbench/coeonlinepage.cpp)：对象字典和本地 Mock 值界面；`CoeOnlinePage::rebuildObjects`、`CoeOnlinePage::addSelectedToStartup`、`CoeOnlinePage::updateList`
- 公共合同：
  - [`src/libs/ethercatdata/devicedescription.h`](../src/libs/ethercatdata/devicedescription.h)：ESI Startup 与 PDO 参数来源；`StartupParameterDescription`、`PdoEntryDescription`
  - [`src/libs/ethercatdata/offlineconfiguration.h`](../src/libs/ethercatdata/offlineconfiguration.h)：Startup SDO 目标配置；`StartupConfiguration`
- 定向测试：
  - [`src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp`](../src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp)（`offscreen-ui`）：`testCoeOnlineMockWorkflow`、`testCoeRepositoryReadOnlyWorkflow`、`testCoeAddConfirmationRepositoryRefresh`
- 相关文档：[`docs/ethercat-workbench.md`](../docs/ethercat-workbench.md)
- 前置功能：`ethercat.devices.esi-repository`、`ethercat.project.mutation`
- 边界提醒：当前 CoE 页不是在线 SDO 服务；显示值为本地 Mock，写入只发生在工程 Startup 配置。

#### `ethercat.workbench.deployment` — 编译准备与运行包部署页

加载预制 ECPKG、启动受信编译准备、部署、激活或取消当前操作。

- Owner：`EtherCATWorkbench`（[`src/plugins/ethercatworkbench`](../src/plugins/ethercatworkbench)）
- 运行边界：`real-controller`
- 证据边界：`offscreen-ui`、`loopback`、`not-hardware-qualified`
- 修改入口：
  - [`src/plugins/ethercatworkbench/deploymentpage.cpp`](../src/plugins/ethercatworkbench/deploymentpage.cpp)：部署和激活用户界面；`DeploymentPage::loadArtifact`、`DeploymentPage::deploy`、`DeploymentPage::activateTrustedPackage`
  - [`src/plugins/ethercatworkbench/runtimepackagecompilerpreparationbridge.cpp`](../src/plugins/ethercatworkbench/runtimepackagecompilerpreparationbridge.cpp)：Workbench 到编译准备服务的桥接；`RuntimePackageCompilerPreparationBridge`
- 公共合同：
  - [`src/plugins/ethercatcore/runtimepackageactivationservice.h`](../src/plugins/ethercatcore/runtimepackageactivationservice.h)：受信激活服务；`class ETHERCATCORE_EXPORT RuntimePackageActivationService`
  - [`src/plugins/ethercatcore/runtimepackagecompilerpreparationcoordinator.h`](../src/plugins/ethercatcore/runtimepackagecompilerpreparationcoordinator.h)：编译准备服务；`class ETHERCATCORE_EXPORT RuntimePackageCompilerPreparationCoordinator`
- 定向测试：
  - [`src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp`](../src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp)（`offscreen-ui`）：`testControllerPackageDeploymentWorkflow`、`testRuntimePackageCompilerPreparationBridge`
- 相关文档：[`docs/ethercat-workbench.md`](../docs/ethercat-workbench.md)、[`docs/ethercat-plugin-development-guide.zh_CN.md`](../docs/ethercat-plugin-development-guide.zh_CN.md)
- 前置功能：`ethercat.compiler.preparation`、`ethercat.runtime.activation`
- 边界提醒：预制包直接部署和工程编译准备是不同入口，但最终都必须经过受信包证据和控制器状态门禁。

#### `ethercat.workbench.semantic-control` — 节点右侧手动控制页

选中从站或模块时显示已验证信号、动作、参数、TTL、确认和实时值。

- Owner：`EtherCATWorkbench`（[`src/plugins/ethercatworkbench`](../src/plugins/ethercatworkbench)）
- 运行边界：`real-controller`
- 证据边界：`offscreen-ui`、`loopback`、`not-hardware-qualified`
- 修改入口：
  - [`src/plugins/ethercatworkbench/semanticcontrolpage.cpp`](../src/plugins/ethercatworkbench/semanticcontrolpage.cpp)：手动控制展示和审批交互；`SemanticControlPage::requestSelectedAction`、`SemanticControlPage::submitConfirmedAction`、`SemanticControlPage::requestLiveRefresh`
  - [`src/plugins/ethercatworkbench/detailsview.cpp`](../src/plugins/ethercatworkbench/detailsview.cpp)：节点选中后的 Control 默认路由；`shouldDefaultToControlPage`、`rebuildPagesWithPreferredKey`
- 公共合同：
  - [`src/plugins/ethercatcore/semanticruntimeservice.h`](../src/plugins/ethercatcore/semanticruntimeservice.h)：共享语义运行时服务；`class ETHERCATCORE_EXPORT SemanticRuntimeService`
- 定向测试：
  - [`src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp`](../src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp)（`offscreen-ui`）：`testControlPageSelectionPolicy`、`testSemanticControlPageSignedActions`、`testSemanticControlPageSchedulesBoundedLiveRefresh`
- 相关文档：[`docs/ethercat-plugin-development-guide.zh_CN.md`](../docs/ethercat-plugin-development-guide.zh_CN.md)、[`docs/ethercat-workbench.md`](../docs/ethercat-workbench.md)
- 前置功能：`ethercat.runtime.manual-control`
- 边界提醒：不是任意 PDO 写入页面；只有同一受信包中已签名、已实例绑定且已合格的动作可以启用。

#### `ethercat.workbench.output-status` — 统一应用程序输出和状态投影

把重要连接、控制和故障信息精简写入应用程序输出，并同步树和快捷按钮状态。

- Owner：`EtherCATWorkbench`（[`src/plugins/ethercatworkbench`](../src/plugins/ethercatworkbench)）
- 运行边界：`mixed-real-loopback`
- 证据边界：`offscreen-ui`
- 修改入口：
  - [`src/plugins/ethercatworkbench/workbenchcontroller.cpp`](../src/plugins/ethercatworkbench/workbenchcontroller.cpp)：控制器关键消息格式化；`WorkbenchController::writeControllerOutput`、`controllerOutputRequested`
  - [`src/plugins/ethercatworkbench/workbenchstatuswidget.cpp`](../src/plugins/ethercatworkbench/workbenchstatuswidget.cpp)：非连接状态的状态栏投影；`WorkbenchStatusWidget::updateStatus`
- 公共合同：
  - [`src/plugins/ethercatcore/stateservice.h`](../src/plugins/ethercatcore/stateservice.h)：插件状态汇总服务；`class ETHERCATCORE_EXPORT StateService`
- 定向测试：
  - [`src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp`](../src/plugins/ethercatworkbench/ethercatworkbenchtests.cpp)（`offscreen-ui`）：`testProviderStartupDiagnosticsPresentation`、`testStatusBarIgnoresControllerConnection`、`testProviderStateTreeAndNavigation`
- 相关文档：[`docs/ethercat-workbench.md`](../docs/ethercat-workbench.md)
- 前置功能：`ethercat.workbench.communication`、`ethercat.core.provider-registry`
- 边界提醒：状态栏不重复显示控制器连接；连接状态由树、快捷按钮和应用程序输出表达。

### 4.8 Mock 工具

#### `ethercat.scan.mock-workflow` — Mock 扫描与拓扑比较

为离线开发提供可取消的模拟扫描、差异比较和接受到工程流程。

- Owner：`EtherCATScan`（[`src/plugins/ethercatscan`](../src/plugins/ethercatscan)）
- 运行边界：`mock-only`
- 证据边界：`unit`、`offscreen-ui`
- 修改入口：
  - [`src/plugins/ethercatscan/mockscanprovider.cpp`](../src/plugins/ethercatscan/mockscanprovider.cpp)：确定性模拟扫描 Provider；`MockScanProvider`
  - [`src/plugins/ethercatscan/scanworkflow.cpp`](../src/plugins/ethercatscan/scanworkflow.cpp)：Mock 扫描和接受编排；`ScanWorkflow::start`、`ScanWorkflow::acceptScan`
- 公共合同：
  - [`src/plugins/ethercatcore/providers.h`](../src/plugins/ethercatcore/providers.h)：扫描 Provider 合同；`class ETHERCATCORE_EXPORT ScanProvider`
  - [`src/libs/ethercatdata/scansnapshot.h`](../src/libs/ethercatdata/scansnapshot.h)：扫描快照值类型；`ScanSnapshot`
- 定向测试：
  - [`src/plugins/ethercatscan/ethercatscantests.cpp`](../src/plugins/ethercatscan/ethercatscantests.cpp)（`offscreen-ui`）：`testMockProviderStateCancellationAndFailure`、`testWorkflowAcceptUndoAndRedo`
- 相关文档：[`docs/ethercat-scan.md`](../docs/ethercat-scan.md)
- 前置功能：`ethercat.devices.esi-repository`、`ethercat.project.mutation`
- 边界提醒：此插件不打开 socket、不访问物理网卡，不能作为真实扫描证据。

#### `ethercat.diagnostics.mock-stream` — Mock 诊断流

生成有界模拟周期、WKC、DC 和告警流，验证诊断界面与生命周期。

- Owner：`EtherCATDiagnostics`（[`src/plugins/ethercatdiagnostics`](../src/plugins/ethercatdiagnostics)）
- 运行边界：`mock-only`
- 证据边界：`unit`、`offscreen-ui`
- 修改入口：
  - [`src/plugins/ethercatdiagnostics/mockdiagnosticsprovider.cpp`](../src/plugins/ethercatdiagnostics/mockdiagnosticsprovider.cpp)：模拟诊断 Provider；`MockDiagnosticsProvider`
  - [`src/plugins/ethercatdiagnostics/diagnosticsworkflow.cpp`](../src/plugins/ethercatdiagnostics/diagnosticsworkflow.cpp)：诊断操作和页面状态；`DiagnosticsWorkflow::startMonitoring`、`DiagnosticsWorkflow::requestMode`
- 公共合同：
  - [`src/plugins/ethercatcore/providers.h`](../src/plugins/ethercatcore/providers.h)：诊断 Provider 合同；`class ETHERCATCORE_EXPORT DiagnosticsProvider`
  - [`src/libs/ethercatdata/diagnosticssnapshot.h`](../src/libs/ethercatdata/diagnosticssnapshot.h)：诊断快照值类型；`DiagnosticsSnapshot`
- 定向测试：
  - [`src/plugins/ethercatdiagnostics/ethercatdiagnosticstests.cpp`](../src/plugins/ethercatdiagnostics/ethercatdiagnosticstests.cpp)（`offscreen-ui`）：`testBoundedAggregationAndAlarmLifecycle`、`testLivePageUpdatesAndShutdown`
- 相关文档：[`docs/ethercat-diagnostics.md`](../docs/ethercat-diagnostics.md)
- 前置功能：`ethercat.core.provider-registry`
- 边界提醒：真实 ControllerState、Performance 和 Alarm 数据属于 Product API/Workbench，不属于此 Mock 插件。

### 4.9 自动化网关

#### `ethercat.gateway.loopback-transport` — 回环 MCP 与 REST 监听器

以事务方式启动默认关闭、仅绑定 127.0.0.1 的 MCP Streamable HTTP 和 REST 服务。

- Owner：`EtherCATAutomationGateway`（[`src/plugins/ethercatautomationgateway`](../src/plugins/ethercatautomationgateway)）
- 运行边界：`loopback-only`
- 证据边界：`unit`、`loopback`
- 修改入口：
  - [`src/plugins/ethercatautomationgateway/gatewayserver.cpp`](../src/plugins/ethercatautomationgateway/gatewayserver.cpp)：回环监听器和路由；`GatewayServer::start`
  - [`src/plugins/ethercatautomationgateway/gatewayruntime.cpp`](../src/plugins/ethercatautomationgateway/gatewayruntime.cpp)：事务启动、回滚和重启；`GatewayRuntimeController`、`QHostAddress::LocalHost`
- 公共合同：
  - [`src/plugins/ethercatautomationgateway/automationdispatcher.h`](../src/plugins/ethercatautomationgateway/automationdispatcher.h)：MCP/REST 共享分发合同；`class AutomationDispatcher`、`dispatch`
- 定向测试：
  - [`src/plugins/ethercatautomationgateway/ethercatautomationgatewaytests.cpp`](../src/plugins/ethercatautomationgateway/ethercatautomationgatewaytests.cpp)（`loopback`）：`testDefaultOffAndClosedToolCatalog`、`testListenerLifecycleAndAtomicRollback`、`testMcpRestIntegrationAndOriginBoundary`
- 相关文档：[`docs/ethercat-automation-gateway.md`](../docs/ethercat-automation-gateway.md)
- 前置功能：`ethercat.core.provider-registry`
- 边界提醒：网关不是控制器 Provider，不得绑定局域网地址或进入 125 us 实时周期。

#### `ethercat.gateway.controller-views-intents` — 自动化只读视图与语义意图

让 MCP/REST 读取 IDE 共享上下文，并提交需要审批的语义操作意图。

- Owner：`EtherCATAutomationGateway`（[`src/plugins/ethercatautomationgateway`](../src/plugins/ethercatautomationgateway)）
- 运行边界：`loopback-only`
- 证据边界：`unit`、`loopback`
- 修改入口：
  - [`src/plugins/ethercatautomationgateway/automationdispatcher.cpp`](../src/plugins/ethercatautomationgateway/automationdispatcher.cpp)：共享视图、脱敏和变更拒绝；`AutomationDispatcher::mockContexts`、`AutomationDispatcher::rejectMutation`、`runtime.operation.request`
- 公共合同：
  - [`src/plugins/ethercatcore/automationservice.h`](../src/plugins/ethercatcore/automationservice.h)：Workbench 自动化快照；`class ETHERCATCORE_EXPORT AutomationService`
  - [`src/plugins/ethercatcore/semanticruntimeservice.h`](../src/plugins/ethercatcore/semanticruntimeservice.h)：语义读取和意图合同；`class ETHERCATCORE_EXPORT SemanticRuntimeService`
- 定向测试：
  - [`src/plugins/ethercatautomationgateway/ethercatautomationgatewaytests.cpp`](../src/plugins/ethercatautomationgateway/ethercatautomationgatewaytests.cpp)（`loopback`）：`testMutationsAreRejectedWithoutProviderCalls`、`testSemanticRuntimeOperationIntentAndJournal`、`testVendorDetailsAreNotProjected`
- 相关文档：[`docs/ethercat-automation-gateway.md`](../docs/ethercat-automation-gateway.md)
- 前置功能：`ethercat.gateway.loopback-transport`、`ethercat.runtime.manual-control`
- 边界提醒：当前 controller.* 视图只接受 Mock context；网关不能审批，也不能直接调用 Product API 或厂家协议。

#### `ethercat.gateway.contract-tools` — Adapter、工件与协议查询工具

提供关闭目录内的 Adapter 列表、外层工件校验和网关协议协商工具。

- Owner：`EtherCATAutomationGateway`（[`src/plugins/ethercatautomationgateway`](../src/plugins/ethercatautomationgateway)）
- 运行边界：`loopback-only`
- 证据边界：`unit`、`loopback`
- 修改入口：
  - [`src/plugins/ethercatautomationgateway/automationdispatcher.cpp`](../src/plugins/ethercatautomationgateway/automationdispatcher.cpp)：协议工具分发与闭集校验；`adapter.list`、`artifact.validate`、`gateway.get-protocol`
- 公共合同：
  - [`src/plugins/ethercatautomationgateway/automationdispatcher.h`](../src/plugins/ethercatautomationgateway/automationdispatcher.h)：共享工具目录和分发接口；`static QStringList toolNames`、`QJsonObject dispatch`
- 定向测试：
  - [`src/plugins/ethercatautomationgateway/ethercatautomationgatewaytests.cpp`](../src/plugins/ethercatautomationgateway/ethercatautomationgatewaytests.cpp)（`loopback`）：`testArtifactValidationAndBuildSystemSync`、`testDefaultOffAndClosedToolCatalog`
- 相关文档：[`docs/ethercat-automation-gateway.md`](../docs/ethercat-automation-gateway.md)
- 前置功能：`ethercat.gateway.loopback-transport`
- 边界提醒：artifact.validate 只检查 controller-tools-v1 外层结构，不替代 ECPKG 签名、Schema 或编译器验证。
- 边界提醒：adapter.list 当前可能返回 Registry unavailable，不能据此声称已发布 Adapter 自动化目录。

## 5. 修改前的最短决策

- 改工程字段或保存格式：从 `ethercat.project.model-format` 开始。
- 加新厂家/型号：从 `ethercat.devices.esi-repository` 和
  `ethercat.adapters.catalog-authorization` 开始，不改 Workbench/Product API。
- 改连接、扫描、状态或协议：从 `ethercat.product-api.*` 开始。
- 改页面或按钮：从 `ethercat.workbench.*` 开始，
  只调用 Core 公共服务。
- 改手动控制：先看 `ethercat.runtime.binding-actions`，再看
  `ethercat.runtime.manual-control` 和 `ethercat.product-api.output-transactions`。
- 改编译/签名/部署：依次看 `ethercat.compiler.*`、
  `ethercat.runtime.package-evidence`、`ethercat.runtime.activation`。
- Mock Scan/Diagnostics 不能作为真实控制器入口；
  以对应条目的边界字段为准。

## 6. 领域知识卡

默认读取有界摘要，避免把全部领域上下文一次性载入：
`python3 scripts/ethercat_feature_locator.py context <FEATURE_ID>`；
需要完整知识卡和相关问题时再追加 `--full`。

| Area | 知识卡 | 功能数 | 用途 |
|---|---|---:|---|
| `architecture` | 架构与公共合同知识卡 | 4 | 在不遍历实现插件的前提下确认跨插件值对象、Provider 和公共服务的正确边界。 |
| `project` | 工程模型知识卡 | 2 | 维护 .ecatproject 的唯一事实来源、格式迁移和可撤销变更。 |
| `devices` | 设备、ESI 与 Adapter 知识卡 | 2 | 用原始厂家证据和数据驱动适配完成精确设备识别，避免在上层写死型号逻辑。 |
| `online` | 真实控制器在线功能知识卡 | 8 | 维护 Product API 三通道、会话、控制权、拓扑证据、部署和原子输出的一致在线快照。 |
| `compiler` | 编译与准备知识卡 | 3 | 把工程快照和新鲜硬件证据确定性转换为可签名、可恢复、可验证的运行包。 |
| `runtime` | 签名运行时与控制知识卡 | 4 | 在签名包、项目实例、控制器证明和审批一致时执行厂家无关的语义动作。 |
| `ui` | Workbench 界面知识卡 | 10 | 让工程树、右侧属性页、顶部/左下快捷操作和输出面板投影同一套服务状态。 |
| `mock` | Mock 工具知识卡 | 2 | 提供确定性的离线扫描和诊断测试，同时保持与真实控制器证据的严格隔离。 |
| `automation` | 自动化网关知识卡 | 3 | 让外部 AI 通过本机受限入口读取 IDE 共享事实并提交需审批的语义意图。 |

## 7. 已知问题台账

按状态查询：`python3 scripts/ethercat_feature_locator.py issues --status open`；
读取单项：`python3 scripts/ethercat_feature_locator.py issue <ISSUE_ID>`。

| Issue ID | 状态 | 严重度 | 影响功能 | 标题 |
|---|---|---|---|---|
| `ethercat.issue.compiler-provisioning` | `open` | `p0` | `ethercat.compiler.project-projection`、`ethercat.compiler.backend`、`ethercat.compiler.preparation`、`ethercat.workbench.deployment` | 受信编译器交付与发现尚未产品化 |
| `ethercat.issue.detached-sign-ui-flow` | `open` | `p0` | `ethercat.compiler.preparation`、`ethercat.runtime.package-evidence`、`ethercat.runtime.activation`、`ethercat.workbench.deployment` | Workbench detached-sign 流程未形成完整用户闭环 |
| `ethercat.issue.current-project-hardware-acceptance` | `blocked` | `p0` | `ethercat.compiler.project-projection`、`ethercat.product-api.topology-evidence`、`ethercat.product-api.package-deployment`、`ethercat.product-api.control-lifecycle`、`ethercat.product-api.output-transactions`、`ethercat.runtime.activation`、`ethercat.runtime.manual-control`、`ethercat.workbench.deployment`、`ethercat.workbench.semantic-control` | 当前工程到真实硬件的完整验收尚未闭环 |
| `ethercat.issue.startup-sdo-compiler` | `open` | `p0` | `ethercat.project.model-format`、`ethercat.project.mutation`、`ethercat.workbench.configuration-pages`、`ethercat.compiler.project-projection`、`ethercat.compiler.backend` | 非空 Startup SDO 尚未进入编译闭环 |
| `ethercat.issue.restore-project-binding-guard` | `open` | `p0` | `ethercat.product-api.control-lifecycle`、`ethercat.product-api.package-deployment`、`ethercat.product-api.semantic-attestation`、`ethercat.runtime.package-evidence`、`ethercat.runtime.binding-actions`、`ethercat.runtime.activation`、`ethercat.workbench.communication` | Restore 运行前缺少当前工程绑定门禁 |
| `ethercat.issue.topology-service` | `open` | `p1` | `ethercat.core.topology-service`、`ethercat.product-api.topology-evidence`、`ethercat.scan.mock-workflow`、`ethercat.workbench.project-navigation`、`ethercat.workbench.communication`、`ethercat.gateway.controller-views-intents` | 统一拓扑服务尚未接入 Mock 与 Gateway |
| `ethercat.issue.engineering-coordinator` | `planned` | `p1` | `ethercat.workbench.communication`、`ethercat.workbench.deployment`、`ethercat.workbench.output-status`、`ethercat.product-api.control-lifecycle`、`ethercat.product-api.package-deployment`、`ethercat.runtime.activation`、`ethercat.gateway.controller-views-intents` | 工程操作协调逻辑仍集中在 WorkbenchController |
| `ethercat.issue.operation-journal` | `planned` | `p1` | `ethercat.compiler.preparation`、`ethercat.runtime.activation`、`ethercat.runtime.manual-control`、`ethercat.gateway.controller-views-intents` | 操作记录尚无统一查询与审计索引 |
| `ethercat.issue.scan-diagnostics-dependency` | `planned` | `p1` | `ethercat.scan.mock-workflow`、`ethercat.diagnostics.mock-stream`、`ethercat.core.provider-registry` | Scan 与 Diagnostics 对 Workbench 存在反向依赖 |
| `ethercat.issue.gateway-real-read-views` | `planned` | `p1` | `ethercat.gateway.controller-views-intents`、`ethercat.product-api.telemetry`、`ethercat.product-api.topology-evidence`、`ethercat.runtime.manual-control`、`ethercat.workbench.output-status` | Gateway controller 视图尚未接入真实公共事实 |
| `ethercat.issue.adapter-catalog-service` | `planned` | `p2` | `ethercat.adapters.catalog-authorization`、`ethercat.workbench.esi-library`、`ethercat.gateway.contract-tools` | Adapter 目录尚无统一公共查询服务 |
| `ethercat.issue.crypto-identity-library` | `planned` | `p2` | `ethercat.adapters.catalog-authorization`、`ethercat.compiler.backend`、`ethercat.runtime.package-evidence` | Canonical JSON、哈希与签名实现仍有重复 |
| `ethercat.issue.task-editor` | `planned` | `p2` | `ethercat.project.model-format`、`ethercat.project.mutation`、`ethercat.runtime.binding-actions`、`ethercat.compiler.project-projection`、`ethercat.compiler.backend`、`ethercat.workbench.semantic-control` | 通用自动流程编辑器尚未实现 |
| `ethercat.issue.adapter-coverage` | `planned` | `p2` | `ethercat.devices.esi-repository`、`ethercat.adapters.catalog-authorization`、`ethercat.runtime.binding-actions`、`ethercat.workbench.configuration-pages`、`ethercat.workbench.semantic-control` | DI、模拟量和更多驱动器缺少精确 Adapter |
| `ethercat.issue.sv630n-unit-qualification` | `blocked` | `p2` | `ethercat.adapters.catalog-authorization`、`ethercat.runtime.binding-actions`、`ethercat.runtime.manual-control`、`ethercat.workbench.semantic-control` | SV630N 速度工程单位换算尚未签名闭环 |
| `ethercat.issue.coe-online-sdo` | `planned` | `p2` | `ethercat.workbench.coe-view`、`ethercat.product-api.runtime-resources` | CoE 页面尚不是在线 SDO 浏览器 |
