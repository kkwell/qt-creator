add_qtc_plugin(EtherCATScan
  DEPENDS EtherCATData Qt::Widgets Utils
  PLUGIN_DEPENDS
    Core
    EtherCATCore
    EtherCATDevices
    EtherCATProject
    EtherCATWorkbench
  SOURCES
    ethercatscanconstants.h
    ethercatscanplugin.cpp
    ethercatscantr.h
    mockscanprovider.cpp
    mockscanprovider.h
    scanpropertypages.cpp
    scanpropertypages.h
    scanworkflow.cpp
    scanworkflow.h
    topologycomparison.cpp
    topologycomparison.h
)

extend_qtc_plugin(EtherCATScan
  CONDITION WITH_TESTS
  SOURCES
    ethercatscantests.cpp
    ethercatscantests.h
)
