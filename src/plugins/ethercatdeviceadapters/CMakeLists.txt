add_qtc_plugin(EtherCATDeviceAdapters
  DEPENDS EtherCATData Utils qtcMonocypher
  PLUGIN_DEPENDS Core EtherCATCore
  SOURCES
    adapterpackagerepository.cpp
    adapterpackagerepository.h
    deviceadapterauthorization_p.cpp
    deviceadapterauthorization_p.h
    ethercatdeviceadaptersplugin.cpp
)

extend_qtc_plugin(EtherCATDeviceAdapters
  CONDITION WITH_TESTS
  SOURCES
    ethercatdeviceadapterstests.cpp
    ethercatdeviceadapterstests.h
    testdata/device-adapter-v3-contract.fixture.json
    testdata/device-adapter-v3.schema.json
)
