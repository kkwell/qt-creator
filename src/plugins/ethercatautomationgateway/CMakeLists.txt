find_package(Qt6 6.5 COMPONENTS HttpServer REQUIRED)

add_qtc_plugin(EtherCATAutomationGateway
  DEPENDS
    EtherCATData
    ExtensionSystem
    McpServerLib
    Qt::Core
    Qt::HttpServer
    Qt::Network
    Qt::Widgets
    Utils
  PLUGIN_DEPENDS
    Core
    EtherCATCore
    EtherCATWorkbench
    ProjectExplorer
  SOURCES
    automationdispatcher.cpp
    automationdispatcher.h
    controller-tools-v1-contracts.qrc
    ethercatautomationgatewayconstants.h
    ethercatautomationgatewayplugin.cpp
    ethercatautomationgatewaytr.h
    gatewayruntime.cpp
    gatewayruntime.h
    gatewayserver.cpp
    gatewayserver.h
    gatewaysettingspage.cpp
    gatewaysettingspage.h
)

extend_qtc_plugin(EtherCATAutomationGateway
  CONDITION WITH_TESTS
  SOURCES
    ethercatautomationgatewaytests.cpp
    ethercatautomationgatewaytests.h
    tests/controller_tools_v1_probe.py
)
