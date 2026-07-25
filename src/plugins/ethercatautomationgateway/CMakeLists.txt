find_package(Qt6 6.5 COMPONENTS HttpServer REQUIRED)

add_qtc_plugin(EtherCATAutomationGateway
  DEPENDS
    EtherCATData
    ExtensionSystem
    McpServerLib
    Qt::Core
    Qt::HttpServer
    Qt::Network
    Utils
  PLUGIN_DEPENDS
    Core
    EtherCATCore
    EtherCATWorkbench
  SOURCES
    automationdispatcher.cpp
    automationdispatcher.h
    controller-tools-v1-contracts.qrc
    ethercatautomationgatewayconstants.h
    ethercatautomationgatewayplugin.cpp
    gatewayserver.cpp
    gatewayserver.h
)

extend_qtc_plugin(EtherCATAutomationGateway
  CONDITION WITH_TESTS
  SOURCES
    ethercatautomationgatewaytests.cpp
    ethercatautomationgatewaytests.h
)
