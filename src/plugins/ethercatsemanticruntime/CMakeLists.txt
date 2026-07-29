add_qtc_plugin(EtherCATSemanticRuntime
  DEPENDS EtherCATData Qt::Core Utils qtcMonocypher
  PLUGIN_DEPENDS
    Core
    EtherCATCore
    EtherCATProject
  SOURCES
    ecpkgcontainer.cpp
    ecpkgcontainer.h
    ed25519verifier.cpp
    ed25519verifier.h
    ethercatsemanticruntimeplugin.cpp
    semanticruntimeexecutor.cpp
    semanticruntimeexecutor.h
)

extend_qtc_plugin(EtherCATSemanticRuntime
  CONDITION WITH_TESTS
  DEFINES
    ETHERCAT_SEMANTIC_RUNTIME_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  SOURCES
    ethercatsemanticruntimetests.cpp
    ethercatsemanticruntimetests.h
    testdata/api035-manifest.json
    testdata/api036-manifest.json
)
