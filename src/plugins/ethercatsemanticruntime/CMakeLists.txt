add_qtc_plugin(EtherCATSemanticRuntime
  DEPENDS EtherCATData Qt::Core Utils
  PLUGIN_DEPENDS
    Core
    EtherCATCore
    EtherCATProject
  SOURCES
    ethercatsemanticruntimeplugin.cpp
    semanticruntimeexecutor.cpp
    semanticruntimeexecutor.h
)

extend_qtc_plugin(EtherCATSemanticRuntime
  CONDITION WITH_TESTS
  SOURCES
    ethercatsemanticruntimetests.cpp
    ethercatsemanticruntimetests.h
)
