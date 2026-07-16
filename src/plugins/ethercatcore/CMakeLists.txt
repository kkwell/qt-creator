add_qtc_plugin(EtherCATCore
  DEPENDS EtherCATData Qt::Widgets Utils
  PLUGIN_DEPENDS Core
  SOURCES
    ethercatcore_global.h
    ethercatcoreconstants.h
    ethercatcoreplugin.cpp
    ethercatcoresettings.cpp
    ethercatcoresettings.h
    ethercatcoretr.h
    providerregistry.cpp
    providerregistry.h
    providers.cpp
    providers.h
    selectionservice.cpp
    selectionservice.h
    stateservice.cpp
    stateservice.h
)

extend_qtc_plugin(EtherCATCore
  CONDITION WITH_TESTS
  SOURCES
    ethercatcoretests.cpp
    ethercatcoretests.h
)
