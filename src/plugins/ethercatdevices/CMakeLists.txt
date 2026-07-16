add_qtc_plugin(EtherCATDevices
  DEPENDS EtherCATData Qt::Concurrent Utils
  PLUGIN_DEPENDS Core EtherCATCore
  SOURCES
    devicerepository.cpp
    devicerepository.h
    esiparser.cpp
    esiparser.h
    ethercatdevicesconstants.h
    ethercatdevicesplugin.cpp
    ethercatdevicestr.h
)

extend_qtc_plugin(EtherCATDevices
  CONDITION WITH_TESTS
  SOURCES
    ethercatdevicestests.cpp
    ethercatdevicestests.h
)
