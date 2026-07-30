add_qtc_plugin(EtherCATDeviceAdapters
  DEPENDS EtherCATData Utils
  PLUGIN_DEPENDS Core EtherCATCore
  SOURCES
    adapterpackagerepository.cpp
    adapterpackagerepository.h
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
